#ifndef TEST_CHANNEL_QUEUE_H
#define TEST_CHANNEL_QUEUE_H
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint8_t *data; uint32_t values[8]; unsigned int head, tail, count; } queue_t;
void queue_init(queue_t *queue, unsigned int element_size, unsigned int element_count);
void queue_free(queue_t *queue);
bool queue_try_add(queue_t *queue, const void *value);
bool queue_try_remove(queue_t *queue, void *value);
void queue_add_blocking(queue_t *queue, const void *value);
#endif
