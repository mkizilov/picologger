#include "unity.h"
#include "power_service.h"
#include "power_clock.h"
#include "logger.h"
#include "sensor.h"

#include <string.h>

static uint64_t now;
static bool supported, low, select_ok, settings_busy, sensor_running;
static bool timer_pending, adc_pending, usb_work, interrupts_disabled;
static bool i2c_ok;
static bool clock_faulted;
static unsigned int reclocks;
static bool irq_during_wfi;
static logger_state_t log_state;
static logger_flash_state_t guard;
static unsigned int select_calls, wait_calls, guard_requests, guard_releases;
static uint32_t sys_hz, prior_irq;

uint64_t time_us_64(void) { return now; }
uint32_t save_and_disable_interrupts(void) { interrupts_disabled = true; return prior_irq; }
void restore_interrupts(uint32_t value) { TEST_ASSERT_EQUAL_UINT(prior_irq, value); interrupts_disabled = prior_irq != 0; }
void fake_wfi(void)
{
    TEST_ASSERT_TRUE(interrupts_disabled);
    ++wait_calls;
    if (irq_during_wfi) { timer_pending = true; irq_during_wfi = false; }
}
bool sampling_timer_pending(void) { TEST_ASSERT_TRUE(interrupts_disabled); return timer_pending; }
bool adc_capture_pending(void) { TEST_ASSERT_TRUE(interrupts_disabled); return adc_pending; }
bool usb_console_has_work(void) { TEST_ASSERT_TRUE(interrupts_disabled); return usb_work; }
bool settings_service_busy(void) { return settings_busy; }
bool i2c_bus_reclock(void) { TEST_ASSERT_EQUAL_INT(LOGGER_FLASH_READY, guard); ++reclocks; return i2c_ok; }
void sensor_get_status(sensor_status_t *status) { *status = (sensor_status_t){.running = sensor_running}; }
void logger_get_status(logger_status_t *status) { *status = (logger_status_t){.state = log_state}; }
bool logger_flash_prepare(void)
{
    if (guard != LOGGER_FLASH_IDLE || log_state != LOGGER_STOPPED) return false;
    ++guard_requests; guard = LOGGER_FLASH_REQUESTING; return true;
}
logger_flash_state_t logger_flash_state(void) { return guard; }
bool logger_flash_release(void) { ++guard_releases; guard = LOGGER_FLASH_RELEASING; return true; }
void power_clock_init(uint32_t rate_hz) { low = supported && rate_hz <= 10; sys_hz = low ? 48000000 : 125000000; }
bool power_clock_select(bool desired)
{
    TEST_ASSERT_EQUAL_INT(LOGGER_FLASH_READY, guard);
    TEST_ASSERT_FALSE(sensor_running);
    ++select_calls;
    if (!select_ok) return false;
    low = desired; sys_hz = low ? 48000000 : 125000000;
    return true;
}
bool power_clock_supported(void) { return supported; }
bool power_clock_faulted(void) { return clock_faulted; }
bool power_clock_low(void) { return supported && low; }
uint32_t power_clock_sys_hz(void) { return sys_hz; }

void setUp(void)
{
    now = 0; supported = select_ok = true;
    i2c_ok = true; reclocks = 0;
    clock_faulted = false;
    low = settings_busy = sensor_running = timer_pending = adc_pending = usb_work = interrupts_disabled = false;
    irq_during_wfi = false;
    log_state = LOGGER_STOPPED; guard = LOGGER_FLASH_IDLE;
    select_calls = wait_calls = guard_requests = guard_releases = prior_irq = 0;
    power_service_init(50);
}
void tearDown(void) {}

static void slow_rate_switches_only_while_storage_is_quiescent(void)
{
    TEST_ASSERT_EQUAL_STRING("normal", power_service_mode());
    TEST_ASSERT_EQUAL_INT(POWER_RATE_PENDING, power_service_prepare_rate(10));
    TEST_ASSERT_EQUAL_UINT(1, guard_requests);
    power_service_poll();
    TEST_ASSERT_EQUAL_UINT(0, select_calls);
    guard = LOGGER_FLASH_READY;
    power_service_poll();
    TEST_ASSERT_EQUAL_UINT(1, select_calls);
    TEST_ASSERT_EQUAL_UINT(1, reclocks);
    TEST_ASSERT_EQUAL_STRING("low", power_service_mode());
    TEST_ASSERT_TRUE(power_service_busy());
    bool ok;
    TEST_ASSERT_FALSE(power_service_take_result(&ok));
    guard = LOGGER_FLASH_IDLE;
    power_service_poll();
    TEST_ASSERT_TRUE(power_service_take_result(&ok));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(48000000, power_service_sys_hz());
    TEST_ASSERT_EQUAL_INT(POWER_RATE_PENDING, power_service_prepare_rate(11));
    guard = LOGGER_FLASH_READY;
    power_service_poll();
    guard = LOGGER_FLASH_IDLE;
    power_service_poll();
    TEST_ASSERT_TRUE(power_service_take_result(&ok));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("normal", power_service_mode());
}

