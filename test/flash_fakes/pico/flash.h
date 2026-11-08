#ifndef TEST_FLASH_PICO_H
#define TEST_FLASH_PICO_H
#include <stdint.h>
int flash_safe_execute(void (*callback)(void *), void *context, uint32_t timeout_ms);
#endif
