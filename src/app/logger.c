#include "logger.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "firmware_version.h"
#include "command_io.h"
#include "log_writer.h"
#include "logger_channel.h"
#include "sd_storage.h"
#include "sensor.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#ifndef PICOLOGGER_SAMPLE_RATE_HZ
#define PICOLOGGER_SAMPLE_RATE_HZ 50
#endif
#ifndef PICOLOGGER_LOG_ROTATE_BYTES
#define PICOLOGGER_LOG_ROTATE_BYTES 16777216u
#endif

enum { BATCH_COUNT = 4 };
#define MSG_KIND_MASK UINT32_C(0xf0000000)
#define MSG_VALUE_MASK UINT32_C(0x0fffffff)
#define MSG_FREE UINT32_C(0x00000000)
#define MSG_DROPPED UINT32_C(0x10000000)
#define MSG_UNCERTAIN UINT32_C(0x20000000)
#define MSG_WORST UINT32_C(0x30000000)
#define MSG_ONLINE UINT32_C(0x40000000)
#define MSG_SYNC UINT32_C(0x50000000)
#define MSG_RECOVERED UINT32_C(0x60000000)
#define MSG_RECOVERY_ERRORS UINT32_C(0x70000000)
#define MSG_STOP UINT32_C(0x80000000)
#define MSG_START UINT32_C(0x90000000)
#define MSG_FILE UINT32_C(0xa0000000)
#define MSG_IO_REQUEST UINT32_C(0xb0000000)
#define MSG_IO_ACK UINT32_C(0xc0000000)
#define MSG_IO_REPLY UINT32_C(0xd0000000)
#define MSG_IO_CANCEL UINT32_C(0xe0000000)
#define MSG_FLASH UINT32_C(0xf0000000)
enum { FLASH_INIT_OK = 1, FLASH_INIT_FAILED, FLASH_PREPARE, FLASH_READY, FLASH_REJECTED,
       FLASH_RESUME, FLASH_RELEASED, FLASH_WORKER_PROGRESS, FLASH_WRITER_PROGRESS, FLASH_FAULT_LOGGED,
       FLASH_PHYSICAL_PROGRESS };

typedef struct {
    _Alignas(512) uint8_t bytes[LOG_WRITE_CHUNK];
    uint32_t count;
} log_batch_t;

/* Queue messages transfer ownership. Only the owning core may access a batch. */
static log_batch_t batches[BATCH_COUNT];
static log_writer_t writer;
_Alignas(8) static uint32_t storage_stack[2048];
static unsigned int free_mask;
static int active_batch = -1;
static uint64_t batch_started, next_report, dropped, uncertain, worst_write_us, worst_sync_us;
static uint64_t recovered_bytes, recovery_errors;
static bool online, control_ok = true;
static bool channel_available, flash_supported, flash_control_sent;
static logger_flash_state_t flash_state = LOGGER_FLASH_IDLE;
static logger_state_t state = LOGGER_RUNNING;
static bool control_sent;
static uint32_t start_rate, current_file;
static uint32_t worker_progress, writer_progress, physical_progress;
static bool physical_observed;
static uint64_t physical_at_us;
static uint32_t boot_rate = PICOLOGGER_SAMPLE_RATE_HZ, boot_rotate = PICOLOGGER_LOG_ROTATE_BYTES;
static bool boot_logging = true;
static boot_report_t boot_report;
static bool fault_logged;

typedef struct { uint32_t kind; char name[COMMAND_FILENAME_MAX + 1]; } io_request_t;
typedef struct { char line[64]; bool final; } io_reply_t;
static io_request_t io_request;
static io_reply_t io_reply;
static bool io_busy, io_request_pending, io_reply_ready, io_ack_pending, io_cancel_pending;
static bool io_cancelling;
static uint64_t last_heartbeat;

static void worker_heartbeat(void)
{
    uint64_t now = time_us_64();
    if (now - last_heartbeat >= UINT64_C(250000)) {
        last_heartbeat = now;
        logger_channel_to_main(MSG_FLASH | FLASH_WORKER_PROGRESS);
    }
}

typedef struct {
    uint32_t kind;
    bool waiting, started, final;
    uint64_t offset;
    uint32_t crc;
} worker_io_t;

