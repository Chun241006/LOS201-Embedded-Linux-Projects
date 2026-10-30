#define _POSIX_C_SOURCE 200809L

#include "../include/sms_common.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <mqueue.h>
#include <pthread.h>
#include <limits.h>
#include <math.h>
#include <sched.h>
#include <sys/mman.h>
/*
 * SIGINT flag.
 * The signal handler only sets this flag.
 */
static volatile sig_atomic_t stop_requested = 0;

/*
 * Handle Ctrl+C safely.
 */
static void sms_signal_handler(int signum)
{
    (void)signum;
    stop_requested = 1;
}
/*
 * Initialize default application configuration.
 */
static void sms_config_init(sms_config_t *config)
{
    config->sample_interval_ms = SMS_DEFAULT_SAMPLE_MS;
    config->temperature_threshold = SMS_DEFAULT_TEMP_THRESHOLD;
    config->humidity_threshold = SMS_DEFAULT_HUM_THRESHOLD;
    config->duration_seconds = SMS_DEFAULT_DURATION;
    config->verbose = false;
}

/*
 * Print the application configuration.
 */
static void sms_config_print(const sms_config_t *config)
{
    printf("====================================\n");
    printf(" Sensor Monitoring System - ASM2\n");
    printf("====================================\n");

    printf("Sample interval : %u ms\n",
           config->sample_interval_ms);

    printf("Temperature limit: %.1f C\n",
           config->temperature_threshold);

    printf("Humidity limit   : %.1f %%\n",
           config->humidity_threshold);

    printf("Duration         : %u seconds\n",
           config->duration_seconds);

    printf("Verbose          : %s\n",
           config->verbose ? "ON" : "OFF");
}

/*
 * Parse a positive unsigned integer.
 */
static int parse_positive_uint(const char *text, unsigned int *value)
{
    char *end;
    unsigned long parsed;

    if (text == NULL || value == NULL ||
        text[0] == '\0' || text[0] == '-')
        return -1;

    errno = 0;
    parsed = strtoul(text, &end, 10);

    if (errno != 0 || *end != '\0' ||
        parsed == 0 || parsed > UINT_MAX)
        return -1;

    *value = (unsigned int)parsed;
    return 0;
}

/*
 * Parse duration in seconds.
 * Zero means run indefinitely.
 */
static int parse_duration(const char *text, unsigned int *value)
{
    char *end;
    unsigned long parsed;

    if (text == NULL || value == NULL ||
        text[0] == '\0' || text[0] == '-')
        return -1;

    errno = 0;
    parsed = strtoul(text, &end, 10);

    if (errno != 0 || *end != '\0' ||
        parsed > UINT_MAX)
        return -1;

    *value = (unsigned int)parsed;
    return 0;
}

/*
 * Parse a floating-point threshold.
 */
static int parse_threshold(const char *text, double *value)
{
    char *end;
    double parsed;

    if (text == NULL || value == NULL || text[0] == '\0')
        return -1;

    errno = 0;
    parsed = strtod(text, &end);

    if (errno != 0 || *end != '\0' || !isfinite(parsed))
        return -1;

    *value = parsed;
    return 0;
}

/*
 * Parse command-line arguments.
 */
static int sms_parse_args(int argc,
                          char *argv[],
                          sms_config_t *config)
{
    int option;

    opterr = 0;

    while ((option = getopt(argc, argv, "i:t:h:d:v")) != -1) {
        switch (option) {
        case 'i':
            if (parse_positive_uint(
                    optarg, &config->sample_interval_ms) != 0) {
                fprintf(stderr, "Invalid sample interval\n");
                return -1;
            }
            break;

        case 't':
            if (parse_threshold(
                    optarg, &config->temperature_threshold) != 0) {
                fprintf(stderr, "Invalid temperature threshold\n");
                return -1;
            }
            break;

        case 'h':
            if (parse_threshold(
                    optarg, &config->humidity_threshold) != 0) {
                fprintf(stderr, "Invalid humidity threshold\n");
                return -1;
            }
            break;

        case 'd':
            if (parse_duration(
                    optarg, &config->duration_seconds) != 0) {
                fprintf(stderr, "Invalid duration\n");
                return -1;
            }
            break;

        case 'v':
            config->verbose = true;
            break;

        default:
            fprintf(stderr,
                    "Usage: %s [-i ms] [-t C] [-h %%] "
                    "[-d sec] [-v]\n",
                    argv[0]);
            return -1;
        }
    }

    if (optind != argc) {
        fprintf(stderr, "Unexpected argument: %s\n",
                argv[optind]);
        return -1;
    }

    return 0;
}

