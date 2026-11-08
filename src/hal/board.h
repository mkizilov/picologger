#ifndef PICOLOGGER_BOARD_H
#define PICOLOGGER_BOARD_H

#include <stdbool.h>
#include <stdint.h>

void board_init(void);
uint32_t board_milliseconds(void);
void board_set_led(bool on);
void board_idle(void);

#endif
