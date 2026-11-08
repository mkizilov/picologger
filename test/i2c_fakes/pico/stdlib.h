#ifndef TEST_I2C_PICO_STDLIB_H
#define TEST_I2C_PICO_STDLIB_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define GPIO_FUNC_I2C 3
#define GPIO_OUT true
struct repeating_timer { int unused; };
typedef bool (*repeating_timer_callback_t)(struct repeating_timer *);
bool add_repeating_timer_us(int64_t delay, repeating_timer_callback_t callback,
                            void *user_data, struct repeating_timer *timer);
void gpio_init(unsigned int pin);
void gpio_set_dir(unsigned int pin, bool output);
void gpio_put(unsigned int pin, bool value);
void gpio_set_function(unsigned int pin, unsigned int function);
void gpio_pull_up(unsigned int pin);
uint64_t time_us_64(void);
#endif
