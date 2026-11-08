#ifndef PICOLOGGER_SPSC_RING_H
#define PICOLOGGER_SPSC_RING_H

#include "spsc_index.h"
#include <stddef.h>

typedef struct {
    unsigned char *storage;
    size_t record_size;
    uint32_t capacity;
    _Alignas(4) spsc_index_t head;
    _Alignas(4) spsc_index_t tail;
} spsc_ring_t;

/* Exactly one producer and one consumer, both outside interrupt context.
 * The storage and ring must remain alive until both cores stop using them. */
bool spsc_ring_init(spsc_ring_t *ring, void *storage, size_t bytes, uint32_t capacity, size_t record_size);
/* Optional level is the conservative producer-side occupancy snapshot. */
bool spsc_ring_push(spsc_ring_t *ring, const void *record, uint32_t *level);
bool spsc_ring_pop(spsc_ring_t *ring, void *record);
/* Consumer only; producer must have stopped before using this to finish a drain. */
bool spsc_ring_empty(spsc_ring_t *ring);
/* Producer only: conservative snapshot suitable for a high-water metric. */
uint32_t spsc_ring_producer_level(spsc_ring_t *ring);

#endif
