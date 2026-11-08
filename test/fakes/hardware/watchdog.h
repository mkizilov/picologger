#ifndef TEST_HARDWARE_WATCHDOG_H
#define TEST_HARDWARE_WATCHDOG_H
#include <stdbool.h>
bool watchdog_caused_reboot(void);
bool watchdog_enable_caused_reboot(void);
#endif
