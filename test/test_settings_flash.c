#include "unity.h"
#include "settings_flash.h"
#include "hardware/flash.h"

#include <string.h>

uint8_t fake_xip[PICO_FLASH_SIZE_BYTES];
static bool safe, fail_before, fail_after, bad_start, overlap;
static int busy_channel = -1;
static unsigned int calls, erases, programs;
static uintptr_t ram_start, ram_end;

uintptr_t settings_test_ram_start(void) { return ram_start; }
uintptr_t settings_test_ram_end(void) { return ram_end; }

uintptr_t settings_test_link_start(void)
{
    return (uintptr_t)fake_xip + 8192u + (bad_start ? 4096u : 0u);
}
uintptr_t settings_test_image_end(void)
{
    return (uintptr_t)fake_xip + (overlap ? 8193u : 8000u);
}
bool dma_channel_is_busy(unsigned int channel) { TEST_ASSERT_TRUE(safe); return (int)channel == busy_channel; }
int flash_safe_execute(void (*callback)(void *), void *context, uint32_t timeout_ms)
{
    TEST_ASSERT_EQUAL_UINT(1000, timeout_ms);
    ++calls;
    if (fail_before) return -1;
    safe = true;
    callback(context);
    safe = false;
    return fail_after ? -1 : 0;
}
void flash_range_erase(uint32_t offset, size_t count)
{
    TEST_ASSERT_TRUE(safe);
    TEST_ASSERT_EQUAL_UINT(FLASH_SECTOR_SIZE, count);
    TEST_ASSERT_TRUE(offset == 8192 || offset == 12288);
    memset(fake_xip + offset, 0xff, count);
    ++erases;
}
void flash_range_program(uint32_t offset, const uint8_t *data, size_t count)
{
    TEST_ASSERT_TRUE(safe);
    TEST_ASSERT_EQUAL_UINT(FLASH_PAGE_SIZE, count);
    TEST_ASSERT_TRUE(offset == 8192 || offset == 12288);
    memcpy(fake_xip + offset, data, count);
    ++programs;
}
void setUp(void) { memset(fake_xip, 0, sizeof(fake_xip)); }
void tearDown(void) { settings_flash_set_guard(false); }

static void flash_operations_require_guard_safe_execute_and_reserved_layout(void)
{
    settings_flash_t f = settings_flash_interface();
    uint8_t page[SETTINGS_PAGE_SIZE], read[SETTINGS_PAGE_SIZE];
    ram_start = (uintptr_t)page;
    ram_end = ram_start + sizeof(page);
    memset(page, 0x5a, sizeof(page));
    TEST_ASSERT_TRUE(f.read(f.context, 0, read, sizeof(read)));
    TEST_ASSERT_FALSE(f.erase(f.context, 0));
    TEST_ASSERT_EQUAL_UINT(0, calls);
    settings_flash_set_guard(true);
    bad_start = true;
    TEST_ASSERT_FALSE(f.read(f.context, 0, read, sizeof(read)));
    TEST_ASSERT_FALSE(f.erase(f.context, 0));
    bad_start = false; overlap = true;
    TEST_ASSERT_FALSE(f.erase(f.context, 0));
    overlap = false;
    TEST_ASSERT_FALSE(f.erase(f.context, 2));
    TEST_ASSERT_FALSE(f.program(f.context, 0, page, sizeof(page) - 1));
    TEST_ASSERT_FALSE(f.program(f.context, 0, fake_xip, sizeof(page)));
    busy_channel = 5;
    TEST_ASSERT_FALSE(f.erase(f.context, 0));
    TEST_ASSERT_EQUAL_UINT(0, erases);
    busy_channel = -1;
    TEST_ASSERT_TRUE(f.erase(f.context, 0));
    TEST_ASSERT_TRUE(f.program(f.context, 0, page, sizeof(page)));
    TEST_ASSERT_TRUE(f.read(f.context, 0, read, sizeof(read)));
    TEST_ASSERT_EQUAL_MEMORY(page, read, sizeof(page));
    TEST_ASSERT_EQUAL_UINT(1, erases);
    TEST_ASSERT_EQUAL_UINT(1, programs);
    fail_before = true;
    TEST_ASSERT_FALSE(f.erase(f.context, 1));
    TEST_ASSERT_EQUAL_UINT(1, erases);
    fail_before = false; fail_after = true;
    TEST_ASSERT_FALSE(f.erase(f.context, 1));
    TEST_ASSERT_EQUAL_UINT(2, erases);
    TEST_ASSERT_FALSE(safe);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(flash_operations_require_guard_safe_execute_and_reserved_layout);
    return UNITY_END();
}
