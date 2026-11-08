#ifndef TEST_POWER_STDLIB_H
#define TEST_POWER_STDLIB_H
#include <stdint.h>
uint64_t time_us_64(void);
void fake_wfi(void);
#define __wfi fake_wfi
#endif
