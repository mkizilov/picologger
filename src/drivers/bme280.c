#include "bme280.h"

#define REG_ID 0xd0
#define REG_RESET 0xe0
#define REG_STATUS 0xf3
#define REG_CTRL_HUM 0xf2
#define REG_CTRL_MEAS 0xf4
#define REG_CONFIG 0xf5
#define REG_DATA 0xf7

static bool valid(const bme280_t *s)
{
    return s && s->bus.read && s->bus.write && (s->address == 0x76 || s->address == 0x77);
}

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static int16_t signed16(uint16_t x) { return x <= 32767 ? (int16_t)x : (int16_t)((int32_t)x - 65536); }
static int16_t signed12(uint16_t x) { return x < 2048 ? (int16_t)x : (int16_t)((int32_t)x - 4096); }

/* The datasheet's signed right shifts round toward negative infinity. */
static int64_t shift(int64_t x, unsigned int bits)
{
    int64_t divisor = INT64_C(1) << bits;
    return x >= 0 ? x / divisor : -1 - (-1 - x) / divisor;
}

bme280_result_t bme280_check_id(bme280_t *s, uint8_t *id)
{
    if (!valid(s) || !id) return BME280_BAD_ARGUMENT;
    if (s->bus.read(s->bus.context, s->address, REG_ID, id, 1)) return BME280_BUS_ERROR;
    return *id == 0x60 ? BME280_OK : BME280_BAD_ID;
}

bme280_result_t bme280_soft_reset(bme280_t *s)
{
    if (!valid(s)) return BME280_BAD_ARGUMENT;
    s->calibrated = false;
    s->configured = false;
    return s->bus.write(s->bus.context, s->address, REG_RESET, 0xb6) ? BME280_BUS_ERROR : BME280_OK;
}

bme280_result_t bme280_load_calibration(bme280_t *s)
{
    if (!valid(s)) return BME280_BAD_ARGUMENT;
    s->calibrated = false;
    uint8_t status, a[26], h[7];
    if (s->bus.read(s->bus.context, s->address, REG_STATUS, &status, 1)) return BME280_BUS_ERROR;
    if (status & 1u) return BME280_BUSY;
    if (s->bus.read(s->bus.context, s->address, 0x88, a, sizeof(a)) ||
        s->bus.read(s->bus.context, s->address, 0xe1, h, sizeof(h))) return BME280_BUS_ERROR;
    bme280_calibration_t c = {0};
    c.t1 = u16(a); c.t2 = signed16(u16(a + 2)); c.t3 = signed16(u16(a + 4));
    c.p1 = u16(a + 6); c.p2 = signed16(u16(a + 8)); c.p3 = signed16(u16(a + 10));
    c.p4 = signed16(u16(a + 12)); c.p5 = signed16(u16(a + 14)); c.p6 = signed16(u16(a + 16));
    c.p7 = signed16(u16(a + 18)); c.p8 = signed16(u16(a + 20)); c.p9 = signed16(u16(a + 22));
    c.h1 = a[25]; c.h2 = signed16(u16(h)); c.h3 = h[2];
    c.h4 = signed12((uint16_t)((uint16_t)h[3] * 16u + (h[4] & 15u)));
    c.h5 = signed12((uint16_t)((uint16_t)h[5] * 16u + (h[4] >> 4)));
    c.h6 = h[6] < 128 ? (int8_t)h[6] : (int8_t)((int16_t)h[6] - 256);
    if (!c.t1 || !c.p1) return BME280_BAD_CALIBRATION;
    s->calibration = c;
    s->calibrated = true;
    return BME280_OK;
}

bme280_result_t bme280_configure(bme280_t *s, const bme280_config_t *cfg)
{
    if (!valid(s) || !cfg || cfg->temperature > 5 || cfg->pressure > 5 ||
        cfg->humidity > 5 || cfg->filter > 4 || cfg->standby > 7 ||
        (cfg->mode != 0 && cfg->mode != 1 && cfg->mode != 3)) return BME280_BAD_ARGUMENT;
    s->configured = false;
    uint8_t ctrl, status;
    if (s->bus.read(s->bus.context, s->address, REG_CTRL_MEAS, &ctrl, 1)) return BME280_BUS_ERROR;
    if ((ctrl & 3u) && s->bus.write(s->bus.context, s->address, REG_CTRL_MEAS, ctrl & 0xfcu)) return BME280_BUS_ERROR;
    if (s->bus.read(s->bus.context, s->address, REG_STATUS, &status, 1)) return BME280_BUS_ERROR;
    if (status & 9u) return BME280_BUSY;
    if (s->bus.write(s->bus.context, s->address, REG_CTRL_HUM, cfg->humidity) ||
        s->bus.write(s->bus.context, s->address, REG_CONFIG, (uint8_t)(cfg->standby << 5 | cfg->filter << 2)) ||
        s->bus.write(s->bus.context, s->address, REG_CTRL_MEAS,
                     (uint8_t)(cfg->temperature << 5 | cfg->pressure << 2 | cfg->mode))) return BME280_BUS_ERROR;
    s->osrs_temperature = cfg->temperature;
    s->osrs_pressure = cfg->pressure;
    s->osrs_humidity = cfg->humidity;
    s->configured = true;
    return BME280_OK;
}

