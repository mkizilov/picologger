#include "spsc_ring.h"

#include <string.h>

bool spsc_ring_init(spsc_ring_t *ring, void *storage, size_t bytes, uint32_t capacity, size_t record_size)
{
    if (!ring || !storage || !capacity || (capacity & (capacity - 1u)) || capacity > UINT32_C(0x80000000) ||
        !record_size || record_size > SIZE_MAX / capacity || bytes < (size_t)capacity * record_size) return false;
    if (!spsc_index_init(&ring->head) || !spsc_index_init(&ring->tail)) return false;
    ring->storage = storage;
    ring->record_size = record_size;
    ring->capacity = capacity;
    return true;
}

bool spsc_ring_push(spsc_ring_t *ring, const void *record, uint32_t *level)
{
    uint32_t head = spsc_index_load_relaxed(&ring->head);
    uint32_t tail = spsc_index_load_acquire(&ring->tail);
    if (head - tail == ring->capacity) { if (level) *level = ring->capacity; return false; }
    memcpy(ring->storage + (size_t)(head & (ring->capacity - 1u)) * ring->record_size, record, ring->record_size);
    spsc_index_store_release(&ring->head, head + 1u);
    if (level) *level = head - tail + 1u;
    return true;
}

bool spsc_ring_pop(spsc_ring_t *ring, void *record)
{
    uint32_t tail = spsc_index_load_relaxed(&ring->tail);
    uint32_t head = spsc_index_load_acquire(&ring->head);
    if (tail == head) return false;
    memcpy(record, ring->storage + (size_t)(tail & (ring->capacity - 1u)) * ring->record_size, ring->record_size);
    spsc_index_store_release(&ring->tail, tail + 1u);
    return true;
}

bool spsc_ring_empty(spsc_ring_t *ring)
{
    return spsc_index_load_relaxed(&ring->tail) == spsc_index_load_acquire(&ring->head);
}

uint32_t spsc_ring_producer_level(spsc_ring_t *ring)
{
    return spsc_index_load_relaxed(&ring->head) - spsc_index_load_acquire(&ring->tail);
}
