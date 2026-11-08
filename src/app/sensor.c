#include "sensor.h"

#include <inttypes.h>
#include <stdio.h>

#include "bme280.h"
#include "i2c_bus.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

static bool found[128];
static unsigned int scan_errors;
static uint8_t ids[2];
static bme280_result_t id_results[2];
static bool id_checked[2];
static bme280_t sensor;
static bme280_result_t sensor_result = BME280_NO_DATA;
static bool waiting_for_reset, ready;
static uint64_t reset_time, next_read;

void sensor_start(void)
{
    i2c_bus_init();
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
}

void sensor_poll(void)
{
    uint64_t now = time_us_64();
    if (waiting_for_reset && now - reset_time >= 2000) {
        sensor_result = bme280_load_calibration(&sensor);
        if (sensor_result == BME280_OK) {
            const bme280_config_t config = {1, 1, 1, 0, 0, 3};
            sensor_result = bme280_configure(&sensor, &config);
            if (sensor_result == BME280_OK) { waiting_for_reset = false; ready = true; }
        }
        if (sensor_result != BME280_OK && sensor_result != BME280_BUSY) waiting_for_reset = false;
        if (now - reset_time > 100000 && !ready) { waiting_for_reset = false; sensor_result = BME280_BUSY; }
    }
    if (now < next_read) return;
    next_read = now + 1000000;
    if (!stdio_usb_connected()) return;
    bme280_reading_t reading;
    bme280_result_t result = ready ? bme280_read(&sensor, &reading) : sensor_result;
    if (result != BME280_OK) {
        printf("BME280 read unavailable (status %d)\n", (int)result);
        return;
    }
    int32_t temp = reading.temperature_centi_c;
    uint32_t magnitude = temp < 0 ? (uint32_t)-temp : (uint32_t)temp;
    printf("BME280 T=%s%" PRIu32 ".%02" PRIu32 " C P=%" PRIu32 " Pa RH=%" PRIu32 ".%02" PRIu32 " %%\n",
           temp < 0 ? "-" : "", magnitude / 100, magnitude % 100,
           reading.pressure_pa_q24_8 / 256, reading.humidity_rh_q22_10 / 1024,
           (reading.humidity_rh_q22_10 % 1024) * 100 / 1024);
}