static void worker_io_close(worker_io_t *io)
{
    if (io->kind == 1) (void)sd_storage_list_close();
    if (io->kind == 2) (void)sd_storage_dump_close();
    *io = (worker_io_t){0};
}

static bool same_ascii_casefold(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'a' && x <= 'z') x = (char)(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z') y = (char)(y - 'a' + 'A');
        if (x != y) return false;
    }
    return *a == *b;
}

static void worker_io_begin(worker_io_t *io, const log_writer_t *storage)
{
    __dmb();
    *io = (worker_io_t){.kind = io_request.kind, .crc = UINT32_MAX};
    if (!storage->online) { (void)snprintf(io_reply.line, sizeof(io_reply.line), "ERR SD unavailable\n"); io->final = true; return; }
    if (io->kind == 1) {
        if (sd_storage_list_open()) return;
    } else if (io->kind == 2) {
        char current[16];
        (void)snprintf(current, sizeof(current), "LOG_%04u.BIN", storage->current_number);
        if (storage->file_open && same_ascii_casefold(current, io_request.name)) {
            (void)snprintf(io_reply.line, sizeof(io_reply.line), "ERR active file; stop first\n");
            io->final = true;
            return;
        }
        if (sd_storage_dump_open(io_request.name)) return;
    }
    (void)snprintf(io_reply.line, sizeof(io_reply.line), "ERR open failed\n");
    io->final = true;
}

static void worker_io_step(worker_io_t *io, bool media_online)
{
    if (!io->kind || io->waiting) return;
    if (!media_online && !io->final) {
        if (io->kind == 1) (void)sd_storage_list_close();
        if (io->kind == 2) (void)sd_storage_dump_close();
        (void)snprintf(io_reply.line, sizeof(io_reply.line), "ERR SD disconnected\n");
        io->final = true;
    } else if (!io->started && !io->final) {
        if (io->kind == 1) (void)snprintf(io_reply.line, sizeof(io_reply.line), "BEGIN LS\n");
        else (void)snprintf(io_reply.line, sizeof(io_reply.line), "BEGIN DUMP %s\n", io_request.name);
        io->started = true;
    } else if (!io->final && io->kind == 1) {
        sd_list_entry_t entry;
        int result = sd_storage_list_next(&entry);
        if (result == 1) (void)snprintf(io_reply.line, sizeof(io_reply.line), "%s %s %" PRIu64 "\n",
                                       entry.directory ? "DIR" : "FILE", entry.name, entry.size);
        else if (result == 2) (void)snprintf(io_reply.line, sizeof(io_reply.line), "WARN unsupported filename\n");
        else {
            bool closed = sd_storage_list_close();
            (void)snprintf(io_reply.line, sizeof(io_reply.line), result == 0 && closed ? "END LS\n" : "ERR list failed\n");
            io->final = true;
        }
    } else if (!io->final) {
        uint8_t data[24];
        size_t count = 0;
        if (!sd_storage_dump_read(data, sizeof(data), &count)) {
            (void)sd_storage_dump_close();
            (void)snprintf(io_reply.line, sizeof(io_reply.line), "ERR read failed\n");
            io->final = true;
        } else if (!count) {
            bool ok = sd_storage_dump_close();
            if (ok) (void)snprintf(io_reply.line, sizeof(io_reply.line), "END DUMP %" PRIu64 " %08" PRIx32 "\n",
                                   io->offset, io->crc ^ UINT32_MAX);
            else (void)snprintf(io_reply.line, sizeof(io_reply.line), "ERR close failed\n");
            io->final = true;
        } else {
            char encoded[33];
            (void)command_base64(data, count, encoded, sizeof(encoded));
            io->crc = command_crc32_update(io->crc, data, count);
            (void)snprintf(io_reply.line, sizeof(io_reply.line), "DATA %" PRIu64 " %s\n", io->offset, encoded);
            io->offset += count;
        }
    }
    io_reply.final = io->final;
    io->waiting = true;
    __dmb();
    logger_channel_to_main(MSG_IO_REPLY);
}

