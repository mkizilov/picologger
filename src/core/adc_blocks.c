#include "adc_blocks.h"

#include <stdatomic.h>
#include <string.h>

_Static_assert((ADC_BLOCK_QUEUE_CAPACITY & (ADC_BLOCK_QUEUE_CAPACITY - 1)) == 0,
               "ADC block capacity must be a power of two");

void adc_blocks_init(adc_blocks_t *blocks)
{
    memset(blocks, 0, sizeof(*blocks));
}

void adc_blocks_fail(adc_blocks_t *blocks, adc_capture_fault_t fault)
{
    if (blocks->fault == ADC_CAPTURE_OK) blocks->fault = (uint32_t)fault;
    blocks->reserved = false;
}

adc_block_t *adc_blocks_reserve(adc_blocks_t *blocks, uint8_t channel,
                                bool fifo_overrun, uint64_t completed_us)
{
    if (blocks->fault != ADC_CAPTURE_OK) return NULL;
    if (fifo_overrun) { adc_blocks_fail(blocks, ADC_CAPTURE_FIFO_OVERRUN); return NULL; }
    if (blocks->reserved || channel != blocks->expected_channel) {
        adc_blocks_fail(blocks, ADC_CAPTURE_DMA_ORDER);
        return NULL;
    }
    uint32_t head = blocks->head;
    uint32_t tail = blocks->tail;
    atomic_signal_fence(memory_order_acquire);
    if (head - tail == ADC_BLOCK_QUEUE_CAPACITY) {
        adc_blocks_fail(blocks, ADC_CAPTURE_QUEUE_FULL);
        return NULL;
    }
    adc_block_t *slot = &blocks->slots[head & (ADC_BLOCK_QUEUE_CAPACITY - 1)];
    slot->sequence = blocks->next_sequence;
    slot->completed_us = completed_us;
    blocks->reserved = true;
    return slot;
}

void adc_blocks_commit(adc_blocks_t *blocks)
{
    if (!blocks->reserved || blocks->fault != ADC_CAPTURE_OK) return;
    blocks->reserved = false;
    ++blocks->next_sequence;
    blocks->expected_channel ^= 1u;
    /* Publish after the whole block is written; only the ISR writes head. */
    atomic_signal_fence(memory_order_release);
    ++blocks->head;
}

bool adc_blocks_take(adc_blocks_t *blocks, adc_block_t *out)
{
    uint32_t tail = blocks->tail;
    if (tail == blocks->head) return false;
    /* Observe the complete slot before copying; the producer cannot reuse it
     * until this consumer publishes the new tail. */
    atomic_signal_fence(memory_order_acquire);
    *out = blocks->slots[tail & (ADC_BLOCK_QUEUE_CAPACITY - 1)];
    atomic_signal_fence(memory_order_release);
    blocks->tail = tail + 1u;
    return true;
}
