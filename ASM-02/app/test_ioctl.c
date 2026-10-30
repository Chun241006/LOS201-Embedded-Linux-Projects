#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "../driver/sms_sensor.h"

int main(void)
{
    int fd;
    uint32_t interval = 500;

    fd = open(SMS_DEVICE_PATH, O_RDONLY);

    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ioctl(fd, SMS_SET_INTERVAL, &interval) < 0) {
        perror("ioctl");
        close(fd);
        return 1;
    }

    printf("Sensor interval set to %u ms\n", interval);

    close(fd);
    return 0;
}
