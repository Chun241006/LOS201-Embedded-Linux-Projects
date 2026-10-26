#include "../include/sms_common.h"

#include <errno.h>
#include <string.h>

/*
 * Initialize circular buffer and synchronization objects.
 * Return 0 on success, -1 on failure.
 */
int circular_buffer_init(circular_buffer_t *buffer)
{
    if (buffer == NULL)
        return -1;

    memset(buffer, 0, sizeof(*buffer));

    if (pthread_mutex_init(&buffer->mutex, NULL) != 0)
        return -1;

    if (pthread_cond_init(&buffer->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&buffer->mutex);
        return -1;
    }

    return 0;
}

/*
 * Insert a sensor sample into the circular buffer.
 * Return 0 on success, 1 if full, -1 on error.
 */
int circular_buffer_push(circular_buffer_t *buffer,
                         const sensor_sample_t *sample)
{
    if (buffer == NULL || sample == NULL)
        return -1;

    if (pthread_mutex_lock(&buffer->mutex) != 0)
        return -1;

    if (buffer->count == SMS_BUFFER_SIZE) {
        pthread_mutex_unlock(&buffer->mutex);
        return 1;
    }

    buffer->samples[buffer->head] = *sample;

    buffer->head = (buffer->head + 1) % SMS_BUFFER_SIZE;
    buffer->count++;

    pthread_cond_signal(&buffer->not_empty);

    pthread_mutex_unlock(&buffer->mutex);

    return 0;
}


/*
 * Remove one sensor sample from the circular buffer.
 * Return 0 on success, 1 when stopped, -1 on error.
 */
int circular_buffer_pop(circular_buffer_t *buffer,
                        sensor_sample_t *sample,
                        const atomic_bool *running)
{
    if (buffer == NULL || sample == NULL || running == NULL)
        return -1;

    if (pthread_mutex_lock(&buffer->mutex) != 0)
        return -1;

    while (buffer->count == 0 && atomic_load(running)){
        if (pthread_cond_wait(&buffer->not_empty,
                              &buffer->mutex) != 0) {
            pthread_mutex_unlock(&buffer->mutex);
            return -1;
        }
    }

    if (buffer->count == 0 && !atomic_load(running)) {
        pthread_mutex_unlock(&buffer->mutex);
        return 1;
    }

    *sample = buffer->samples[buffer->tail];

    buffer->tail = (buffer->tail + 1) % SMS_BUFFER_SIZE;
    buffer->count--;

    pthread_mutex_unlock(&buffer->mutex);

    return 0;
}


/*
 * Release synchronization resources.
 * Call only after all worker threads have stopped.
 */
void circular_buffer_destroy(circular_buffer_t *buffer)
{
    if (buffer == NULL)
        return;

    pthread_cond_destroy(&buffer->not_empty);
    pthread_mutex_destroy(&buffer->mutex);
}