static void rejects_unsafe_requests_and_reports_guard_or_clock_failure(void)
{
    sensor_running = true;
    TEST_ASSERT_EQUAL_INT(POWER_RATE_BUSY, power_service_prepare_rate(1));
    sensor_running = false;
    settings_busy = true;
    TEST_ASSERT_EQUAL_INT(POWER_RATE_BUSY, power_service_prepare_rate(1));
    settings_busy = false;
    TEST_ASSERT_EQUAL_INT(POWER_RATE_ERROR, power_service_prepare_rate(0));
    TEST_ASSERT_EQUAL_INT(POWER_RATE_PENDING, power_service_prepare_rate(1));
    guard = LOGGER_FLASH_READY;
    select_ok = false;
    power_service_poll();
    guard = LOGGER_FLASH_IDLE;
    power_service_poll();
    bool ok = true;
    TEST_ASSERT_TRUE(power_service_take_result(&ok));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_STRING("error", power_service_mode());
    TEST_ASSERT_FALSE(power_service_can_start());
    TEST_ASSERT_EQUAL_INT(POWER_RATE_ERROR, power_service_prepare_rate(1));
}

static void failed_i2c_reclock_prevents_start(void)
{
    TEST_ASSERT_EQUAL_INT(POWER_RATE_PENDING, power_service_prepare_rate(10));
    guard = LOGGER_FLASH_READY;
    i2c_ok = false;
    power_service_poll();
    guard = LOGGER_FLASH_IDLE;
    power_service_poll();
    bool ok = true;
    TEST_ASSERT_TRUE(power_service_take_result(&ok));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_UINT(1, reclocks);
    TEST_ASSERT_FALSE(power_service_can_start());
}

static void unsupported_board_keeps_normal_operation(void)
{
    supported = false;
    power_service_init(1);
    TEST_ASSERT_EQUAL_STRING("unavailable", power_service_mode());
    TEST_ASSERT_EQUAL_INT(POWER_RATE_READY, power_service_prepare_rate(1));
    TEST_ASSERT_EQUAL_UINT(0, guard_requests);
    clock_faulted = true;
    power_service_init(1);
    TEST_ASSERT_FALSE(power_service_can_start());
    TEST_ASSERT_EQUAL_STRING("error", power_service_mode());
}

static void idle_wait_checks_pending_work_with_interrupts_masked(void)
{
    power_service_init(10);
    sensor_running = true; log_state = LOGGER_RUNNING;
    power_service_idle();
    TEST_ASSERT_EQUAL_UINT(1, wait_calls);
    TEST_ASSERT_FALSE(interrupts_disabled);
    timer_pending = true; power_service_idle(); timer_pending = false;
    adc_pending = true; power_service_idle(); adc_pending = false;
    usb_work = true; power_service_idle(); usb_work = false;
    prior_irq = 1; power_service_idle(); TEST_ASSERT_TRUE(interrupts_disabled); prior_irq = 0; interrupts_disabled = false;
    TEST_ASSERT_EQUAL_UINT(1, wait_calls);
    irq_during_wfi = true;
    power_service_idle();
    TEST_ASSERT_EQUAL_UINT(2, wait_calls);
    TEST_ASSERT_TRUE(timer_pending);
    power_service_idle(); /* A pending sample is not put back to sleep. */
    TEST_ASSERT_EQUAL_UINT(2, wait_calls);
    timer_pending = false;
    sensor_running = false; power_service_idle();
    TEST_ASSERT_EQUAL_UINT(2, wait_calls);
}

static void guard_timeout_does_not_change_clocks(void)
{
    TEST_ASSERT_EQUAL_INT(POWER_RATE_PENDING, power_service_prepare_rate(5));
    now = UINT64_C(10000000);
    power_service_poll();
    TEST_ASSERT_EQUAL_UINT(1, guard_releases);
    TEST_ASSERT_EQUAL_UINT(0, select_calls);
    guard = LOGGER_FLASH_IDLE;
    power_service_poll();
    bool ok = true;
    TEST_ASSERT_TRUE(power_service_take_result(&ok));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_STRING("normal", power_service_mode());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(slow_rate_switches_only_while_storage_is_quiescent);
    RUN_TEST(rejects_unsafe_requests_and_reports_guard_or_clock_failure);
    RUN_TEST(unsupported_board_keeps_normal_operation);
    RUN_TEST(idle_wait_checks_pending_work_with_interrupts_masked);
    RUN_TEST(guard_timeout_does_not_change_clocks);
    RUN_TEST(failed_i2c_reclock_prevents_start);
    return UNITY_END();
}
