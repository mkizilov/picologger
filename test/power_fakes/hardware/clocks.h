#ifndef TEST_POWER_CLOCKS_H
#define TEST_POWER_CLOCKS_H
#include <stdbool.h>
#include <stdint.h>
typedef unsigned int uint;
typedef enum { clk_sys, clk_peri, clk_usb, clk_adc, clk_ref, CLK_COUNT } clock_handle_t;
typedef struct { uint32_t ctrl; } test_clock_slice_t;
typedef struct { test_clock_slice_t clk[CLK_COUNT]; } test_clocks_hw_t;
extern test_clocks_hw_t fake_clocks_hw;
#define clocks_hw (&fake_clocks_hw)
#define XOSC_HZ 12000000u
uint32_t clock_get_hz(clock_handle_t clock);
void clock_configure_undivided(clock_handle_t clock, uint32_t src, uint32_t aux, uint32_t frequency);
bool check_sys_clock_khz(uint32_t khz, uint *vco, uint *p1, uint *p2);
bool set_sys_clock_khz(uint32_t khz, bool required);
void set_sys_clock_48mhz(void);
#endif
