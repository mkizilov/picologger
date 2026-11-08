#include "unity.h"
#include "logger.h"
#include "log_format.h"
#include "log_writer.h"
#include "sd_storage.h"

#include <string.h>

static uint64_t now;
static uint32_t incoming[16], outgoing[16];
static unsigned int in_read, in_write, out_count, barrier_count, sample_count, sample_pos;
static bool fifo_ready = true, launched;

void fake_memory_barrier(void) { ++barrier_count; }
uint64_t time_us_64(void) { return now; }
void sleep_ms(uint32_t milliseconds) { (void)milliseconds; }
bool stdio_usb_connected(void) { return true; }
bool multicore_fifo_rvalid(void) { return in_read < in_write; }
bool multicore_fifo_wready(void) { return fifo_ready; }
uint32_t multicore_fifo_pop_blocking(void) { return incoming[in_read++]; }
void multicore_fifo_push_blocking(uint32_t value)
{
    TEST_ASSERT_TRUE(fifo_ready);
    TEST_ASSERT_TRUE(out_count < 16);
    outgoing[out_count++] = value;
}
void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack, size_t size)
{
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_NOT_NULL(stack);
    TEST_ASSERT_EQUAL_UINT(8192, size);
    launched = true;
}
log_storage_t sd_storage_interface(void) { return (log_storage_t){0}; }
bool sensor_take_sample(log_record_t *record)
{
    if (sample_pos == sample_count) return false;
    *record = (log_record_t){.sequence = sample_pos++, .timestamp_us = now};
    return true;
}
void setUp(void) {}
void tearDown(void) {}

static void batches_are_nonblocking_and_ownership_is_returned(void)
{
    logger_start();
    TEST_ASSERT_TRUE(launched);
    for (unsigned int i = 0; i < 4; ++i) {
        sample_count += 2;
        logger_poll();
        TEST_ASSERT_EQUAL_UINT(i, out_count);
        now += UINT64_C(1000000);
        logger_poll();
        TEST_ASSERT_EQUAL_UINT(i + 1, out_count);
        TEST_ASSERT_EQUAL_UINT(i, outgoing[i]);
    }
    sample_count += 2;
    logger_poll();
    TEST_ASSERT_EQUAL_UINT(8, sample_pos);
    logger_note_drop(2);
    incoming[in_write++] = UINT32_C(0x10000003);
    incoming[in_write++] = UINT32_C(0x20000002);
    incoming[in_write++] = UINT32_C(0x30002ee0);
    incoming[in_write++] = UINT32_C(0x50001770);
    incoming[in_write++] = UINT32_C(0x40000001);
    incoming[in_write++] = 0;
    now = UINT64_C(10000000);
    logger_poll();
    TEST_ASSERT_EQUAL_UINT(10, sample_pos);
    TEST_ASSERT_TRUE(barrier_count >= 9);
    fifo_ready = false;
    now += UINT64_C(1000000);
    logger_poll();
    TEST_ASSERT_EQUAL_UINT(4, out_count);
    fifo_ready = true;
    logger_poll();
    TEST_ASSERT_EQUAL_UINT(5, out_count);
    TEST_ASSERT_EQUAL_UINT(0, outgoing[4]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(batches_are_nonblocking_and_ownership_is_returned);
    return UNITY_END();
}
