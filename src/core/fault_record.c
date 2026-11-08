#include "fault_record.h"

#if defined(__APPLE__) && defined(__MACH__)
#define FAULT_RAM(name) __attribute__((noinline)) name
#else
#define FAULT_RAM(name) __attribute__((noinline, section(".time_critical." #name))) name
#endif

static uint32_t FAULT_RAM(fault_checksum_word)(uint32_t crc, uint32_t word)
{
    for (unsigned int byte = 0; byte < 4; ++byte) {
        crc ^= (word >> (byte * 8)) & 0xffu;
        for (unsigned int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1u));
    }
    return crc;
}

uint32_t FAULT_RAM(fault_record_checksum)(const volatile retained_fault_t *r)
{
    uint32_t crc = UINT32_MAX;
    crc = fault_checksum_word(crc, r->boot_id);
    crc = fault_checksum_word(crc, r->detail.flags);
    crc = fault_checksum_word(crc, r->detail.pc);
    crc = fault_checksum_word(crc, r->detail.lr);
    crc = fault_checksum_word(crc, r->detail.xpsr);
    crc = fault_checksum_word(crc, r->detail.sp);
    crc = fault_checksum_word(crc, r->detail.exception_lr);
    crc = fault_checksum_word(crc, r->detail.icsr);
    crc = fault_checksum_word(crc, r->detail.shcsr);
    return crc ^ UINT32_MAX;
}

bool fault_record_read_any(const volatile retained_fault_t *record, fault_detail_t *detail, uint32_t *boot_id)
{
    if (!record || !detail || !boot_id || record->magic != FAULT_RECORD_MAGIC ||
        (record->detail.flags & ~FAULT_STACK_VALID) != 0 || record->checksum != fault_record_checksum(record)) return false;
    *boot_id = record->boot_id;
    detail->flags = record->detail.flags;
    detail->pc = record->detail.pc;
    detail->lr = record->detail.lr;
    detail->xpsr = record->detail.xpsr;
    detail->sp = record->detail.sp;
    detail->exception_lr = record->detail.exception_lr;
    detail->icsr = record->detail.icsr;
    detail->shcsr = record->detail.shcsr;
    return true;
}

bool fault_record_read(const volatile retained_fault_t *record, uint32_t expected_boot, fault_detail_t *detail)
{
    uint32_t boot_id;
    return fault_record_read_any(record, detail, &boot_id) && boot_id == expected_boot;
}

bool fault_record_is_acknowledged(const volatile fault_ack_t *ack, uint32_t boot_id, uint32_t checksum)
{
    return ack && ack->magic == FAULT_ACK_MAGIC && ack->boot_id == boot_id && ack->inverse == ~boot_id &&
           ack->record_checksum == checksum && ack->checksum_inverse == ~checksum;
}

uint32_t fault_counter_advance(volatile uint32_t *scratch, bool *previous_valid, uint32_t *previous)
{
    *previous_valid = scratch[0] == FAULT_COUNTER_MAGIC && scratch[2] == ~scratch[1];
    *previous = *previous_valid ? scratch[1] : 0;
    uint32_t next = *previous_valid ? *previous + 1u : 1u;
    scratch[0] = 0;
    scratch[1] = next;
    scratch[2] = ~next;
    scratch[0] = FAULT_COUNTER_MAGIC;
    return next;
}