static void send_delta(uint32_t kind, uint64_t *sent, uint64_t total)
{
    while (*sent < total) {
        uint64_t remaining = total - *sent;
        uint32_t part = remaining > MSG_VALUE_MASK ? MSG_VALUE_MASK : (uint32_t)remaining;
        logger_channel_to_main(kind | part);
        *sent += part;
    }
}

static void storage_worker(void)
{
    bool lockout_ready = flash_safe_execute_core_init();
    logger_channel_to_main(MSG_FLASH | (lockout_ready ? FLASH_INIT_OK : FLASH_INIT_FAILED));
    sd_storage_set_progress_hook(worker_heartbeat);
    log_header_t header = {.sample_rate_hz = boot_rate, .channel_count = 3,
                           .channel_ids = {1, 2, 3}, .boot_counter = boot_report.boot_counter,
                           .reset_reason = boot_report.reset_reason, .watchdog_reason = boot_report.watchdog_reason,
                           .fault_mask = boot_report.fault_mask, .watchdog_missing = boot_report.watchdog_missing};
    header.fault[0] = boot_report.fault[0];
    header.fault[1] = boot_report.fault[1];
    header.fault_boot_id[0] = boot_report.fault_boot_id[0];
    header.fault_boot_id[1] = boot_report.fault_boot_id[1];
    const char *version = PICOLOGGER_VERSION;
    unsigned int i = 0;
    while (i < LOG_FIRMWARE_SIZE - 1 && version[i]) {
        unsigned char c = (unsigned char)version[i];
        header.firmware[i] = (char)(c >= 32 && c <= 126 ? c : '_');
        ++i;
    }
    if (!i) { header.firmware[0] = '?'; }
    log_storage_t storage = sd_storage_interface();
    bool initialized = log_writer_init(&writer, &storage, &header, boot_rotate);
    if (initialized) writer.enabled = boot_logging;
    uint64_t sent_dropped = 0, sent_uncertain = 0, sent_worst = 0, sent_sync = 0;
    uint64_t sent_recovered = 0, sent_recovery_errors = 0;
    uint64_t sent_accepted = 0, sent_writes = 0;
    bool last_online = false, status_sent = false;
    bool fault_log_notified = false;
    unsigned int last_file = 0;
    worker_io_t io = {0};
    bool flash_quiesced = false;
    for (;;) {
        if (initialized && !flash_quiesced) log_writer_maintain(&writer);
        bool current_online = initialized && writer.online;
        if (!status_sent || current_online != last_online) {
            logger_channel_to_main(MSG_ONLINE | (current_online ? 1u : 0u));
            last_online = current_online;
            status_sent = true;
        }
        uint32_t index;
        if (logger_channel_from_main(&index)) {
            __dmb();
            if (index < BATCH_COUNT) {
                if (initialized && !flash_quiesced) log_writer_append(&writer, batches[index].bytes, batches[index].count);
                else writer.dropped += batches[index].count;
                __dmb();
                logger_channel_to_main(MSG_FREE | index);
            } else if (index == MSG_STOP) {
                bool ok = initialized && log_writer_pause(&writer);
                logger_channel_to_main(MSG_STOP | (ok ? 1u : 0u));
            } else if ((index & MSG_KIND_MASK) == MSG_START) {
                bool ok = initialized && !flash_quiesced && log_writer_resume(&writer, index & MSG_VALUE_MASK);
                logger_channel_to_main(MSG_START | (ok ? 1u : 0u));
            } else if (index == MSG_IO_REQUEST) {
                if (!io.kind && !flash_quiesced) worker_io_begin(&io, &writer);
            } else if (index == MSG_IO_ACK) {
                if (io.waiting) {
                    if (io.final) worker_io_close(&io);
                    else io.waiting = false;
                }
            } else if (index == MSG_IO_CANCEL) {
                worker_io_close(&io);
                logger_channel_to_main(MSG_IO_CANCEL);
            } else if (index == (MSG_FLASH | FLASH_PREPARE)) {
                if (lockout_ready && initialized && !writer.enabled && !writer.file_open && !io.kind) {
                    flash_quiesced = true;
                    logger_channel_to_main(MSG_FLASH | FLASH_READY);
                } else logger_channel_to_main(MSG_FLASH | FLASH_REJECTED);
            } else if (index == (MSG_FLASH | FLASH_RESUME)) {
                flash_quiesced = false;
                logger_channel_to_main(MSG_FLASH | FLASH_RELEASED);
            }
        }
        if (!flash_quiesced) worker_io_step(&io, initialized && writer.online);
        unsigned int file_number = initialized ? writer.current_number : 0;
        if (file_number && boot_report.fault_mask && !fault_log_notified) {
            logger_channel_to_main(MSG_FLASH | FLASH_FAULT_LOGGED);
            fault_log_notified = true;
        }
        if (file_number != last_file) {
            logger_channel_to_main(MSG_FILE | file_number);
            last_file = file_number;
        }
        send_delta(MSG_DROPPED, &sent_dropped, writer.dropped);
        send_delta(MSG_UNCERTAIN, &sent_uncertain, writer.uncertain);
        send_delta(MSG_RECOVERED, &sent_recovered, writer.recovered_bytes);
        send_delta(MSG_RECOVERY_ERRORS, &sent_recovery_errors, writer.recovery_errors);
        if (initialized && writer.accepted_records != sent_accepted) {
            sent_accepted = writer.accepted_records;
            logger_channel_to_main(MSG_FLASH | FLASH_WRITER_PROGRESS);
        }
        if (initialized && writer.write_operations != sent_writes) {
            sent_writes = writer.write_operations;
            logger_channel_to_main(MSG_FLASH | FLASH_PHYSICAL_PROGRESS);
        }
        worker_heartbeat();
        if (writer.worst_write_us > sent_worst) {
            sent_worst = writer.worst_write_us;
            uint32_t value = sent_worst > MSG_VALUE_MASK ? MSG_VALUE_MASK : (uint32_t)sent_worst;
            logger_channel_to_main(MSG_WORST | value);
        }
        if (writer.worst_sync_us > sent_sync) {
            sent_sync = writer.worst_sync_us;
            uint32_t value = sent_sync > MSG_VALUE_MASK ? MSG_VALUE_MASK : (uint32_t)sent_sync;
            logger_channel_to_main(MSG_SYNC | value);
        }
        sleep_ms(1);
    }
}

