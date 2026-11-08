#ifndef PICOLOGGER_USB_CONSOLE_H
#define PICOLOGGER_USB_CONSOLE_H

#include "log_format.h"

void usb_console_init(void);
void usb_console_poll(void);
void usb_console_sample(const log_record_t *record);

#endif
