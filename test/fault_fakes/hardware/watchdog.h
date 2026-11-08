#ifndef TEST_FAULT_WATCHDOG_H
#define TEST_FAULT_WATCHDOG_H
#include <stdbool.h>
bool watchdog_enable_caused_reboot(void);
bool watchdog_caused_reboot(void);
#endif
