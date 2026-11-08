#include "unity.h"
#include "adc_blocks.h"

#include <limits.h>

static adc_blocks_t queue;

void setUp(void) { adc_blocks_init(&queue); }
void tearDown(void) {}

static void publish(uint8_t channel, uint16_t value)
{
    adc_block_t *block = adc_blocks_reserve(&queue, channel, false, 1000u + queue.next_sequence);
    TEST_ASSERT_NOT_NULL(block);
    for (unsigned int i = 0; i < ADC_BLOCK_SAMPLES; ++i) block->samples[i] = (uint16_t)(value + i);
    adc_blocks_commit(&queue);
}

static void alternating_blocks_have_sequences_and_payload(void)
{
    adc_block_t out;
    TEST_ASSERT_FALSE(adc_blocks_take(&queue, &out));
    adc_block_t *slot = adc_blocks_reserve(&queue, 0, false, 123);
    TEST_ASSERT_NOT_NULL(slot);
    slot->samples[0] = 1234;
    TEST_ASSERT_FALSE(adc_blocks_take(&queue, &out));
    adc_blocks_commit(&queue);
    publish(1, 200);
    TEST_ASSERT_TRUE(adc_blocks_take(&queue, &out));
    TEST_ASSERT_EQUAL_UINT32(0, out.sequence);
    TEST_ASSERT_EQUAL_UINT64(123, out.completed_us);
    TEST_ASSERT_EQUAL_UINT16(1234, out.samples[0]);
    TEST_ASSERT_TRUE(adc_blocks_take(&queue, &out));
    TEST_ASSERT_EQUAL_UINT32(1, out.sequence);
    TEST_ASSERT_EQUAL_UINT16(200, out.samples[0]);
    TEST_ASSERT_EQUAL_UINT16(455, out.samples[255]);
    TEST_ASSERT_FALSE(adc_blocks_take(&queue, &out));
}

static void queue_full_stops_without_overwriting(void)
{
    for (unsigned int i = 0; i < ADC_BLOCK_QUEUE_CAPACITY; ++i) publish((uint8_t)(i & 1u), (uint16_t)i);
    TEST_ASSERT_NULL(adc_blocks_reserve(&queue, 0, false, 0));
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_QUEUE_FULL, queue.fault);
    adc_block_t out;
    for (unsigned int i = 0; i < ADC_BLOCK_QUEUE_CAPACITY; ++i) {
        TEST_ASSERT_TRUE(adc_blocks_take(&queue, &out));
        TEST_ASSERT_EQUAL_UINT32(i, out.sequence);
        TEST_ASSERT_EQUAL_UINT16(i, out.samples[0]);
    }
    TEST_ASSERT_FALSE(adc_blocks_take(&queue, &out));
    TEST_ASSERT_NULL(adc_blocks_reserve(&queue, 0, false, 0));
}

static void detects_overrun_order_and_aborted_reservation(void)
{
    TEST_ASSERT_NULL(adc_blocks_reserve(&queue, 0, true, 0));
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_FIFO_OVERRUN, queue.fault);
    adc_blocks_init(&queue);
    TEST_ASSERT_NULL(adc_blocks_reserve(&queue, 1, false, 0));
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_DMA_ORDER, queue.fault);
    adc_blocks_init(&queue);
    TEST_ASSERT_NOT_NULL(adc_blocks_reserve(&queue, 0, false, 0));
    adc_blocks_fail(&queue, ADC_CAPTURE_DMA_LATE);
    adc_blocks_commit(&queue);
    adc_block_t out;
    TEST_ASSERT_FALSE(adc_blocks_take(&queue, &out));
    adc_blocks_fail(&queue, ADC_CAPTURE_STALLED);
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_DMA_LATE, queue.fault);
}

static void queue_and_sequence_wrap(void)
{
    queue.head = queue.tail = UINT32_MAX;
    queue.next_sequence = UINT32_MAX;
    publish(0, 4);
    publish(1, 5);
    adc_block_t out;
    TEST_ASSERT_TRUE(adc_blocks_take(&queue, &out));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, out.sequence);
    TEST_ASSERT_TRUE(adc_blocks_take(&queue, &out));
    TEST_ASSERT_EQUAL_UINT32(0, out.sequence);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(alternating_blocks_have_sequences_and_payload);
    RUN_TEST(queue_full_stops_without_overwriting);
    RUN_TEST(detects_overrun_order_and_aborted_reservation);
    RUN_TEST(queue_and_sequence_wrap);
    return UNITY_END();
}
