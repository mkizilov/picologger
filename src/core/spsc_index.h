#ifndef PICOLOGGER_SPSC_INDEX_H
#define PICOLOGGER_SPSC_INDEX_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef _Atomic uint32_t spsc_index_t;
bool spsc_index_init(spsc_index_t *index);
uint32_t spsc_index_load_relaxed(const spsc_index_t *index);
uint32_t spsc_index_load_acquire(const spsc_index_t *index);
void spsc_index_store_release(spsc_index_t *index, uint32_t value);

#endif
