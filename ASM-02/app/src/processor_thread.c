#include "../include/sms_common.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <mqueue.h>
/*
 * Rolling average of the 10 most recent sensor samples.
 */
typedef struct {
    double temperatures[SMS_AVG_WINDOW];
    double humidities[SMS_AVG_WINDOW];

    size_t index;
    size_t count;

    double temperature_sum;
    double humidity_sum;
} rolling_average_t;

/*
 * Add a sample and calculate the current averages.
 */
static void rolling_average_update(rolling_average_t *avg,
                                   const sensor_sample_t *sample,
                                   double *avg_temperature,
                                   double *avg_humidity)
{
    if (avg->count == SMS_AVG_WINDOW) {
        avg->temperature_sum -= avg->temperatures[avg->index];
        avg->humidity_sum -= avg->humidities[avg->index];
    } else {
        avg->count++;
    }

    avg->temperatures[avg->index] = sample->temperature;
    avg->humidities[avg->index] = sample->humidity;

    avg->temperature_sum += sample->temperature;
    avg->humidity_sum += sample->humidity;

    avg->index = (avg->index + 1) % SMS_AVG_WINDOW;

    *avg_temperature = avg->temperature_sum / avg->count;
    *avg_humidity = avg->humidity_sum / avg->count;
}

/*
 * Detect abnormal sensor values.
 * Return the number of alerts detected (0 to 3).
 */
static int detect_alerts(const sensor_sample_t *current,
                         const sensor_sample_t *previous,
                         const sms_config_t *config,
                         sms_alert_t alerts[3])
{
    int count = 0;

    if (current == NULL || config == NULL || alerts == NULL)
        return 0;

    if (current->temperature > config->temperature_threshold) {
        alerts[count++] = (sms_alert_t) {
            .timestamp_ms = current->timestamp_ms,
            .type = SMS_ALERT_TEMPERATURE,
            .value = current->temperature,
            .threshold = config->temperature_threshold
        };
    }

    if (current->humidity > config->humidity_threshold) {
        alerts[count++] = (sms_alert_t) {
            .timestamp_ms = current->timestamp_ms,
            .type = SMS_ALERT_HUMIDITY,
            .value = current->humidity,
            .threshold = config->humidity_threshold
        };
    }

    if (previous != NULL &&
        current->temperature - previous->temperature > 5.0) {
        alerts[count++] = (sms_alert_t) {
            .timestamp_ms = current->timestamp_ms,
            .type = SMS_ALERT_TEMP_SPIKE,
            .value = current->temperature - previous->temperature,
            .threshold = 5.0
        };
    }

    return count;
}

/*
 * Processor Thread:
 * Consume sensor samples, calculate rolling averages,
 * detect alerts and send messages to POSIX queues.
 */
void *processor_thread(void *arg)
{
    sms_context_t *ctx = (sms_context_t *)arg;
    rolling_average_t avg = {0};

    sensor_sample_t current;
    sensor_sample_t previous;

    sms_alert_t alerts[3];
    sms_log_record_t log_record;

    double avg_temperature;
    double avg_humidity;

    int alert_count;
    int ret;
    bool has_previous = false;

    if (ctx == NULL)
        return NULL;

    while (1) {
        ret = circular_buffer_pop(&ctx->buffer,
                                  &current,
                                  &ctx->running);

        if (ret == 1)
            break;

        if (ret < 0) {
            fprintf(stderr, "[PROCESSOR] Buffer pop failed\n");
            break;
        }

        rolling_average_update(&avg,
                               &current,
                               &avg_temperature,
                               &avg_humidity);

        alert_count = detect_alerts(
            &current,
            has_previous ? &previous : NULL,
            &ctx->config,
            alerts
        );

        for (int i = 0; i < alert_count; i++) {
            if (mq_send(ctx->alert_queue,
                        (const char *)&alerts[i],
                        sizeof(sms_alert_t),
                        0) == -1) {
		  ctx->alert_queue_errors++;
                perror("[PROCESSOR] mq_send alert");
            }
        }

        log_record.timestamp_ms = current.timestamp_ms;
        log_record.temperature = current.temperature;
        log_record.humidity = current.humidity;
        log_record.average_temperature = avg_temperature;
        log_record.average_humidity = avg_humidity;

        if (mq_send(ctx->log_queue,
                    (const char *)&log_record,
                    sizeof(sms_log_record_t),
                    0) == -1) {
		ctx->log_queue_errors++;
            perror("[PROCESSOR] mq_send log");
        }

        previous = current;
        has_previous = true;

        if (ctx->config.verbose) {
            printf("[PROCESSOR] Avg T=%.2f C, Avg H=%.2f %%\n",
                   avg_temperature,
                   avg_humidity);
        }
    }

        /*
     * Processor has finished consuming sensor samples.
     * No more messages will be produced.
     */
    atomic_store(&ctx->processor_done, true);
    atomic_store(&ctx->producer_done, true);

    printf("[PROCESSOR] Thread stopped\n");

    return NULL;
}
