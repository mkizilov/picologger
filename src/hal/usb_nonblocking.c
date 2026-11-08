#include "usb_nonblocking.h"
#include "fault_report.h"

#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/stdio_usb.h"
#include "tusb.h"

bool usb_nb_connected(void) { return stdio_usb_connected(); }

bool usb_nb_input_ready(void)
{
    uint32_t state = save_and_disable_interrupts();
    bool ready = tud_cdc_connected() && tud_cdc_available() != 0;
    restore_interrupts(state);
    return ready;
}

bool usb_nb_write_ready(size_t length)
{
    if (length > 64) return false;
    uint32_t state = save_and_disable_interrupts();
    bool ready = tud_cdc_connected() && tud_cdc_write_available() >= length;
    restore_interrupts(state);
    return ready;
}

size_t usb_nb_read(uint8_t *data, size_t capacity)
{
    uint32_t state = save_and_disable_interrupts();
    uint32_t count = 0;
    if (tud_cdc_connected() && tud_cdc_available())
        count = tud_cdc_read(data, (uint32_t)capacity);
    restore_interrupts(state);
    return count;
}

bool usb_nb_write(const char *data, size_t length)
{
    if (length > 64) return false;
    uint32_t state = save_and_disable_interrupts();
    bool ok = false;
    if (tud_cdc_connected() && tud_cdc_write_available() >= length) {
        ok = tud_cdc_write(data, (uint32_t)length) == length;
        (void)tud_cdc_write_flush();
    }
    restore_interrupts(state);
    return ok;
}

void usb_nb_reboot(void)
{
    uint32_t state = save_and_disable_interrupts();
    watchdog_reboot(0, 0, 100);
    fault_preserve_sram_on_watchdog();
    restore_interrupts(state);
}
