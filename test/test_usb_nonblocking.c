#include "unity.h"
#include "usb_nonblocking.h"

static bool locked, attached = true;
static uint32_t capacity, writes, reads, flushes, reboots;

uint32_t save_and_disable_interrupts(void) { TEST_ASSERT_FALSE(locked); locked = true; return 123; }
void restore_interrupts(uint32_t state) { TEST_ASSERT_TRUE(locked); TEST_ASSERT_EQUAL_UINT(123, state); locked = false; }
bool stdio_usb_connected(void) { return attached; }
bool tud_cdc_connected(void) { TEST_ASSERT_TRUE(locked); return attached; }
uint32_t tud_cdc_available(void) { TEST_ASSERT_TRUE(locked); return 1; }
uint32_t tud_cdc_read(void *buffer, uint32_t size)
{
    TEST_ASSERT_TRUE(locked); TEST_ASSERT_TRUE(size > 0);
    ((uint8_t *)buffer)[0] = 'x'; ++reads; return 1;
}
uint32_t tud_cdc_write_available(void) { TEST_ASSERT_TRUE(locked); return capacity; }
uint32_t tud_cdc_write(const void *buffer, uint32_t size)
{
    TEST_ASSERT_TRUE(locked); TEST_ASSERT_NOT_NULL(buffer); ++writes; return size;
}
uint32_t tud_cdc_write_flush(void) { TEST_ASSERT_TRUE(locked); ++flushes; return 1; }
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms)
{
    TEST_ASSERT_EQUAL_UINT(0, pc); TEST_ASSERT_EQUAL_UINT(0, sp); TEST_ASSERT_EQUAL_UINT(100, delay_ms); ++reboots;
}
void setUp(void) {}
void tearDown(void) {}

static void bounded_whole_frame_and_nonblocking_input(void)
{
    TEST_ASSERT_TRUE(usb_nb_connected());
    uint8_t byte;
    TEST_ASSERT_EQUAL_UINT(1, usb_nb_read(&byte, 1));
    TEST_ASSERT_EQUAL_UINT('x', byte);
    TEST_ASSERT_EQUAL_UINT(1, reads);
    capacity = 3;
    TEST_ASSERT_FALSE(usb_nb_write("four", 4));
    TEST_ASSERT_EQUAL_UINT(0, writes);
    capacity = 64;
    TEST_ASSERT_TRUE(usb_nb_write("four", 4));
    TEST_ASSERT_EQUAL_UINT(1, writes);
    TEST_ASSERT_EQUAL_UINT(1, flushes);
    TEST_ASSERT_FALSE(usb_nb_write("x", 65));
    attached = false;
    TEST_ASSERT_FALSE(usb_nb_write("x", 1));
    TEST_ASSERT_EQUAL_UINT(0, usb_nb_read(&byte, 1));
    TEST_ASSERT_FALSE(locked);
    usb_nb_reboot();
    TEST_ASSERT_EQUAL_UINT(1, reboots);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(bounded_whole_frame_and_nonblocking_input);
    return UNITY_END();
}
