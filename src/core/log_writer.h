#ifndef PICOLOGGER_LOG_WRITER_H
#define PICOLOGGER_LOG_WRITER_H

#include "log_format.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOG_WRITE_CHUNK 4096u
#define LOG_RECORDS_PER_BATCH (LOG_WRITE_CHUNK / LOG_RECORD_SIZE)
#define LOG_RETRY_US UINT64_C(3000000)
#define LOG_SYNC_US UINT64_C(5000000)
#define LOG_PRESENCE_US UINT64_C(5000000)

/* All callbacks run on the storage core only. create returns 0 on success,
 * 1 if the name already exists, and -1 for any other failure. */
typedef struct {
    void *context;
    bool (*mount)(void *);
    int (*create)(void *, const char *);
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
    uint8_t header[LOG_HEADER_SIZE];
    size_t used;
    uint32_t record_limit, file_records, synced_records, written_bytes;
    unsigned int next_name;
    bool online;
    uint64_t next_retry, next_sync, next_presence;
    uint64_t dropped, uncertain, worst_write_us, worst_sync_us;
} log_writer_t;

bool log_writer_init(log_writer_t *writer, const log_storage_t *storage,
                     const log_header_t *header, uint32_t rotate_bytes);
void log_writer_maintain(log_writer_t *writer);
/* Input holds count pre-encoded, complete LOG_RECORD_SIZE records. */
void log_writer_append(log_writer_t *writer, const uint8_t *records, size_t count);

#endif
