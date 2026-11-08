#ifndef PICOLOGGER_LOG_WRITER_H
#define PICOLOGGER_LOG_WRITER_H

#include "log_format.h"
#include "log_recovery.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOG_WRITE_CHUNK 4096u
#define LOG_RECORDS_PER_BATCH (LOG_WRITE_CHUNK / LOG_RECORD_SIZE)
#define LOG_RETRY_US UINT64_C(3000000)
#define LOG_SYNC_US UINT64_C(5000000)
#define LOG_PRESENCE_US UINT64_C(5000000)

/* All callbacks run on the storage core only. create publishes a synced
 * header, reopens the new file immediately after it, and reports header I/O times.
 * It returns 0 on success, 1 on name collision, -1 on any other failure. */
typedef struct {
    void *context;
    bool (*mount)(void *);
    bool (*recover_newest)(void *, bool *, log_recovery_result_t *);
    int (*create)(void *, const char *, const uint8_t *, size_t, uint64_t *, uint64_t *);
    bool (*write)(void *, const uint8_t *, size_t, size_t *);
    bool (*sync)(void *);
    bool (*close)(void *);
    void (*unmount)(void *);
    bool (*present)(void *);
    uint64_t (*now_us)(void *);
} log_storage_t;

typedef struct {
    log_storage_t storage;
    _Alignas(512) uint8_t chunk[LOG_WRITE_CHUNK];
    _Alignas(512) uint8_t header[LOG_HEADER_V2_SIZE];
    size_t used;
    uint32_t record_limit, file_records, synced_records, written_bytes;
    unsigned int next_name, current_number;
    bool online, enabled, file_open;
    uint64_t next_retry, next_sync, next_presence;
    uint64_t dropped, uncertain, worst_write_us, worst_sync_us, recovered_bytes, recovery_errors, accepted_records, write_operations;
} log_writer_t;

bool log_writer_init(log_writer_t *writer, const log_storage_t *storage,
                     const log_header_t *header, uint32_t rotate_bytes);
void log_writer_maintain(log_writer_t *writer);
/* These run only on the storage core; they can perform filesystem I/O. */
bool log_writer_pause(log_writer_t *writer);
bool log_writer_resume(log_writer_t *writer, uint32_t rate_hz);
/* Input holds count pre-encoded, complete LOG_RECORD_SIZE records. */
void log_writer_append(log_writer_t *writer, const uint8_t *records, size_t count);

#endif
