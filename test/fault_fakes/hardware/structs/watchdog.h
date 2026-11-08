#ifndef TEST_FAULT_WATCHDOG_STRUCT_H
#define TEST_FAULT_WATCHDOG_STRUCT_H
#include <stdint.h>
typedef struct { volatile uint32_t ctrl, reason, scratch[8]; } watchdog_hw_t;
extern watchdog_hw_t fake_watchdog_hw;
#define watchdog_hw (&fake_watchdog_hw)
#endif
