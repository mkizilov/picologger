#ifndef PICOLOGGER_USB_CONSOLE_H
#define PICOLOGGER_USB_CONSOLE_H

#include <stdbool.h>
#include "log_format.h"

void usb_console_init(void);
void usb_console_poll(void);
bool usb_console_has_work(void);
void usb_console_sample(const log_record_t *record);

#endif
