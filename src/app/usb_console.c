#include "usb_console.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "adc_app.h"
#include "command.h"
#include "command_io.h"
#include "logger.h"
#include "fault_report.h"
#include "sensor.h"
#include "settings_service.h"
#include "usb_nonblocking.h"
#include "watchdog_monitor.h"
#include "pico/stdlib.h"

static command_parser_t parser;
static command_dispatch_t dispatch;
static command_output_t output;
static bool connected, streaming, stopping, starting, rebooting, reboot_announced, reboot_sent, crashing;
static uint64_t stream_dropped, reboot_deadline, crash_deadline;
static uint8_t input[16];
static size_t input_pos, input_count;

/* A leading newline separates frames from truncated legacy diagnostics. */
static bool emit(const char *format, ...)
{
    char frame[64] = {'\n', '@'};
    va_list args;
    va_start(args, format);
    int n = vsnprintf(frame + 2, sizeof(frame) - 3, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(frame) - 3) { ++output.dropped; return false; }
    frame[2 + n] = '\n';
    return command_output_write(&output, frame, (size_t)n + 3);
}

static const char *state_name(logger_state_t state)
{
    switch (state) {
        case LOGGER_RUNNING: return "running";
        case LOGGER_STOPPING: return "stopping";
        case LOGGER_STOPPED: return "stopped";
        case LOGGER_STARTING: return "starting";
    }
    return "unknown";
}

static bool handle_status(void *context, const command_request_t *request)
{
    (void)context; (void)request;
    sensor_status_t sensor;
    logger_status_t storage;
    sensor_get_status(&sensor);
    logger_get_status(&storage);
    (void)emit("STATUS uptime_s=%" PRIu64, time_us_64() / UINT64_C(1000000));
    (void)emit("STATUS sampling=%u rate_hz=%" PRIu32, sensor.running ? 1u : 0u, sensor.rate_hz);
    (void)emit("STATUS bme_samples=%" PRIu64, sensor.samples);
    (void)emit("STATUS bme_missed=%" PRIu64, sensor.missed);
    (void)emit("STATUS bme_errors=%" PRIu64, sensor.read_errors);
    (void)emit("STATUS ring_high_water=%" PRIu32, sensor.ring_high_water);
    (void)emit("STATUS adc_samples=%" PRIu64, adc_app_total_samples());
    (void)emit("STATUS adc_fault=%" PRIu32, adc_app_fault());
    (void)emit("STATUS adc_flash_pauses=%" PRIu32, adc_app_flash_pauses());
    (void)emit("STATUS watchdog_missing=0x%02" PRIx32, watchdog_monitor_missing());
    if (storage.current_file)
        (void)emit("STATUS log=%s sd=%u file=LOG_%04" PRIu32 ".BIN",
                   state_name(storage.state), storage.online ? 1u : 0u, storage.current_file);
    else (void)emit("STATUS log=%s sd=%u file=-", state_name(storage.state), storage.online ? 1u : 0u);
    (void)emit("STATUS dropped=%" PRIu64, storage.dropped);
    (void)emit("STATUS uncertain=%" PRIu64, storage.uncertain);
    (void)emit("STATUS stream_dropped=%" PRIu64, stream_dropped);
    (void)emit("STATUS output_dropped=%" PRIu32, output.dropped);
    (void)emit("END STATUS");
    return true;
}

static bool begin_stop(void)
{
    logger_status_t status;
    logger_get_status(&status);
    if (status.state == LOGGER_STOPPED) return sensor_set_running(false);
    if (status.state != LOGGER_RUNNING || !sensor_set_running(false)) return false;
    if (!logger_request_stop()) {
        if (!sensor_set_running(true)) (void)emit("ERR timer restore failed");
        return false;
    }
    return true;
}

static bool handle_stop(void *context, const command_request_t *request)
{
    (void)context; (void)request;
    logger_status_t status;
    logger_get_status(&status);
    if (status.state == LOGGER_STOPPED) {
        if (sensor_set_running(false)) (void)emit("OK stopped");
        else (void)emit("ERR timer stop failed");
    }
    else if (!begin_stop()) (void)emit("ERR stop busy or timer failure");
    else { stopping = true; (void)emit("PENDING stop"); }
    return true;
}

static bool handle_start(void *context, const command_request_t *request)
{
    (void)context; (void)request;
    logger_status_t storage;
    sensor_status_t sensor;
    logger_get_status(&storage);
    sensor_get_status(&sensor);
    if (storage.state == LOGGER_RUNNING) {
        if (sensor.running || sensor_set_running(true)) (void)emit("OK running");
        else (void)emit("ERR sensor unavailable");
    } else if (storage.state != LOGGER_STOPPED || !sensor.available || !logger_request_start(sensor.rate_hz))
        (void)emit("ERR start unavailable or busy");
    else { starting = true; (void)emit("PENDING start"); }
    return true;
}

