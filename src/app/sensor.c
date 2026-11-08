#include "sensor.h"

#include <inttypes.h>
#include <stdio.h>

#include "bme280.h"
#include "i2c_bus.h"
#include "interval_stats.h"
#include "log_format.h"
#include "logger.h"
#include "ring_buffer.h"
#include "sampling_timer.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

static bool found[128];
static unsigned int scan_errors;
static uint8_t ids[2];
static bme280_result_t id_results[2];
static bool id_checked[2];
static bme280_t sensor;
static bme280_result_t sensor_result = BME280_NO_DATA;
static bool waiting_for_reset, ready, timer_failed, have_latest;
static uint64_t reset_time, next_read, stats_start, missed, read_errors;
static interval_stats_t intervals;
static uint32_t previous_sequence;
static ring_local_t samples;
static log_record_t sample_storage[256], latest;

#ifndef PICOLOGGER_SAMPLE_RATE_HZ
#define PICOLOGGER_SAMPLE_RATE_HZ 50
#endif

void sensor_start(void)
{
    i2c_bus_init();
    (void)ring_local_init(&samples, sample_storage, sizeof(sample_storage), 256, sizeof(sample_storage[0]));
    for (unsigned int address = 8; address <= 0x77; ++address) {
        int result = i2c_bus_probe((uint8_t)address);
        if (result > 0) found[address] = true;
        if (result < 0) ++scan_errors;
    }
    sensor.bus = i2c_bus_interface();
    for (unsigned int i = 0; i < 2; ++i) {
        sensor.address = (uint8_t)(0x76 + i);
        if (!found[sensor.address]) continue;
        id_checked[i] = true;
        id_results[i] = bme280_check_id(&sensor, &ids[i]);
        sensor_result = id_results[i];
        if (id_results[i] == BME280_OK) {
            sensor_result = bme280_soft_reset(&sensor);
            if (sensor_result == BME280_OK) {
                reset_time = time_us_64();
                waiting_for_reset = true;
            }
            break;
        }
    }
    next_read = time_us_64() + 1000000;
}

void sensor_report_boot(void)
{
    printf("I2C0 scan (0x08-0x77):");
    bool any = false;
    for (unsigned int address = 8; address <= 0x77; ++address) {
        if (found[address]) { printf(" 0x%02x", address); any = true; }
    }
    if (!any) printf(" none");
    printf("; errors=%u\n", scan_errors);
    for (unsigned int i = 0; i < 2; ++i) {
        if (id_checked[i] && id_results[i] == BME280_BAD_ID)
            printf("BME280 ID mismatch at 0x%02x: got 0x%02x, expected 0x60\n", 0x76 + i, (unsigned int)ids[i]);
        else if (id_checked[i] && id_results[i] == BME280_BUS_ERROR)
            printf("BME280 ID read failed at 0x%02x\n", 0x76 + i);
    }
    if (waiting_for_reset || ready) printf("BME280 selected at 0x%02x\n", (unsigned int)sensor.address);
    else printf("BME280 unavailable (status %d)\n", (int)sensor_result);
    printf("Sampling rate=%u Hz, timer period=%u us\n",
           (unsigned int)PICOLOGGER_SAMPLE_RATE_HZ, 1000000u / (unsigned int)PICOLOGGER_SAMPLE_RATE_HZ);
    if (timer_failed) puts("Sampling timer initialization failed");
}

static void sample_once(const sampling_tick_t *tick)
{
    missed += (uint32_t)(tick->sequence - previous_sequence - 1u);
    previous_sequence = tick->sequence;
    interval_stats_observe(&intervals, tick->timestamp_us);
    bme280_reading_t reading;
    sampling_pulse_begin();
    sensor_result = bme280_read(&sensor, &reading);
    sampling_pulse_end();
    if (sensor_result != BME280_OK) { ++read_errors; return; }
    log_record_t record = {.sequence = tick->sequence, .timestamp_us = tick->timestamp_us,
        .values = {reading.temperature_centi_c, (int32_t)(reading.pressure_pa_q24_8 / 256),
                   (int32_t)reading.humidity_rh_q22_10, 0}, .flags = 0};
    latest = record;
    have_latest = true;
    if (!ring_local_push(&samples, &record)) logger_note_drop(1);
}

bool sensor_take_sample(log_record_t *record) { return ring_local_pop(&samples, record); }

static void report_statistics(uint64_t now)
{
    if (!ready || now - stats_start < 10000000) return;
    if (stdio_usb_connected()) {
        printf("Sampling interval_us count=%" PRIu64 " min=%" PRIu64 " max=%" PRIu64
               " mean=%" PRIu64 " missed=%" PRIu64 " ring_overflows=%" PRIu32 " read_errors=%" PRIu64 "\n",
               intervals.count, intervals.count ? intervals.minimum : 0, intervals.count ? intervals.maximum : 0,
               intervals.count ? intervals.sum / intervals.count : 0, missed, samples.overflows, read_errors);
    }
    stats_start = now;
    interval_stats_reset_window(&intervals);
    missed = read_errors = 0;
    samples.overflows = 0;
}

void sensor_poll(void)
{
    uint64_t now = time_us_64();
    if (waiting_for_reset && now - reset_time >= 2000) {
        sensor_result = bme280_load_calibration(&sensor);
        if (sensor_result == BME280_OK) {
            const bme280_config_t config = {1, 1, 1, 0, 0, 3};
            sensor_result = bme280_configure(&sensor, &config);
            if (sensor_result == BME280_OK) {
                waiting_for_reset = false;
                ready = sampling_timer_start(PICOLOGGER_SAMPLE_RATE_HZ);
                timer_failed = !ready;
                if (ready) stats_start = time_us_64();
            }
        }
        if (sensor_result != BME280_OK && sensor_result != BME280_BUSY) waiting_for_reset = false;
        if (now - reset_time > 100000 && !ready) { waiting_for_reset = false; sensor_result = BME280_BUSY; }
    }
    sampling_tick_t tick;
    if (ready && sampling_timer_take(&tick)) sample_once(&tick);
    now = time_us_64();
    report_statistics(now);
    if (now < next_read) return;
    next_read = now + 1000000;
    if (!stdio_usb_connected()) return;
    if (timer_failed) { puts("Sampling timer initialization failed"); return; }
    if (!have_latest || sensor_result != BME280_OK) {
        printf("BME280 read unavailable (status %d)\n", (int)sensor_result);
        return;
    }
    int32_t temp = latest.values[0];
    uint32_t magnitude = temp < 0 ? (uint32_t)-temp : (uint32_t)temp;
    printf("BME280 T=%s%" PRIu32 ".%02" PRIu32 " C P=%" PRIu32 " Pa RH=%" PRIu32 ".%02" PRIu32 " %%\n",
           temp < 0 ? "-" : "", magnitude / 100, magnitude % 100,
           (uint32_t)latest.values[1], (uint32_t)latest.values[2] / 1024,
           ((uint32_t)latest.values[2] % 1024) * 100 / 1024);
}
