#include "log_writer.h"
#include "power_policy.h"

#include <stdio.h>
#include <string.h>

static void disconnect(log_writer_t *writer)
{
    writer->uncertain += writer->file_records - writer->synced_records;
    writer->storage.unmount(writer->storage.context);
    writer->online = false;
    writer->file_open = false;
    writer->current_number = 0;
    writer->file_records = writer->synced_records = writer->written_bytes = 0;
    writer->used = 0;
    writer->next_retry = writer->storage.now_us(writer->storage.context) + LOG_RETRY_US;
}

static bool create_file(log_writer_t *writer)
{
    char name[20];
    while (writer->next_name <= 9999) {
        (void)snprintf(name, sizeof(name), "0:/LOG_%04u.BIN", writer->next_name++);
        uint64_t write_us = 0, sync_us = 0;
        int result = writer->storage.create(writer->storage.context, name, writer->header,
                                            LOG_HEADER_V2_SIZE, &write_us, &sync_us);
        if (write_us > writer->worst_write_us) writer->worst_write_us = write_us;
        if (sync_us > writer->worst_sync_us) writer->worst_sync_us = sync_us;
        if (result == 1) continue;
        if (result != 0) return false;
        ++writer->write_operations; /* Synced header write. */
        writer->file_open = true;
        writer->current_number = writer->next_name - 1;
        writer->used = 0;
        writer->file_records = writer->synced_records = 0;
        writer->written_bytes = LOG_HEADER_V2_SIZE;
        return true;
    }
    return false;
}

static bool write_chunk(log_writer_t *writer)
{
    size_t written = 0;
    uint64_t start = writer->storage.now_us(writer->storage.context);
    bool ok = writer->storage.write(writer->storage.context, writer->chunk, writer->used, &written);
    uint64_t duration = writer->storage.now_us(writer->storage.context) - start;
    if (duration > writer->worst_write_us) writer->worst_write_us = duration;
    if (!ok || written != writer->used) return false;
    ++writer->write_operations;
    writer->written_bytes += (uint32_t)written;
    writer->used = 0;
    return true;
}

static bool sync_file(log_writer_t *writer)
{
    uint64_t start = writer->storage.now_us(writer->storage.context);
    bool ok = writer->storage.sync(writer->storage.context);
    uint64_t duration = writer->storage.now_us(writer->storage.context) - start;
    if (duration > writer->worst_sync_us) writer->worst_sync_us = duration;
    if (!ok) return false;
    writer->synced_records = writer->written_bytes > LOG_HEADER_V2_SIZE ?
        (writer->written_bytes - LOG_HEADER_V2_SIZE) / LOG_RECORD_SIZE : 0;
    writer->next_sync = writer->storage.now_us(writer->storage.context) + writer->sync_interval;
    return true;
}

static bool rotate(log_writer_t *writer)
{
    if ((writer->used && !write_chunk(writer)) || !sync_file(writer) ||
        !writer->storage.close(writer->storage.context)) return false;
    writer->file_open = false;
    writer->current_number = 0;
    return create_file(writer);
}

bool log_writer_pause(log_writer_t *writer)
{
    if (!writer->enabled) return true;
    writer->enabled = false;
    if (!writer->online || !writer->file_open) return true;
    if ((writer->used && !write_chunk(writer)) || !sync_file(writer) ||
        !writer->storage.close(writer->storage.context)) {
        disconnect(writer);
        return false;
    }
    writer->file_open = false;
    writer->current_number = 0;
    writer->used = 0;
    writer->file_records = writer->synced_records = writer->written_bytes = 0;
    return true;
}

