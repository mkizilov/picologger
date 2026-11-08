#ifndef TEST_LOGGER_MULTICORE_H
#define TEST_LOGGER_MULTICORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool multicore_fifo_rvalid(void);
bool multicore_fifo_wready(void);
uint32_t multicore_fifo_pop_blocking(void);
void multicore_fifo_push_blocking(uint32_t value);
void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack, size_t size);
#endif
