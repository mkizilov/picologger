#ifndef TEST_FAULT_SIO_H
#define TEST_FAULT_SIO_H
#include <stdint.h>
typedef struct { volatile uint32_t cpuid; } sio_hw_t;
extern sio_hw_t fake_sio_hw;
#define sio_hw (&fake_sio_hw)
#endif