static bool handle_rate(void *context, const command_request_t *request)
{
    (void)context;
    logger_status_t storage;
    logger_get_status(&storage);
    if (storage.state != LOGGER_STOPPED || !sensor_set_rate(request->rate_hz)) (void)emit("ERR stop first");
    else (void)emit("OK rate=%" PRIu32, request->rate_hz);
    return true;
}

static bool handle_stream(void *context, const command_request_t *request)
{
    (void)context;
    streaming = request->enabled;
    (void)emit("OK stream=%s", streaming ? "on" : "off");
    if (streaming) (void)emit("CSV sequence,timestamp_us,T_centi,P_Pa,RH_q1024");
    return true;
}

static bool handle_ls(void *context, const command_request_t *request)
{
    (void)context; (void)request;
    if (!logger_request_list()) (void)emit("ERR storage command busy");
    else (void)emit("PENDING ls");
    return true;
}

static bool handle_dump(void *context, const command_request_t *request)
{
    (void)context;
    if (!logger_request_dump(request->filename)) (void)emit("ERR storage command busy");
    else (void)emit("PENDING dump");
    return true;
}

static bool handle_reboot(void *context, const command_request_t *request)
{
    (void)context; (void)request;
    if (settings_service_busy()) (void)emit("ERR config busy");
    else if (rebooting) (void)emit("ERR reboot pending");
    else {
        rebooting = true;
        reboot_deadline = time_us_64() + UINT64_C(10000000);
        (void)emit("PENDING reboot");
    }
    return true;
}

static bool handle_help(void *context, const command_request_t *request)
{
    (void)context; (void)request;
    (void)emit("HELP status start stop rate <1-50>");
    (void)emit("HELP stream on|off ls dump <filename> reboot");
    (void)emit("HELP stop before rate or dumping the current file");
    (void)emit("HELP config get | config set <key> <value> | config reset");
    (void)emit("END HELP");
    return true;
}

static bool handle_crash(void *context, const command_request_t *request)
{
    (void)context; (void)request;
    if (settings_service_busy() || crashing) (void)emit("ERR crash unavailable");
    else {
        crashing = true;
        crash_deadline = time_us_64() + UINT64_C(100000);
        (void)emit("PENDING crash");
    }
    return true;
}

static bool handle_config_get(void *context, const command_request_t *request)
{
    (void)context; (void)request;
    settings_snapshot_t saved;
    settings_t active;
    bool readable;
    settings_service_get(&saved, &active, &readable);
    (void)emit("CONFIG readable=%u busy=%u slot=%d revision=%" PRIu32,
               readable ? 1u : 0u, settings_service_busy() ? 1u : 0u, saved.active_slot, saved.revision);
    (void)emit("CONFIG saved rate=%" PRIu32 " channels=%" PRIu32, saved.values.rate_hz, saved.values.channels);
    (void)emit("CONFIG saved rotate_bytes=%" PRIu32, saved.values.rotate_bytes);
    (void)emit("CONFIG saved start_on_boot=%u", saved.values.start_on_boot ? 1u : 0u);
    (void)emit("CONFIG active rate=%" PRIu32 " channels=%" PRIu32, active.rate_hz, active.channels);
    (void)emit("CONFIG active rotate_bytes=%" PRIu32, active.rotate_bytes);
    (void)emit("CONFIG active start_on_boot=%u", active.start_on_boot ? 1u : 0u);
    (void)emit("END CONFIG");
    return true;
}

static void report_config_request(settings_request_t result)
{
    switch (result) {
        case SETTINGS_REQUEST_ACCEPTED: (void)emit("PENDING config; ADC will pause"); break;
        case SETTINGS_REQUEST_UNCHANGED: (void)emit("OK config unchanged"); break;
        case SETTINGS_REQUEST_BUSY: (void)emit("ERR stop first or storage/flash busy"); break;
        case SETTINGS_REQUEST_UNAVAILABLE: (void)emit("ERR settings unavailable"); break;
        case SETTINGS_REQUEST_INVALID: (void)emit("ERR bad settings"); break;
    }
}

static bool handle_config_set(void *context, const command_request_t *request)
{
    (void)context;
    settings_snapshot_t saved;
    settings_t active;
    bool readable;
    settings_service_get(&saved, &active, &readable);
    (void)active; (void)readable;
    switch (request->config_key) {
        case CONFIG_RATE: saved.values.rate_hz = request->config_value; break;
        case CONFIG_CHANNELS: saved.values.channels = request->config_value; break;
        case CONFIG_ROTATE_BYTES: saved.values.rotate_bytes = request->config_value; break;
        case CONFIG_START_ON_BOOT: saved.values.start_on_boot = request->config_value != 0; break;
    }
    report_config_request(settings_service_request(&saved.values));
    return true;
}

static bool handle_config_reset(void *context, const command_request_t *request)
{
    (void)context; (void)request;
    report_config_request(settings_service_reset());
    return true;
}

