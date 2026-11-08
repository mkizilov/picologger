#ifndef TEST_IRQ_H
#define TEST_IRQ_H
#include <stdbool.h>
enum { DMA_IRQ_0 = 0, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY = 128 };
void irq_add_shared_handler(unsigned int irq, void (*handler)(void), unsigned char priority);
void irq_set_enabled(unsigned int irq, bool enabled);
#endif
