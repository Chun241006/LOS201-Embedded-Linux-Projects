#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <dlfcn.h>

#include "../../driver/sms_sensor.h"

static int sensor_fd = -1;
static unsigned int interval_ms = 500;
static struct timespec last_read = {0};

int open(const char *path, int flags, ...)
{
    static int (*real_open)(const char *, int, ...) = NULL;

    if (!real_open)
        real_open = dlsym(RTLD_NEXT, "open");

    if (strcmp(path, SMS_DEVICE_PATH) == 0) {
        int fd = real_open("/dev/null", O_RDONLY);

        if (fd >= 0)
            sensor_fd = fd;

        return fd;
    }

    if (flags & O_CREAT) {
        va_list ap;
        mode_t mode;

        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);

        return real_open(path, flags, mode);
    }

    return real_open(path, flags);
}

int ioctl(int fd, unsigned long request, ...)
{
    static int (*real_ioctl)(int, unsigned long, ...) = NULL;

    if (!real_ioctl)
        real_ioctl = dlsym(RTLD_NEXT, "ioctl");

    va_list ap;
    void *arg;

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);

    if (fd == sensor_fd && request == SMS_SET_INTERVAL) {
        unsigned int value = *(unsigned int *)arg;

        if (value < 1 || value > 60000) {
            errno = EINVAL;
            return -1;
        }

        interval_ms = value;
        return 0;
    }

    return real_ioctl(fd, request, arg);
}

ssize_t read(int fd, void *buf, size_t count)
{
    static ssize_t (*real_read)(int, void *, size_t) = NULL;

    if (!real_read)
        real_read = dlsym(RTLD_NEXT, "read");

    if (fd != sensor_fd)
        return real_read(fd, buf, count);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    long elapsed_ms =
        (now.tv_sec - last_read.tv_sec) * 1000L +
        (now.tv_nsec - last_read.tv_nsec) / 1000000L;

    if (last_read.tv_sec != 0 &&
        elapsed_ms < (long)interval_ms) {
        errno = EAGAIN;
        return -1;
    }

    char data[128];

    double temperature = 15.0 + (rand() % 301) / 10.0;
    double humidity = 30.0 + (rand() % 601) / 10.0;

    unsigned long long timestamp =
        (unsigned long long)now.tv_sec * 1000ULL +
        (unsigned long long)now.tv_nsec / 1000000ULL;

    int len = snprintf(data, sizeof(data),
                       "%llu,%.1f,%.1f\n",
                       timestamp, temperature, humidity);

    if (len < 0 || (size_t)len > count) {
        errno = EINVAL;
        return -1;
    }

    memcpy(buf, data, (size_t)len);
    last_read = now;

    return len;
}

int close(int fd)
{
    static int (*real_close)(int) = NULL;

    if (!real_close)
        real_close = dlsym(RTLD_NEXT, "close");

    if (fd == sensor_fd)
        sensor_fd = -1;

    return real_close(fd);
}
