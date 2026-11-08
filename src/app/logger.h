#ifndef PICOLOGGER_LOGGER_H
#define PICOLOGGER_LOGGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum { LOGGER_RUNNING, LOGGER_STOPPING, LOGGER_STOPPED, LOGGER_STARTING } logger_state_t;
typedef struct {
    logger_state_t state;
    bool online, control_ok;
    uint32_t current_file;
    uint64_t dropped, uncertain;
} logger_status_t;

void logger_start(void);
void logger_poll(void);
void logger_note_drop(uint32_t count);
void logger_report_boot(void);
bool logger_request_stop(void);
bool logger_request_start(uint32_t rate_hz);
void logger_get_status(logger_status_t *status);
bool logger_request_list(void);
bool logger_request_dump(const char *name);
bool logger_take_reply(char *line, size_t capacity, bool *final);
bool logger_io_busy(void);
void logger_cancel_io(void);

#endif
