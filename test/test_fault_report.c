#include "unity.h"
#include "fault_report.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/psm.h"
#include "fault_reset.h"

#include <string.h>

watchdog_hw_t fake_watchdog_hw;
psm_hw_t fake_psm_hw;
volatile retained_fault_t picologger_retained_faults[2];
volatile fault_ack_t picologger_fault_acks[2];
volatile watchdog_diag_t picologger_watchdog_diag;
static bool timeout_reset, forced_reset;
bool watchdog_enable_caused_reboot(void) { return timeout_reset; }
bool watchdog_caused_reboot(void) { return forced_reset; }
void setUp(void)
{
    memset(&fake_watchdog_hw, 0, sizeof(fake_watchdog_hw));
    memset((void *)picologger_retained_faults, 0, sizeof(picologger_retained_faults));
    memset((void *)picologger_fault_acks, 0, sizeof(picologger_fault_acks));
    memset((void *)&picologger_watchdog_diag, 0, sizeof(picologger_watchdog_diag));
    timeout_reset = forced_reset = false;
}
void tearDown(void) {}

static void boot_reports_warm_faults_once_and_preserves_counter(void)
{
    fault_boot_init();
    TEST_ASSERT_EQUAL_UINT32(1, fault_boot_report()->boot_counter);
    retained_fault_t r = {.boot_id = 1, .detail = {FAULT_STACK_VALID, 0x10001234, 0x10005678,
        0x21000000, 0x20041000, 0xfffffff9, 3, 0x8000}};
    r.checksum = fault_record_checksum(&r); r.magic = FAULT_RECORD_MAGIC;
    picologger_retained_faults[1] = r;
    fake_watchdog_hw.scratch[3] = FAULT_SCRATCH_MARKER;
    fake_watchdog_hw.reason = 2;
    forced_reset = true;
    fault_boot_init();
    const boot_report_t *report = fault_boot_report();
    TEST_ASSERT_EQUAL_UINT32(2, report->boot_counter);
    TEST_ASSERT_EQUAL_UINT32(BOOT_RESET_HARDFAULT, report->reset_reason);
    TEST_ASSERT_EQUAL_UINT32(2, report->fault_mask);
    TEST_ASSERT_EQUAL_UINT32(0x10001234, report->fault[1].pc);
    TEST_ASSERT_EQUAL_UINT32(0x10005678, report->fault[1].lr);
    fault_report_print();
    timeout_reset = true;
    fault_boot_init();
    report = fault_boot_report();
    TEST_ASSERT_EQUAL_UINT32(3, report->boot_counter);
    TEST_ASSERT_EQUAL_UINT32(BOOT_RESET_WATCHDOG, report->reset_reason);
    TEST_ASSERT_EQUAL_UINT32(2, report->fault_mask);
    TEST_ASSERT_EQUAL_UINT32(1, report->fault_boot_id[1]);
    fault_report_mark_logged();
    fault_boot_init();
    report = fault_boot_report();
    TEST_ASSERT_EQUAL_UINT32(4, report->boot_counter);
    TEST_ASSERT_EQUAL_UINT32(0, report->fault_mask);
}

static void acknowledging_old_report_does_not_erase_newer_fault(void)
{
    fault_boot_init();
    retained_fault_t r = {.boot_id = 1, .detail = {.flags = FAULT_STACK_VALID, .pc = 123}};
    r.checksum = fault_record_checksum(&r); r.magic = FAULT_RECORD_MAGIC;
    picologger_retained_faults[0] = r;
    fake_watchdog_hw.scratch[3] = FAULT_SCRATCH_MARKER;
    forced_reset = true;
    fault_boot_init();
    TEST_ASSERT_EQUAL_UINT32(1, fault_boot_report()->fault_mask);
    r.magic = 0; r.boot_id = 2; r.detail.pc = 456; r.checksum = fault_record_checksum(&r); r.magic = FAULT_RECORD_MAGIC;
    picologger_retained_faults[0] = r;
    fault_report_mark_logged();
    fault_boot_init();
    TEST_ASSERT_EQUAL_UINT32(1, fault_boot_report()->fault_mask);
    TEST_ASSERT_EQUAL_UINT32(2, fault_boot_report()->fault_boot_id[0]);
    TEST_ASSERT_EQUAL_UINT32(456, fault_boot_report()->fault[0].pc);
    fault_report_mark_logged();
    r.magic = 0; r.detail.pc = 789; r.checksum = fault_record_checksum(&r); r.magic = FAULT_RECORD_MAGIC;
    picologger_retained_faults[0] = r;
    fault_boot_init();
    TEST_ASSERT_EQUAL_UINT32(1, fault_boot_report()->fault_mask);
    TEST_ASSERT_EQUAL_UINT32(789, fault_boot_report()->fault[0].pc);
}