void usb_console_init(void)
{
    command_parser_init(&parser);
    dispatch.handler[COMMAND_STATUS] = handle_status;
    dispatch.handler[COMMAND_START] = handle_start;
    dispatch.handler[COMMAND_STOP] = handle_stop;
    dispatch.handler[COMMAND_RATE] = handle_rate;
    dispatch.handler[COMMAND_STREAM] = handle_stream;
    dispatch.handler[COMMAND_LS] = handle_ls;
    dispatch.handler[COMMAND_DUMP] = handle_dump;
    dispatch.handler[COMMAND_REBOOT] = handle_reboot;
    dispatch.handler[COMMAND_HELP] = handle_help;
    dispatch.handler[COMMAND_CONFIG_GET] = handle_config_get;
    dispatch.handler[COMMAND_CONFIG_SET] = handle_config_set;
    dispatch.handler[COMMAND_CONFIG_RESET] = handle_config_reset;
    dispatch.handler[COMMAND_CRASH] = handle_crash;
}

void usb_console_sample(const log_record_t *record)
{
    if (!connected || !streaming) return;
    if (output.count > COMMAND_OUTPUT_CAPACITY - 1024 ||
        !emit("CSV,%" PRIu32 ",%" PRIu64 ",%" PRId32 ",%" PRId32 ",%" PRId32,
              record->sequence, record->timestamp_us, record->values[0], record->values[1], record->values[2]))
        ++stream_dropped;
}

static void progress_controls(void)
{
    logger_status_t status;
    logger_get_status(&status);
    if (crashing && (output.count == 0 || time_us_64() >= crash_deadline)) {
        crashing = false;
        fault_force_crash();
    }
    if (stopping && status.state == LOGGER_STOPPED) {
        stopping = false;
        (void)emit(status.control_ok ? "OK stopped" : "ERR stop flush failed");
    }
    if (starting && status.state == LOGGER_RUNNING) {
        starting = false;
        if (rebooting) { /* Reboot will stop the newly opened writer. */ }
        else if (sensor_set_running(true)) (void)emit("OK started");
        else {
            (void)emit("ERR timer start failed");
            if (!logger_request_stop()) (void)emit("ERR writer stop failed");
        }
    } else if (starting && status.state == LOGGER_STOPPED && !status.control_ok) {
        starting = false;
        (void)emit("ERR storage start failed");
    }
    if (rebooting && !reboot_sent) {
        if (status.state == LOGGER_RUNNING) (void)begin_stop();
        if (status.state == LOGGER_STOPPED && !reboot_announced) {
            (void)emit(status.control_ok ? "OK reboot" : "WARN reboot after flush error");
            reboot_announced = true;
        }
        if ((reboot_announced && output.count == 0) || time_us_64() >= reboot_deadline) {
            reboot_sent = true;
            usb_nb_reboot();
        }
    }
}

void usb_console_poll(void)
{
    bool saved, adc_resumed;
    if (settings_service_take_result(&saved, &adc_resumed)) {
        if (saved) (void)emit("OK config saved; reboot to apply");
        else (void)emit("ERR config save uncertain; check config");
        if (!adc_resumed) (void)emit("ERR ADC resume failed; reboot required");
    }
    bool is_connected = usb_nb_connected();
    if (!is_connected) {
        if (connected) {
            command_parser_init(&parser);
            command_output_reset(&output);
            input_pos = input_count = 0;
            streaming = false;
            logger_cancel_io();
        }
        connected = false;
        progress_controls();
        return;
    }
    if (!connected) { connected = true; (void)emit("READY type help"); }
    if (COMMAND_OUTPUT_CAPACITY - output.count >= 64) {
        char reply[64];
        bool final;
        if (logger_take_reply(reply, sizeof(reply), &final)) {
            (void)final;
            size_t length = strlen(reply);
            if (length && reply[length - 1] == '\n') reply[length - 1] = '\0';
            (void)emit("%s", reply);
        }
    }
    progress_controls();
    if (COMMAND_OUTPUT_CAPACITY - output.count >= 1024) {
        if (input_pos == input_count) {
            input_pos = 0;
            input_count = usb_nb_read(input, sizeof(input));
        }
        while (input_pos < input_count) {
            command_request_t request;
            command_parse_result_t result = command_feed(&parser, input[input_pos++], &request);
            if (result == COMMAND_READY) {
                if ((rebooting || crashing) && request.kind != COMMAND_STATUS && request.kind != COMMAND_HELP)
                    (void)emit("ERR reset pending");
                else (void)command_dispatch(&dispatch, NULL, &request);
            }
            else if (result == COMMAND_UNKNOWN) (void)emit("ERR unknown command");
            else if (result == COMMAND_BAD_ARGUMENT) (void)emit("ERR bad arguments");
            else if (result == COMMAND_TOO_LONG) (void)emit("ERR line too long");
            if (result != COMMAND_MORE) break;
        }
    }
    char frame[64];
    size_t length = command_output_frame(&output, frame, sizeof(frame));
    if (length && usb_nb_write(frame, length)) command_output_consume(&output, length);
}
