#ifndef TEST_LOGGER_MULTICORE_H
#define TEST_LOGGER_MULTICORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack, size_t size);
#endif