static void cold_or_corrupt_state_never_reports_stale_fault(void)
{
    fake_watchdog_hw.scratch[3] = FAULT_SCRATCH_MARKER;
    picologger_retained_faults[0].magic = FAULT_RECORD_MAGIC;
    fault_boot_init();
    TEST_ASSERT_EQUAL_UINT32(1, fault_boot_report()->boot_counter);
    TEST_ASSERT_EQUAL_UINT32(0, fault_boot_report()->fault_mask);
    fake_watchdog_hw.scratch[2] ^= 1;
    fault_boot_init();
    TEST_ASSERT_EQUAL_UINT32(1, fault_boot_report()->boot_counter);
    TEST_ASSERT_EQUAL_UINT32(0, fault_boot_report()->fault_mask);
}

static void watchdog_reset_selection_preserves_sram_banks(void)
{
    fake_psm_hw.wdsel = UINT32_MAX;
    fault_preserve_sram_on_watchdog();
    TEST_ASSERT_EQUAL_UINT32(0, fake_psm_hw.wdsel & FAULT_SRAM_WDSEL_BITS);
    TEST_ASSERT_TRUE((fake_psm_hw.wdsel & ~FAULT_SRAM_WDSEL_BITS) != 0);
}

static void watchdog_reason_retains_last_missing_mask(void)
{
    fault_boot_init();
    fault_note_watchdog(5);
    timeout_reset = true;
    fake_watchdog_hw.reason = 1;
    fault_boot_init();
    TEST_ASSERT_EQUAL_UINT32(BOOT_RESET_WATCHDOG, fault_boot_report()->reset_reason);
    TEST_ASSERT_EQUAL_UINT32(5, fault_boot_report()->watchdog_missing);
    fault_note_watchdog(0);
    fault_boot_init();
    TEST_ASSERT_EQUAL_UINT32(0, fault_boot_report()->watchdog_missing);
    fault_note_watchdog(7);
    picologger_watchdog_diag.checksum ^= 1;
    fault_boot_init();
    TEST_ASSERT_EQUAL_UINT32(0, fault_boot_report()->watchdog_missing);
}

static void both_cores_and_invalid_stack_are_reported(void)
{
    fault_boot_init();
    retained_fault_t first = {.boot_id = 1, .detail = {.sp = 0xdeadbeef, .exception_lr = 0xfffffff9, .icsr = 3}};
    retained_fault_t second = {.boot_id = 1, .detail = {.flags = FAULT_STACK_VALID, .pc = 0x10000100, .lr = 0x10000200}};
    first.checksum = fault_record_checksum(&first); first.magic = FAULT_RECORD_MAGIC;
    second.checksum = fault_record_checksum(&second); second.magic = FAULT_RECORD_MAGIC;
    picologger_retained_faults[0] = first;
    picologger_retained_faults[1] = second;
    fake_watchdog_hw.scratch[3] = FAULT_SCRATCH_MARKER;
    forced_reset = true;
    fault_boot_init();
    TEST_ASSERT_EQUAL_UINT32(3, fault_boot_report()->fault_mask);
    TEST_ASSERT_EQUAL_UINT32(0, fault_boot_report()->fault[0].flags);
    TEST_ASSERT_EQUAL_UINT32(0, fault_boot_report()->fault[0].pc);
    TEST_ASSERT_EQUAL_UINT32(0x10000100, fault_boot_report()->fault[1].pc);
    fault_report_mark_logged();
    fault_boot_init();
    TEST_ASSERT_EQUAL_UINT32(0, fault_boot_report()->fault_mask);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(boot_reports_warm_faults_once_and_preserves_counter);
    RUN_TEST(cold_or_corrupt_state_never_reports_stale_fault);
    RUN_TEST(acknowledging_old_report_does_not_erase_newer_fault);
    RUN_TEST(watchdog_reason_retains_last_missing_mask);
    RUN_TEST(watchdog_reset_selection_preserves_sram_banks);
    RUN_TEST(both_cores_and_invalid_stack_are_reported);
    return UNITY_END();
}
