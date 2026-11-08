#ifndef TEST_LOGGER_STDLIB_H
#define TEST_LOGGER_STDLIB_H
#include <stdint.h>
uint64_t time_us_64(void);
void sleep_ms(uint32_t milliseconds);
void fake_wfi(void);
#define __wfi fake_wfi
#endif
