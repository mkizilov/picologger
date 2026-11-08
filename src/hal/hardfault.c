#include "fault_report.h"
#include "fault_reset.h"

#include "hardware/address_mapped.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/m0plus.h"
#include "hardware/regs/psm.h"
#include "hardware/regs/watchdog.h"
#include "hardware/structs/psm.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/watchdog.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

/* SDK RP2040 linker script keeps this NOLOAD section out of the BSS clear. */
__attribute__((section(".uninitialized_data.picologger_fault"), aligned(4), used))
volatile retained_fault_t picologger_retained_faults[2];
__attribute__((section(".uninitialized_data.picologger_fault_ack"), aligned(4), used))
volatile fault_ack_t picologger_fault_acks[2];
__attribute__((section(".uninitialized_data.picologger_watchdog"), aligned(4), used))
volatile watchdog_diag_t picologger_watchdog_diag;

void __attribute__((noreturn, noinline, used, section(".time_critical.picologger_fault_capture")))
picologger_fault_capture(const uint32_t *frame, uint32_t exception_lr)
{
    __asm volatile("cpsid i" ::: "memory");
    unsigned int core = sio_hw->cpuid & 1u;
    volatile retained_fault_t *r = &picologger_retained_faults[core];
    uintptr_t sp = (uintptr_t)frame;
    bool valid = !(sp & 3u) && sp >= (uintptr_t)SRAM_BASE && sp <= (uintptr_t)SRAM_END - 8u * sizeof(uint32_t);
    fault_record_prepare(r, watchdog_hw->scratch[1], frame, valid, (uint32_t)sp, exception_lr,
                         scb_hw->icsr, *(volatile const uint32_t *)(PPB_BASE + M0PLUS_SHCSR_OFFSET));
    __dmb();
    r->magic = FAULT_RECORD_MAGIC;
    __dmb();
    watchdog_hw->scratch[3] = FAULT_SCRATCH_MARKER;
    watchdog_hw->scratch[4] = 0;
    psm_hw->wdsel = PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS | FAULT_SRAM_WDSEL_BITS);
    hw_set_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_TRIGGER_BITS);
    __dmb();
    for (;;) __asm volatile("wfi");
}

void __attribute__((naked, section(".time_critical.isr_hardfault"))) isr_hardfault(void)
{
    __asm volatile(
        "mov r0, lr\n"
        "movs r1, #4\n"
        "tst r0, r1\n"
        "beq 1f\n"
        "mrs r0, psp\n"
        "b 2f\n"
        "1: mrs r0, msp\n"
        "2: mov r1, lr\n"
        "ldr r2, =picologger_fault_capture\n"
        "bx r2\n");
}

void __attribute__((noinline)) fault_force_crash(void)
{
    __asm volatile("udf #0");
}
