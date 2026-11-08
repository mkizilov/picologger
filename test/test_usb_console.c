#include "unity.h"
#include "adc_app.h"
#include "logger.h"
#include "power_service.h"
#include "sensor.h"
#include "settings_service.h"
#include "usb_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t input[8192];
static size_t input_read, input_write;
static char transmitted[65536];
static size_t transmitted_length;
static bool attached = true, writable = true, io_busy, cancelled, rebooted, crashed;
static uint64_t now;
static logger_status_t storage = {.state = LOGGER_RUNNING, .online = true, .control_ok = true,
                                  .current_file = 12, .dropped = 5, .uncertain = 2};
static sensor_status_t sensor = {.samples = 100, .missed = 3, .read_errors = 2,
                                 .ring_high_water = 17, .rate_hz = 50, .running = true, .available = true};
static const char *replies[4];
static bool final_reply[4];
static unsigned int reply_index, reply_count;
static char dumped_name[33];
static settings_snapshot_t saved_settings = {{50, 3, 16777216, true}, 0, -1};
static settings_t active_settings = {50, 3, 16777216, true};
static bool config_busy, config_done;
static bool power_busy, power_done, power_ok = true;
static power_rate_result_t power_request = POWER_RATE_READY;

uint64_t time_us_64(void) { return now; }
uint64_t adc_app_total_samples(void) { return 123456; }
uint32_t adc_app_fault(void) { return 0; }
uint32_t adc_app_flash_pauses(void) { return 1; }
uint32_t watchdog_monitor_missing(void) { return 0; }
const char *power_service_mode(void) { return "normal"; }
uint32_t power_service_sys_hz(void) { return 125000000; }
bool power_service_busy(void) { return power_busy; }
bool power_service_can_start(void) { return power_ok; }
power_rate_result_t power_service_prepare_rate(uint32_t rate)
{
    (void)rate;
    if (power_request == POWER_RATE_PENDING) power_busy = true;
    return power_request;
}
bool power_service_take_result(bool *ok)
{
    if (!power_done) return false;
    power_done = power_busy = false; *ok = power_ok; return true;
}
void settings_service_get(settings_snapshot_t *saved, settings_t *active, bool *readable)
{
    *saved = saved_settings; *active = active_settings; *readable = true;
}
bool settings_service_busy(void) { return config_busy; }
settings_request_t settings_service_request(const settings_t *value)
{
    if (sensor.running || config_busy) return SETTINGS_REQUEST_BUSY;
    saved_settings.values = *value; config_busy = true; return SETTINGS_REQUEST_ACCEPTED;
}
settings_request_t settings_service_reset(void)
{
    return settings_service_request(&active_settings);
}
bool settings_service_take_result(bool *saved, bool *resumed)
{
    if (!config_done) return false;
    config_done = config_busy = false; *saved = *resumed = true; return true;
}
bool usb_nb_connected(void) { return attached; }
bool usb_nb_input_ready(void) { return attached && input_read < input_write; }
bool usb_nb_write_ready(size_t length) { return attached && writable && length <= 64; }
size_t usb_nb_read(uint8_t *data, size_t capacity)
{
    TEST_ASSERT_TRUE(capacity <= 16);
    size_t remaining = input_write - input_read;
    size_t n = remaining < capacity ? remaining : capacity;
    memcpy(data, input + input_read, n);
    input_read += n;
    return n;
}
bool usb_nb_write(const char *data, size_t length)
{
    TEST_ASSERT_TRUE(length <= 64);
    if (!writable) return false;
    TEST_ASSERT_TRUE(transmitted_length + length < sizeof(transmitted));
    memcpy(transmitted + transmitted_length, data, length);
    transmitted_length += length;
    transmitted[transmitted_length] = 0;
    return true;
}
void usb_nb_reboot(void) { rebooted = true; }
void fault_force_crash(void) { crashed = true; }
void sensor_get_status(sensor_status_t *out) { *out = sensor; }
bool sensor_set_running(bool value) { sensor.running = value; return sensor.available; }
bool sensor_set_rate(uint32_t rate) { if (sensor.running) return false; sensor.rate_hz = rate; return true; }
void logger_get_status(logger_status_t *out) { *out = storage; }
bool logger_request_stop(void) { if (storage.state != LOGGER_RUNNING) return false; storage.state = LOGGER_STOPPING; return true; }
bool logger_request_start(uint32_t rate) { if (storage.state != LOGGER_STOPPED || rate != sensor.rate_hz) return false; storage.state = LOGGER_STARTING; return true; }
bool logger_request_list(void) { if (io_busy) return false; io_busy = true; return true; }
bool logger_request_dump(const char *name)
{
    if (io_busy) return false;
    io_busy = true;
    (void)snprintf(dumped_name, sizeof(dumped_name), "%s", name);
    return true;
}
bool logger_take_reply(char *line, size_t capacity, bool *final)
{
    if (reply_index == reply_count) return false;
    size_t length = strlen(replies[reply_index]);
    if (length >= capacity) return false;
    memcpy(line, replies[reply_index], length + 1);
    *final = final_reply[reply_index++];
    if (*final) io_busy = false;
    return true;
}
void logger_cancel_io(void) { if (io_busy) { cancelled = true; io_busy = false; } }
void setUp(void) {}
void tearDown(void) {}

