#ifndef PICOLOGGER_ADC_CAPTURE_H
#define PICOLOGGER_ADC_CAPTURE_H

#include "adc_blocks.h"

/* Starts GP26 / ADC0 at nominal 10 kHz, two 256-sample DMA ring halves. */
bool adc_capture_start(void);
/* Called in foreground; detects a FIFO overrun or stalled DMA. */
void adc_capture_check(void);
bool adc_capture_take(adc_block_t *block);
adc_capture_fault_t adc_capture_fault(void);
bool adc_capture_fifo_overrun(void);
bool adc_capture_fifo_underrun(void);

#endif
