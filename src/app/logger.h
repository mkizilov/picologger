#ifndef PICOLOGGER_LOGGER_H
#define PICOLOGGER_LOGGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "fault_record.h"
#include "log_format.h"

#define LOGGER_SAMPLE_RING_CAPACITY 1024u

typedef enum { LOGGER_RUNNING, LOGGER_STOPPING, LOGGER_STOPPED, LOGGER_STARTING } logger_state_t;
typedef enum { LOGGER_FLASH_IDLE, LOGGER_FLASH_REQUESTING, LOGGER_FLASH_READY,
               LOGGER_FLASH_RELEASING, LOGGER_FLASH_FAILED } logger_flash_state_t;
typedef struct {
    logger_state_t state;
    bool online, control_ok;
    uint32_t current_file;
    uint64_t dropped, uncertain;
    uint32_t worker_progress, writer_progress, physical_progress;
    bool physical_observed;
    uint64_t physical_at_us;
    uint32_t ring_progress;
} logger_status_t;

void logger_start(void);
/* Called before logger_start; false start leaves the writer stopped at boot. */
void logger_configure_boot(uint32_t rate_hz, uint32_t rotate_bytes, bool start);
void logger_configure_boot_report(const boot_report_t *report);
bool logger_take_fault_logged(void);
void logger_poll(void);
/* Core 0 is the sole producer; core 1 is the sole consumer. */
bool logger_submit_sample(const log_record_t *record);
uint32_t logger_ring_level(void);
uint32_t logger_ring_high_water(void);
uint32_t logger_take_ring_overflows(void);
void logger_report_boot(void);
/* Stops accepting new records; core 1 drains all records accepted before the request. */
bool logger_request_stop(void);
bool logger_request_start(uint32_t rate_hz);
void logger_get_status(logger_status_t *status);
bool logger_request_list(void);
bool logger_request_dump(const char *name);
bool logger_take_reply(char *line, size_t capacity, bool *final);
bool logger_io_busy(void);
void logger_cancel_io(void);
bool logger_flash_prepare(void);
bool logger_flash_release(void);
logger_flash_state_t logger_flash_state(void);

#endif
