#ifndef SMS_COMMON_H
#define SMS_COMMON_H
#include <mqueue.h>
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <time.h>

#define SMS_BUFFER_SIZE 64
#define SMS_AVG_WINDOW 10

#define SMS_DEFAULT_SAMPLE_MS 500
#define SMS_DEFAULT_TEMP_THRESHOLD 40.0
#define SMS_DEFAULT_HUM_THRESHOLD 80.0
#define SMS_DEFAULT_DURATION 60

#define SMS_ALERT_QUEUE_NAME "/sms_alert_queue"
#define SMS_LOG_QUEUE_NAME   "/sms_log_queue"

typedef struct {
    uint64_t timestamp_ms;
    double temperature;
    double humidity;
} sensor_sample_t;

typedef struct {
    sensor_sample_t samples[SMS_BUFFER_SIZE];

    size_t head;
    size_t tail;
    size_t count;

    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
} circular_buffer_t;

typedef struct {
    unsigned int sample_interval_ms;
    double temperature_threshold;
    double humidity_threshold;
    unsigned int duration_seconds;
    bool verbose;
} sms_config_t;

typedef enum {
    SMS_ALERT_TEMPERATURE = 1,
    SMS_ALERT_HUMIDITY = 2,
    SMS_ALERT_TEMP_SPIKE = 3
} sms_alert_type_t;

typedef struct {
    uint64_t timestamp_ms;
    sms_alert_type_t type;
    double value;
    double threshold;
} sms_alert_t;
typedef struct {
    uint64_t timestamp_ms;

    double temperature;
    double humidity;

    double average_temperature;
    double average_humidity;
} sms_log_record_t;
typedef struct {
    sms_config_t config;
    circular_buffer_t buffer;

    atomic_bool running;
    atomic_bool sensor_failed;
    atomic_bool processor_done;
    atomic_bool producer_done;
    mqd_t alert_queue;
    mqd_t log_queue;

    unsigned long samples_received;
    unsigned long samples_dropped;
    unsigned long read_errors;
    unsigned long temperature_alerts;
    unsigned long humidity_alerts;
    unsigned long spike_alerts;
    unsigned long alert_queue_errors;
    unsigned long log_queue_errors;
} sms_context_t;

int circular_buffer_init(circular_buffer_t *buffer);

void circular_buffer_destroy(circular_buffer_t *buffer);

int circular_buffer_push(circular_buffer_t *buffer,
                         const sensor_sample_t *sample);

int circular_buffer_pop(circular_buffer_t *buffer,
                        sensor_sample_t *sample,
                        const atomic_bool *running);
void *sensor_thread(void *arg);
void *processor_thread(void *arg);
void *alert_thread(void *arg);
void *logger_thread(void *arg);
#endif
