#include "fault_report.h"
#include "fault_reset.h"

#include <inttypes.h>
#include <stdio.h>

#include "hardware/structs/watchdog.h"
#include "hardware/structs/psm.h"
#include "hardware/address_mapped.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"

extern volatile retained_fault_t picologger_retained_faults[2];
extern volatile fault_ack_t picologger_fault_acks[2];
extern volatile watchdog_diag_t picologger_watchdog_diag;
static boot_report_t report;

void fault_preserve_sram_on_watchdog(void) { hw_clear_bits(&psm_hw->wdsel, FAULT_SRAM_WDSEL_BITS); }

void fault_boot_init(void)
{
    report = (boot_report_t){0};
    report.watchdog_reason = watchdog_hw->reason;
    bool timeout = watchdog_enable_caused_reboot();
    bool forced = watchdog_caused_reboot();
    bool previous_valid;
    uint32_t previous;
    report.boot_counter = fault_counter_advance(watchdog_hw->scratch, &previous_valid, &previous);
    report.reset_reason = timeout ? BOOT_RESET_WATCHDOG : forced ? BOOT_RESET_FORCED : BOOT_RESET_UNKNOWN;
    if (timeout && previous_valid && picologger_watchdog_diag.magic == WATCHDOG_DIAG_MAGIC &&
        picologger_watchdog_diag.boot_id == previous &&
        picologger_watchdog_diag.checksum == (previous ^ picologger_watchdog_diag.missing ^ UINT32_C(0x659c021b)))
        report.watchdog_missing = picologger_watchdog_diag.missing;
    if (watchdog_hw->scratch[3] == FAULT_SCRATCH_MARKER) {
        for (unsigned int core = 0; core < 2; ++core) {
            uint32_t origin;
            if (fault_record_read_any(&picologger_retained_faults[core], &report.fault[core], &origin) &&
                !fault_record_is_acknowledged(&picologger_fault_acks[core], origin, picologger_retained_faults[core].checksum)) {
                report.fault_boot_id[core] = origin;
                report.fault_checksum[core] = picologger_retained_faults[core].checksum;
                report.fault_mask |= 1u << core;
            }
        }
    }
    if (forced && previous_valid) {
        for (unsigned int core = 0; core < 2; ++core)
            if ((report.fault_mask & (1u << core)) && report.fault_boot_id[core] == previous)
                report.reset_reason = BOOT_RESET_HARDFAULT;
    }
}

const boot_report_t *fault_boot_report(void) { return &report; }

void fault_report_print(void)
{
    printf("Boot counter=%" PRIu32 " reset_code=%" PRIu32 " watchdog_reason=0x%08" PRIx32
           " watchdog_missing=0x%08" PRIx32 "\n",
           report.boot_counter, report.reset_reason, report.watchdog_reason, report.watchdog_missing);
    for (unsigned int core = 0; core < 2; ++core) {
        if (!(report.fault_mask & (1u << core))) continue;
        const fault_detail_t *f = &report.fault[core];
        printf("HardFault core=%u origin_boot=%" PRIu32 " stack_valid=%u PC=0x%08" PRIx32 " LR=0x%08" PRIx32 "\n",
               core, report.fault_boot_id[core], (unsigned int)((f->flags & FAULT_STACK_VALID) != 0), f->pc, f->lr);
        printf("HardFault SP=0x%08" PRIx32 " EXC_RETURN=0x%08" PRIx32 " XPSR=0x%08" PRIx32
               " ICSR=0x%08" PRIx32 " SHCSR=0x%08" PRIx32 "\n",
               f->sp, f->exception_lr, f->xpsr, f->icsr, f->shcsr);
    }
}

void fault_note_watchdog(uint32_t missing)
{
    picologger_watchdog_diag.magic = 0;
    if (!missing) { __dmb(); return; }
    picologger_watchdog_diag.boot_id = report.boot_counter;
    picologger_watchdog_diag.missing = missing;
    picologger_watchdog_diag.checksum = report.boot_counter ^ missing ^ UINT32_C(0x659c021b);
    __dmb();
    picologger_watchdog_diag.magic = WATCHDOG_DIAG_MAGIC;
    __dmb();
}

void fault_report_mark_logged(void)
{
    for (unsigned int core = 0; core < 2; ++core) {
        if (!(report.fault_mask & (1u << core))) continue;
        volatile fault_ack_t *ack = &picologger_fault_acks[core];
        ack->magic = 0;
        ack->boot_id = report.fault_boot_id[core];
        ack->inverse = ~report.fault_boot_id[core];
        ack->record_checksum = report.fault_checksum[core];
        ack->checksum_inverse = ~report.fault_checksum[core];
        __dmb();
        ack->magic = FAULT_ACK_MAGIC;
    }
    __dmb();
}
