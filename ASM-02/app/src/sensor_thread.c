#define _POSIX_C_SOURCE 200809L
#include "../include/sms_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "../../driver/sms_sensor.h"
/*
 * Parse a CSV sensor record:
 * timestamp_ms,temperature,humidity
 *
 * Return 0 on success, -1 on invalid input.
 */
static int parse_sensor_csv(const char *csv,
                            sensor_sample_t *sample)
{
    uint64_t timestamp;
    double temperature;
    double humidity;
    int matched;
    int consumed = 0;

    if (csv == NULL || sample == NULL)
        return -1;

    matched = sscanf(csv,
                     "%" SCNu64 ",%lf,%lf %n",
                     &timestamp,
                     &temperature,
                     &humidity,
                     &consumed);

    if (matched != 3 || consumed == 0 || csv[consumed] != '\0')
        return -1;

    if (temperature < 15.0 || temperature > 45.0)
        return -1;

    if (humidity < 30.0 || humidity > 90.0)
        return -1;

    sample->timestamp_ms = timestamp;
    sample->temperature = temperature;
    sample->humidity = humidity;

    return 0;
}

/*
 * Read one sample from the kernel sensor device.
 * Return 0 on success, -1 on failure.
 */
/*
 * Read one sensor sample with bounded retries.
 *
 * EAGAIN: driver is not ready yet.
 * EINTR: system call was interrupted.
 *
 * Return:
 *   0  - success
 *  -1  - read/open/parse error
 *  -2  - stop requested
 */
static int sensor_read_one(sensor_sample_t *sample,
                           const atomic_bool *running)
{
    int fd;
    ssize_t bytes_read = -1;
    char buffer[128];
    struct timespec start_time;
    struct timespec current_time;
    const long retry_timeout_ms = 500;
    int saved_errno = 0;

    if (sample == NULL || running == NULL)
        return -1;

    fd = open(SMS_DEVICE_PATH, O_RDONLY);

    if (fd < 0) {
        perror("[SENSOR] open");
        return -1;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &start_time) != 0) {
        perror("[SENSOR] clock_gettime");
        close(fd);
        return -1;
    }

    while (atomic_load(running)) {
        bytes_read = read(fd, buffer, sizeof(buffer) - 1);

        if (bytes_read >= 0)
            break;

        saved_errno = errno;

        if (saved_errno == EINTR)
            continue;

        if (saved_errno != EAGAIN) {
            errno = saved_errno;
            perror("[SENSOR] read");
            close(fd);
            return -1;
        }

        if (clock_gettime(CLOCK_MONOTONIC, &current_time) != 0) {
            perror("[SENSOR] clock_gettime");
            close(fd);
            return -1;
        }

        long elapsed_ms =
            (current_time.tv_sec - start_time.tv_sec) * 1000L +
            (current_time.tv_nsec - start_time.tv_nsec) / 1000000L;

        if (elapsed_ms >= retry_timeout_ms) {
            fprintf(stderr,
                    "[SENSOR] Read retry timeout after %ld ms\n",
                    elapsed_ms);
            close(fd);
            return -1;
        }

        struct timespec retry_delay = {
            .tv_sec = 0,
            .tv_nsec = 5000000L
        };

        while (nanosleep(&retry_delay, &retry_delay) != 0) {
            if (errno != EINTR)
                break;

            if (!atomic_load(running))
                break;
        }
    }

    if (!atomic_load(running)) {
        close(fd);
        return -2;
    }

    if (bytes_read == 0) {
        fprintf(stderr, "[SENSOR] Sensor returned EOF\n");
        close(fd);
        return -1;
    }

    buffer[bytes_read] = '\0';

    if (close(fd) != 0) {
        perror("[SENSOR] close");
        return -1;
    }

    return parse_sensor_csv(buffer, sample);
}

/*
 * Add milliseconds to an absolute timespec value.
 */
