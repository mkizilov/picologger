#ifndef TEST_FAULT_PSM_STRUCT_H
#define TEST_FAULT_PSM_STRUCT_H
#include <stdint.h>
typedef struct { volatile uint32_t wdsel; } psm_hw_t;
extern psm_hw_t fake_psm_hw;
#define psm_hw (&fake_psm_hw)
#endif