void logger_start(void)
{
    state = boot_logging ? LOGGER_RUNNING : LOGGER_STOPPED;
    free_mask = (1u << BATCH_COUNT) - 1u;
    next_report = time_us_64() + UINT64_C(10000000);
    channel_available = logger_channel_init();
    if (!channel_available) { state = LOGGER_STOPPED; return; }
    __dmb();
    multicore_launch_core1_with_stack(storage_worker, storage_stack, sizeof(storage_stack));
}

void logger_configure_boot(uint32_t rate_hz, uint32_t rotate_bytes, bool start)
{
    boot_rate = rate_hz;
    boot_rotate = rotate_bytes;
    boot_logging = start;
}

void logger_configure_boot_report(const boot_report_t *report) { boot_report = *report; }
bool logger_take_fault_logged(void)
{
    bool logged = fault_logged;
    fault_logged = false;
    return logged;
}

void logger_note_drop(uint32_t count) { dropped += count; }

bool logger_request_stop(void)
{
    if (state == LOGGER_STOPPED) return true;
    if (!channel_available || state != LOGGER_RUNNING || flash_state != LOGGER_FLASH_IDLE) return false;
    state = LOGGER_STOPPING;
    control_sent = false;
    return true;
}

bool logger_request_start(uint32_t rate_hz)
{
    if (!channel_available || flash_state != LOGGER_FLASH_IDLE || state != LOGGER_STOPPED || rate_hz < 1 || rate_hz > 50) return false;
    start_rate = rate_hz;
    state = LOGGER_STARTING;
    control_sent = false;
    return true;
}

void logger_get_status(logger_status_t *status)
{
    *status = (logger_status_t){state, online, control_ok, current_file, dropped, uncertain,
                                worker_progress, writer_progress, physical_progress, physical_observed, physical_at_us};
}

bool logger_io_busy(void) { return !channel_available || io_busy || io_ack_pending || io_cancel_pending || flash_state != LOGGER_FLASH_IDLE; }

logger_flash_state_t logger_flash_state(void) { return flash_state; }

