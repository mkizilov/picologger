#ifndef TEST_ADC_PICO_STDLIB_H
#define TEST_ADC_PICO_STDLIB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
uint64_t time_us_64(void);
uint32_t time_us_32(void);
void fake_compiler_barrier(void);
#define __compiler_memory_barrier() fake_compiler_barrier()
#endif
