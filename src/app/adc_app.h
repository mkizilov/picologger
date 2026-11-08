#ifndef PICOLOGGER_ADC_APP_H
#define PICOLOGGER_ADC_APP_H

#include <stdint.h>

void adc_app_start(void);
void adc_app_report_boot(void);
void adc_app_poll(void);
uint64_t adc_app_total_samples(void);
uint32_t adc_app_fault(void);

#endif
