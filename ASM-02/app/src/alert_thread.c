#define _POSIX_C_SOURCE 200809L

#include "../include/sms_common.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <mqueue.h>
#include <time.h>
/*
 * Convert an alert type into a readable string.
 */
static const char *alert_type_name(sms_alert_type_t type)
{
    switch (type) {
    case SMS_ALERT_TEMPERATURE:
        return "HIGH_TEMPERATURE";

    case SMS_ALERT_HUMIDITY:
        return "HIGH_HUMIDITY";

    case SMS_ALERT_TEMP_SPIKE:
        return "TEMPERATURE_SPIKE";

    default:
        return "UNKNOWN";
    }
}

/*
 * Alert Thread:
 * Receive alerts from the POSIX message queue.
 */
void *alert_thread(void *arg)
{
    sms_context_t *ctx = (sms_context_t *)arg;
    sms_alert_t alert;
    ssize_t received;
    struct timespec timeout;
    if (ctx == NULL)
        return NULL;

    while (1) {

        if (clock_gettime(CLOCK_REALTIME, &timeout) != 0) {
    perror("[ALERT] clock_gettime");
    break;
}

timeout.tv_sec += 1;

received = mq_timedreceive(ctx->alert_queue,
                           (char *)&alert,
                           sizeof(alert),
                           NULL,
                           &timeout);

        if (received < 0) {
    if (errno == EINTR || errno == ETIMEDOUT)
        continue;

    if (errno == EAGAIN) {
    /*
     * Stop only when no more messages can be produced
     * and the alert queue is empty.
     */
    if (atomic_load(&ctx->producer_done))
        break;

    struct timespec pause_time = {
        .tv_sec = 0,
        .tv_nsec = 10000000L
    };

    nanosleep(&pause_time, NULL);
    continue;
}

    perror("[ALERT] mq_timedreceive");
    break;
}
        if ((size_t)received != sizeof(alert)) {
            fprintf(stderr,
                    "[ALERT] Invalid message size\n");
            continue;
        }

        switch (alert.type) {
        case SMS_ALERT_TEMPERATURE:
            ctx->temperature_alerts++;
            break;

        case SMS_ALERT_HUMIDITY:
            ctx->humidity_alerts++;
            break;

        case SMS_ALERT_TEMP_SPIKE:
            ctx->spike_alerts++;
            break;

        default:
            fprintf(stderr,
                    "[ALERT] Unknown alert type\n");
            continue;
        }

        printf("[ALERT][%llu] TYPE=%s VALUE=%.1f THRESHOLD=%.1f\n",
               (unsigned long long)alert.timestamp_ms,
               alert_type_name(alert.type),
               alert.value,
               alert.threshold);
    }

    printf("[ALERT] Thread stopped\n");

    return NULL;
}

