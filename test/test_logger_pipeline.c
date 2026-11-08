#define _POSIX_C_SOURCE 200809L
#include "unity.h"
#include "logger.h"
#include "log_format.h"
#include "log_writer.h"
#include "sd_storage.h"
#include "sensor.h"
#include "settings_service.h"
#include "power_service.h"

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
static atomic_bool stop_worker, media, hold_mount, hold_dump_read, hold_write;
static atomic_uint opened_files, closed_files, mount_attempts, entered_dump_read, entered_write, presence_calls;
static atomic_uint sleeps_one_ms, sleeps_ten_ms;
static atomic_uint_fast64_t clock_us;
static unsigned int next_sequence;
static uint8_t files[3][8192];
static size_t file_sizes[3];
static int active_file = -1;
static const char *output_file;
static unsigned int list_index;
static int dump_index = -1;
static size_t dump_offset, dump_text_size;
static char dump_text[24000];
static uint8_t flash_sectors[2][4096];
static bool test_adc_running, test_flash_armed;
static unsigned int test_adc_pauses, test_adc_resumes, flash_presence_snapshot;
static bool test_power_low;
static unsigned int test_clock_changes;

static bool on_worker(void) { return pthread_getspecific(core_key) != NULL; }

static void wait_briefly(void)
{
    const struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000};
    (void)nanosleep(&delay, NULL);
}

void fake_memory_barrier(void) { atomic_thread_fence(memory_order_seq_cst); }
uint32_t save_and_disable_interrupts(void) { return 0; }
void restore_interrupts(uint32_t state) { (void)state; }
void fake_wfi(void) { abort(); }
bool sampling_timer_pending(void) { return false; }
bool adc_capture_pending(void) { return false; }
bool usb_console_has_work(void) { return false; }
bool i2c_bus_reclock(void)
{
    if (on_worker() || logger_flash_state() != LOGGER_FLASH_READY) abort();
    return true;
}
void power_clock_init(uint32_t rate_hz) { test_power_low = rate_hz <= 10; }
bool power_clock_supported(void) { return true; }
bool power_clock_faulted(void) { return false; }
bool power_clock_low(void) { return test_power_low; }
uint32_t power_clock_sys_hz(void) { return test_power_low ? 48000000u : 125000000u; }
bool power_clock_select(bool low)
{
    if (on_worker() || logger_flash_state() != LOGGER_FLASH_READY) abort();
    unsigned int before = atomic_load(&presence_calls);
    atomic_fetch_add(&clock_us, UINT64_C(10000000));
    for (unsigned int i = 0; i < 5; ++i) wait_briefly();
    if (atomic_load(&presence_calls) != before) abort();
    ++test_clock_changes;
    test_power_low = low;
    return true;
}
uint64_t time_us_64(void) { return atomic_load(&clock_us); }
void sleep_ms(uint32_t milliseconds)
{
    if (milliseconds == 1) (void)atomic_fetch_add(&sleeps_one_ms, 1);
    if (milliseconds == 10) (void)atomic_fetch_add(&sleeps_ten_ms, 1);
    if (atomic_load(&stop_worker)) pthread_exit(NULL);
    wait_briefly();
}
bool stdio_usb_connected(void) { return false; }

