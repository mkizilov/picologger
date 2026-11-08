#include "unity.h"
#include "logger.h"
#include "log_format.h"
#include "log_writer.h"
#include "sd_storage.h"

#include <string.h>

static uint64_t now;
static uint32_t incoming[16], outgoing[16];
static unsigned int in_read, in_write, out_count, barrier_count;
static bool fifo_ready = true, launched, channel_ok = true;

void fake_memory_barrier(void) { ++barrier_count; }
uint64_t time_us_64(void) { return now; }
void sleep_ms(uint32_t milliseconds) { (void)milliseconds; }
bool stdio_usb_connected(void) { return true; }
static bool fake_queue_rvalid(void) { return in_read < in_write; }
static uint32_t fake_queue_pop(void) { return incoming[in_read++]; }
static void fake_queue_push(uint32_t value)
{
    TEST_ASSERT_TRUE(fifo_ready);
    TEST_ASSERT_TRUE(out_count < 16);
    outgoing[out_count++] = value;
}
bool logger_channel_init(void) { return channel_ok; }
bool logger_channel_to_worker(uint32_t value)
{
    TEST_ASSERT_TRUE((value & UINT32_C(0xf0000000)) != 0);
    if (!fifo_ready) return false;
    fake_queue_push(value);
    return true;
}
bool logger_channel_from_worker(uint32_t *value)
{
    if (!fake_queue_rvalid()) return false;
    *value = fake_queue_pop();
    return true;
}
void logger_channel_to_main(uint32_t value) { (void)value; }
bool logger_channel_from_main(uint32_t *value) { (void)value; return false; }
bool flash_safe_execute_core_init(void) { return true; }
void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack, size_t size)
{
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_NOT_NULL(stack);
    TEST_ASSERT_EQUAL_UINT(8192, size);
    launched = true;
}
log_storage_t sd_storage_interface(void) { return (log_storage_t){0}; }
void sd_storage_set_progress_hook(void (*hook)(void)) { (void)hook; }
bool sd_storage_list_open(void) { return false; }
int sd_storage_list_next(sd_list_entry_t *entry) { (void)entry; return -1; }
bool sd_storage_list_close(void) { return true; }
bool sd_storage_dump_open(const char *name) { (void)name; return false; }
bool sd_storage_dump_read(uint8_t *data, size_t size, size_t *read) { (void)data; (void)size; (void)read; return false; }
bool sd_storage_dump_close(void) { return true; }
void setUp(void) {}
void tearDown(void) {}

