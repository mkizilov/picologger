#ifndef PICOLOGGER_FAULT_RECORD_H
#define PICOLOGGER_FAULT_RECORD_H

#include <stdbool.h>
#include <stdint.h>

#define FAULT_RECORD_MAGIC UINT32_C(0x464c5431)
#define FAULT_COUNTER_MAGIC UINT32_C(0x424f4f54)
#define FAULT_SCRATCH_MARKER UINT32_C(0x48524654)
#define FAULT_ACK_MAGIC UINT32_C(0x41434b31)
#define WATCHDOG_DIAG_MAGIC UINT32_C(0x57444731)
#define FAULT_STACK_VALID 1u

typedef struct {
    uint32_t flags, pc, lr, xpsr, sp, exception_lr, icsr, shcsr;
} fault_detail_t;

typedef struct {
    uint32_t magic, checksum, boot_id;
    fault_detail_t detail;
} retained_fault_t;

typedef struct { uint32_t magic, boot_id, inverse, record_checksum, checksum_inverse; } fault_ack_t;
typedef struct { uint32_t magic, boot_id, missing, checksum; } watchdog_diag_t;

typedef enum { BOOT_RESET_UNKNOWN = 0, BOOT_RESET_WATCHDOG = 1,
               BOOT_RESET_FORCED = 2, BOOT_RESET_HARDFAULT = 3 } boot_reset_t;

typedef struct {
    uint32_t boot_counter, reset_reason, watchdog_reason, fault_mask, watchdog_missing;
    fault_detail_t fault[2];
    uint32_t fault_boot_id[2];
    uint32_t fault_checksum[2];
} boot_report_t;

/* Linked into the SDK's SRAM-resident time-critical section. */
uint32_t fault_record_checksum(const volatile retained_fault_t *record);

/* Capture payload first; the caller commits magic only after a memory barrier. */
static inline __attribute__((always_inline)) void fault_record_prepare(
    volatile retained_fault_t *r, uint32_t boot_id, const uint32_t *frame, bool valid,
    uint32_t sp, uint32_t exception_lr, uint32_t icsr, uint32_t shcsr)
{
    r->magic = 0;
    r->boot_id = boot_id;
    r->detail.flags = valid ? FAULT_STACK_VALID : 0;
    r->detail.pc = valid ? frame[6] : 0;
    r->detail.lr = valid ? frame[5] : 0;
    r->detail.xpsr = valid ? frame[7] : 0;
    r->detail.sp = sp;
    r->detail.exception_lr = exception_lr;
    r->detail.icsr = icsr;
    r->detail.shcsr = shcsr;
    r->checksum = fault_record_checksum(r);
}

bool fault_record_read(const volatile retained_fault_t *record, uint32_t expected_boot, fault_detail_t *detail);
bool fault_record_read_any(const volatile retained_fault_t *record, fault_detail_t *detail, uint32_t *boot_id);
bool fault_record_is_acknowledged(const volatile fault_ack_t *ack, uint32_t boot_id, uint32_t checksum);
/* scratch[0..2] is a committed counter tuple; caller owns scratch[3]. */
uint32_t fault_counter_advance(volatile uint32_t *scratch, bool *previous_valid, uint32_t *previous);

#endif