static void timespec_add_ms(struct timespec *ts,
                            unsigned int milliseconds)
{
    ts->tv_sec += milliseconds / 1000;

    ts->tv_nsec +=
        (long)(milliseconds % 1000) * 1000000L;

    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

/*
 * Configure the sensor driver sampling interval.
 */
static int sensor_configure_interval(unsigned int interval_ms)
{
    int fd;
    int ret;

    fd = open(SMS_DEVICE_PATH, O_RDONLY);

    if (fd < 0) {
        perror("[SENSOR] open for ioctl");
        return -1;
    }

    ret = ioctl(fd, SMS_SET_INTERVAL, &interval_ms);

    if (ret < 0) {
        perror("[SENSOR] ioctl SMS_SET_INTERVAL");
        close(fd);
        return -1;
    }

    if (close(fd) != 0) {
        perror("[SENSOR] close after ioctl");
        return -1;
    }

    printf("[SENSOR] Driver interval configured: %u ms\n",
           interval_ms);

    return 0;
}

/*
 * Sensor Thread:
 * Periodically read samples from the kernel sensor device
 * and push them into the shared circular buffer.
 */
void *sensor_thread(void *arg)
{
    sms_context_t *ctx = (sms_context_t *)arg;
    struct timespec next_time;
struct timespec actual_time;
sensor_sample_t sample;
int ret;

double jitter_sum = 0.0;
double jitter_min = 0.0;
double jitter_max = 0.0;
unsigned long jitter_count = 0;

    if (ctx == NULL)
        return NULL;

    if (sensor_configure_interval(
        ctx->config.sample_interval_ms) != 0) {
    fprintf(stderr,
            "[SENSOR] Failed to configure driver interval\n");

    atomic_store(&ctx->sensor_failed, true);
    atomic_store(&ctx->running, false);

    return NULL;
}

    if (clock_gettime(CLOCK_MONOTONIC, &next_time) != 0) {
    perror("[SENSOR] clock_gettime");

    atomic_store(&ctx->sensor_failed, true);
    atomic_store(&ctx->running, false);

    return NULL;
}

    while (atomic_load(&ctx->running)) {

        ret = sensor_read_one(&sample, &ctx->running);

if (ret == -2)
    break;

        if (ret == 0) {
            ctx->samples_received++;

            ret = circular_buffer_push(&ctx->buffer, &sample);

            if (ret == 1) {
                ctx->samples_dropped++;

                fprintf(stderr,
                        "[SENSOR] Circular buffer full\n");
            } else if (ret < 0) {
                ctx->read_errors++;
            } else if (ctx->config.verbose) {
                printf("[SENSOR] %llu,%.1f,%.1f\n",
                       (unsigned long long)sample.timestamp_ms,
                       sample.temperature,
                       sample.humidity);
            }

        } else {
            ctx->read_errors++;
        }

        timespec_add_ms(&next_time,
                        ctx->config.sample_interval_ms);

        do {
            ret = clock_nanosleep(CLOCK_MONOTONIC,
                                  TIMER_ABSTIME,
                                  &next_time,
                                  NULL);
        } while (ret == EINTR &&
                 atomic_load(&ctx->running));

        if (ret != 0 && ret != EINTR) {
    fprintf(stderr,
            "[SENSOR] clock_nanosleep error: %d\n",
            ret);

    atomic_store(&ctx->sensor_failed, true);
    atomic_store(&ctx->running, false);

    break;
}
/*
 * Measure wake-up jitter after absolute sleep.
 */
if (ret == 0 &&
    clock_gettime(CLOCK_MONOTONIC, &actual_time) == 0) {

    double jitter_us =
        (actual_time.tv_sec - next_time.tv_sec) * 1000000.0 +
        (actual_time.tv_nsec - next_time.tv_nsec) / 1000.0;

    if (jitter_us < 0.0)
        jitter_us = 0.0;

    if (jitter_count == 0 || jitter_us < jitter_min)
        jitter_min = jitter_us;

    if (jitter_us > jitter_max)
        jitter_max = jitter_us;

    jitter_sum += jitter_us;
    jitter_count++;
}  
  }

    if (jitter_count > 0) {
    printf("\n========== SENSOR JITTER ==========\n");
    printf("Samples : %lu\n", jitter_count);
    printf("Minimum : %.3f us\n", jitter_min);
    printf("Average : %.3f us\n",
           jitter_sum / jitter_count);
    printf("Maximum : %.3f us\n", jitter_max);
    printf("===================================\n");
}

printf("[SENSOR] Thread stopped\n");

    return NULL;
}
