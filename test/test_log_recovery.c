#include "unity.h"
#include "log_format.h"
#include "log_recovery.h"

#include <string.h>

enum { RECORDS = 12, CAPACITY = LOG_HEADER_V2_SIZE + RECORDS * LOG_RECORD_SIZE + 32 };
typedef struct {
    uint8_t data[CAPACITY];
    size_t length;
    unsigned int reads, truncates;
    bool fail_size, fail_read, short_read, fail_truncate;
} fake_file_t;

static fake_file_t fake;
static uint8_t original[CAPACITY];
static log_recovery_file_t file;

static bool file_size(void *context, uint64_t *size)
{
    fake_file_t *f = context;
    if (f->fail_size) return false;
    *size = f->length;
    return true;
}
static bool file_read(void *context, uint64_t offset, uint8_t *data, size_t length, size_t *read)
{
    fake_file_t *f = context;
    ++f->reads;
    if (f->fail_read || offset > f->length || length > f->length - (size_t)offset) return false;
    *read = f->short_read && length ? length - 1 : length;
    memcpy(data, f->data + (size_t)offset, *read);
    return true;
}
static bool file_truncate(void *context, uint64_t length)
{
    fake_file_t *f = context;
    ++f->truncates;
    if (f->fail_truncate || length > f->length) return false;
    f->length = (size_t)length;
    return true;
}

void setUp(void)
{
    memset(&fake, 0, sizeof(fake));
    memset(original, 0, sizeof(original));
    log_header_t header = {.firmware = "recovery-test", .sample_rate_hz = 50,
                           .channel_count = 1, .channel_ids = {1}, .boot_counter = 42};
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_encode_header(original, sizeof(original), &header));
    for (unsigned int i = 0; i < RECORDS; ++i) {
        log_record_t record = {.sequence = i, .timestamp_us = i * UINT64_C(20000),
                               .values = {(int32_t)i, 0, 0, 0}};
        TEST_ASSERT_EQUAL_INT(LOG_OK, log_encode_record(original + LOG_HEADER_SIZE + i * LOG_RECORD_SIZE,
                                                        LOG_RECORD_SIZE, &record));
    }
    memcpy(fake.data, original, sizeof(original));
    fake.length = LOG_HEADER_SIZE + RECORDS * LOG_RECORD_SIZE;
    file = (log_recovery_file_t){&fake, file_size, file_read, file_truncate};
}
void tearDown(void) {}

static void every_possible_byte_cut_recovers_the_valid_prefix(void)
{
    size_t end = LOG_HEADER_SIZE + RECORDS * LOG_RECORD_SIZE;
    for (size_t cut = 0; cut <= end; ++cut) {
        memcpy(fake.data, original, sizeof(original));
        fake.length = cut;
        fake.truncates = 0;
        log_recovery_result_t result;
        log_recovery_status_t status = log_recover(&file, &result);
        if (cut < LOG_HEADER_SIZE) {
            TEST_ASSERT_EQUAL_INT(LOG_RECOVERY_INVALID_HEADER, status);
            TEST_ASSERT_EQUAL_UINT(cut, fake.length);
            TEST_ASSERT_EQUAL_UINT(0, fake.truncates);
        } else {
            size_t expected = LOG_HEADER_SIZE + (cut - LOG_HEADER_SIZE) / LOG_RECORD_SIZE * LOG_RECORD_SIZE;
            TEST_ASSERT_EQUAL_INT(LOG_RECOVERY_OK, status);
            TEST_ASSERT_EQUAL_UINT64(cut, result.original_size);
            TEST_ASSERT_EQUAL_UINT64(expected, result.recovered_size);
            TEST_ASSERT_EQUAL_UINT(expected, fake.length);
            TEST_ASSERT_EQUAL_MEMORY(original, fake.data, expected);
            TEST_ASSERT_EQUAL_UINT(expected == cut ? 0 : 1, fake.truncates);
        }
    }
}

