#include "board.h"

#include "heartbeat.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
static volatile bool requested_led;
static bool applied_led;
#else
static bool led_on;
#endif

static struct repeating_timer heartbeat_timer;

static bool heartbeat_callback(struct repeating_timer *timer)
{
    (void)timer;
#ifdef CYW43_WL_GPIO_LED_PIN
    requested_led = heartbeat_toggle(requested_led);
#else
    led_on = heartbeat_toggle(led_on);
    gpio_put(PICO_DEFAULT_LED_PIN, led_on);
#endif
    return true;
}

bool board_init(void)
{
#ifdef CYW43_WL_GPIO_LED_PIN
    if (cyw43_arch_init() != 0) {
        return false;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
#else
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, false);
#endif
    return add_repeating_timer_ms(-500, heartbeat_callback, NULL, &heartbeat_timer);
}

void board_poll(void)
{
#ifdef CYW43_WL_GPIO_LED_PIN
    bool desired = requested_led;
    if (desired != applied_led) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, desired);
        applied_led = desired;
    }
#endif
}

const char *board_reset_reason(void)
{
    if (watchdog_enable_caused_reboot()) {
        return "watchdog timeout";
    }
    if (watchdog_caused_reboot()) {
        return "watchdog or software reset";
    }
    return "power-on, RUN reset, or unknown";
}