static bool fake_queue_rvalid(void)
{
    fifo_t *fifo = on_worker() ? &to_worker : &to_main;
    (void)pthread_mutex_lock(&fifo_lock);
    bool valid = fifo->head != fifo->tail;
    (void)pthread_mutex_unlock(&fifo_lock);
    return valid;
}
static bool fake_queue_wready(void)
{
    fifo_t *fifo = on_worker() ? &to_main : &to_worker;
    (void)pthread_mutex_lock(&fifo_lock);
    bool ready = fifo->head - fifo->tail < 8;
    (void)pthread_mutex_unlock(&fifo_lock);
    return ready;
}
static uint32_t fake_queue_pop(void)
{
    fifo_t *fifo = on_worker() ? &to_worker : &to_main;
    (void)pthread_mutex_lock(&fifo_lock);
    if (fifo->head == fifo->tail) abort();
    uint32_t value = fifo->values[fifo->tail++ & 7u];
    (void)pthread_mutex_unlock(&fifo_lock);
    return value;
}
static void fake_queue_push(uint32_t value)
{
    fifo_t *fifo = on_worker() ? &to_main : &to_worker;
    for (;;) {
        (void)pthread_mutex_lock(&fifo_lock);
        if (fifo->head - fifo->tail < 8) {
            fifo->values[fifo->head++ & 7u] = value;
            (void)pthread_mutex_unlock(&fifo_lock);
            return;
        }
        (void)pthread_mutex_unlock(&fifo_lock);
        wait_briefly();
    }
}
bool logger_channel_init(void) { return true; }
bool logger_channel_to_worker(uint32_t value)
{
    if ((value & UINT32_C(0xf0000000)) == 0) abort();
    if (!fake_queue_wready()) return false;
    fake_queue_push(value);
    return true;
}
bool logger_channel_from_worker(uint32_t *value)
{
    if (!fake_queue_rvalid()) return false;
    *value = fake_queue_pop();
    return true;
}
void logger_channel_to_main(uint32_t value)
{
    if ((value & UINT32_C(0xf0000000)) == 0) abort();
    fake_queue_push(value);
}
bool logger_channel_from_main(uint32_t *value)
{
    if (!fake_queue_rvalid()) return false;
    *value = fake_queue_pop();
    return true;
}
bool flash_safe_execute_core_init(void) { return true; }
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
static void publish_samples(unsigned int count)
{
    while (count--) {
        log_record_t record = {.sequence = next_sequence, .timestamp_us = next_sequence * UINT64_C(20000),
            .values = {(int32_t)next_sequence, 100000, 50000, 0}};
        ++next_sequence;
        TEST_ASSERT_TRUE(logger_submit_sample(&record));
    }
}
void sensor_get_status(sensor_status_t *status) { *status = (sensor_status_t){0}; }
bool adc_app_pause_for_flash(bool *was_running)
{
    *was_running = test_adc_running;
    if (test_adc_running) ++test_adc_pauses;
    test_adc_running = false;
    return true;
}
bool adc_app_resume_after_flash(bool was_running)
{
    if (was_running) { ++test_adc_resumes; test_adc_running = true; }
    return true;
}
void adc_app_poll(void) { if (test_adc_running) abort(); }
void settings_flash_set_guard(bool armed)
{
    if (armed) {
        if (test_adc_running || logger_flash_state() != LOGGER_FLASH_READY) abort();
        flash_presence_snapshot = atomic_load(&presence_calls);
    }
    test_flash_armed = armed;
}
static bool flash_read(void *context, unsigned int slot, uint8_t *data, size_t length)
{
    (void)context;
    memcpy(data, flash_sectors[slot], length);
    return true;
}
static bool flash_erase(void *context, unsigned int slot)
{
    (void)context;
    if (!test_flash_armed || test_adc_running || atomic_load(&presence_calls) != flash_presence_snapshot) abort();
    memset(flash_sectors[slot], 0xff, sizeof(flash_sectors[slot]));
    return true;
}
static bool flash_program(void *context, unsigned int slot, const uint8_t *data, size_t length)
{
    (void)context;
    if (!test_flash_armed || test_adc_running || atomic_load(&presence_calls) != flash_presence_snapshot) abort();
    memcpy(flash_sectors[slot], data, length);
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
    (void)atomic_fetch_add(&entered_write, 1);
    while (atomic_load(&hold_write)) wait_briefly();
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
static bool fs_present(void *ctx) { (void)ctx; (void)atomic_fetch_add(&presence_calls, 1); return atomic_load(&media); }
static uint64_t fs_now(void *ctx) { (void)ctx; return time_us_64(); }
log_storage_t sd_storage_interface(void)
{
    return (log_storage_t){NULL, fs_mount, fs_recover, fs_create, fs_write, fs_sync, fs_close, fs_unmount, fs_present, fs_now};
}
void sd_storage_set_progress_hook(void (*hook)(void)) { (void)hook; }
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

static bool pump_dropped(uint64_t target)
{
    for (unsigned int i = 0; i < 2000; ++i) {
        logger_poll();
        logger_status_t status;
        logger_get_status(&status);
        if (status.dropped >= target) return true;
        wait_briefly();
    }
    return false;
}

static bool pump_flash_state(logger_flash_state_t desired)
{
    for (unsigned int i = 0; i < 2000; ++i) {
        logger_poll();
        if (logger_flash_state() == desired) return true;
        wait_briefly();
    }
    return false;
}

static void records_cross_cores_survive_retry_and_rotate(void)
{
    boot_report_t report = {.boot_counter = 9, .reset_reason = BOOT_RESET_HARDFAULT,
                            .watchdog_reason = 2, .fault_mask = 2, .watchdog_missing = 5};
    report.fault[1] = (fault_detail_t){FAULT_STACK_VALID, 0x10001234, 0x10005678, 0x21000000,
                                       0x20041000, 0xfffffff9, 3, 0x8000};
    report.fault_boot_id[1] = 8;
    logger_configure_boot_report(&report);
    atomic_store(&hold_mount, true);
    logger_start();
    TEST_ASSERT_TRUE(pump_until(&mount_attempts, 1));
    publish_samples(10);
    TEST_ASSERT_EQUAL_UINT(10, logger_ring_level());
    atomic_store(&clock_us, UINT64_C(1000001));
    logger_poll();
    TEST_ASSERT_EQUAL_UINT(10, logger_ring_level());
    atomic_store(&hold_mount, false);
    TEST_ASSERT_TRUE(pump_dropped(10));
    TEST_ASSERT_FALSE(logger_take_fault_logged());
    atomic_store(&media, true);
    atomic_store(&clock_us, UINT64_C(5000000));
    TEST_ASSERT_TRUE(pump_until(&opened_files, 1));
    for (unsigned int i = 0; i < 20; ++i) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_TRUE(logger_take_fault_logged());
    TEST_ASSERT_FALSE(logger_take_fault_logged());
    atomic_store(&hold_write, true);
    publish_samples(128);
    TEST_ASSERT_TRUE(pump_until(&entered_write, 1));
    publish_samples(128);
    TEST_ASSERT_EQUAL_UINT(128, logger_ring_level());
    TEST_ASSERT_TRUE(logger_ring_high_water() >= 128);
    atomic_store(&hold_write, false);
    TEST_ASSERT_TRUE(pump_until(&closed_files, 1));
    for (unsigned int i = 0; i < 2000 && logger_ring_level(); ++i) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_EQUAL_UINT(0, logger_ring_level());
    for (unsigned int i = 0; i < 20; ++i) { logger_poll(); wait_briefly(); }
    logger_status_t observed_progress;
    logger_get_status(&observed_progress);
    TEST_ASSERT_TRUE(observed_progress.worker_progress > 0);
    TEST_ASSERT_TRUE(observed_progress.writer_progress > 0);
    TEST_ASSERT_TRUE(observed_progress.ring_progress > 0);
    TEST_ASSERT_TRUE(observed_progress.physical_observed);
    TEST_ASSERT_TRUE(observed_progress.physical_progress > 0);
    TEST_ASSERT_TRUE(logger_request_list());
    TEST_ASSERT_TRUE(collect_io(false));
    for (unsigned int i = 0; i < 10; ++i) { logger_poll(); wait_briefly(); }
    atomic_store(&hold_dump_read, true);
    TEST_ASSERT_TRUE(logger_request_dump("LOG_0001.BIN"));
    TEST_ASSERT_TRUE(collect_dump_start());
    TEST_ASSERT_TRUE(pump_until(&entered_dump_read, 1));
    publish_samples(5);
    TEST_ASSERT_EQUAL_UINT(5, logger_ring_level());
    atomic_store(&hold_dump_read, false);
    TEST_ASSERT_TRUE(collect_io(true));
    TEST_ASSERT_TRUE(logger_request_stop());
    TEST_ASSERT_TRUE(pump_state(LOGGER_STOPPED));
    for (unsigned int i = 0; i < 10; ++i) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_TRUE(logger_flash_prepare());
    TEST_ASSERT_TRUE(pump_flash_state(LOGGER_FLASH_READY));
    TEST_ASSERT_FALSE(logger_request_start(25));
    TEST_ASSERT_FALSE(logger_request_list());
    unsigned int before = atomic_load(&presence_calls);
    atomic_fetch_add(&clock_us, UINT64_C(10000000));
    for (unsigned int i = 0; i < 20; ++i) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_EQUAL_UINT(before, atomic_load(&presence_calls));
    TEST_ASSERT_TRUE(logger_flash_release());
    TEST_ASSERT_TRUE(pump_flash_state(LOGGER_FLASH_IDLE));
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
    TEST_ASSERT_EQUAL_UINT32(9, header.boot_counter);
    TEST_ASSERT_EQUAL_UINT32(BOOT_RESET_HARDFAULT, header.reset_reason);
    TEST_ASSERT_EQUAL_UINT32(5, header.watchdog_missing);
    TEST_ASSERT_EQUAL_UINT32(8, header.fault_boot_id[1]);
    TEST_ASSERT_EQUAL_UINT32(0x10001234, header.fault[1].pc);
    TEST_ASSERT_EQUAL_UINT(3, atomic_load(&opened_files));
    TEST_ASSERT_EQUAL_UINT(LOG_HEADER_V2_SIZE + 21 * LOG_RECORD_SIZE, file_sizes[1]);
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

static void boot_stopped_opens_no_file_until_start(void)
{
    atomic_store(&media, true);
    logger_configure_boot(25, 8192, false);
    logger_start();
    TEST_ASSERT_TRUE(pump_until(&mount_attempts, 1));
    for (unsigned int i = 0; i < 20; ++i) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_EQUAL_UINT(0, atomic_load(&opened_files));
    logger_status_t status;
    logger_get_status(&status);
    TEST_ASSERT_EQUAL_INT(LOGGER_STOPPED, status.state);
    TEST_ASSERT_TRUE(logger_request_start(25));
    TEST_ASSERT_TRUE(pump_state(LOGGER_RUNNING));
    atomic_store(&stop_worker, true);
    for (unsigned int i = 0; i < 16; ++i) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_EQUAL_INT(0, pthread_join(worker_thread, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_key_delete(core_key));
    TEST_ASSERT_EQUAL_UINT(1, atomic_load(&opened_files));
    log_header_t header;
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_header(&header, files[0], file_sizes[0]));
    TEST_ASSERT_EQUAL_UINT32(25, header.sample_rate_hz);
}

static void config_save_coordinates_real_logger_guard_and_adc(void)
{
    memset(flash_sectors, 0xff, sizeof(flash_sectors));
    test_adc_running = true;
    atomic_store(&media, true);
    settings_t defaults = {50, 3, 16777216, true}, changed = {25, 1, 8192, false};
    settings_service_init((settings_flash_t){NULL, flash_read, flash_erase, flash_program}, defaults);
    logger_configure_boot(50, 8192, false);
    logger_start();
    TEST_ASSERT_TRUE(pump_until(&mount_attempts, 1));
    settings_request_t result = SETTINGS_REQUEST_BUSY;
    for (unsigned int i = 0; i < 2000 && result == SETTINGS_REQUEST_BUSY; ++i) {
        logger_poll();
        result = settings_service_request(&changed);
        if (result == SETTINGS_REQUEST_BUSY) wait_briefly();
    }
    TEST_ASSERT_EQUAL_INT(SETTINGS_REQUEST_ACCEPTED, result);
    bool saved = false, resumed = false, completed = false;
    for (unsigned int i = 0; i < 2000; ++i) {
        logger_poll();
        settings_service_poll();
        if (settings_service_take_result(&saved, &resumed)) { completed = true; break; }
        wait_briefly();
    }
    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_TRUE(saved);
    TEST_ASSERT_TRUE(resumed);
    TEST_ASSERT_TRUE(test_adc_running);
    TEST_ASSERT_EQUAL_UINT(1, test_adc_pauses);
    TEST_ASSERT_EQUAL_UINT(1, test_adc_resumes);
    settings_snapshot_t snapshot;
    settings_t active;
    bool readable;
    settings_service_get(&snapshot, &active, &readable);
    TEST_ASSERT_TRUE(readable);
    TEST_ASSERT_EQUAL_UINT32(25, snapshot.values.rate_hz);
    TEST_ASSERT_EQUAL_UINT32(50, active.rate_hz);
    atomic_store(&stop_worker, true);
    for (unsigned int i = 0; i < 16; ++i) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_EQUAL_INT(0, pthread_join(worker_thread, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_key_delete(core_key));
}

static void stop_waits_for_all_previously_published_records(void)
{
    atomic_store(&media, true);
    logger_start();
    TEST_ASSERT_TRUE(pump_until(&opened_files, 1));
    atomic_store(&hold_write, true);
    publish_samples(128);
    TEST_ASSERT_TRUE(pump_until(&entered_write, 1));
    publish_samples(128);
    TEST_ASSERT_EQUAL_UINT(128, logger_ring_level());
    TEST_ASSERT_TRUE(logger_request_stop());
    log_record_t rejected = {.sequence = 999};
    TEST_ASSERT_FALSE(logger_submit_sample(&rejected));
    for (unsigned int i = 0; i < 20; ++i) { logger_poll(); wait_briefly(); }
    logger_status_t status;
    logger_get_status(&status);
    TEST_ASSERT_EQUAL_INT(LOGGER_STOPPING, status.state);
    TEST_ASSERT_EQUAL_UINT(128, logger_ring_level());
    atomic_store(&hold_write, false);
    TEST_ASSERT_TRUE(pump_state(LOGGER_STOPPED));
    atomic_store(&stop_worker, true);
    for (unsigned int i = 0; i < 16; ++i) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_EQUAL_INT(0, pthread_join(worker_thread, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_key_delete(core_key));
    TEST_ASSERT_EQUAL_UINT(8192, file_sizes[0]);
    TEST_ASSERT_EQUAL_UINT(LOG_HEADER_V2_SIZE + 16 * LOG_RECORD_SIZE, file_sizes[1]);
    for (unsigned int i = 0; i < 256; ++i) {
        unsigned int file = i < 240 ? 0 : 1;
        unsigned int index = i < 240 ? i : i - 240;
        log_record_t record;
        TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_record(&record,
            files[file] + LOG_HEADER_V2_SIZE + index * LOG_RECORD_SIZE, LOG_RECORD_SIZE));
        TEST_ASSERT_EQUAL_UINT32(i, record.sequence);
    }
}

static void slow_worker_uses_longer_idle_but_restores_fast_rate(void)
{
    atomic_store(&media, true);
    logger_configure_boot(10, 8192, false);
    logger_start();
    TEST_ASSERT_TRUE(pump_until(&sleeps_ten_ms, 1));
    TEST_ASSERT_TRUE(logger_request_start(11));
    TEST_ASSERT_TRUE(pump_state(LOGGER_RUNNING));
    TEST_ASSERT_TRUE(pump_until(&sleeps_one_ms, 1));
    atomic_store(&stop_worker, true);
    for (unsigned int i = 0; i < 16; ++i) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_EQUAL_INT(0, pthread_join(worker_thread, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_key_delete(core_key));
}

static void clock_transition_waits_for_real_storage_quiescence(void)
{
    atomic_store(&media, true);
    logger_configure_boot(50, 8192, false);
    power_service_init(50);
    logger_start();
    TEST_ASSERT_TRUE(pump_until(&mount_attempts, 1));
    power_rate_result_t request = POWER_RATE_BUSY;
    for (unsigned int i = 0; i < 2000 && request == POWER_RATE_BUSY; ++i) {
        logger_poll();
        request = power_service_prepare_rate(10);
        if (request == POWER_RATE_BUSY) wait_briefly();
    }
    TEST_ASSERT_EQUAL_INT(POWER_RATE_PENDING, request);
    bool ok = false, completed = false;
    for (unsigned int i = 0; i < 2000; ++i) {
        logger_poll();
        power_service_poll();
        if (power_service_take_result(&ok)) { completed = true; break; }
        wait_briefly();
    }
    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT(1, test_clock_changes);
    TEST_ASSERT_TRUE(test_power_low);
    TEST_ASSERT_EQUAL_INT(LOGGER_FLASH_IDLE, logger_flash_state());
    TEST_ASSERT_TRUE(logger_request_start(10));
    TEST_ASSERT_TRUE(pump_state(LOGGER_RUNNING));
    TEST_ASSERT_TRUE(pump_until(&sleeps_ten_ms, 1));
    atomic_store(&stop_worker, true);
    for (unsigned int i = 0; i < 16; ++i) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_EQUAL_INT(0, pthread_join(worker_thread, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_key_delete(core_key));
    log_header_t header;
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_header(&header, files[0], file_sizes[0]));
    TEST_ASSERT_EQUAL_UINT32(10, header.sample_rate_hz);
}

int main(int argc, char **argv)
{
    if (argc > 2) return 1;
    UNITY_BEGIN();
    if (argc == 2 && !strcmp(argv[1], "boot_stopped")) RUN_TEST(boot_stopped_opens_no_file_until_start);
    else if (argc == 2 && !strcmp(argv[1], "config_guard")) RUN_TEST(config_save_coordinates_real_logger_guard_and_adc);
    else if (argc == 2 && !strcmp(argv[1], "stop_backlog")) RUN_TEST(stop_waits_for_all_previously_published_records);
    else if (argc == 2 && !strcmp(argv[1], "low_sleep")) RUN_TEST(slow_worker_uses_longer_idle_but_restores_fast_rate);
    else if (argc == 2 && !strcmp(argv[1], "clock_guard")) RUN_TEST(clock_transition_waits_for_real_storage_quiescence);
    else {
        if (argc == 2) output_file = argv[1];
        RUN_TEST(records_cross_cores_survive_retry_and_rotate);
    }
    return UNITY_END();
}