/*
 * Initialize POSIX Message Queues.
 * Return 0 on success, -1 on failure.
 */
static int sms_queues_init(sms_context_t *ctx)
{
    struct mq_attr alert_attr = {0};
    struct mq_attr log_attr = {0};

    if (ctx == NULL)
        return -1;

    ctx->alert_queue = (mqd_t)-1;
    ctx->log_queue = (mqd_t)-1;

    alert_attr.mq_maxmsg = 10;
    alert_attr.mq_msgsize = sizeof(sms_alert_t);

    log_attr.mq_maxmsg = 10;
    log_attr.mq_msgsize = sizeof(sms_log_record_t);

    mq_unlink(SMS_ALERT_QUEUE_NAME);
    mq_unlink(SMS_LOG_QUEUE_NAME);

    ctx->alert_queue = mq_open(
    SMS_ALERT_QUEUE_NAME,
    O_CREAT | O_RDWR | O_NONBLOCK,
    0600,
    &alert_attr
);

    if (ctx->alert_queue == (mqd_t)-1) {
        perror("[MAIN] mq_open alert");
        return -1;
    }

    ctx->log_queue = mq_open(
    SMS_LOG_QUEUE_NAME,
    O_CREAT | O_RDWR | O_NONBLOCK,
    0600,
    &log_attr
);

    if (ctx->log_queue == (mqd_t)-1) {
        perror("[MAIN] mq_open log");

        mq_close(ctx->alert_queue);
        ctx->alert_queue = (mqd_t)-1;
        mq_unlink(SMS_ALERT_QUEUE_NAME);

        return -1;
    }

    printf("[MAIN] POSIX Message Queues initialized\n");

    return 0;
}

/*
 * Close and remove POSIX Message Queues.
 */
static void sms_queues_cleanup(sms_context_t *ctx)
{
    if (ctx == NULL)
        return;

    if (ctx->alert_queue != (mqd_t)-1) {
        if (mq_close(ctx->alert_queue) == -1)
            perror("[MAIN] mq_close alert");

        ctx->alert_queue = (mqd_t)-1;
    }

    if (ctx->log_queue != (mqd_t)-1) {
        if (mq_close(ctx->log_queue) == -1)
            perror("[MAIN] mq_close log");

        ctx->log_queue = (mqd_t)-1;
    }

    if (mq_unlink(SMS_ALERT_QUEUE_NAME) == -1 &&
        errno != ENOENT) {
        perror("[MAIN] mq_unlink alert");
    }

    if (mq_unlink(SMS_LOG_QUEUE_NAME) == -1 &&
        errno != ENOENT) {
        perror("[MAIN] mq_unlink log");
    }

    printf("[MAIN] POSIX Message Queues cleaned up\n");
}


/*
 * Request Sensor Thread to stop and
 * wake Processor Thread if it is waiting
 * on an empty Circular Buffer.
 */
static void sms_request_stop(sms_context_t *ctx)
{
    if (ctx == NULL)
        return;

    atomic_store(&ctx->running, false);

    pthread_mutex_lock(&ctx->buffer.mutex);

    pthread_cond_broadcast(&ctx->buffer.not_empty);

    pthread_mutex_unlock(&ctx->buffer.mutex);

    printf("[MAIN] Stop requested\n");
}

/*
 * Register SIGINT handler.
 */
static int sms_setup_signals(void)
{
    struct sigaction action = {0};

    action.sa_handler = sms_signal_handler;

    if (sigemptyset(&action.sa_mask) == -1) {
        perror("[MAIN] sigemptyset");
        return -1;
    }

    action.sa_flags = 0;

    if (sigaction(SIGINT, &action, NULL) == -1) {
        perror("[MAIN] sigaction");
        return -1;
    }

    return 0;
}


/*
 * Main application entry point.
 */
