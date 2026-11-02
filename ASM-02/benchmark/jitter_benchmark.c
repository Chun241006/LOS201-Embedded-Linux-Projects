#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <sched.h>

#define SAMPLE_COUNT 1000
#define INTERVAL_NS 10000000L

static void add_ns(struct timespec *ts, long ns)
{
    ts->tv_nsec += ns;

    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

static double diff_us(const struct timespec *actual,
                      const struct timespec *expected)
{
    double seconds =
        (double)(actual->tv_sec - expected->tv_sec);

    double nanoseconds =
        (double)(actual->tv_nsec - expected->tv_nsec);

    return seconds * 1000000.0 + nanoseconds / 1000.0;
}

static int compare_double(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;

    if (x < y)
        return -1;

    if (x > y)
        return 1;

    return 0;
}

int main(int argc, char *argv[])
{
    struct timespec expected;
    struct timespec actual;

    double samples[SAMPLE_COUNT];

    double sum = 0.0;
    double sum_squares = 0.0;
    double min_jitter = 0.0;
    double max_jitter = 0.0;
    double average;
    double stddev;
    double p99;

    unsigned int histogram[4] = {0};

    int policy = SCHED_OTHER;
    struct sched_param param = {0};

    if (argc != 2) {
        fprintf(stderr, "Usage: %s other|fifo\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "fifo") == 0) {
        policy = SCHED_FIFO;
        param.sched_priority = 50;
    } else if (strcmp(argv[1], "other") == 0) {
        policy = SCHED_OTHER;
        param.sched_priority = 0;
    } else {
        fprintf(stderr, "Invalid scheduling policy\n");
        return EXIT_FAILURE;
    }

    if (sched_setscheduler(0, policy, &param) != 0) {
        perror("sched_setscheduler");
        return EXIT_FAILURE;
    }

    int actual_policy = sched_getscheduler(0);

    if (actual_policy == -1) {
        perror("sched_getscheduler");
        return EXIT_FAILURE;
    }

    if (actual_policy != policy) {
        fprintf(stderr, "Scheduling policy mismatch\n");
        return EXIT_FAILURE;
    }

    printf("[SCHED] Requested policy: %s\n", argv[1]);
    printf("[SCHED] Policy verification: SUCCESS\n");

    if (clock_gettime(CLOCK_MONOTONIC, &expected) != 0) {
        perror("clock_gettime");
        return EXIT_FAILURE;
    }

    printf("====================================\n");
    printf(" Jitter Benchmark - ASM2\n");
    printf("====================================\n");
    printf("Policy      : %s\n", argv[1]);
    printf("Priority    : %d\n", param.sched_priority);
    printf("Samples     : %d\n", SAMPLE_COUNT);
    printf("Interval    : 10 ms\n");

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        int ret;

        add_ns(&expected, INTERVAL_NS);

        do {
            ret = clock_nanosleep(
                CLOCK_MONOTONIC,
                TIMER_ABSTIME,
                &expected,
                NULL
            );
        } while (ret == EINTR);

        if (ret != 0) {
            fprintf(stderr,
                    "clock_nanosleep failed: %s\n",
                    strerror(ret));
            return EXIT_FAILURE;
        }

        if (clock_gettime(CLOCK_MONOTONIC, &actual) != 0) {
            perror("clock_gettime");
            return EXIT_FAILURE;
        }

        double jitter = diff_us(&actual, &expected);

        if (jitter < 0.0)
            jitter = 0.0;

        samples[i] = jitter;

        sum += jitter;
        sum_squares += jitter * jitter;

        if (i == 0 || jitter < min_jitter)
            min_jitter = jitter;

        if (jitter > max_jitter)
            max_jitter = jitter;

        if (jitter < 100.0)
            histogram[0]++;
        else if (jitter < 500.0)
            histogram[1]++;
        else if (jitter < 1000.0)
            histogram[2]++;
        else
            histogram[3]++;
    }

    average = sum / SAMPLE_COUNT;

    double variance =
        (sum_squares / SAMPLE_COUNT) -
        (average * average);

    if (variance < 0.0)
        variance = 0.0;

    stddev = sqrt(variance);

    qsort(samples, SAMPLE_COUNT, sizeof(double), compare_double);

    /* Nearest-rank 99th percentile for 1000 samples. */
    p99 = samples[989];

    printf("\n========== JITTER RESULTS ==========\n");
    printf("Minimum jitter : %.3f us\n", min_jitter);
    printf("Average jitter : %.3f us\n", average);
    printf("Maximum jitter : %.3f us\n", max_jitter);
    printf("P99 jitter     : %.3f us\n", p99);
    printf("Std deviation  : %.3f us\n", stddev);

    printf("\n========== HISTOGRAM ==========\n");
    printf("0 - <100 us     : %u\n", histogram[0]);
    printf("100 - <500 us   : %u\n", histogram[1]);
    printf("500 - <1000 us  : %u\n", histogram[2]);
    printf(">=1000 us      : %u\n", histogram[3]);
    printf("====================================\n");

    return EXIT_SUCCESS;
}
