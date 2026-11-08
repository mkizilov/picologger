#ifndef PICOLOGGER_ADC_BLOCKS_H
#define PICOLOGGER_ADC_BLOCKS_H

#include <stdbool.h>
#include <stdint.h>

#define ADC_BLOCK_SAMPLES 256u
#define ADC_BLOCK_QUEUE_CAPACITY 8u

typedef struct {
    uint32_t sequence;
    uint64_t completed_us;
    uint16_t samples[ADC_BLOCK_SAMPLES];
} adc_block_t;

typedef enum {
    ADC_CAPTURE_OK = 0,
    ADC_CAPTURE_NO_DMA,
    ADC_CAPTURE_FIFO_OVERRUN,
    ADC_CAPTURE_QUEUE_FULL,
    ADC_CAPTURE_DMA_ORDER,
    ADC_CAPTURE_DMA_LATE,
    ADC_CAPTURE_DMA_ERROR,
    ADC_CAPTURE_STALLED,
    ADC_CAPTURE_FIFO_UNDERRUN,
    ADC_CAPTURE_BAD_CLOCK
} adc_capture_fault_t;

/* One IRQ producer and one foreground consumer on the same RP2040 core.
 * Aligned 32-bit volatile indices are indivisible on that core; signal fences
 * keep payload accesses on the correct side of publication/release. This is
 * not a C-thread or cross-core queue. Stop DMA before reinitializing it. */
typedef struct {
    adc_block_t slots[ADC_BLOCK_QUEUE_CAPACITY];
    volatile uint32_t head, tail, fault;
    uint32_t next_sequence;
    uint8_t expected_channel;
    bool reserved;
} adc_blocks_t;

void adc_blocks_init(adc_blocks_t *blocks);
/* Producer reserves one slot; queue-full, FIFO overrun and unexpected channel
 * latch a fault. The producer must commit or discard before another reserve. */
adc_block_t *adc_blocks_reserve(adc_blocks_t *blocks, uint8_t channel,
                                bool fifo_overrun, uint64_t completed_us);
void adc_blocks_commit(adc_blocks_t *blocks);
void adc_blocks_fail(adc_blocks_t *blocks, adc_capture_fault_t fault);
bool adc_blocks_take(adc_blocks_t *blocks, adc_block_t *out);

#endif
