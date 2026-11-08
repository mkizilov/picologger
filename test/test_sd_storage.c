#include "unity.h"
#include "sd_storage.h"
#include "ff.h"
#include "hw_config.h"

#include <string.h>

static bool driver_ok, present;
static unsigned int driver_calls, mount_calls, unmount_calls, presence_calls, sync_calls, close_calls;
static FRESULT open_result = FR_OK, write_result = FR_OK;
static UINT bytes_written;
static sd_card_t card;

static bool test_card(sd_card_t *value) { TEST_ASSERT_EQUAL_PTR(&card, value); ++presence_calls; return present; }
bool sd_init_driver(void) { ++driver_calls; card.sd_test_com = test_card; return driver_ok; }
sd_card_t *sd_get_by_num(size_t number) { return number == 0 ? &card : NULL; }
uint64_t time_us_64(void) { return 123456; }
FRESULT f_mount(FATFS *fs, const char *path, unsigned char immediate)
{
    TEST_ASSERT_EQUAL_STRING("0:", path);
    if (fs) { TEST_ASSERT_EQUAL_UINT(1, immediate); ++mount_calls; }
    else { TEST_ASSERT_EQUAL_UINT(0, immediate); ++unmount_calls; }
    return FR_OK;
}
FRESULT f_open(FIL *file, const char *path, unsigned char flags)
{
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_STRING("0:/LOG_0001.BIN", path);
    TEST_ASSERT_EQUAL_UINT(FA_WRITE | FA_CREATE_NEW, flags);
    return open_result;
}
FRESULT f_write(FIL *file, const void *data, UINT size, UINT *written)
{
    TEST_ASSERT_NOT_NULL(file); TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_UINT(4096, size);
    *written = bytes_written;
    return write_result;
}
FRESULT f_sync(FIL *file) { TEST_ASSERT_NOT_NULL(file); ++sync_calls; return FR_OK; }
FRESULT f_close(FIL *file) { TEST_ASSERT_NOT_NULL(file); ++close_calls; return FR_OK; }
void setUp(void) {}
void tearDown(void) {}

static void fatfs_adapter_checks_presence_and_errors(void)
{
    log_storage_t ops = sd_storage_interface();
    TEST_ASSERT_FALSE(ops.mount(ops.context));
    TEST_ASSERT_EQUAL_UINT(1, driver_calls);
    driver_ok = true;
    TEST_ASSERT_FALSE(ops.mount(ops.context));
    TEST_ASSERT_EQUAL_UINT(2, driver_calls);
    TEST_ASSERT_EQUAL_UINT(1, presence_calls);
    present = true;
    TEST_ASSERT_TRUE(ops.mount(ops.context));
    TEST_ASSERT_EQUAL_UINT(1, mount_calls);
    TEST_ASSERT_EQUAL_UINT(2, driver_calls);
    open_result = FR_EXIST;
    TEST_ASSERT_EQUAL_INT(1, ops.create(ops.context, "0:/LOG_0001.BIN"));
    open_result = FR_DISK_ERR;
    TEST_ASSERT_EQUAL_INT(-1, ops.create(ops.context, "0:/LOG_0001.BIN"));
    open_result = FR_OK;
    TEST_ASSERT_EQUAL_INT(0, ops.create(ops.context, "0:/LOG_0001.BIN"));
    uint8_t data[4096] = {0};
    size_t written;
    bytes_written = 123;
    TEST_ASSERT_TRUE(ops.write(ops.context, data, sizeof(data), &written));
    TEST_ASSERT_EQUAL_UINT(123, written);
    write_result = FR_DISK_ERR;
    TEST_ASSERT_FALSE(ops.write(ops.context, data, sizeof(data), &written));
    TEST_ASSERT_TRUE(ops.sync(ops.context));
    TEST_ASSERT_TRUE(ops.close(ops.context));
    TEST_ASSERT_EQUAL_UINT(1, sync_calls);
    TEST_ASSERT_EQUAL_UINT(1, close_calls);
    ops.unmount(ops.context);
    TEST_ASSERT_EQUAL_UINT(1, unmount_calls);
    TEST_ASSERT_TRUE((card.state.m_Status & STA_NOINIT) != 0);
    TEST_ASSERT_TRUE(ops.present(ops.context));
    TEST_ASSERT_EQUAL_UINT64(123456, ops.now_us(ops.context));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(fatfs_adapter_checks_presence_and_errors);
    return UNITY_END();
}
