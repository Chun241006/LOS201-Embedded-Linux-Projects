#include "../include/sms_common.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    circular_buffer_t buffer;
    sensor_sample_t input;
    sensor_sample_t output;
    atomic_bool running = ATOMIC_VAR_INIT(true);
    int result;

    if (circular_buffer_init(&buffer) != 0) {
        fprintf(stderr, "FAIL: buffer initialization\n");
        return EXIT_FAILURE;
    }

    printf("[TEST 1] Initial buffer count: %zu\n",
           buffer.count);

    for (int i = 0; i < SMS_BUFFER_SIZE; i++) {
        input.timestamp_ms = (uint64_t)i;
        input.temperature = 20.0 + i;
        input.humidity = 50.0;

        result = circular_buffer_push(&buffer, &input);

        if (result != 0) {
            fprintf(stderr, "FAIL: push at index %d\n", i);
            circular_buffer_destroy(&buffer);
            return EXIT_FAILURE;
        }
    }

    printf("[TEST 2] Buffer count after 64 pushes: %zu\n",
           buffer.count);

    result = circular_buffer_push(&buffer, &input);

    if (result != 1) {
        fprintf(stderr, "FAIL: full-buffer detection\n");
        circular_buffer_destroy(&buffer);
        return EXIT_FAILURE;
    }

    printf("[TEST 3] Full-buffer detection: PASS\n");

    for (int i = 0; i < SMS_BUFFER_SIZE; i++) {
        result = circular_buffer_pop(&buffer,
                                     &output,
                                     &running);

        if (result != 0 ||
            output.timestamp_ms != (uint64_t)i) {
            fprintf(stderr, "FAIL: FIFO at index %d\n", i);
            circular_buffer_destroy(&buffer);
            return EXIT_FAILURE;
        }
    }

    printf("[TEST 4] FIFO order: PASS\n");
    printf("[TEST 5] Final buffer count: %zu\n",
           buffer.count);

    circular_buffer_destroy(&buffer);

    printf("ALL CIRCULAR BUFFER TESTS PASSED\n");

    return EXIT_SUCCESS;
}