static void feed(const char *text)
{
    size_t length = strlen(text);
    TEST_ASSERT_TRUE(input_write + length < sizeof(input));
    memcpy(input + input_write, text, length);
    input_write += length;
}
static void pump(unsigned int count) { while (count--) usb_console_poll(); }

static void commands_are_bounded_and_preserve_sampling_under_backpressure(void)
{
    usb_console_init();
    pump(2);
    feed("status\nhelp\nrate 25\nrate 0\nunknown\nconfig get\nconfig set rate 25\n");
    pump(100);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@STATUS bme_samples=100"));
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@STATUS ring_high_water=17"));
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@STATUS adc_samples=123456"));
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "file=LOG_0012.BIN"));
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@ERR stop first"));
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@ERR bad arguments"));
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@ERR unknown command"));
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@CONFIG saved rate=50 channels=3"));
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@ERR stop first or storage/flash busy"));
    char long_line[100];
    memset(long_line, 'x', sizeof(long_line));
    long_line[sizeof(long_line) - 2] = '\n'; long_line[sizeof(long_line) - 1] = 0;
    feed(long_line);
    pump(50);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@ERR line too long"));
    feed("stream on\n"); pump(20);
    log_record_t record = {.sequence = 4, .timestamp_us = 12345, .values = {2508, 100653, 67367}};
    usb_console_sample(&record); pump(5);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@CSV,4,12345,2508,100653,67367"));
    writable = false;
    for (unsigned int i = 0; i < 1000; ++i) usb_console_sample(&record);
    pump(20);
    TEST_ASSERT_TRUE(sensor.running);
    writable = true;
    pump(3000);
    feed("status\n"); pump(100);
    const char *drop_report = NULL, *cursor = transmitted;
    while ((cursor = strstr(cursor, "@STATUS stream_dropped=")) != NULL) { drop_report = cursor; ++cursor; }
    TEST_ASSERT_NOT_NULL(drop_report);
    TEST_ASSERT_TRUE(strtoull(drop_report + strlen("@STATUS stream_dropped="), NULL, 10) > 0);
    feed("stream off\nstop\n"); pump(30);
    TEST_ASSERT_EQUAL_INT(LOGGER_STOPPING, storage.state);
    TEST_ASSERT_FALSE(sensor.running);
    storage.state = LOGGER_STOPPED; pump(10);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@OK stopped"));
    feed("config set channels 1\n"); pump(20);
    TEST_ASSERT_TRUE(config_busy);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@PENDING config; ADC will pause"));
    feed("reboot\n"); pump(10);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@ERR config busy"));
    TEST_ASSERT_FALSE(rebooted);
    config_done = true; pump(10);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@OK config saved; reboot to apply"));
    TEST_ASSERT_EQUAL_UINT32(1, saved_settings.values.channels);
    feed("config reset\n"); pump(20);
    TEST_ASSERT_TRUE(config_busy);
    config_done = true; pump(10);
    feed("rate 25\nstart\n"); pump(20);
    TEST_ASSERT_EQUAL_UINT32(25, sensor.rate_hz);
    TEST_ASSERT_EQUAL_INT(LOGGER_STARTING, storage.state);
    storage.state = LOGGER_RUNNING; pump(10);
    TEST_ASSERT_TRUE(sensor.running);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@OK started"));
    feed("stop\n"); pump(10);
    storage.state = LOGGER_STOPPED; storage.control_ok = false; pump(10);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@ERR stop flush failed"));
    feed("start\n"); pump(10);
    storage.state = LOGGER_STOPPED; pump(10);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@ERR storage start failed"));
    storage.control_ok = true;
    feed("start\n"); pump(10);
    storage.state = LOGGER_RUNNING; pump(10);
    feed("ls\n"); pump(10);
    replies[0] = "BEGIN LS\n"; replies[1] = "FILE LOG_0001.BIN 512\n"; replies[2] = "END LS\n";
    final_reply[2] = true; reply_index = 0; reply_count = 3;
    pump(20);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@FILE LOG_0001.BIN 512"));
    feed("dump LOG_0001.BIN\n"); pump(20);
    TEST_ASSERT_EQUAL_STRING("LOG_0001.BIN", dumped_name);
    replies[0] = "BEGIN DUMP LOG_0001.BIN\n"; replies[1] = "DATA 0 TWFu\n";
    replies[2] = "END DUMP 3 11111111\n"; final_reply[0] = final_reply[1] = false; final_reply[2] = true;
    reply_index = 0; reply_count = 3; pump(20);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@DATA 0 TWFu"));
    feed("dump LOG_0001.BIN\n"); pump(20);
    attached = false; pump(2);
    TEST_ASSERT_TRUE(cancelled);
    attached = true; pump(10);
    feed("reboot\n"); pump(10);
    TEST_ASSERT_EQUAL_INT(LOGGER_STOPPING, storage.state);
    storage.state = LOGGER_STOPPED; pump(30);
    TEST_ASSERT_TRUE(rebooted);
}

