#ifndef PICOLOGGER_USB_NONBLOCKING_H
#define PICOLOGGER_USB_NONBLOCKING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool usb_nb_connected(void);
size_t usb_nb_read(uint8_t *data, size_t capacity);
/* Whole frame or nothing, maximum 64 bytes. */
bool usb_nb_write(const char *data, size_t length);
void usb_nb_reboot(void);

#endif
