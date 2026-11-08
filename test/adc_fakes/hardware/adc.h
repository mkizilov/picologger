#ifndef TEST_ADC_H
#define TEST_ADC_H
#include <stdbool.h>
#include <stdint.h>
typedef struct { volatile uint32_t fcs, fifo; } adc_hw_t;
extern adc_hw_t fake_adc_hw;
#define adc_hw (&fake_adc_hw)
void adc_init(void);
void adc_gpio_init(unsigned int pin);
void adc_select_input(unsigned int input);
void adc_set_round_robin(unsigned int mask);
void adc_fifo_setup(bool enabled, bool dreq, uint16_t threshold, bool error, bool shift);
void adc_fifo_drain(void);
void adc_set_clkdiv(float divider);
void adc_run(bool run);
#endif
