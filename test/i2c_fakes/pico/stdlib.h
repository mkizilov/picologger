#ifndef TEST_I2C_PICO_STDLIB_H
#define TEST_I2C_PICO_STDLIB_H
#include <stdbool.h>
#include <stdint.h>
#define GPIO_FUNC_I2C 3
void gpio_set_function(unsigned int pin, unsigned int function);
void gpio_pull_up(unsigned int pin);
uint64_t time_us_64(void);
#endif
