#define _POSIX_C_SOURCE 200809L

#include "../include/sms_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <mqueue.h>

#define SMS_LOG_FILE "/tmp/sms.log"
#define SMS_LOG_ROTATED_FILE "/tmp/sms.log.1"
#define SMS_LOG_MAX_SIZE (1024L * 1024L)
#define SMS_LOG_FLUSH_INTERVAL 5

/*
 * Open the application log file in append mode.
 */
static FILE *logger_open_file(void)
{
    FILE *file = fopen(SMS_LOG_FILE, "a");

    if (file == NULL)
        perror("[LOGGER] fopen");

    return file;
}

/*
 * Rotate log file when its size reaches 1 MB.
 * Return 0 on success, -1 on failure.
 */
static int logger_rotate_if_needed(FILE **file)
{
    long size;

    if (file == NULL || *file == NULL)
        return -1;

    size = ftell(*file);

    if (size < 0) {
        perror("[LOGGER] ftell");
        return -1;
    }

    if (size < SMS_LOG_MAX_SIZE)
        return 0;

    if (fclose(*file) != 0) {
        perror("[LOGGER] fclose");
        *file = NULL;
        return -1;
    }

    *file = NULL;

    if (rename(SMS_LOG_FILE, SMS_LOG_ROTATED_FILE) != 0) {
        perror("[LOGGER] rename");
        return -1;
    }

    *file = logger_open_file();

    if (*file == NULL)
        return -1;

    printf("[LOGGER] Log rotated successfully\n");

    return 0;
}

/*
 * Logger Thread:
 * Receive processed sensor records from POSIX Message Queue
 * and write them to /tmp/sms.log.
 */
void *logger_thread(void *arg)
{
    sms_context_t *ctx = (sms_context_t *)arg;
    sms_log_record_t record;

    FILE *log_file;
    ssize_t received;
struct timespec timeout;
    time_t last_flush;
    time_t now;

    if (ctx == NULL)
        return NULL;

    log_file = logger_open_file();

    if (log_file == NULL)
        return NULL;

    last_flush = time(NULL);

while (1) {
    /*
     * Flush buffered log data every 5 seconds,
     * even when no new sensor records arrive.
     */
    now = time(NULL);

    if (now == (time_t)-1) {
        perror("[LOGGER] time");
        break;
    }

    if (now - last_flush >= SMS_LOG_FLUSH_INTERVAL) {
        if (fflush(log_file) != 0) {
            perror("[LOGGER] fflush");
            break;
        }

        last_flush = now;
    }

        if (clock_gettime(CLOCK_REALTIME, &timeout) != 0) {
    perror("[LOGGER] clock_gettime");
    break;
}

timeout.tv_sec += 1;

received = mq_timedreceive(ctx->log_queue,
                           (char *)&record,
                           sizeof(record),
                           NULL,
                           &timeout);

        if (received < 0) {
    if (errno == EINTR || errno == ETIMEDOUT)
        continue;

    if (errno == EAGAIN) {
    /*
     * Exit only after Processor has finished
     * and the log queue is empty.
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

    perror("[LOGGER] mq_timedreceive");
    break;
}
        if ((size_t)received != sizeof(record)) {
            fprintf(stderr, "[LOGGER] Invalid record size\n");
            continue;
        }

        fprintf(log_file,
                "timestamp=%llu temp=%.1f humidity=%.1f "
                "avg_temp=%.2f avg_humidity=%.2f\n",
                (unsigned long long)record.timestamp_ms,
                record.temperature,
                record.humidity,
                record.average_temperature,
                record.average_humidity);

        if (logger_rotate_if_needed(&log_file) != 0)
            break;

    }

    if (log_file != NULL) {
        fflush(log_file);
        fclose(log_file);
    }

    printf("[LOGGER] Thread stopped\n");

    return NULL;
}
