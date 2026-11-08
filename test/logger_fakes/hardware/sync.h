#ifndef TEST_LOGGER_SYNC_H
#define TEST_LOGGER_SYNC_H
#include <stdint.h>
void fake_memory_barrier(void);
uint32_t save_and_disable_interrupts(void);
void restore_interrupts(uint32_t state);
#define __dmb() fake_memory_barrier()
#endif
