#define _POSIX_C_SOURCE 200809L
#include "unity.h"
#include "logger.h"
#include "log_format.h"
#include "log_writer.h"
#include "sd_storage.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { uint32_t values[8], head, tail; } fifo_t;
static fifo_t to_worker, to_main;
static pthread_mutex_t fifo_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t worker_thread;
static pthread_key_t core_key;
static int worker_marker;
static void (*test_worker_entry)(void);
static atomic_bool stop_worker, media, hold_mount, hold_dump_read;
static atomic_uint returned_batches, opened_files, closed_files, mount_attempts, entered_dump_read;
static atomic_uint_fast64_t clock_us;
static unsigned int pending_samples, next_sequence;
static uint8_t files[3][8192];
static size_t file_sizes[3];
static int active_file = -1;
static const char *output_file;
static unsigned int list_index;
static int dump_index = -1;
static size_t dump_offset, dump_text_size;
static char dump_text[24000];

static bool on_worker(void) { return pthread_getspecific(core_key) != NULL; }

static void wait_briefly(void)
{
    const struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000};
    (void)nanosleep(&delay, NULL);
}

void fake_memory_barrier(void) { atomic_thread_fence(memory_order_seq_cst); }
uint64_t time_us_64(void) { return atomic_load(&clock_us); }
void sleep_ms(uint32_t milliseconds)
{
    (void)milliseconds;
    if (atomic_load(&stop_worker)) pthread_exit(NULL);
    wait_briefly();
}
bool stdio_usb_connected(void) { return false; }

bool multicore_fifo_rvalid(void)
{
    fifo_t *fifo = on_worker() ? &to_worker : &to_main;
    (void)pthread_mutex_lock(&fifo_lock);
    bool valid = fifo->head != fifo->tail;
    (void)pthread_mutex_unlock(&fifo_lock);
    return valid;
}
bool multicore_fifo_wready(void)
{
    fifo_t *fifo = on_worker() ? &to_main : &to_worker;
    (void)pthread_mutex_lock(&fifo_lock);
    bool ready = fifo->head - fifo->tail < 8;
    (void)pthread_mutex_unlock(&fifo_lock);
    return ready;
}
uint32_t multicore_fifo_pop_blocking(void)
{
    fifo_t *fifo = on_worker() ? &to_worker : &to_main;
    (void)pthread_mutex_lock(&fifo_lock);
    if (fifo->head == fifo->tail) abort();
    uint32_t value = fifo->values[fifo->tail++ & 7u];
    (void)pthread_mutex_unlock(&fifo_lock);
    return value;
}
void multicore_fifo_push_blocking(uint32_t value)
{
    fifo_t *fifo = on_worker() ? &to_main : &to_worker;
    for (;;) {
        (void)pthread_mutex_lock(&fifo_lock);
        if (fifo->head - fifo->tail < 8) {
            fifo->values[fifo->head++ & 7u] = value;
            (void)pthread_mutex_unlock(&fifo_lock);
            if (on_worker() && (value & UINT32_C(0xf0000000)) == 0)
                (void)atomic_fetch_add(&returned_batches, 1);
            return;
        }
        (void)pthread_mutex_unlock(&fifo_lock);
        wait_briefly();
    }
}
static void *run_worker(void *arg)
{
    (void)arg;
    if (pthread_setspecific(core_key, &worker_marker) != 0) abort();
    test_worker_entry();
    return NULL;
}
void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack, size_t size)
{
    (void)stack;
    if (size != 8192) abort();
    test_worker_entry = entry;
    if (pthread_key_create(&core_key, NULL) != 0) abort();
    if (pthread_create(&worker_thread, NULL, run_worker, NULL) != 0) abort();
}
bool sensor_take_sample(log_record_t *record)
{
    if (!pending_samples) return false;
    --pending_samples;
    *record = (log_record_t){.sequence = next_sequence, .timestamp_us = next_sequence * UINT64_C(20000),
        .values = {(int32_t)next_sequence, 100000, 50000, 0}};
    ++next_sequence;
    return true;
}