int main(int argc, char *argv[])
{
    sms_context_t ctx = {0};
	pthread_t sensor_tid;
pthread_t processor_tid;
pthread_t alert_tid;
pthread_t logger_tid;
pthread_attr_t thread_attr;
int ret;
struct sched_param sensor_sched = {0};
int exit_status = EXIT_SUCCESS;
struct timespec start_time;
struct timespec current_time;
bool sensor_started = false;
bool processor_started = false;
bool alert_started = false;
bool logger_started = false;
    /*
     * Initialize default configuration.
     */
    sms_config_init(&ctx.config);

    /*
     * Parse command-line arguments.
     */
    if (sms_parse_args(argc, argv, &ctx.config) != 0)
        return EXIT_FAILURE;

    sms_config_print(&ctx.config);

    /*
     * Initialize shared application state.
     */
    /*
 * Lock current and future memory pages to reduce
 * page faults during real-time execution.
 */
if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
    perror("[MAIN] mlockall");
    fprintf(stderr,
            "[MAIN] Continuing without memory locking\n");
} else {
    printf("[MAIN] Memory locking enabled\n");
} 
    atomic_init(&ctx.running, true);
    atomic_init(&ctx.sensor_failed, false); 
    atomic_init(&ctx.processor_done, false);
    atomic_init(&ctx.producer_done, false);
    ctx.alert_queue = (mqd_t)-1;
    ctx.log_queue = (mqd_t)-1;

    /*
     * Register SIGINT handler.
     */
    if (sms_setup_signals() != 0)
        return EXIT_FAILURE;

    /*
     * Initialize Circular Buffer.
     */
    if (circular_buffer_init(&ctx.buffer) != 0) {
        fprintf(stderr, "[MAIN] Circular Buffer init failed\n");
        return EXIT_FAILURE;
    }

    /*
     * Initialize POSIX Message Queues.
     */
    if (sms_queues_init(&ctx) != 0) {
        fprintf(stderr, "[MAIN] Message Queue init failed\n");

        sms_queues_cleanup(&ctx);
        circular_buffer_destroy(&ctx.buffer);

        return EXIT_FAILURE;
    }

        printf("[MAIN] System initialization completed\n");

/*
 * Use smaller thread stacks to stay within
 * the memory locking limit.
 */
ret = pthread_attr_init(&thread_attr);

if (ret != 0) {
    fprintf(stderr,
            "[MAIN] pthread_attr_init failed: %s\n",
            strerror(ret));
    exit_status = EXIT_FAILURE;
    goto shutdown;
}

ret = pthread_attr_setstacksize(&thread_attr, 256 * 1024);

if (ret != 0) {
    fprintf(stderr,
            "[MAIN] pthread_attr_setstacksize failed: %s\n",
            strerror(ret));

    pthread_attr_destroy(&thread_attr);
    exit_status = EXIT_FAILURE;
    goto shutdown;
}

    /*
     * Start Logger Thread.
     */
    ret = pthread_create(&logger_tid, &thread_attr,
                     logger_thread, &ctx);

    if (ret != 0) {
        fprintf(stderr,
                "[MAIN] Logger thread creation failed: %s\n",
                strerror(ret));
        exit_status = EXIT_FAILURE;
        goto shutdown;
    }

    logger_started = true;

    /*
     * Start Alert Thread.
     */
    ret = pthread_create(&alert_tid, &thread_attr,
                     alert_thread, &ctx);

    if (ret != 0) {
        fprintf(stderr,
                "[MAIN] Alert thread creation failed: %s\n",
                strerror(ret));
            exit_status = EXIT_FAILURE;
            goto shutdown;
    }

    alert_started = true;

    /*
     * Start Processor Thread.
     */
    ret = pthread_create(&processor_tid, &thread_attr,
                     processor_thread, &ctx);

    if (ret != 0) {
        fprintf(stderr,
                "[MAIN] Processor thread creation failed: %s\n",
                strerror(ret));
	 exit_status = EXIT_FAILURE;
        goto shutdown;
    }

    processor_started = true;

    /*
     * Start Sensor Thread.
     */
    ret = pthread_create(&sensor_tid, &thread_attr,
                     sensor_thread, &ctx);

    if (ret != 0) {
        fprintf(stderr,
                "[MAIN] Sensor thread creation failed: %s\n",
                strerror(ret));
               exit_status = EXIT_FAILURE;
               goto shutdown;
    }

    sensor_started = true;
   pthread_attr_destroy(&thread_attr);
