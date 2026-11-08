#ifndef PICOLOGGER_WATCHDOG_MONITOR_H
#define PICOLOGGER_WATCHDOG_MONITOR_H

#include <stdint.h>

void watchdog_monitor_start(void);
void watchdog_monitor_poll(void);
uint32_t watchdog_monitor_missing(void);

#endif