static bool fs_mount(void *ctx)
{
    (void)ctx;
    (void)atomic_fetch_add(&mount_attempts, 1);
    while (atomic_load(&hold_mount)) wait_briefly();
    return atomic_load(&media);
}
static bool fs_recover(void *ctx, bool *found, log_recovery_result_t *result)
{
    (void)ctx; (void)result;
    *found = false;
    return true;
}
static int fs_create(void *ctx, const char *name, const uint8_t *header, size_t size,
                     uint64_t *write_us, uint64_t *sync_us)
{
    (void)ctx;
    unsigned int number;
    if (sscanf(name, "0:/LOG_%4u.BIN", &number) != 1 || number < 1 || number > 3) return -1;
    if (number <= atomic_load(&opened_files)) return 1;
    active_file = (int)number - 1;
    if (size != LOG_HEADER_V2_SIZE) abort();
    memcpy(files[active_file], header, size);
    file_sizes[active_file] = size;
    *write_us = *sync_us = 0;
    (void)atomic_fetch_add(&opened_files, 1);
    return 0;
}
static bool fs_write(void *ctx, const uint8_t *data, size_t length, size_t *written)
{
    (void)ctx;
    if ((uintptr_t)data % 512u != 0) abort();
    if (!atomic_load(&media) || active_file < 0 || file_sizes[active_file] + length > sizeof(files[0])) {
        *written = 0;
        return false;
    }
    memcpy(files[active_file] + file_sizes[active_file], data, length);
    file_sizes[active_file] += length;
    *written = length;
    return true;
}
static bool fs_sync(void *ctx) { (void)ctx; return atomic_load(&media); }
static bool fs_close(void *ctx) { (void)ctx; (void)atomic_fetch_add(&closed_files, 1); return true; }
static void fs_unmount(void *ctx) { (void)ctx; }
static bool fs_present(void *ctx) { (void)ctx; return atomic_load(&media); }
static uint64_t fs_now(void *ctx) { (void)ctx; return time_us_64(); }
log_storage_t sd_storage_interface(void)
{
    return (log_storage_t){NULL, fs_mount, fs_recover, fs_create, fs_write, fs_sync, fs_close, fs_unmount, fs_present, fs_now};
}
bool sd_storage_list_open(void) { list_index = 0; return atomic_load(&media); }
int sd_storage_list_next(sd_list_entry_t *entry)
{
    if (list_index >= atomic_load(&opened_files)) return 0;
    (void)snprintf(entry->name, sizeof(entry->name), "LOG_%04u.BIN", list_index + 1);
    entry->size = file_sizes[list_index++];
    entry->directory = false;
    return 1;
}
bool sd_storage_list_close(void) { return true; }
bool sd_storage_dump_open(const char *name)
{
    unsigned int number;
    if (sscanf(name, "LOG_%4u.BIN", &number) != 1 || number < 1 || number > atomic_load(&opened_files)) return false;
    dump_index = (int)number - 1;
    dump_offset = 0;
    return true;
}
bool sd_storage_dump_read(uint8_t *data, size_t capacity, size_t *read)
{
    if (dump_index < 0) return false;
    (void)atomic_fetch_add(&entered_dump_read, 1);
    while (atomic_load(&hold_dump_read)) wait_briefly();
    size_t remaining = file_sizes[dump_index] - dump_offset;
    *read = remaining < capacity ? remaining : capacity;
    memcpy(data, files[dump_index] + dump_offset, *read);
    dump_offset += *read;
    return true;
}
bool sd_storage_dump_close(void) { dump_index = -1; return true; }

void setUp(void) {}
void tearDown(void) {}

static bool pump_until(atomic_uint *value, unsigned int target)
{
    for (unsigned int i = 0; i < 2000; ++i) {
        logger_poll();
        if (atomic_load(value) >= target) return true;
        wait_briefly();
    }
    return false;
}

static bool collect_io(bool keep_dump)
{
    for (unsigned int i = 0; i < 5000; ++i) {
        logger_poll();
        char line[64];
        bool final;
        if (logger_take_reply(line, sizeof(line), &final)) {
            if (keep_dump) {
                size_t length = strlen(line);
                if (dump_text_size + length >= sizeof(dump_text)) abort();
                memcpy(dump_text + dump_text_size, line, length);
                dump_text_size += length;
            }
            if (final) return true;
        }
        wait_briefly();
    }
    return false;
}

static bool collect_dump_start(void)
{
    for (unsigned int i = 0; i < 2000; ++i) {
        logger_poll();
        char line[64];
        bool final;
        if (logger_take_reply(line, sizeof(line), &final)) {
            if (final || strcmp(line, "BEGIN DUMP LOG_0001.BIN\n")) return false;
            dump_text_size = strlen(line);
            memcpy(dump_text, line, dump_text_size);
            return true;
        }
        wait_briefly();
    }
    return false;
}

static bool next_reply(char *line, size_t size, bool *final)
{
    for (unsigned int i = 0; i < 2000; ++i) {
        logger_poll();
        if (logger_take_reply(line, size, final)) return true;
        wait_briefly();
    }
    return false;
}

static bool pump_state(logger_state_t desired)
{
    for (unsigned int i = 0; i < 2000; ++i) {
        logger_poll();
        logger_status_t status;
        logger_get_status(&status);
        if (status.state == desired) return true;
        wait_briefly();
    }
    return false;
}

