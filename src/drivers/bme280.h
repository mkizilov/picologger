#ifndef PICOLOGGER_BME280_H
#define PICOLOGGER_BME280_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *context;
    int (*read)(void *context, uint8_t address, uint8_t reg, uint8_t *data, size_t length);
    int (*write)(void *context, uint8_t address, uint8_t reg, uint8_t value);
} bme280_bus_t;

typedef enum {
    BME280_OK = 0, BME280_BUS_ERROR, BME280_BAD_ID, BME280_BUSY,
    BME280_BAD_ARGUMENT, BME280_BAD_CALIBRATION, BME280_NO_DATA
} bme280_result_t;

typedef struct {
    uint16_t t1, p1;
    int16_t t2, t3, p2, p3, p4, p5, p6, p7, p8, p9;
    uint8_t h1, h3;
    int16_t h2, h4, h5;
    int8_t h6;
} bme280_calibration_t;

typedef struct {
    bme280_bus_t bus;
    uint8_t address;
    bool calibrated;
    bool configured;
    uint8_t osrs_temperature, osrs_pressure, osrs_humidity;
    bme280_calibration_t calibration;
} bme280_t;

/* Register encodings: oversampling 0..5 (off, 1x..16x), filter 0..4,
 * standby 0..7; mode 0=sleep, 1=forced, 3=normal. */
typedef struct {
    uint8_t temperature, pressure, humidity, filter, standby, mode;
} bme280_config_t;

typedef struct {
    int32_t temperature_centi_c;
    uint32_t pressure_pa_q24_8;
    uint32_t humidity_rh_q22_10;
} bme280_reading_t;

typedef struct {
    uint32_t temperature, pressure;
    uint16_t humidity;
} bme280_raw_t;

bme280_result_t bme280_check_id(bme280_t *sensor, uint8_t *observed_id);
bme280_result_t bme280_soft_reset(bme280_t *sensor);
/* Call at least 2 ms after reset; returns BUSY while NVM copy is underway. */
bme280_result_t bme280_load_calibration(bme280_t *sensor);
/* Returns BUSY until the device has entered sleep before configuration. */
bme280_result_t bme280_configure(bme280_t *sensor, const bme280_config_t *config);
/* Raw fields are returned as 20-bit temperature/pressure and 16-bit humidity. */
bme280_result_t bme280_read_raw(bme280_t *sensor, bme280_raw_t *raw);
/* Wait for the first conversion before reading. All three channels must be enabled. */
bme280_result_t bme280_read(bme280_t *sensor, bme280_reading_t *reading);

#endif
