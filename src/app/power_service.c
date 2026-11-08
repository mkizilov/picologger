#include "power_service.h"

#include "adc_capture.h"
#include "i2c_bus.h"
#include "logger.h"
#include "power_clock.h"
#include "power_policy.h"
#include "sampling_timer.h"
#include "sensor.h"
#include "settings_service.h"
#include "usb_console.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

typedef enum { POWER_IDLE, POWER_WAIT_GUARD, POWER_WAIT_RELEASE } power_state_t;
static power_state_t state;
static bool desired_low, result, completed, release_reported, failed;
static uint64_t deadline;

void power_service_init(uint32_t rate_hz)
{
    state = POWER_IDLE;
    completed = release_reported = failed = false;
    power_clock_init(rate_hz);
    failed = power_clock_faulted() ||
             (power_clock_supported() && (rate_hz <= PICOLOGGER_LOW_RATE_MAX_HZ) != power_clock_low());
}

power_rate_result_t power_service_prepare_rate(uint32_t rate_hz)
{
    if (rate_hz < 1 || rate_hz > 50 || failed) return POWER_RATE_ERROR;
    if (state != POWER_IDLE || completed || settings_service_busy()) return POWER_RATE_BUSY;
    sensor_status_t sensor;
    logger_status_t storage;
    sensor_get_status(&sensor);
    logger_get_status(&storage);
    if (sensor.running || storage.state != LOGGER_STOPPED) return POWER_RATE_BUSY;
    if (!power_clock_supported()) return POWER_RATE_READY;
    desired_low = rate_hz <= PICOLOGGER_LOW_RATE_MAX_HZ;
    if (desired_low == power_clock_low()) return POWER_RATE_READY;
    if (!logger_flash_prepare()) return POWER_RATE_BUSY;
    result = release_reported = false;
    state = POWER_WAIT_GUARD;
    deadline = time_us_64() + UINT64_C(10000000);
    return POWER_RATE_PENDING;
}

void power_service_poll(void)
{
    if (state == POWER_WAIT_GUARD) {
        logger_flash_state_t guard = logger_flash_state();
        if (guard == LOGGER_FLASH_READY) {
            result = power_clock_select(desired_low);
            if (result) result = i2c_bus_reclock();
            if (!result) failed = true;
            (void)logger_flash_release();
            state = POWER_WAIT_RELEASE;
            deadline = time_us_64() + UINT64_C(10000000);
        } else if (guard == LOGGER_FLASH_FAILED || time_us_64() >= deadline) {
            (void)logger_flash_release();
            state = POWER_WAIT_RELEASE;
            deadline = time_us_64() + UINT64_C(10000000);
        }
    }
    if (state == POWER_WAIT_RELEASE && logger_flash_state() == LOGGER_FLASH_IDLE) {
        state = POWER_IDLE;
        if (!release_reported) completed = true;
    } else if (state == POWER_WAIT_RELEASE && !release_reported && time_us_64() >= deadline) {
        result = false;
        release_reported = completed = true;
    }
}

bool power_service_take_result(bool *ok)
{
    if (!completed || !ok) return false;
    *ok = result;
    completed = false;
    return true;
}
bool power_service_busy(void) { return state != POWER_IDLE; }
bool power_service_can_start(void) { return !failed && state == POWER_IDLE && !completed; }
const char *power_service_mode(void)
{
    if (failed) return "error";
    if (!power_clock_supported()) return "unavailable";
    return power_clock_low() ? "low" : "normal";
}
uint32_t power_service_sys_hz(void) { return power_clock_sys_hz(); }

void power_service_idle(void)
{
    if (!power_clock_low() || state != POWER_IDLE || completed || settings_service_busy()) return;
    sensor_status_t sensor;
    logger_status_t storage;
    sensor_get_status(&sensor);
    logger_get_status(&storage);
    if (!sensor.running || storage.state != LOGGER_RUNNING) return;
    uint32_t interrupts = save_and_disable_interrupts();
    if (!interrupts && !sampling_timer_pending() && !adc_capture_pending() && !usb_console_has_work()) __wfi();
    restore_interrupts(interrupts);
}
