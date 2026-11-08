#ifndef TEST_FAULT_ADDRESS_MAPPED_H
#define TEST_FAULT_ADDRESS_MAPPED_H
#include <stdint.h>
static inline void hw_set_bits(volatile uint32_t *address, uint32_t bits) { *address |= bits; }
static inline void hw_clear_bits(volatile uint32_t *address, uint32_t bits) { *address &= ~bits; }
#endif