static void reboot_timeout_does_not_wait_forever(void)
{
    usb_console_init();
    pump(2);
    feed("reboot\n");
    pump(10);
    TEST_ASSERT_EQUAL_INT(LOGGER_STOPPING, storage.state);
    TEST_ASSERT_FALSE(sensor.running);
    writable = false;
    now = UINT64_C(10000001);
    pump(5);
    TEST_ASSERT_TRUE(rebooted);
}

static void hidden_crash_dispatches_without_help_advertisement(void)
{
    usb_console_init();
    pump(2);
    feed("help\n"); pump(20);
    TEST_ASSERT_NULL(strstr(transmitted, "crash"));
    feed("crash\n"); pump(20);
    TEST_ASSERT_TRUE(crashed);
}

static void rate_transition_is_async_and_usb_remains_serviceable(void)
{
    storage.state = LOGGER_STOPPED;
    sensor.running = false;
    usb_console_init();
    pump(3);
    TEST_ASSERT_FALSE(usb_console_has_work());
    power_request = POWER_RATE_PENDING;
    feed("rate 10\n");
    TEST_ASSERT_TRUE(usb_console_has_work());
    pump(10);
    TEST_ASSERT_TRUE(power_busy);
    TEST_ASSERT_EQUAL_UINT32(50, sensor.rate_hz);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@PENDING rate"));
    feed("start\nreboot\n"); pump(20);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@ERR power transition busy"));
    TEST_ASSERT_FALSE(rebooted);
    power_done = true; pump(20);
    TEST_ASSERT_EQUAL_UINT32(10, sensor.rate_hz);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@OK rate=10"));
    feed("status\n");
    pump(1);
    TEST_ASSERT_TRUE(usb_console_has_work());
    writable = false;
    TEST_ASSERT_FALSE(usb_console_has_work());
    writable = true;
    TEST_ASSERT_TRUE(usb_console_has_work());
    pump(100);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@STATUS power=normal clk_sys=125000000"));
    power_request = POWER_RATE_PENDING;
    power_ok = false;
    feed("rate 25\n"); pump(10);
    power_done = true; pump(20);
    TEST_ASSERT_EQUAL_UINT32(10, sensor.rate_hz);
    TEST_ASSERT_NOT_NULL(strstr(transmitted, "@ERR power clock; reboot required"));
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    if (argc == 2 && !strcmp(argv[1], "timeout")) RUN_TEST(reboot_timeout_does_not_wait_forever);
    else if (argc == 2 && !strcmp(argv[1], "crash")) RUN_TEST(hidden_crash_dispatches_without_help_advertisement);
    else if (argc == 2 && !strcmp(argv[1], "power")) RUN_TEST(rate_transition_is_async_and_usb_remains_serviceable);
    else if (argc == 1) RUN_TEST(commands_are_bounded_and_preserve_sampling_under_backpressure);
    else return 1;
    return UNITY_END();
}
