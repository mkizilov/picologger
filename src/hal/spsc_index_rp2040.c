#include "spsc_index.h"

_Static_assert(sizeof(spsc_index_t) == sizeof(uint32_t), "RP2040 indices must be 32-bit");
_Static_assert(_Alignof(spsc_index_t) >= 4, "RP2040 indices must be word aligned");

/* Aligned word accesses are indivisible on RP2040. These GNU-asm accesses are
 * target-specific: C11 atomics on this SDK may call spinlock-based helpers. */
static inline uint32_t load_word(const spsc_index_t *index)
{
    uint32_t value;
    __asm volatile("ldr %0, [%1]" : "=l"(value) : "l"(index) : "memory");
    return value;
}
static inline void store_word(spsc_index_t *index, uint32_t value)
{
    __asm volatile("str %0, [%1]" : : "l"(value), "l"(index) : "memory");
}
bool spsc_index_init(spsc_index_t *index) { store_word(index, 0); return true; }
uint32_t spsc_index_load_relaxed(const spsc_index_t *index) { return load_word(index); }
uint32_t spsc_index_load_acquire(const spsc_index_t *index)
{
    uint32_t value = load_word(index);
    __asm volatile("dmb" ::: "memory");
    return value;
}
void spsc_index_store_release(spsc_index_t *index, uint32_t value)
{
    __asm volatile("dmb" ::: "memory");
    store_word(index, value);
}
