#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <mqueue.h>
#include <errno.h>
#include <string.h>

#define TEST_QUEUE "/sms_mqueue_full_test"

int main(void)
{
    struct mq_attr attr = {0};
    mqd_t queue;
    int success_count = 0;
    int error_count = 0;

    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(int);

    mq_unlink(TEST_QUEUE);

    queue = mq_open(
        TEST_QUEUE,
        O_CREAT | O_RDWR | O_NONBLOCK,
        0600,
        &attr
    );

    if (queue == (mqd_t)-1) {
        perror("mq_open");
        return EXIT_FAILURE;
    }

    for (int i = 1; i <= 11; i++) {
        int value = i;

        if (mq_send(queue,
                    (const char *)&value,
                    sizeof(value),
                    0) == -1) {

            printf("Message %d: FAILED (%s)\n",
                   i, strerror(errno));

            error_count++;
        } else {
            printf("Message %d: SUCCESS\n", i);
            success_count++;
        }
    }

    printf("\n========== TEST RESULTS ==========\n");
    printf("Successful sends : %d\n", success_count);
    printf("Failed sends     : %d\n", error_count);
    printf("==================================\n");

    mq_close(queue);
    mq_unlink(TEST_QUEUE);

    return EXIT_SUCCESS;
}
