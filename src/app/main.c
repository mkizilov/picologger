#include <stdio.h>

#include "board.h"
#include "firmware_version.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/unique_id.h"

int main(void)
{
    stdio_init_all();
    const bool heartbeat_ok = board_init();
    char id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    pico_get_unique_board_id_string(id, sizeof(id));
    const char *reset_reason = board_reset_reason();
    bool announced = false;
    for (;;) {
        board_poll();
        if (stdio_usb_connected()) {
            if (!announced) {
                printf("PicoLogger version: %s\nBoard ID: %s\nReset reason: %s\n",
                       PICOLOGGER_VERSION, id, reset_reason);
                if (!heartbeat_ok) {
                    puts("Heartbeat initialization failed");
                }
                announced = true;
            }
        } else {
            announced = false;
        }
        tight_loop_contents();
    }
}
