#ifndef TEST_CYW43_ARCH_H
#define TEST_CYW43_ARCH_H
#include <stdbool.h>
int cyw43_arch_init(void);
void cyw43_arch_gpio_put(unsigned int pin, bool value);
#endif
