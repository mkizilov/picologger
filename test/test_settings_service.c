#include "unity.h"
#include "adc_app.h"
#include "logger.h"
#include "sensor.h"
#include "settings_service.h"

#include <string.h>

static uint8_t sectors[2][4096];
static bool sensor_running, adc_running, guard_armed, fail_read, fail_program, fail_adc_resume;
static unsigned int erase_calls, program_calls, adc_pauses, adc_resumes, drains;
static logger_flash_state_t guard_state;
static uint64_t now;
static const settings_t defaults = {50, 3, 16777216, true};
static const settings_t changed = {25, 1, 8192, false};

uint64_t time_us_64(void) { return now; }
void sensor_get_status(sensor_status_t *status) { *status = (sensor_status_t){.running = sensor_running}; }
bool logger_flash_prepare(void) { if (guard_state != LOGGER_FLASH_IDLE) return false; guard_state = LOGGER_FLASH_REQUESTING; return true; }
bool logger_flash_release(void) { guard_state = LOGGER_FLASH_RELEASING; return true; }
logger_flash_state_t logger_flash_state(void) { return guard_state; }
void settings_flash_set_guard(bool armed)
{
    if (armed) { TEST_ASSERT_EQUAL_INT(LOGGER_FLASH_READY, guard_state); TEST_ASSERT_FALSE(adc_running); }
    guard_armed = armed;
}
bool adc_app_pause_for_flash(bool *was_running)
{
    *was_running = adc_running;
    if (adc_running) ++adc_pauses;
    adc_running = false;
    return true;
}
bool adc_app_resume_after_flash(bool was_running)
{
    TEST_ASSERT_FALSE(guard_armed);
    if (was_running) { ++adc_resumes; adc_running = !fail_adc_resume; }
    return !fail_adc_resume;
}
void adc_app_poll(void) { TEST_ASSERT_FALSE(adc_running); ++drains; }
static bool read_slot(void *context, unsigned int slot, uint8_t *data, size_t length)
{
    (void)context;
    if (fail_read) return false;
    memcpy(data, sectors[slot], length);
    return true;
}
static bool erase_slot(void *context, unsigned int slot)
{
    (void)context;
    TEST_ASSERT_TRUE(guard_armed);
    TEST_ASSERT_FALSE(adc_running);
    ++erase_calls;
    memset(sectors[slot], 0xff, sizeof(sectors[slot]));
    return true;
}
static bool program_slot(void *context, unsigned int slot, const uint8_t *data, size_t length)
{
    (void)context;
    TEST_ASSERT_TRUE(guard_armed);
    TEST_ASSERT_FALSE(adc_running);
    ++program_calls;
    memcpy(sectors[slot], data, length);
    return !fail_program;
}
void setUp(void)
{
    memset(sectors, 0xff, sizeof(sectors));
    sensor_running = guard_armed = fail_read = fail_program = fail_adc_resume = false;
    adc_running = true;
    erase_calls = program_calls = adc_pauses = adc_resumes = drains = 0;
    guard_state = LOGGER_FLASH_IDLE;
    now = 0;
    settings_service_init((settings_flash_t){NULL, read_slot, erase_slot, program_slot}, defaults);
}
void tearDown(void) {}

static void saves_only_after_guard_and_adc_pause_then_releases(void)
{
    sensor_running = true;
    TEST_ASSERT_EQUAL_INT(SETTINGS_REQUEST_BUSY, settings_service_request(&changed));
    sensor_running = false;
    TEST_ASSERT_EQUAL_INT(SETTINGS_REQUEST_ACCEPTED, settings_service_request(&changed));
    settings_service_poll();
    TEST_ASSERT_EQUAL_UINT(0, erase_calls);
    TEST_ASSERT_TRUE(adc_running);
    guard_state = LOGGER_FLASH_READY;
    settings_service_poll();
    TEST_ASSERT_EQUAL_UINT(1, erase_calls);
    TEST_ASSERT_EQUAL_UINT(1, program_calls);
    TEST_ASSERT_EQUAL_UINT(1, adc_pauses);
    TEST_ASSERT_EQUAL_UINT(1, adc_resumes);
    TEST_ASSERT_EQUAL_UINT(1, drains);
    TEST_ASSERT_TRUE(adc_running);
    TEST_ASSERT_FALSE(guard_armed);
    TEST_ASSERT_TRUE(settings_service_busy());
    bool saved, resumed;
    TEST_ASSERT_FALSE(settings_service_take_result(&saved, &resumed));
    guard_state = LOGGER_FLASH_IDLE;
    settings_service_poll();
    TEST_ASSERT_TRUE(settings_service_take_result(&saved, &resumed));
    TEST_ASSERT_TRUE(saved); TEST_ASSERT_TRUE(resumed);
    settings_snapshot_t snapshot;
    settings_t active;
    bool readable;
    settings_service_get(&snapshot, &active, &readable);
    TEST_ASSERT_TRUE(readable);
    TEST_ASSERT_EQUAL_UINT32(25, snapshot.values.rate_hz);
    TEST_ASSERT_EQUAL_UINT32(50, active.rate_hz);
    TEST_ASSERT_EQUAL_INT(SETTINGS_REQUEST_UNCHANGED, settings_service_request(&changed));
}

