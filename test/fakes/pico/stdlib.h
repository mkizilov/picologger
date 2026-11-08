#ifndef TEST_PICO_STDLIB_H
#define TEST_PICO_STDLIB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PICO_DEFAULT_LED_PIN 25
#define GPIO_OUT true
struct repeating_timer { int unused; };
typedef bool (*repeating_timer_callback_t)(struct repeating_timer *);
bool add_repeating_timer_ms(int32_t delay, repeating_timer_callback_t callback,
                            void *user_data, struct repeating_timer *timer);
void gpio_init(unsigned int pin);
void gpio_set_dir(unsigned int pin, bool output);
void gpio_put(unsigned int pin, bool value);
#endif
