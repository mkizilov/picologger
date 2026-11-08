#ifndef PICOLOGGER_ADC_APP_H
#define PICOLOGGER_ADC_APP_H

#include <stdint.h>
#include <stdbool.h>

void adc_app_start(void);
void adc_app_configure_boot(bool enabled);
void adc_app_report_boot(void);
void adc_app_poll(void);
uint64_t adc_app_total_samples(void);
uint32_t adc_app_fault(void);
uint32_t adc_app_flash_pauses(void);
bool adc_app_watchdog_expected(void);
bool adc_app_pause_for_flash(bool *was_running);
bool adc_app_resume_after_flash(bool was_running);

#endif
