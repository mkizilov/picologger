#include <stdio.h>

#include "board.h"
#include "adc_app.h"
#include "firmware_version.h"
#include "logger.h"
#include "sensor.h"
#include "settings_flash.h"
#include "settings_service.h"
#include "usb_console.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/unique_id.h"

int main(void)
{
    stdio_init_all();
    usb_console_init();
    const bool heartbeat_ok = board_init();
    const settings_t defaults = {PICOLOGGER_SAMPLE_RATE_HZ, SETTINGS_CHANNEL_MASK,
                                 PICOLOGGER_LOG_ROTATE_BYTES, true};
    settings_service_init(settings_flash_interface(), defaults);
    settings_snapshot_t saved;
    settings_t boot;
    bool settings_readable;
    settings_service_get(&saved, &boot, &settings_readable);
    bool bme_enabled = (boot.channels & SETTINGS_CHANNEL_BME280) != 0;
    sensor_configure_boot(boot.rate_hz, bme_enabled, boot.start_on_boot);
    adc_app_configure_boot((boot.channels & SETTINGS_CHANNEL_ADC) != 0);
    logger_configure_boot(boot.rate_hz, boot.rotate_bytes, boot.start_on_boot && bme_enabled);
    sensor_start();
    adc_app_start();
    logger_start();
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
                if (!settings_readable) puts("Settings read failed; compiled defaults active, saves disabled");
                sensor_report_boot();
                adc_app_report_boot();
                logger_report_boot();
                announced = true;
            }
        } else {
            announced = false;
        }
        sensor_poll();
        adc_app_poll();
        logger_poll();
        settings_service_poll();
        usb_console_poll();
        tight_loop_contents();
    }
}
