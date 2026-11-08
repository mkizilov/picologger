#include <stdio.h>

#include "board.h"
#include "heartbeat.h"

int main(void)
{
    board_init();
    puts("PicoLogger starting");
    for (;;) {
        board_set_led(heartbeat_led_on(board_milliseconds()));
        board_idle();
    }
}
