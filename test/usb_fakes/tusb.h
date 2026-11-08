#ifndef TEST_USB_TUSB_H
#define TEST_USB_TUSB_H
#include <stdbool.h>
#include <stdint.h>
bool tud_cdc_connected(void);
uint32_t tud_cdc_available(void);
uint32_t tud_cdc_read(void *buffer, uint32_t size);
uint32_t tud_cdc_write_available(void);
uint32_t tud_cdc_write(const void *buffer, uint32_t size);
uint32_t tud_cdc_write_flush(void);
#endif
