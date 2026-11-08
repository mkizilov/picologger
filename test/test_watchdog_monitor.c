#include "unity.h"
#include "adc_app.h"
#include "logger.h"
#include "sensor.h"
#include "watchdog_health.h"
#include "watchdog_monitor.h"

static uint64_t now, adc_samples, deadline;
static bool adc_enabled = true;
static sensor_status_t sensor = {.expected = true, .rate_hz = 50};
static logger_status_t storage = {.state = LOGGER_RUNNING, .online = true};
static unsigned int feeds;
static unsigned int preserves;
static bool irq_locked;
static uint32_t last_noted;

uint64_t time_us_64(void) { return now; }
void sensor_get_status(sensor_status_t *out) { *out = sensor; }
void logger_get_status(logger_status_t *out) { *out = storage; }
uint64_t adc_app_total_samples(void) { return adc_samples; }
bool adc_app_watchdog_expected(void) { return adc_enabled; }
uint32_t save_and_disable_interrupts(void) { irq_locked = true; return 123; }
void restore_interrupts(uint32_t state) { TEST_ASSERT_EQUAL_UINT(123, state); irq_locked = false; }
void watchdog_enable(uint32_t ms, bool pause) { TEST_ASSERT_TRUE(irq_locked); TEST_ASSERT_EQUAL_UINT(2000, ms); TEST_ASSERT_TRUE(pause); deadline = now + 2000000; }
void watchdog_update(void) { ++feeds; deadline = now + 2000000; }
void fault_note_watchdog(uint32_t value) { last_noted = value; }
void fault_preserve_sram_on_watchdog(void) { TEST_ASSERT_TRUE(irq_locked); ++preserves; }
void setUp(void)
{
    now = adc_samples = deadline = feeds = preserves = 0;
    adc_enabled = true;
    sensor = (sensor_status_t){.expected = true, .rate_hz = 50};
    storage = (logger_status_t){.state = LOGGER_RUNNING, .online = true};
    irq_locked = false;
}
void tearDown(void) {}

static void feeds_only_after_required_progress_and_stops_when_stalled(void)
{
    watchdog_monitor_start();
    TEST_ASSERT_EQUAL_UINT(1, preserves);
    TEST_ASSERT_FALSE(irq_locked);
    watchdog_monitor_poll();
    TEST_ASSERT_EQUAL_UINT(0, feeds);
    ++sensor.samples; adc_samples += 256; ++storage.worker_progress; ++storage.writer_progress;
    ++storage.ring_progress;
    ++storage.physical_progress; storage.physical_observed = true;
    now = 100000;
    watchdog_monitor_poll();
    TEST_ASSERT_EQUAL_UINT(1, feeds);
    ++now;
    watchdog_monitor_poll();
    TEST_ASSERT_EQUAL_UINT(1, feeds);
    ++sensor.samples; adc_samples += 256; ++storage.worker_progress;
    ++storage.ring_progress;
    now = 1700001;
    watchdog_monitor_poll();
    TEST_ASSERT_EQUAL_UINT(1, feeds);
    TEST_ASSERT_EQUAL_UINT32(WATCHDOG_MISSING_WRITER, watchdog_monitor_missing());
    TEST_ASSERT_EQUAL_UINT32(WATCHDOG_MISSING_WRITER, last_noted);
    storage.online = false;
    ++sensor.samples; adc_samples += 256; ++storage.worker_progress;
    ++storage.ring_progress;
    ++now;
    watchdog_monitor_poll();
    TEST_ASSERT_EQUAL_UINT(2, feeds);
    sensor.expected = false;
    adc_enabled = false;
    storage.state = LOGGER_STOPPED;
    ++storage.worker_progress;
    now += 100000;
    watchdog_monitor_poll();
    TEST_ASSERT_EQUAL_UINT(3, feeds);
    now += WATCHDOG_PROGRESS_WINDOW_US + 1;
    watchdog_monitor_poll();
    TEST_ASSERT_EQUAL_UINT(3, feeds);
    TEST_ASSERT_EQUAL_UINT32(WATCHDOG_MISSING_WORKER, watchdog_monitor_missing());
}

static void a_stall_on_either_core_stops_feeding(void)
{
    watchdog_monitor_start();
    watchdog_monitor_poll();
    ++sensor.samples; adc_samples += 256; ++storage.worker_progress; ++storage.writer_progress;
    ++storage.ring_progress;
    ++storage.physical_progress; storage.physical_observed = true;
    now = 100000;
    watchdog_monitor_poll();
    TEST_ASSERT_EQUAL_UINT(1, feeds);
    uint64_t core0_deadline = deadline;
    now = core0_deadline - 1;
    TEST_ASSERT_FALSE(now >= deadline);
    now = core0_deadline;
    TEST_ASSERT_TRUE(now >= deadline); /* No main-loop poll can feed a stalled core 0. */

    setUp();
    watchdog_monitor_start();
    watchdog_monitor_poll();
    ++sensor.samples; adc_samples += 256; ++storage.worker_progress; ++storage.writer_progress;
    ++storage.ring_progress;
    ++storage.physical_progress; storage.physical_observed = true;
    now = 100000;
    watchdog_monitor_poll();
    bool expired = false;
    for (unsigned int i = 0; i < 60; ++i) {
        now += 100000;
        ++sensor.samples; adc_samples += 256; ++storage.writer_progress; ++storage.physical_progress;
        ++storage.ring_progress;
        if (now >= deadline) { expired = true; break; }
        watchdog_monitor_poll();
    }
    TEST_ASSERT_TRUE(expired);
    TEST_ASSERT_TRUE((watchdog_monitor_missing() & WATCHDOG_MISSING_WORKER) != 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(feeds_only_after_required_progress_and_stops_when_stalled);
    RUN_TEST(a_stall_on_either_core_stops_feeding);
    return UNITY_END();
}
