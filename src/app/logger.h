#ifndef PICOLOGGER_LOGGER_H
#define PICOLOGGER_LOGGER_H

#include <stdint.h>

void logger_start(void);
void logger_poll(void);
void logger_note_drop(uint32_t count);
void logger_report_boot(void);

#endif
