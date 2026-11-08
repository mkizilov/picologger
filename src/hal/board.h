#ifndef PICOLOGGER_BOARD_H
#define PICOLOGGER_BOARD_H

#include <stdbool.h>
bool board_init(void);
void board_poll(void);
const char *board_reset_reason(void);

#endif