static void ring_data_and_control_messages_are_independent(void)
{
    logger_start();
    TEST_ASSERT_TRUE(launched);
    log_record_t record = {0};
    for (unsigned int i = 0; i < LOGGER_SAMPLE_RING_CAPACITY; ++i) TEST_ASSERT_TRUE(logger_submit_sample(&record));
    TEST_ASSERT_EQUAL_UINT(LOGGER_SAMPLE_RING_CAPACITY, logger_ring_level());
    TEST_ASSERT_EQUAL_UINT(LOGGER_SAMPLE_RING_CAPACITY, logger_ring_high_water());
    TEST_ASSERT_FALSE(logger_submit_sample(&record));
    TEST_ASSERT_FALSE(logger_submit_sample(&record));
    TEST_ASSERT_EQUAL_UINT32(2, logger_take_ring_overflows());
    TEST_ASSERT_EQUAL_UINT32(0, logger_take_ring_overflows());
    logger_poll();
    TEST_ASSERT_EQUAL_UINT(0, out_count);
    incoming[in_write++] = UINT32_C(0x10000003);
    incoming[in_write++] = UINT32_C(0x20000002);
    incoming[in_write++] = UINT32_C(0x30002ee0);
    incoming[in_write++] = UINT32_C(0x50001770);
    incoming[in_write++] = UINT32_C(0x60000007);
    incoming[in_write++] = UINT32_C(0x70000002);
    incoming[in_write++] = UINT32_C(0x40000001);
    now = UINT64_C(10000000);
    logger_poll();
    TEST_ASSERT_TRUE(barrier_count >= 7);
    fifo_ready = false;
    now += UINT64_C(1000000);
    TEST_ASSERT_TRUE(logger_request_stop());
    TEST_ASSERT_FALSE(logger_submit_sample(&record));
    TEST_ASSERT_EQUAL_UINT32(0, logger_take_ring_overflows());
    TEST_ASSERT_FALSE(logger_request_start(25));
    logger_poll();
    TEST_ASSERT_EQUAL_UINT(0, out_count);
    fifo_ready = true;
    logger_poll();
    TEST_ASSERT_EQUAL_UINT(1, out_count);
    TEST_ASSERT_EQUAL_UINT32(UINT32_C(0x80000000), outgoing[0]);
    incoming[in_write++] = UINT32_C(0x80000001);
    logger_poll();
    logger_status_t status;
    logger_get_status(&status);
    TEST_ASSERT_EQUAL_INT(LOGGER_STOPPED, status.state);
    TEST_ASSERT_TRUE(status.control_ok);
    incoming[in_write++] = UINT32_C(0xf0000001);
    logger_poll();
    TEST_ASSERT_TRUE(logger_flash_prepare());
    logger_poll();
    TEST_ASSERT_EQUAL_UINT32(UINT32_C(0xf0000003), outgoing[1]);
    incoming[in_write++] = UINT32_C(0xf0000004);
    logger_poll();
    TEST_ASSERT_EQUAL_INT(LOGGER_FLASH_READY, logger_flash_state());
    TEST_ASSERT_TRUE(logger_flash_release());
    logger_poll();
    TEST_ASSERT_EQUAL_UINT32(UINT32_C(0xf0000006), outgoing[2]);
    incoming[in_write++] = UINT32_C(0xf0000007);
    logger_poll();
    TEST_ASSERT_EQUAL_INT(LOGGER_FLASH_IDLE, logger_flash_state());
    TEST_ASSERT_TRUE(logger_flash_prepare());
    TEST_ASSERT_TRUE(logger_flash_release());
    TEST_ASSERT_EQUAL_INT(LOGGER_FLASH_IDLE, logger_flash_state());
    TEST_ASSERT_TRUE(logger_flash_prepare());
    logger_poll();
    TEST_ASSERT_EQUAL_UINT32(UINT32_C(0xf0000003), outgoing[3]);
    TEST_ASSERT_TRUE(logger_flash_release());
    TEST_ASSERT_FALSE(logger_request_list());
    logger_poll();
    TEST_ASSERT_EQUAL_UINT32(UINT32_C(0xf0000006), outgoing[4]);
    incoming[in_write++] = UINT32_C(0xf0000004);
    incoming[in_write++] = UINT32_C(0xf0000007);
    logger_poll();
    TEST_ASSERT_EQUAL_INT(LOGGER_FLASH_IDLE, logger_flash_state());
    TEST_ASSERT_TRUE(logger_request_start(25));
    logger_poll();
    TEST_ASSERT_EQUAL_UINT32(UINT32_C(0x90000019), outgoing[5]);
    incoming[in_write++] = UINT32_C(0x90000001);
    logger_poll();
    logger_get_status(&status);
    TEST_ASSERT_EQUAL_INT(LOGGER_RUNNING, status.state);
}

static void missing_channel_fails_closed_without_hanging(void)
{
    channel_ok = false;
    logger_start();
    TEST_ASSERT_FALSE(launched);
    TEST_ASSERT_FALSE(logger_request_list());
    TEST_ASSERT_FALSE(logger_flash_prepare());
    log_record_t record = {0};
    for (unsigned int i = 0; i < 3; ++i) TEST_ASSERT_FALSE(logger_submit_sample(&record));
    logger_status_t status;
    logger_get_status(&status);
    TEST_ASSERT_EQUAL_INT(LOGGER_STOPPED, status.state);
    TEST_ASSERT_EQUAL_UINT64(3, status.dropped);
}

static void flash_without_lockout_initialization_is_rejected(void)
{
    logger_configure_boot(50, 8192, false);
    logger_start();
    incoming[in_write++] = UINT32_C(0xf0000002);
    logger_poll();
    TEST_ASSERT_FALSE(logger_flash_prepare());
    TEST_ASSERT_EQUAL_INT(LOGGER_FLASH_IDLE, logger_flash_state());
}

static void sample_publication_never_sends_payload_messages(void)
{
    logger_start();
    log_record_t record = {.sequence = 123};
    TEST_ASSERT_TRUE(logger_submit_sample(&record));
    logger_poll();
    TEST_ASSERT_EQUAL_UINT(0, out_count);
    now = UINT64_C(1000000);
    logger_poll();
    TEST_ASSERT_EQUAL_UINT(0, out_count);
    TEST_ASSERT_EQUAL_UINT(1, logger_ring_level());
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    if (argc == 2 && !strcmp(argv[1], "no_channel")) RUN_TEST(missing_channel_fails_closed_without_hanging);
    else if (argc == 2 && !strcmp(argv[1], "no_flash")) RUN_TEST(flash_without_lockout_initialization_is_rejected);
    else if (argc == 2 && !strcmp(argv[1], "ring_only")) RUN_TEST(sample_publication_never_sends_payload_messages);
    else if (argc == 1) RUN_TEST(ring_data_and_control_messages_are_independent);
    else return 1;
    return UNITY_END();
}
