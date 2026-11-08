#ifndef PICOLOGGER_LOG_RECOVERY_H
#define PICOLOGGER_LOG_RECOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *context;
    bool (*size)(void *context, uint64_t *size);
    bool (*read_at)(void *context, uint64_t offset, uint8_t *data, size_t length, size_t *read);
    /* Must durably sync the new length before returning success. */
    bool (*truncate_sync)(void *context, uint64_t length);
} log_recovery_file_t;

typedef enum {
    LOG_RECOVERY_OK = 0,
    LOG_RECOVERY_INVALID_HEADER,
    LOG_RECOVERY_IO_ERROR
} log_recovery_status_t;

typedef struct {
    uint64_t original_size;
    uint64_t recovered_size;
} log_recovery_result_t;

/* Scan only record-aligned positions after a valid v1 header. Invalid or
 * incomplete headers and read errors never cause truncation. */
log_recovery_status_t log_recover(const log_recovery_file_t *file, log_recovery_result_t *result);

#endif
