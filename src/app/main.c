#include <stdio.h>

#include "board.h"
#include "adc_app.h"
#include "firmware_version.h"
#include "sensor.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/unique_id.h"

int main(void)
{
    stdio_init_all();
    const bool heartbeat_ok = board_init();
    sensor_start();
    adc_app_start();
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
                sensor_report_boot();
                adc_app_report_boot();
                announced = true;
            }
        } else {
            announced = false;
        }
        sensor_poll();
        adc_app_poll();
        tight_loop_contents();
    }
}