bool logger_flash_prepare(void)
{
    if (!channel_available || !flash_supported || state != LOGGER_STOPPED || logger_io_busy()) return false;
    flash_state = LOGGER_FLASH_REQUESTING;
    flash_control_sent = false;
    return true;
}

bool logger_flash_release(void)
{
    if (flash_state == LOGGER_FLASH_IDLE) return true;
    if (flash_state == LOGGER_FLASH_REQUESTING && !flash_control_sent) {
        flash_state = LOGGER_FLASH_IDLE;
        return true;
    }
    if (flash_state != LOGGER_FLASH_REQUESTING && flash_state != LOGGER_FLASH_READY && flash_state != LOGGER_FLASH_FAILED)
        return false;
    flash_state = LOGGER_FLASH_RELEASING;
    flash_control_sent = false;
    return true;
}

bool logger_request_list(void)
{
    if (logger_io_busy()) return false;
    io_request = (io_request_t){.kind = 1};
    io_busy = io_request_pending = true;
    return true;
}

bool logger_request_dump(const char *name)
{
    if (logger_io_busy() || !command_filename_valid(name)) return false;
    io_request = (io_request_t){.kind = 2};
    memcpy(io_request.name, name, strlen(name) + 1);
    io_busy = io_request_pending = true;
    return true;
}

bool logger_take_reply(char *line, size_t capacity, bool *final)
{
    if (!io_reply_ready || !line || !final) return false;
    size_t length = strlen(io_reply.line);
    if (length >= capacity) return false;
    memcpy(line, io_reply.line, length + 1);
    *final = io_reply.final;
    io_reply_ready = false;
    io_ack_pending = true;
    if (*final) io_busy = false;
    return true;
}

void logger_cancel_io(void)
{
    if (!io_busy || io_cancelling) return;
    io_cancelling = true;
    io_request_pending = false;
    io_cancel_pending = true;
    if (io_reply_ready) { io_reply_ready = false; io_ack_pending = true; }
}

void logger_report_boot(void)
{
    printf("SD logging: BME280 records, rotate=%" PRIu32 " bytes, SPI1 GP10/11/12 CS13\n",
           boot_rotate);
    if (!channel_available) puts("Storage communication initialization failed");
}