static void timeout_releases_guard_without_writing(void)
{
    TEST_ASSERT_EQUAL_INT(SETTINGS_REQUEST_ACCEPTED, settings_service_request(&changed));
    now = UINT64_C(10000000);
    settings_service_poll();
    TEST_ASSERT_EQUAL_INT(LOGGER_FLASH_RELEASING, guard_state);
    TEST_ASSERT_EQUAL_UINT(0, erase_calls);
    guard_state = LOGGER_FLASH_IDLE;
    settings_service_poll();
    bool saved, resumed;
    TEST_ASSERT_TRUE(settings_service_take_result(&saved, &resumed));
    TEST_ASSERT_FALSE(saved); TEST_ASSERT_TRUE(resumed);
}

static void failed_program_is_reloaded_and_reported_uncertain(void)
{
    TEST_ASSERT_EQUAL_INT(SETTINGS_REQUEST_ACCEPTED, settings_service_request(&changed));
    fail_program = true;
    guard_state = LOGGER_FLASH_READY;
    settings_service_poll();
    guard_state = LOGGER_FLASH_IDLE;
    settings_service_poll();
    bool saved, resumed, readable;
    TEST_ASSERT_TRUE(settings_service_take_result(&saved, &resumed));
    TEST_ASSERT_FALSE(saved);
    settings_snapshot_t snapshot;
    settings_t active;
    settings_service_get(&snapshot, &active, &readable);
    TEST_ASSERT_TRUE(readable);
    TEST_ASSERT_EQUAL_UINT32(25, snapshot.values.rate_hz);
    TEST_ASSERT_EQUAL_UINT32(50, active.rate_hz);
}

static void read_failure_refuses_writes_and_adc_resume_failure_is_reported(void)
{
    fail_read = true;
    settings_service_init((settings_flash_t){NULL, read_slot, erase_slot, program_slot}, defaults);
    TEST_ASSERT_EQUAL_INT(SETTINGS_REQUEST_UNAVAILABLE, settings_service_request(&changed));
    TEST_ASSERT_EQUAL_UINT(0, erase_calls);
    fail_read = false;
    settings_service_init((settings_flash_t){NULL, read_slot, erase_slot, program_slot}, defaults);
    TEST_ASSERT_EQUAL_INT(SETTINGS_REQUEST_ACCEPTED, settings_service_request(&changed));
    fail_adc_resume = true;
    guard_state = LOGGER_FLASH_READY;
    settings_service_poll();
    guard_state = LOGGER_FLASH_IDLE;
    settings_service_poll();
    bool saved, resumed;
    TEST_ASSERT_TRUE(settings_service_take_result(&saved, &resumed));
    TEST_ASSERT_TRUE(saved); TEST_ASSERT_FALSE(resumed);
}

static void stuck_release_reports_error_and_keeps_service_busy(void)
{
    TEST_ASSERT_EQUAL_INT(SETTINGS_REQUEST_ACCEPTED, settings_service_request(&changed));
    guard_state = LOGGER_FLASH_FAILED;
    settings_service_poll();
    TEST_ASSERT_EQUAL_INT(LOGGER_FLASH_RELEASING, guard_state);
    now = UINT64_C(10000000);
    settings_service_poll();
    bool saved, resumed;
    TEST_ASSERT_TRUE(settings_service_take_result(&saved, &resumed));
    TEST_ASSERT_FALSE(saved);
    TEST_ASSERT_TRUE(settings_service_busy());
    TEST_ASSERT_EQUAL_INT(SETTINGS_REQUEST_BUSY, settings_service_request(&changed));
    guard_state = LOGGER_FLASH_IDLE;
    settings_service_poll();
    TEST_ASSERT_FALSE(settings_service_busy());
    TEST_ASSERT_FALSE(settings_service_take_result(&saved, &resumed));
}

static void first_default_is_persisted_and_reset_restores_compiled_values(void)
{
    TEST_ASSERT_EQUAL_INT(SETTINGS_REQUEST_ACCEPTED, settings_service_request(&defaults));
    guard_state = LOGGER_FLASH_READY;
    settings_service_poll();
    guard_state = LOGGER_FLASH_IDLE;
    settings_service_poll();
    bool saved, resumed;
    TEST_ASSERT_TRUE(settings_service_take_result(&saved, &resumed));
    TEST_ASSERT_TRUE(saved);
    TEST_ASSERT_TRUE(settings_encode(sectors[0], &changed, 9));
    memset(sectors[1], 0xff, sizeof(sectors[1]));
    guard_state = LOGGER_FLASH_IDLE;
    settings_service_init((settings_flash_t){NULL, read_slot, erase_slot, program_slot}, defaults);
    TEST_ASSERT_EQUAL_INT(SETTINGS_REQUEST_ACCEPTED, settings_service_reset());
    guard_state = LOGGER_FLASH_READY;
    settings_service_poll();
    guard_state = LOGGER_FLASH_IDLE;
    settings_service_poll();
    TEST_ASSERT_TRUE(settings_service_take_result(&saved, &resumed));
    TEST_ASSERT_TRUE(saved);
    settings_snapshot_t snapshot;
    settings_t active;
    bool readable;
    settings_service_get(&snapshot, &active, &readable);
    TEST_ASSERT_EQUAL_UINT32(50, snapshot.values.rate_hz);
    TEST_ASSERT_EQUAL_UINT32(25, active.rate_hz);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(saves_only_after_guard_and_adc_pause_then_releases);
    RUN_TEST(timeout_releases_guard_without_writing);
    RUN_TEST(failed_program_is_reloaded_and_reported_uncertain);
    RUN_TEST(read_failure_refuses_writes_and_adc_resume_failure_is_reported);
    RUN_TEST(stuck_release_reports_error_and_keeps_service_busy);
    RUN_TEST(first_default_is_persisted_and_reset_restores_compiled_values);
    return UNITY_END();
}
