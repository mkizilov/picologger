#ifndef PICOLOGGER_RING_BUFFER_H
#define PICOLOGGER_RING_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

typedef struct {
    unsigned char *storage;
    size_t record_size;
    uint32_t capacity;
    atomic_uint head;
    atomic_uint tail;
    atomic_uint overflows;
} ring_buffer_t;

/* Initialize before sharing. Storage must outlive the ring; capacity is a
 * nonzero power of two, at most 2^31. Returns false if arguments are invalid
 * or the implementation cannot provide lock-free atomic unsigned integers. */
bool ring_buffer_init(ring_buffer_t *ring, void *storage, size_t storage_bytes,
                      uint32_t capacity, size_t record_size);

/* Only the producer may push; an observed full ring rejects the record and increments
 * overflows modulo 2^32. The input must not overlap ring storage. */
bool ring_buffer_push(ring_buffer_t *ring, const void *record);
/* Only the consumer may pop. The output must not overlap ring storage. */
bool ring_buffer_pop(ring_buffer_t *ring, void *record);
/* An atomic snapshot of rejected pushes; may be read by either side. */
uint32_t ring_buffer_overflows(const ring_buffer_t *ring);

/* Single-context variant: push and pop must both run in the same non-ISR
 * context. Unlike ring_buffer_t it is not safe across interrupts or cores. */
typedef struct {
    unsigned char *storage;
    size_t record_size;
    uint32_t capacity, head, tail, overflows;
} ring_local_t;

bool ring_local_init(ring_local_t *ring, void *storage, size_t storage_bytes,
                     uint32_t capacity, size_t record_size);
bool ring_local_push(ring_local_t *ring, const void *record);
bool ring_local_pop(ring_local_t *ring, void *record);

#endif
