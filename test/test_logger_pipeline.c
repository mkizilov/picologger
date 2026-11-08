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
static atomic_bool stop_worker, media, hold_mount;
static atomic_uint returned_batches, opened_files, closed_files, mount_attempts;
static atomic_uint_fast64_t clock_us;
static unsigned int pending_samples, next_sequence;
static uint8_t files[3][8192];
static size_t file_sizes[3];
static int active_file = -1;
static const char *output_file;

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
static int fs_create(void *ctx, const char *name)
{
    (void)ctx;
    unsigned int number;
    if (sscanf(name, "0:/LOG_%4u.BIN", &number) != 1 || number < 1 || number > 3) return -1;
    if (number <= atomic_load(&opened_files)) return 1;
    active_file = (int)number - 1;
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
    return (log_storage_t){NULL, fs_mount, fs_create, fs_write, fs_sync, fs_close, fs_unmount, fs_present, fs_now};
}

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
    atomic_store(&stop_worker, true);
    for (unsigned int i = 0; i < 16; ++i) { logger_poll(); wait_briefly(); }
    TEST_ASSERT_EQUAL_INT(0, pthread_join(worker_thread, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_key_delete(core_key));
    TEST_ASSERT_EQUAL_UINT(8188, file_sizes[0]);
    log_header_t header;
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_header(&header, files[0], file_sizes[0]));
    TEST_ASSERT_EQUAL_STRING("test-version", header.firmware);
    for (unsigned int i = 0; i < 254; ++i) {
        log_record_t record;
        TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_record(&record, files[0] + LOG_HEADER_SIZE + i * LOG_RECORD_SIZE,
                                                       LOG_RECORD_SIZE));
        TEST_ASSERT_EQUAL_UINT32(i + 10, record.sequence);
    }
    if (output_file) {
        FILE *file = fopen(output_file, "wb");
        TEST_ASSERT_NOT_NULL(file);
        TEST_ASSERT_EQUAL_UINT(file_sizes[0], fwrite(files[0], 1, file_sizes[0], file));
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
