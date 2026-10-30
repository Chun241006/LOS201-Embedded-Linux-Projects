#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "asgn1_ioctl.h"

int main(void)
{
    int fd;
    char version[64] = {0};
    struct asgn1_stats stats = {0};
    int mode;
    char read_buf[128];
    ssize_t n;

    fd = open("/dev/asgn1", O_RDWR);

    if (fd < 0) {
        perror("open");
        return 1;
    }

    printf("=== ASM1 IOCTL TEST ===\n");

    /* Test GET_VERSION */
    if (ioctl(fd, ASGN1_GET_VERSION, version) < 0) {
        perror("ASGN1_GET_VERSION");
    } else {
        printf("Driver version: %s\n", version);
    }

    /* Test GET_STATS */
    if (ioctl(fd, ASGN1_GET_STATS, &stats) < 0) {
        perror("ASGN1_GET_STATS");
    } else {
        printf("Open count     : %d\n", stats.open_count);
        printf("Write count    : %d\n", stats.write_count);
        printf("Read count     : %d\n", stats.read_count);
        printf("Buffer length  : %d\n", stats.buffer_len);
        printf("Last write size: %d\n", stats.last_write_size);
    }

    /* Test RESET_BUFFER */
    printf("\n=== TEST RESET_BUFFER ===\n");

    if (ioctl(fd, ASGN1_RESET_BUFFER) < 0) {
        perror("ASGN1_RESET_BUFFER");
    } else {
        printf("RESET_BUFFER: SUCCESS\n");
    }

    /* Check statistics after reset */
    memset(&stats, 0, sizeof(stats));

    if (ioctl(fd, ASGN1_GET_STATS, &stats) < 0) {
        perror("GET_STATS after reset");
    } else {
        printf("Statistics after RESET_BUFFER:\n");
        printf("Open count     : %d\n", stats.open_count);
        printf("Write count    : %d\n", stats.write_count);
        printf("Read count     : %d\n", stats.read_count);
        printf("Buffer length  : %d\n", stats.buffer_len);
        printf("Last write size: %d\n", stats.last_write_size);
    }

    /* Test SET_MODE = 0 */
    printf("\n=== TEST SET_MODE 0 ===\n");

    mode = 0;

    if (ioctl(fd, ASGN1_SET_MODE, &mode) < 0) {
        perror("SET_MODE 0");
    } else {
        printf("SET_MODE 0: SUCCESS\n");
    }

    if (write(fd, "Hello", 5) < 0)
        perror("write mode 0");

    lseek(fd, 0, SEEK_SET);

    memset(read_buf, 0, sizeof(read_buf));

    n = read(fd, read_buf, sizeof(read_buf) - 1);

    if (n < 0)
        perror("read mode 0");
    else
        printf("Mode 0 output: %s\n", read_buf);


    /* Test SET_MODE = 1 */
    printf("\n=== TEST SET_MODE 1 ===\n");

    mode = 1;

    if (ioctl(fd, ASGN1_SET_MODE, &mode) < 0) {
        perror("SET_MODE 1");
    } else {
        printf("SET_MODE 1: SUCCESS\n");
    }

    if (write(fd, "Hello", 5) < 0)
        perror("write mode 1");

    /* Reopen device to reset file position */
    close(fd);

    fd = open("/dev/asgn1", O_RDWR);

    if (fd < 0) {
        perror("reopen device");
        return 1;
    }

    memset(read_buf, 0, sizeof(read_buf));

    n = read(fd, read_buf, sizeof(read_buf) - 1);

    if (n < 0)
        perror("read mode 1");
    else
        printf("Mode 1 output: %s\n", read_buf);


    /* Test invalid mode */
    printf("\n=== TEST INVALID MODE ===\n");

    mode = 2;

    if (ioctl(fd, ASGN1_SET_MODE, &mode) < 0)
        perror("SET_MODE 2 (expected error)");
    else
        printf("ERROR: Invalid mode was accepted!\n");

    /* Test unsupported IOCTL command */
    printf("\n=== TEST INVALID IOCTL (ENOTTY) ===\n");

    errno = 0;

    if (ioctl(fd, _IO('X', 99)) == -1 && errno == ENOTTY) {
        printf("Invalid IOCTL: PASS (ENOTTY)\n");
    } else {
        printf("Invalid IOCTL: FAIL\n");
    }

    close(fd);

    return 0;
}
