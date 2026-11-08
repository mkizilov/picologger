#include "logger.h"

#include <inttypes.h>
#include <stdio.h>

#include "firmware_version.h"
#include "log_writer.h"
#include "sd_storage.h"
#include "sensor.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
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

typedef struct {
    _Alignas(512) uint8_t bytes[LOG_WRITE_CHUNK];
    uint32_t count;
} log_batch_t;

/* FIFO transfers ownership. Only the owning core may access a batch. */
static log_batch_t batches[BATCH_COUNT];
static log_writer_t writer;
_Alignas(8) static uint32_t storage_stack[2048];
static unsigned int free_mask;
static int active_batch = -1;
static uint64_t batch_started, next_report, dropped, uncertain, worst_write_us, worst_sync_us;
static uint64_t recovered_bytes, recovery_errors;
static bool online;

static void send_delta(uint32_t kind, uint64_t *sent, uint64_t total)
{
    while (*sent < total) {
        uint64_t remaining = total - *sent;
        uint32_t part = remaining > MSG_VALUE_MASK ? MSG_VALUE_MASK : (uint32_t)remaining;
        multicore_fifo_push_blocking(kind | part);
        *sent += part;
    }
}

static void storage_worker(void)
{
    log_header_t header = {.sample_rate_hz = PICOLOGGER_SAMPLE_RATE_HZ, .channel_count = 3,
                           .channel_ids = {1, 2, 3}, .boot_counter = 0};
    const char *version = PICOLOGGER_VERSION;
    unsigned int i = 0;
    while (i < LOG_FIRMWARE_SIZE - 1 && version[i]) {
        unsigned char c = (unsigned char)version[i];
        header.firmware[i] = (char)(c >= 32 && c <= 126 ? c : '_');
        ++i;
    }
    if (!i) { header.firmware[0] = '?'; }
    log_storage_t storage = sd_storage_interface();
    bool initialized = log_writer_init(&writer, &storage, &header, PICOLOGGER_LOG_ROTATE_BYTES);
    uint64_t sent_dropped = 0, sent_uncertain = 0, sent_worst = 0, sent_sync = 0;
    uint64_t sent_recovered = 0, sent_recovery_errors = 0;
    bool last_online = false, status_sent = false;
    for (;;) {
        if (initialized) log_writer_maintain(&writer);
        bool current_online = initialized && writer.online;
        if (!status_sent || current_online != last_online) {
            multicore_fifo_push_blocking(MSG_ONLINE | (current_online ? 1u : 0u));
            last_online = current_online;
            status_sent = true;
        }
        if (multicore_fifo_rvalid()) {
            uint32_t index = multicore_fifo_pop_blocking();
            __dmb();
            if (index < BATCH_COUNT) {
                if (initialized) log_writer_append(&writer, batches[index].bytes, batches[index].count);
                else writer.dropped += batches[index].count;
                __dmb();
                multicore_fifo_push_blocking(MSG_FREE | index);
            }
        }
        send_delta(MSG_DROPPED, &sent_dropped, writer.dropped);
        send_delta(MSG_UNCERTAIN, &sent_uncertain, writer.uncertain);
        send_delta(MSG_RECOVERED, &sent_recovered, writer.recovered_bytes);
        send_delta(MSG_RECOVERY_ERRORS, &sent_recovery_errors, writer.recovery_errors);
        if (writer.worst_write_us > sent_worst) {
            sent_worst = writer.worst_write_us;
            uint32_t value = sent_worst > MSG_VALUE_MASK ? MSG_VALUE_MASK : (uint32_t)sent_worst;
            multicore_fifo_push_blocking(MSG_WORST | value);
        }
        if (writer.worst_sync_us > sent_sync) {
            sent_sync = writer.worst_sync_us;
            uint32_t value = sent_sync > MSG_VALUE_MASK ? MSG_VALUE_MASK : (uint32_t)sent_sync;
            multicore_fifo_push_blocking(MSG_SYNC | value);
        }
        sleep_ms(1);
    }
}

void logger_start(void)
{
    free_mask = (1u << BATCH_COUNT) - 1u;
    next_report = time_us_64() + UINT64_C(10000000);
    multicore_launch_core1_with_stack(storage_worker, storage_stack, sizeof(storage_stack));
}

void logger_note_drop(uint32_t count) { dropped += count; }

void logger_report_boot(void)
{
    printf("SD logging: BME280 records, rotate=%" PRIu32 " bytes, SPI1 GP10/11/12 CS13\n",
           (uint32_t)PICOLOGGER_LOG_ROTATE_BYTES);
}

void logger_poll(void)
{
    while (multicore_fifo_rvalid()) {
        uint32_t message = multicore_fifo_pop_blocking();
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
            default: break;
        }
    }
    uint64_t now = time_us_64();
    if (active_batch < 0 && free_mask) {
        for (unsigned int i = 0; i < BATCH_COUNT; ++i) {
            if (free_mask & (1u << i)) {
                active_batch = (int)i;
                free_mask &= ~(1u << i);
                batches[i].count = 0;
                break;
            }
        }
    }
    if (active_batch >= 0) {
        log_batch_t *batch = &batches[active_batch];
        log_record_t record;
        while (batch->count < LOG_RECORDS_PER_BATCH && sensor_take_sample(&record)) {
            if (!batch->count) batch_started = now;
            if (log_encode_record(batch->bytes + batch->count * LOG_RECORD_SIZE, LOG_RECORD_SIZE, &record) != LOG_OK) {
                ++dropped;
                continue;
            }
            ++batch->count;
        }
        if (batch->count && (batch->count == LOG_RECORDS_PER_BATCH || now - batch_started >= UINT64_C(1000000)) &&
            multicore_fifo_wready()) {
            __dmb();
            multicore_fifo_push_blocking((uint32_t)active_batch);
            active_batch = -1;
        }
    }
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