static void corrupt_trailing_records_are_removed_but_middle_damage_is_not_hidden(void)
{
    fake.data[LOG_HEADER_SIZE + (RECORDS - 1) * LOG_RECORD_SIZE + 7] ^= 1;
    fake.data[LOG_HEADER_SIZE + (RECORDS - 2) * LOG_RECORD_SIZE + 30] ^= 1;
    fake.length += 7;
    log_recovery_result_t result;
    TEST_ASSERT_EQUAL_INT(LOG_RECOVERY_OK, log_recover(&file, &result));
    TEST_ASSERT_EQUAL_UINT(LOG_HEADER_SIZE + (RECORDS - 2) * LOG_RECORD_SIZE, fake.length);
    TEST_ASSERT_EQUAL_MEMORY(original, fake.data, fake.length);
    memcpy(fake.data, original, sizeof(original));
    fake.length = LOG_HEADER_SIZE + RECORDS * LOG_RECORD_SIZE;
    fake.data[LOG_HEADER_SIZE + LOG_RECORD_SIZE + 4] ^= 1;
    fake.truncates = 0;
    TEST_ASSERT_EQUAL_INT(LOG_RECOVERY_OK, log_recover(&file, &result));
    TEST_ASSERT_EQUAL_UINT(0, fake.truncates);
    TEST_ASSERT_EQUAL_UINT(LOG_HEADER_SIZE + RECORDS * LOG_RECORD_SIZE, fake.length);
}

static void invalid_header_and_io_errors_never_trigger_unsafe_truncation(void)
{
    log_recovery_result_t result;
    fake.data[0] ^= 1;
    TEST_ASSERT_EQUAL_INT(LOG_RECOVERY_INVALID_HEADER, log_recover(&file, &result));
    TEST_ASSERT_EQUAL_UINT(0, fake.truncates);
    fake.data[0] ^= 1;
    fake.fail_size = true;
    TEST_ASSERT_EQUAL_INT(LOG_RECOVERY_IO_ERROR, log_recover(&file, &result));
    fake.fail_size = false;
    fake.fail_read = true;
    TEST_ASSERT_EQUAL_INT(LOG_RECOVERY_IO_ERROR, log_recover(&file, &result));
    fake.fail_read = false;
    fake.short_read = true;
    TEST_ASSERT_EQUAL_INT(LOG_RECOVERY_IO_ERROR, log_recover(&file, &result));
    fake.short_read = false;
    TEST_ASSERT_EQUAL_UINT(0, fake.truncates);
    fake.length -= 1;
    fake.fail_truncate = true;
    TEST_ASSERT_EQUAL_INT(LOG_RECOVERY_IO_ERROR, log_recover(&file, &result));
    TEST_ASSERT_EQUAL_UINT(1, fake.truncates);
    TEST_ASSERT_EQUAL_UINT(LOG_HEADER_SIZE + RECORDS * LOG_RECORD_SIZE - 1, fake.length);
}

static void no_valid_records_leaves_only_the_header(void)
{
    for (unsigned int i = 0; i < RECORDS; ++i)
        fake.data[LOG_HEADER_SIZE + i * LOG_RECORD_SIZE + 30] ^= 1;
    log_recovery_result_t result;
    TEST_ASSERT_EQUAL_INT(LOG_RECOVERY_OK, log_recover(&file, &result));
    TEST_ASSERT_EQUAL_UINT(LOG_HEADER_SIZE, fake.length);
    TEST_ASSERT_EQUAL_MEMORY(original, fake.data, LOG_HEADER_SIZE);
}

