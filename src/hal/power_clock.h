#ifndef PICOLOGGER_POWER_CLOCK_H
#define PICOLOGGER_POWER_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

/* Initialize before I2C, ADC and SD start. Runtime changes require stopped
 * sampling and a quiescent storage core. USB and ADC clocks remain running. */
void power_clock_init(uint32_t rate_hz);
bool power_clock_select(bool low);
bool power_clock_supported(void);
bool power_clock_faulted(void);
bool power_clock_low(void);
uint32_t power_clock_sys_hz(void);

#endif
