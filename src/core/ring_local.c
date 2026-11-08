#include "ring_buffer.h"

#include <string.h>

bool ring_local_init(ring_local_t *ring, void *storage, size_t storage_bytes,
                     uint32_t capacity, size_t record_size)
{
    if (ring == NULL || storage == NULL || capacity == 0 ||
        (capacity & (capacity - 1)) != 0 || capacity > UINT32_C(0x80000000) ||
        record_size == 0 || record_size > SIZE_MAX / capacity ||
        storage_bytes < (size_t)capacity * record_size) return false;
    *ring = (ring_local_t){storage, record_size, capacity, 0, 0, 0};
    return true;
}

bool ring_local_push(ring_local_t *ring, const void *record)
{
    if (ring->head - ring->tail == ring->capacity) {
        ++ring->overflows;
        return false;
    }
    memcpy(ring->storage + (size_t)(ring->head & (ring->capacity - 1)) * ring->record_size,
           record, ring->record_size);
    ++ring->head;
    return true;
}

bool ring_local_pop(ring_local_t *ring, void *record)
{
    if (ring->tail == ring->head) return false;
    memcpy(record, ring->storage + (size_t)(ring->tail & (ring->capacity - 1)) * ring->record_size,
           ring->record_size);
    ++ring->tail;
    return true;
}