/*
 * Request real-time scheduling for Sensor Thread.
 * Fall back to the default policy if permission is denied.
 */
sensor_sched.sched_priority = 50;

ret = pthread_setschedparam(sensor_tid,
                            SCHED_FIFO,
                            &sensor_sched);

if (ret != 0) {
    fprintf(stderr,
            "[MAIN] SCHED_FIFO unavailable: %s\n",
            strerror(ret));

    printf("[MAIN] Sensor Thread continues with default scheduling\n");
} else {
    printf("[MAIN] Sensor Thread configured with SCHED_FIFO priority 50\n");
}

printf("[MAIN] All four threads started\n");

    if (clock_gettime(CLOCK_MONOTONIC, &start_time) != 0) {
        perror("[MAIN] clock_gettime");
        exit_status = EXIT_FAILURE;
        goto shutdown;
    }

    /*
     * Run until the configured duration expires
     * or SIGINT is received.
     */
    while (!stop_requested && atomic_load(&ctx.running)) {
        if (clock_gettime(CLOCK_MONOTONIC, &current_time) != 0) {
            perror("[MAIN] clock_gettime");
            exit_status = EXIT_FAILURE;
            break;
        }

        time_t elapsed =
            current_time.tv_sec - start_time.tv_sec;

        if (ctx.config.duration_seconds > 0 &&
    elapsed >= (time_t)ctx.config.duration_seconds)
    break;

        struct timespec pause_time = {
            .tv_sec = 0,
            .tv_nsec = 100000000L
        };

        nanosleep(&pause_time, NULL);
    }

shutdown:
    /*
     * Stop Sensor first so no new samples are produced.
     */
    atomic_store(&ctx.running, false);

    if (sensor_started) {
        ret = pthread_join(sensor_tid, NULL);

        if (ret != 0) {
            fprintf(stderr,
                    "[MAIN] Sensor join failed: %s\n",
                    strerror(ret));
            exit_status = EXIT_FAILURE;
        }
    }

    /*
     * Wake Processor after Sensor has stopped.
     */
    sms_request_stop(&ctx);

    if (processor_started) {
    ret = pthread_join(processor_tid, NULL);

    if (ret != 0) {
        fprintf(stderr,
                "[MAIN] Processor join failed: %s\n",
                strerror(ret));
        exit_status = EXIT_FAILURE;
    }
}

/*
 * Processor is no longer producing messages.
 * Allow Alert and Logger to drain their queues.
 */
atomic_store(&ctx.processor_done, true);
atomic_store(&ctx.producer_done, true);
    /*
     * Alert and Logger drain their queues before exiting.
     */
    if (alert_started) {
        ret = pthread_join(alert_tid, NULL);

        if (ret != 0) {
            fprintf(stderr,
                    "[MAIN] Alert join failed: %s\n",
                    strerror(ret));
            exit_status = EXIT_FAILURE;
        }
    }

    if (logger_started) {
        ret = pthread_join(logger_tid, NULL);

        if (ret != 0) {
            fprintf(stderr,
                    "[MAIN] Logger join failed: %s\n",
                    strerror(ret));
            exit_status = EXIT_FAILURE;
        }
    }

	if (atomic_load(&ctx.sensor_failed)) {
    fprintf(stderr,
            "[MAIN] Sensor initialization failed\n");

    exit_status = EXIT_FAILURE;
}  
  /*
     * Print final statistics after all threads have stopped.
     */
    printf("\n========== SMS STATISTICS ==========\n");
    printf("Samples received    : %lu\n", ctx.samples_received);
    printf("Samples dropped     : %lu\n", ctx.samples_dropped);
    printf("Read errors         : %lu\n", ctx.read_errors);
    printf("Temperature alerts  : %lu\n", ctx.temperature_alerts);
    printf("Humidity alerts     : %lu\n", ctx.humidity_alerts);
    printf("Temperature spikes  : %lu\n", ctx.spike_alerts);
    printf("Alert queue errors  : %lu\n", ctx.alert_queue_errors);
    printf("Log queue errors    : %lu\n", ctx.log_queue_errors);
    printf("====================================\n");

    sms_queues_cleanup(&ctx);
    circular_buffer_destroy(&ctx.buffer);

    printf("[MAIN] Application stopped\n");

    return exit_status;
}
