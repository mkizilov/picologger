#include "settings_service.h"

#include "adc_app.h"
#include "logger.h"
#include "sensor.h"
#include "settings_flash.h"
#include "pico/stdlib.h"

typedef enum { SERVICE_IDLE, SERVICE_WAIT_GUARD, SERVICE_WAIT_RELEASE } service_state_t;
static settings_flash_t backend;
static settings_t defaults, active, requested;
static settings_snapshot_t snapshot;
static bool readable, completed, saved_result, adc_result;
static bool release_reported;
static service_state_t state;
static uint64_t deadline;

static bool equal_settings(const settings_t *a, const settings_t *b)
{
    return a->rate_hz == b->rate_hz && a->channels == b->channels &&
           a->rotate_bytes == b->rotate_bytes && a->start_on_boot == b->start_on_boot;
}

void settings_service_init(settings_flash_t flash, settings_t compiled_defaults)
{
    backend = flash;
    defaults = compiled_defaults;
    state = SERVICE_IDLE;
    completed = release_reported = false;
    snapshot = (settings_snapshot_t){compiled_defaults, 0, -1};
    readable = settings_load(&backend, &defaults, &snapshot);
    active = snapshot.values;
}

void settings_service_get(settings_snapshot_t *out, settings_t *running, bool *is_readable)
{
    *out = snapshot;
    *running = active;
    *is_readable = readable;
}

bool settings_service_busy(void) { return state != SERVICE_IDLE; }

settings_request_t settings_service_request(const settings_t *values)
{
    if (!settings_valid(values)) return SETTINGS_REQUEST_INVALID;
    if (!readable) return SETTINGS_REQUEST_UNAVAILABLE;
    if (state != SERVICE_IDLE || completed) return SETTINGS_REQUEST_BUSY;
    if (snapshot.active_slot >= 0 && equal_settings(values, &snapshot.values)) return SETTINGS_REQUEST_UNCHANGED;
    sensor_status_t sensor;
    sensor_get_status(&sensor);
    if (sensor.running) return SETTINGS_REQUEST_BUSY;
    if (!logger_flash_prepare()) return SETTINGS_REQUEST_BUSY;
    requested = *values;
    saved_result = false;
    adc_result = true;
    release_reported = false;
    state = SERVICE_WAIT_GUARD;
    deadline = time_us_64() + UINT64_C(10000000);
    return SETTINGS_REQUEST_ACCEPTED;
}

settings_request_t settings_service_reset(void) { return settings_service_request(&defaults); }

void settings_service_poll(void)
{
    if (state == SERVICE_WAIT_GUARD) {
        logger_flash_state_t guard = logger_flash_state();
        if (guard == LOGGER_FLASH_READY) {
            bool was_running = false;
            if (adc_app_pause_for_flash(&was_running)) {
                adc_app_poll();
                settings_flash_set_guard(true);
                saved_result = settings_save(&backend, &requested, &snapshot);
                settings_flash_set_guard(false);
                adc_result = adc_app_resume_after_flash(was_running);
                if (!saved_result) readable = settings_load(&backend, &defaults, &snapshot);
            } else adc_result = false;
            (void)logger_flash_release();
            state = SERVICE_WAIT_RELEASE;
            deadline = time_us_64() + UINT64_C(10000000);
        } else if (guard == LOGGER_FLASH_FAILED || time_us_64() >= deadline) {
            (void)logger_flash_release();
            state = SERVICE_WAIT_RELEASE;
            deadline = time_us_64() + UINT64_C(10000000);
        }
    }
    if (state == SERVICE_WAIT_RELEASE && logger_flash_state() == LOGGER_FLASH_IDLE) {
        state = SERVICE_IDLE;
        if (!release_reported) completed = true;
    } else if (state == SERVICE_WAIT_RELEASE && !release_reported && time_us_64() >= deadline) {
        saved_result = false;
        release_reported = completed = true;
    }
}

bool settings_service_take_result(bool *saved, bool *adc_resumed)
{
    if (!completed || !saved || !adc_resumed) return false;
    *saved = saved_result;
    *adc_resumed = adc_result;
    completed = false;
    return true;
}
