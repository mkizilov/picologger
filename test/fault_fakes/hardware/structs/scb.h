#ifndef TEST_FAULT_SCB_H
#define TEST_FAULT_SCB_H
#include <stdint.h>
typedef struct { volatile uint32_t icsr; } scb_hw_t;
extern scb_hw_t fake_scb_hw;
#define scb_hw (&fake_scb_hw)
#endif
