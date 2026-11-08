#include "ring_buffer.h"

#include <limits.h>
#include <string.h>

_Static_assert(UINT_MAX == UINT32_MAX, "ring indices require 32-bit unsigned int");

bool ring_buffer_init(ring_buffer_t *ring, void *storage, size_t storage_bytes,
                      uint32_t capacity, size_t record_size)
{
    if (ring == NULL || storage == NULL || capacity == 0 ||
        (capacity & (capacity - 1)) != 0 || capacity > UINT32_C(0x80000000) ||
        record_size == 0 || record_size > SIZE_MAX / capacity ||
        storage_bytes < (size_t)capacity * record_size) {
        return false;
    }
    atomic_init(&ring->head, 0);
    atomic_init(&ring->tail, 0);
    atomic_init(&ring->overflows, 0);
    if (!atomic_is_lock_free(&ring->head) || !atomic_is_lock_free(&ring->tail) ||
        !atomic_is_lock_free(&ring->overflows)) {
        return false;
    }
    ring->storage = storage;
    ring->capacity = capacity;
    ring->record_size = record_size;
    return true;
}

bool ring_buffer_push(ring_buffer_t *ring, const void *record)
{
    /* Only the producer writes head; its own previous value needs no ordering. */
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    /* Acquire pairs with the consumer's release, so the slot is not reused
     * until the consumer's copy out of it has completed. */
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    if (head - tail == ring->capacity) {
        /* Only the producer writes this diagnostic; no payload is published
         * through it, so relaxed load/store suffices and avoids atomic RMW. */
        unsigned int count = atomic_load_explicit(&ring->overflows, memory_order_relaxed);
        atomic_store_explicit(&ring->overflows, count + 1u, memory_order_relaxed);
        return false;
    }
    memcpy(ring->storage + (size_t)(head & (ring->capacity - 1)) * ring->record_size,
           record, ring->record_size);
    /* Release publishes the entire record before the consumer observes head. */
    atomic_store_explicit(&ring->head, head + 1u, memory_order_release);
    return true;
}

bool ring_buffer_pop(ring_buffer_t *ring, void *record)
{
    /* Only the consumer writes tail; no ordering is needed for its own index. */
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    /* Acquire pairs with the producer's release, making the record visible. */
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    if (tail == head) {
        return false;
    }
    memcpy(record, ring->storage + (size_t)(tail & (ring->capacity - 1)) * ring->record_size,
           ring->record_size);
    /* Release prevents the producer from reusing the slot before the copy. */
    atomic_store_explicit(&ring->tail, tail + 1u, memory_order_release);
    return true;
}

uint32_t ring_buffer_overflows(const ring_buffer_t *ring)
{
    /* The count does not synchronize record accesses; only its value matters. */
    return atomic_load_explicit(&ring->overflows, memory_order_relaxed);
}
