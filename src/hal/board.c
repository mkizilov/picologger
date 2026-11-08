#include "board.h"

#include "pico/stdlib.h"

void board_init(void)
{
    stdio_init_all();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
}

uint32_t board_milliseconds(void)
{
    return to_ms_since_boot(get_absolute_time());
}

void board_set_led(bool on)
{
    gpio_put(PICO_DEFAULT_LED_PIN, on);
}

void board_idle(void)
{
    sleep_ms(10);
}