static bme280_result_t compensate(const bme280_calibration_t *c, uint32_t t, uint32_t p,
                                   uint32_t h, bme280_reading_t *out)
{
    int64_t v1 = shift(((int64_t)(t >> 3) - (int64_t)c->t1 * 2) * c->t2, 11);
    int64_t d = (int64_t)(t >> 4) - c->t1;
    int64_t v2 = shift(shift(d * d, 12) * c->t3, 14);
    int64_t fine = v1 + v2;
    int64_t temp = shift(fine * 5 + 128, 8);
    if (temp < -4000 || temp > 8500) return BME280_BAD_CALIBRATION;
    v1 = fine - 128000;
    v2 = v1 * v1 * c->p6 + v1 * c->p5 * INT64_C(131072) + (int64_t)c->p4 * INT64_C(34359738368);
    v1 = shift(v1 * v1 * c->p3, 8) + v1 * c->p2 * 4096;
    v1 += INT64_C(140737488355328);
    if (v1 > INT64_MAX / c->p1 || v1 < INT64_MIN / c->p1) return BME280_BAD_CALIBRATION;
    v1 = shift(v1 * c->p1, 33);
    if (v1 <= 0) return BME280_BAD_CALIBRATION;
    int64_t numerator = (INT64_C(1048576) - p) * INT64_C(2147483648) - v2;
    if (numerator > INT64_MAX / 3125 || numerator < INT64_MIN / 3125) return BME280_BAD_CALIBRATION;
    int64_t pressure = numerator * 3125 / v1;
    if (pressure < 0 || pressure > INT64_C(200000) * 65536) return BME280_BAD_CALIBRATION;
    v1 = shift((int64_t)c->p9 * shift(pressure, 13) * shift(pressure, 13), 25);
    v2 = shift((int64_t)c->p8 * pressure, 19);
    pressure = shift(pressure + v1 + v2, 8) + (int64_t)c->p7 * 16;
    if (pressure < 0 || pressure > UINT32_MAX) return BME280_BAD_CALIBRATION;
    int64_t humidity = fine - 76800;
    int64_t factor = shift((int64_t)h * 16384 - (int64_t)c->h4 * 1048576 - c->h5 * humidity + 16384, 15);
    int64_t scale = shift(shift(humidity * c->h6, 10) * (shift(humidity * c->h3, 11) + 32768), 10) + 2097152;
    humidity = factor * shift(scale * c->h2 + 8192, 14);
    if (humidity < 0) humidity = 0;
    if (humidity > INT64_C(1) << 40) return BME280_BAD_CALIBRATION;
    humidity -= shift(shift(shift(humidity, 15) * shift(humidity, 15), 7) * c->h1, 4);
    if (humidity < 0) humidity = 0;
    if (humidity > 419430400) humidity = 419430400;
    out->temperature_centi_c = (int32_t)temp;
    out->pressure_pa_q24_8 = (uint32_t)pressure;
    out->humidity_rh_q22_10 = (uint32_t)shift(humidity, 12);
    return BME280_OK;
}

bme280_result_t bme280_read_raw(bme280_t *s, bme280_raw_t *raw)
{
    if (!valid(s) || !raw) return BME280_BAD_ARGUMENT;
    uint8_t a[8];
    if (s->bus.read(s->bus.context, s->address, REG_DATA, a, sizeof(a))) return BME280_BUS_ERROR;
    bme280_raw_t decoded = {
        .pressure = (uint32_t)a[0] << 12 | (uint32_t)a[1] << 4 | a[2] >> 4,
        .temperature = (uint32_t)a[3] << 12 | (uint32_t)a[4] << 4 | a[5] >> 4,
        .humidity = (uint16_t)((uint16_t)a[6] << 8 | a[7])
    };
    *raw = decoded;
    return BME280_OK;
}

bme280_result_t bme280_read(bme280_t *s, bme280_reading_t *reading)
{
    if (!valid(s) || !reading) return BME280_BAD_ARGUMENT;
    if (!s->calibrated) return BME280_BAD_CALIBRATION;
    if (!s->configured || !s->osrs_temperature || !s->osrs_pressure || !s->osrs_humidity) return BME280_NO_DATA;
    bme280_raw_t raw;
    bme280_result_t result = bme280_read_raw(s, &raw);
    if (result != BME280_OK) return result;
    return compensate(&s->calibration, raw.temperature, raw.pressure, raw.humidity, reading);
}