bool log_writer_resume(log_writer_t *writer, uint32_t rate_hz)
{
    if (rate_hz < 1 || rate_hz > 50) return false;
    if (writer->enabled) return true;
    log_header_t header;
    if (log_decode_header(&header, writer->header, sizeof(writer->header)) != LOG_OK) return false;
    header.sample_rate_hz = rate_hz;
    if (log_encode_header_v3(writer->header, sizeof(writer->header), &header) != LOG_OK) return false;
    writer->sync_interval = rate_hz <= PICOLOGGER_LOW_RATE_MAX_HZ ? LOG_SYNC_LOW_US : LOG_SYNC_US;
    writer->presence_interval = rate_hz <= PICOLOGGER_LOW_RATE_MAX_HZ ? LOG_SYNC_LOW_US : LOG_PRESENCE_US;
    writer->enabled = true;
    if (writer->online && !create_file(writer)) disconnect(writer);
    if (writer->online) {
        uint64_t now = writer->storage.now_us(writer->storage.context);
        writer->next_sync = now + writer->sync_interval;
        writer->next_presence = now + writer->presence_interval;
    }
    return true;
}

bool log_writer_init(log_writer_t *writer, const log_storage_t *storage,
                     const log_header_t *header, uint32_t rotate_bytes)
{
    if (!writer || !storage || !header || !storage->mount || !storage->recover_newest || !storage->create || !storage->write ||
        !storage->sync || !storage->close || !storage->unmount || !storage->present || !storage->now_us ||
        rotate_bytes < LOG_WRITE_CHUNK * 2u) return false;
    memset(writer, 0, sizeof(*writer));
    writer->storage = *storage;
    writer->enabled = true;
    writer->sync_interval = header->sample_rate_hz <= PICOLOGGER_LOW_RATE_MAX_HZ ? LOG_SYNC_LOW_US : LOG_SYNC_US;
    writer->presence_interval = header->sample_rate_hz <= PICOLOGGER_LOW_RATE_MAX_HZ ? LOG_SYNC_LOW_US : LOG_PRESENCE_US;
    writer->record_limit = (rotate_bytes - LOG_HEADER_V2_SIZE) / LOG_RECORD_SIZE;
    return log_encode_header_v3(writer->header, sizeof(writer->header), header) == LOG_OK;
}

void log_writer_maintain(log_writer_t *writer)
{
    uint64_t now = writer->storage.now_us(writer->storage.context);
    if (!writer->online) {
        if (now < writer->next_retry) return;
        if (!writer->storage.mount(writer->storage.context)) { disconnect(writer); return; }
        bool found = false;
        log_recovery_result_t recovered = {0};
        if (!writer->storage.recover_newest(writer->storage.context, &found, &recovered)) {
            ++writer->recovery_errors;
            disconnect(writer);
            return;
        }
        if (found) writer->recovered_bytes += recovered.original_size - recovered.recovered_size;
        writer->next_name = 1;
        if (writer->enabled && !create_file(writer)) { disconnect(writer); return; }
        writer->online = true;
        now = writer->storage.now_us(writer->storage.context);
        writer->next_sync = now + writer->sync_interval;
        writer->next_presence = now + writer->presence_interval;
        return;
    }
    if (now >= writer->next_presence) {
        /* Ensure the SPI driver has finished any multi-block write before
         * issuing the card-presence command. */
        if (writer->file_open && !sync_file(writer)) { disconnect(writer); return; }
        if (!writer->storage.present(writer->storage.context)) { disconnect(writer); return; }
        now = writer->storage.now_us(writer->storage.context);
        writer->next_presence = now + writer->presence_interval;
    } else if (writer->file_open && now >= writer->next_sync && !sync_file(writer)) disconnect(writer);
}

void log_writer_append(log_writer_t *writer, const uint8_t *records, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        if (!writer->enabled || !writer->online || !writer->file_open) { writer->dropped += count - index; return; }
        if (writer->file_records == writer->record_limit && !rotate(writer)) {
            disconnect(writer);
            writer->dropped += count - index;
            return;
        }
        ++writer->file_records;
        size_t offset = 0;
        while (offset < LOG_RECORD_SIZE) {
            size_t n = LOG_WRITE_CHUNK - writer->used;
            if (n > LOG_RECORD_SIZE - offset) n = LOG_RECORD_SIZE - offset;
            memcpy(writer->chunk + writer->used, records + index * LOG_RECORD_SIZE + offset, n);
            writer->used += n;
            offset += n;
            if (writer->used == LOG_WRITE_CHUNK && !write_chunk(writer)) {
                disconnect(writer);
                writer->dropped += count - index - 1;
                return;
            }
        }
        ++writer->accepted_records;
    }
}
