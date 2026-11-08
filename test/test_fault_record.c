#include "unity.h"
#include "fault_record.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void warm_boot_counter_validates_and_wraps(void)
{
    uint32_t scratch[4] = {0};
    bool valid;
    uint32_t previous;
    TEST_ASSERT_EQUAL_UINT32(1, fault_counter_advance(scratch, &valid, &previous));
    TEST_ASSERT_FALSE(valid);
    TEST_ASSERT_EQUAL_UINT32(2, fault_counter_advance(scratch, &valid, &previous));
    TEST_ASSERT_TRUE(valid);
    TEST_ASSERT_EQUAL_UINT32(1, previous);
    scratch[1] = UINT32_MAX; scratch[2] = 0;
    TEST_ASSERT_EQUAL_UINT32(0, fault_counter_advance(scratch, &valid, &previous));
    TEST_ASSERT_TRUE(valid);
    scratch[2] ^= 1;
    TEST_ASSERT_EQUAL_UINT32(1, fault_counter_advance(scratch, &valid, &previous));
    TEST_ASSERT_FALSE(valid);
}

static void retained_fault_requires_magic_boot_and_checksum(void)
{
    retained_fault_t record = {.boot_id = 42, .detail = {FAULT_STACK_VALID, 0x10001234, 0x10005678,
        0x21000000, 0x20041000, 0xfffffff9, 3, 0x8000}};
    record.checksum = fault_record_checksum(&record);
    TEST_ASSERT_EQUAL_UINT32(UINT32_C(0x6701cdd3), record.checksum);
    record.magic = FAULT_RECORD_MAGIC;
    fault_detail_t detail;
    TEST_ASSERT_TRUE(fault_record_read(&record, 42, &detail));
    TEST_ASSERT_EQUAL_UINT32(0x10001234, detail.pc);
    TEST_ASSERT_EQUAL_UINT32(0x10005678, detail.lr);
    TEST_ASSERT_FALSE(fault_record_read(&record, 41, &detail));
    for (size_t i = 0; i < sizeof(record); ++i) {
        retained_fault_t corrupted = record;
        ((uint8_t *)&corrupted)[i] ^= 1;
        TEST_ASSERT_FALSE(fault_record_read(&corrupted, 42, &detail));
    }
    record.detail.pc ^= 1;
    TEST_ASSERT_FALSE(fault_record_read(&record, 42, &detail));
    record.detail.pc ^= 1;
    record.magic = 0;
    TEST_ASSERT_FALSE(fault_record_read(&record, 42, &detail));
}

static void captures_exception_frame_or_reports_invalid_stack(void)
{
    uint32_t frame[8] = {0, 0, 0, 0, 0, 0x10005678, 0x10001234, 0x21000000};
    retained_fault_t record = {.magic = FAULT_RECORD_MAGIC};
    fault_record_prepare(&record, 9, frame, true, 0x20041000, 0xfffffff9, 3, 0x8000);
    TEST_ASSERT_EQUAL_UINT32(0, record.magic);
    record.magic = FAULT_RECORD_MAGIC;
    fault_detail_t detail;
    TEST_ASSERT_TRUE(fault_record_read(&record, 9, &detail));
    TEST_ASSERT_EQUAL_UINT32(FAULT_STACK_VALID, detail.flags);
    TEST_ASSERT_EQUAL_UINT32(0x10001234, detail.pc);
    TEST_ASSERT_EQUAL_UINT32(0x10005678, detail.lr);
    TEST_ASSERT_EQUAL_UINT32(0x21000000, detail.xpsr);
    fault_record_prepare(&record, 10, NULL, false, 0xdeadbeef, 0xfffffff9, 3, 0);
    record.magic = FAULT_RECORD_MAGIC;
    TEST_ASSERT_TRUE(fault_record_read(&record, 10, &detail));
    TEST_ASSERT_EQUAL_UINT32(0, detail.flags);
    TEST_ASSERT_EQUAL_UINT32(0, detail.pc);
    TEST_ASSERT_EQUAL_UINT32(0, detail.lr);
    TEST_ASSERT_EQUAL_UINT32(0xdeadbeef, detail.sp);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(warm_boot_counter_validates_and_wraps);
    RUN_TEST(retained_fault_requires_magic_boot_and_checksum);
    RUN_TEST(captures_exception_frame_or_reports_invalid_stack);
    return UNITY_END();
}
