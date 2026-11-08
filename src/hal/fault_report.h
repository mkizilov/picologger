#ifndef PICOLOGGER_FAULT_REPORT_H
#define PICOLOGGER_FAULT_REPORT_H

#include "fault_record.h"

void fault_boot_init(void);
const boot_report_t *fault_boot_report(void);
void fault_report_print(void);
/* Acknowledge only after a synced log header containing this report exists. */
void fault_report_mark_logged(void);
void fault_note_watchdog(uint32_t missing);
void fault_preserve_sram_on_watchdog(void);
/* Deliberate test fault; never call from an interrupt. */
void fault_force_crash(void);

#endif