static void records_cross_cores_survive_retry_and_rotate(void)
{
    atomic_store(&hold_mount, true);
    logger_start();
    TEST_ASSERT_TRUE(pump_until(&mount_attempts, 1));
    pending_samples = 10;
    logger_poll();
    atomic_store(&clock_us, UINT64_C(1000001));
    logger_poll();
    TEST_ASSERT_EQUAL_UINT(0, atomic_load(&returned_batches));
    atomic_store(&hold_mount, false);
    TEST_ASSERT_TRUE(pump_until(&returned_batches, 1));
    atomic_store(&media, true);
    atomic_store(&clock_us, UINT64_C(5000000));
    TEST_ASSERT_TRUE(pump_until(&opened_files, 1));
    pending_samples = 256;
    TEST_ASSERT_TRUE(pump_until(&closed_files, 1));
    while (pending_samples) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_TRUE(logger_request_list());
    TEST_ASSERT_TRUE(collect_io(false));
    for (unsigned int i = 0; i < 10; ++i) { logger_poll(); wait_briefly(); }
    atomic_store(&hold_dump_read, true);
    TEST_ASSERT_TRUE(logger_request_dump("LOG_0001.BIN"));
    TEST_ASSERT_TRUE(collect_dump_start());
    TEST_ASSERT_TRUE(pump_until(&entered_dump_read, 1));
    pending_samples = 5;
    for (unsigned int i = 0; i < 100 && pending_samples; ++i) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_EQUAL_UINT(0, pending_samples);
    atomic_store(&hold_dump_read, false);
    TEST_ASSERT_TRUE(collect_io(true));
    TEST_ASSERT_TRUE(logger_request_stop());
    TEST_ASSERT_TRUE(pump_state(LOGGER_STOPPED));
    TEST_ASSERT_TRUE(logger_request_start(25));
    TEST_ASSERT_TRUE(pump_state(LOGGER_RUNNING));
    logger_status_t status;
    for (unsigned int i = 0; i < 1000; ++i) {
        logger_poll();
        logger_get_status(&status);
        if (status.current_file == 3) break;
        wait_briefly();
    }
    TEST_ASSERT_EQUAL_UINT(3, status.current_file);
    for (unsigned int i = 0; i < 10; ++i) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_TRUE(logger_request_dump("log_0003.bin"));
    char response[64];
    bool final;
    TEST_ASSERT_TRUE(next_reply(response, sizeof(response), &final));
    TEST_ASSERT_TRUE(final);
    TEST_ASSERT_EQUAL_STRING("ERR active file; stop first\n", response);
    for (unsigned int i = 0; i < 10; ++i) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_TRUE(logger_request_list());
    TEST_ASSERT_TRUE(next_reply(response, sizeof(response), &final));
    TEST_ASSERT_FALSE(final);
    TEST_ASSERT_EQUAL_STRING("BEGIN LS\n", response);
    logger_cancel_io();
    for (unsigned int i = 0; i < 2000 && logger_io_busy(); ++i) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_FALSE(logger_io_busy());
    TEST_ASSERT_TRUE(logger_request_list());
    TEST_ASSERT_TRUE(collect_io(false));
    atomic_store(&stop_worker, true);
    for (unsigned int i = 0; i < 16; ++i) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_EQUAL_INT(0, pthread_join(worker_thread, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_key_delete(core_key));
    TEST_ASSERT_EQUAL_UINT(8192, file_sizes[0]);
    log_header_t header;
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_header(&header, files[0], file_sizes[0]));
    TEST_ASSERT_EQUAL_STRING("test-version", header.firmware);
    TEST_ASSERT_EQUAL_UINT(3, atomic_load(&opened_files));
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_header(&header, files[2], file_sizes[2]));
    TEST_ASSERT_EQUAL_UINT32(25, header.sample_rate_hz);
    for (unsigned int i = 0; i < 240; ++i) {
        log_record_t record;
        TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_record(&record, files[0] + LOG_HEADER_V2_SIZE + i * LOG_RECORD_SIZE,
                                                       LOG_RECORD_SIZE));
        TEST_ASSERT_EQUAL_UINT32(i + 10, record.sequence);
    }
    if (output_file) {
        FILE *file = fopen(output_file, "wb");
        TEST_ASSERT_NOT_NULL(file);
        TEST_ASSERT_EQUAL_UINT(file_sizes[0], fwrite(files[0], 1, file_sizes[0], file));
        TEST_ASSERT_EQUAL_INT(0, fclose(file));
        char path[1024];
        int length = snprintf(path, sizeof(path), "%s.dump", output_file);
        TEST_ASSERT_TRUE(length > 0 && (size_t)length < sizeof(path));
        file = fopen(path, "wb");
        TEST_ASSERT_NOT_NULL(file);
        TEST_ASSERT_EQUAL_UINT(dump_text_size, fwrite(dump_text, 1, dump_text_size, file));
        TEST_ASSERT_EQUAL_INT(0, fclose(file));
    }
}

int main(int argc, char **argv)
{
    if (argc > 2) return 1;
    if (argc == 2) output_file = argv[1];
    UNITY_BEGIN();
    RUN_TEST(records_cross_cores_survive_retry_and_rotate);
    return UNITY_END();
}