static void every_possible_v2_byte_cut_recovers_the_valid_prefix(void)
{
    log_header_t header = {.firmware = "recovery-v2", .sample_rate_hz = 50,
                           .channel_count = 1, .channel_ids = {1}};
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_encode_header_v2(original, sizeof(original), &header));
    for (unsigned int i = 0; i < RECORDS; ++i) {
        log_record_t record = {.sequence = i, .timestamp_us = i * UINT64_C(20000)};
        TEST_ASSERT_EQUAL_INT(LOG_OK, log_encode_record(original + LOG_HEADER_V2_SIZE + i * LOG_RECORD_SIZE,
                                                        LOG_RECORD_SIZE, &record));
    }
    size_t end = LOG_HEADER_V2_SIZE + RECORDS * LOG_RECORD_SIZE;
    for (size_t cut = 0; cut <= end; ++cut) {
        memcpy(fake.data, original, sizeof(original));
        fake.length = cut;
        fake.truncates = 0;
        log_recovery_result_t result;
        log_recovery_status_t status = log_recover(&file, &result);
        if (cut < LOG_HEADER_V2_SIZE) {
            TEST_ASSERT_EQUAL_INT(LOG_RECOVERY_INVALID_HEADER, status);
            TEST_ASSERT_EQUAL_UINT(0, fake.truncates);
            TEST_ASSERT_EQUAL_UINT(cut, fake.length);
        } else {
            size_t expected = LOG_HEADER_V2_SIZE + (cut - LOG_HEADER_V2_SIZE) / LOG_RECORD_SIZE * LOG_RECORD_SIZE;
            TEST_ASSERT_EQUAL_INT(LOG_RECOVERY_OK, status);
            TEST_ASSERT_EQUAL_UINT(expected, fake.length);
            TEST_ASSERT_EQUAL_MEMORY(original, fake.data, expected);
        }
    }
    memcpy(fake.data, original, sizeof(original));
    fake.length = end;
    fake.data[40] ^= 1;
    fake.truncates = 0;
    log_recovery_result_t result;
    TEST_ASSERT_EQUAL_INT(LOG_RECOVERY_INVALID_HEADER, log_recover(&file, &result));
    TEST_ASSERT_EQUAL_UINT(0, fake.truncates);
}

static void every_possible_v3_byte_cut_recovers_the_valid_prefix(void)
{
    log_header_t header = {.firmware = "recovery-v3", .sample_rate_hz = 50, .channel_count = 1,
                           .channel_ids = {1}, .boot_counter = 8, .reset_reason = BOOT_RESET_HARDFAULT,
                           .watchdog_reason = 2, .fault_mask = 1};
    header.fault[0] = (fault_detail_t){FAULT_STACK_VALID, 0x10001234, 0x10005678, 0x21000000,
                                      0x20041000, 0xfffffff9, 3, 0x8000};
    header.fault_boot_id[0] = 7;
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_encode_header_v3(original, sizeof(original), &header));
    for (unsigned int i = 0; i < RECORDS; ++i) {
        log_record_t record = {.sequence = i, .timestamp_us = i * UINT64_C(20000)};
        TEST_ASSERT_EQUAL_INT(LOG_OK, log_encode_record(original + LOG_HEADER_V2_SIZE + i * LOG_RECORD_SIZE,
                                                        LOG_RECORD_SIZE, &record));
    }
    size_t end = LOG_HEADER_V2_SIZE + RECORDS * LOG_RECORD_SIZE;
    for (size_t cut = 0; cut <= end; ++cut) {
        memcpy(fake.data, original, sizeof(original));
        fake.length = cut;
        fake.truncates = 0;
        log_recovery_result_t result;
        log_recovery_status_t status = log_recover(&file, &result);
        if (cut < LOG_HEADER_V2_SIZE) {
            TEST_ASSERT_EQUAL_INT(LOG_RECOVERY_INVALID_HEADER, status);
            TEST_ASSERT_EQUAL_UINT(0, fake.truncates);
        } else {
            size_t expected = LOG_HEADER_V2_SIZE + (cut - LOG_HEADER_V2_SIZE) / LOG_RECORD_SIZE * LOG_RECORD_SIZE;
            TEST_ASSERT_EQUAL_INT(LOG_RECOVERY_OK, status);
            TEST_ASSERT_EQUAL_UINT(expected, fake.length);
            TEST_ASSERT_EQUAL_MEMORY(original, fake.data, expected);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(every_possible_byte_cut_recovers_the_valid_prefix);
    RUN_TEST(corrupt_trailing_records_are_removed_but_middle_damage_is_not_hidden);
    RUN_TEST(invalid_header_and_io_errors_never_trigger_unsafe_truncation);
    RUN_TEST(no_valid_records_leaves_only_the_header);
    RUN_TEST(every_possible_v2_byte_cut_recovers_the_valid_prefix);
    RUN_TEST(every_possible_v3_byte_cut_recovers_the_valid_prefix);
    return UNITY_END();
}
