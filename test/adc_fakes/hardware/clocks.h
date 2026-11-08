#ifndef TEST_CLOCKS_H
#define TEST_CLOCKS_H
#include <stdint.h>
enum { clk_adc = 1 };
uint32_t clock_get_hz(int clock);
#endif