void logger_poll(void)
{
    if (!channel_available) {
        log_record_t discarded;
        for (unsigned int n = 0; n < 8 && sensor_take_sample(&discarded); ++n) ++dropped;
        return;
    }
    uint32_t message;
    for (unsigned int n = 0; n < 8 && logger_channel_from_worker(&message); ++n) {
        __dmb();
        uint32_t value = message & MSG_VALUE_MASK;
        switch (message & MSG_KIND_MASK) {
            case MSG_FREE: if (value < BATCH_COUNT) free_mask |= 1u << value; break;
            case MSG_DROPPED: dropped += value; break;
            case MSG_UNCERTAIN: uncertain += value; break;
            case MSG_WORST: if (value > worst_write_us) worst_write_us = value; break;
            case MSG_SYNC: if (value > worst_sync_us) worst_sync_us = value; break;
            case MSG_RECOVERED: recovered_bytes += value; break;
            case MSG_RECOVERY_ERRORS: recovery_errors += value; break;
            case MSG_ONLINE: online = value != 0; break;
            case MSG_FILE: current_file = value; break;
            case MSG_STOP: control_ok = value != 0; state = LOGGER_STOPPED; control_sent = false; break;
            case MSG_START: control_ok = value != 0; state = value ? LOGGER_RUNNING : LOGGER_STOPPED; control_sent = false; break;
            case MSG_IO_REPLY:
                if (io_busy && !io_cancelling) io_reply_ready = true;
                else io_ack_pending = true;
                break;
            case MSG_IO_CANCEL: io_busy = io_cancelling = false; break;
            case MSG_FLASH:
                if (value == FLASH_INIT_OK) flash_supported = true;
                else if (value == FLASH_INIT_FAILED) flash_supported = false;
                else if (value == FLASH_READY && flash_state == LOGGER_FLASH_REQUESTING) flash_state = LOGGER_FLASH_READY;
                else if (value == FLASH_REJECTED && flash_state == LOGGER_FLASH_REQUESTING) flash_state = LOGGER_FLASH_FAILED;
                else if (value == FLASH_RELEASED && flash_state == LOGGER_FLASH_RELEASING) flash_state = LOGGER_FLASH_IDLE;
                else if (value == FLASH_WORKER_PROGRESS) ++worker_progress;
                else if (value == FLASH_WRITER_PROGRESS) ++writer_progress;
                else if (value == FLASH_FAULT_LOGGED) fault_logged = true;
                else if (value == FLASH_PHYSICAL_PROGRESS) {
                    ++physical_progress; physical_observed = true; physical_at_us = time_us_64();
                }
                break;
            default: break;
        }
    }
    if (io_ack_pending && logger_channel_to_worker(MSG_IO_ACK)) {
        io_ack_pending = false;
    }
    if (io_cancel_pending && logger_channel_to_worker(MSG_IO_CANCEL)) {
        io_cancel_pending = false;
    }
    if (io_request_pending && !io_ack_pending && !io_cancel_pending) {
        __dmb();
        if (logger_channel_to_worker(MSG_IO_REQUEST)) io_request_pending = false;
    }
    uint64_t now = time_us_64();
    if ((state == LOGGER_RUNNING || (state == LOGGER_STOPPING && !control_sent)) && active_batch < 0 && free_mask) {
        for (unsigned int i = 0; i < BATCH_COUNT; ++i) {
            if (free_mask & (1u << i)) {
                active_batch = (int)i;
                free_mask &= ~(1u << i);
                batches[i].count = 0;
                break;
            }
        }
    }
    bool drained = false;
    if (active_batch >= 0 && (state == LOGGER_RUNNING || (state == LOGGER_STOPPING && !control_sent))) {
        log_batch_t *batch = &batches[active_batch];
        log_record_t record;
        for (unsigned int n = 0; n < 8 && batch->count < LOG_RECORDS_PER_BATCH; ++n) {
            if (!sensor_take_sample(&record)) { drained = true; break; }
            if (!batch->count) batch_started = now;
            if (log_encode_record(batch->bytes + batch->count * LOG_RECORD_SIZE, LOG_RECORD_SIZE, &record) != LOG_OK) {
                ++dropped;
                continue;
            }
            ++batch->count;
        }
        if (batch->count && (batch->count == LOG_RECORDS_PER_BATCH || now - batch_started >= UINT64_C(100000) ||
                             (state == LOGGER_STOPPING && drained))) {
            __dmb();
            if (logger_channel_to_worker((uint32_t)active_batch)) active_batch = -1;
        }
        if (state == LOGGER_STOPPING && drained && active_batch >= 0 && !batch->count) {
            free_mask |= 1u << active_batch;
            active_batch = -1;
        }
    }
    if (state == LOGGER_STOPPING && drained && active_batch < 0 && !control_sent && logger_channel_to_worker(MSG_STOP)) {
        control_sent = true;
    }
    if (state == LOGGER_STARTING && !control_sent && logger_channel_to_worker(MSG_START | start_rate)) {
        control_sent = true;
    }
    if (flash_state == LOGGER_FLASH_REQUESTING && !flash_control_sent &&
        logger_channel_to_worker(MSG_FLASH | FLASH_PREPARE)) flash_control_sent = true;
    if (flash_state == LOGGER_FLASH_RELEASING && !flash_control_sent &&
        logger_channel_to_worker(MSG_FLASH | FLASH_RESUME)) flash_control_sent = true;
    if (now >= next_report) {
        if (stdio_usb_connected()) {
            printf("SD online=%u dropped=%" PRIu64 " uncertain=%" PRIu64 " worst_write_us=%" PRIu64 "%s"
                   " worst_sync_us=%" PRIu64 "%s recovered_bytes=%" PRIu64 " recovery_errors=%" PRIu64 "\n",
                   online ? 1u : 0u, dropped, uncertain, worst_write_us,
                   worst_write_us == MSG_VALUE_MASK ? "+" : "", worst_sync_us,
                   worst_sync_us == MSG_VALUE_MASK ? "+" : "", recovered_bytes, recovery_errors);
        }
        next_report = now + UINT64_C(10000000);
    }
}
