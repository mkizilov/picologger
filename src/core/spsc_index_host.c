#include "spsc_index.h"

bool spsc_index_init(spsc_index_t *index)
{
    atomic_init(index, 0);
    return atomic_is_lock_free(index);
}
uint32_t spsc_index_load_relaxed(const spsc_index_t *index) { return atomic_load_explicit(index, memory_order_relaxed); }
uint32_t spsc_index_load_acquire(const spsc_index_t *index) { return atomic_load_explicit(index, memory_order_acquire); }
void spsc_index_store_release(spsc_index_t *index, uint32_t value) { atomic_store_explicit(index, value, memory_order_release); }
