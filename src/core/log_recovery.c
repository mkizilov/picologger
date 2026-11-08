#include "log_recovery.h"

#include "log_format.h"

log_recovery_status_t log_recover(const log_recovery_file_t *file, log_recovery_result_t *result)
{
    if (!file || !result || !file->size || !file->read_at || !file->truncate_sync)
        return LOG_RECOVERY_IO_ERROR;
    uint64_t size;
    if (!file->size(file->context, &size)) return LOG_RECOVERY_IO_ERROR;
    result->original_size = size;
    result->recovered_size = size;
    if (size < LOG_HEADER_SIZE) return LOG_RECOVERY_INVALID_HEADER;
    uint8_t buffer[LOG_HEADER_V2_SIZE];
    size_t read = 0;
    if (!file->read_at(file->context, 0, buffer, LOG_HEADER_SIZE, &read) || read != LOG_HEADER_SIZE)
        return LOG_RECOVERY_IO_ERROR;
    uint16_t version = (uint16_t)(buffer[4] | (uint16_t)buffer[5] << 8);
    uint16_t header_size = (uint16_t)(buffer[6] | (uint16_t)buffer[7] << 8);
    if (!((version == LOG_FORMAT_VERSION && header_size == LOG_HEADER_SIZE) ||
          (version == LOG_FORMAT_VERSION_V2 && header_size == LOG_HEADER_V2_SIZE)))
        return LOG_RECOVERY_INVALID_HEADER;
    if (size < header_size) return LOG_RECOVERY_INVALID_HEADER;
    if (header_size > LOG_HEADER_SIZE &&
        (!file->read_at(file->context, 0, buffer, header_size, &read) || read != header_size))
        return LOG_RECOVERY_IO_ERROR;
    log_header_t header;
    if (log_decode_header(&header, buffer, header_size) != LOG_OK) return LOG_RECOVERY_INVALID_HEADER;

    uint64_t records = (size - header_size) / LOG_RECORD_SIZE;
    while (records) {
        uint64_t offset = header_size + (records - 1) * LOG_RECORD_SIZE;
        if (!file->read_at(file->context, offset, buffer, LOG_RECORD_SIZE, &read) || read != LOG_RECORD_SIZE)
            return LOG_RECOVERY_IO_ERROR;
        log_record_t record;
        if (log_decode_record(&record, buffer, LOG_RECORD_SIZE) == LOG_OK) break;
        --records;
    }
    uint64_t recovered = header_size + records * LOG_RECORD_SIZE;
    if (recovered != size && !file->truncate_sync(file->context, recovered)) return LOG_RECOVERY_IO_ERROR;
    result->recovered_size = recovered;
    return LOG_RECOVERY_OK;
}
