#include "unity.h"
#include "bme280.h"

#include <string.h>

typedef struct {
    uint8_t regs[256];
    uint8_t writes[16], values[16];
    unsigned int write_count;
    bool fail_read, fail_write;
} fake_bus_t;

static fake_bus_t fake;
static bme280_t sensor;

static int fake_read(void *context, uint8_t address, uint8_t reg, uint8_t *out, size_t n)
{
    fake_bus_t *bus = context;
    if (address != 0x76 || bus->fail_read || n > 256u - reg) return -1;
    memcpy(out, bus->regs + reg, n);
    return 0;
}

static int fake_write(void *context, uint8_t address, uint8_t reg, uint8_t value)
{
    fake_bus_t *bus = context;
    if (address != 0x76 || bus->fail_write || bus->write_count >= 16) return -1;
    bus->writes[bus->write_count] = reg;
    bus->values[bus->write_count++] = value;
    bus->regs[reg] = value;
    return 0;
}

static void set16(uint8_t address, int32_t value)
{
    uint16_t encoded = (uint16_t)value;
    fake.regs[address] = (uint8_t)encoded;
    fake.regs[address + 1] = (uint8_t)(encoded >> 8);
}

static void set_raw(uint32_t p, uint32_t t, uint16_t h)
{
    fake.regs[0xf7] = (uint8_t)(p >> 12);
    fake.regs[0xf8] = (uint8_t)(p >> 4);
    fake.regs[0xf9] = (uint8_t)(p << 4);
    fake.regs[0xfa] = (uint8_t)(t >> 12);
    fake.regs[0xfb] = (uint8_t)(t >> 4);
    fake.regs[0xfc] = (uint8_t)(t << 4);
    fake.regs[0xfd] = (uint8_t)(h >> 8);
    fake.regs[0xfe] = (uint8_t)h;
}

void setUp(void)
{
    memset(&fake, 0, sizeof(fake));
    memset(&sensor, 0, sizeof(sensor));
    sensor.bus = (bme280_bus_t){&fake, fake_read, fake_write};
    sensor.address = 0x76;
    fake.regs[0xd0] = 0x60;
    /* Published BMP280 temperature/pressure example, evaluated using Bosch's
     * shared formula. Humidity coefficients are a separate synthetic fixture. */
    const int32_t values[] = {27504, 26435, -1000, 36477, -10685, 3024,
                              2855, 140, -7, 15500, -14600, 6000};
    for (unsigned int i = 0; i < 12; ++i) set16((uint8_t)(0x88 + 2 * i), values[i]);
    fake.regs[0xa1] = 75;
    set16(0xe1, 375);
    fake.regs[0xe3] = 0;
    fake.regs[0xe4] = 18; fake.regs[0xe5] = 0x21; fake.regs[0xe6] = 3; fake.regs[0xe7] = 30;
    set_raw(415148, 519888, 30000);
}

void tearDown(void) {}

static void reference_compensation_from_fake_registers(void)
{
    uint8_t id;
    TEST_ASSERT_EQUAL_INT(BME280_OK, bme280_check_id(&sensor, &id));
    TEST_ASSERT_EQUAL_HEX8(0x60, id);
    TEST_ASSERT_EQUAL_INT(BME280_OK, bme280_soft_reset(&sensor));
    TEST_ASSERT_EQUAL_HEX8(0xe0, fake.writes[0]);
    TEST_ASSERT_EQUAL_HEX8(0xb6, fake.values[0]);
    fake.regs[0xf3] = 1;
    TEST_ASSERT_EQUAL_INT(BME280_BUSY, bme280_load_calibration(&sensor));
    fake.regs[0xf3] = 0;
    TEST_ASSERT_EQUAL_INT(BME280_OK, bme280_load_calibration(&sensor));
    TEST_ASSERT_EQUAL_INT16(289, sensor.calibration.h4);
    TEST_ASSERT_EQUAL_INT16(50, sensor.calibration.h5);
    const bme280_config_t config = {1, 1, 1, 0, 0, 3};
    TEST_ASSERT_EQUAL_INT(BME280_OK, bme280_configure(&sensor, &config));
    TEST_ASSERT_EQUAL_HEX8(0xf2, fake.writes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, fake.values[1]);
    TEST_ASSERT_EQUAL_HEX8(0xf5, fake.writes[2]);
    TEST_ASSERT_EQUAL_HEX8(0xf4, fake.writes[3]);
    TEST_ASSERT_EQUAL_HEX8(0x27, fake.values[3]);
    bme280_raw_t raw;
    TEST_ASSERT_EQUAL_INT(BME280_OK, bme280_read_raw(&sensor, &raw));
    TEST_ASSERT_EQUAL_UINT32(519888, raw.temperature);
    TEST_ASSERT_EQUAL_UINT32(415148, raw.pressure);
    TEST_ASSERT_EQUAL_UINT32(30000, raw.humidity);
    bme280_reading_t out;
    TEST_ASSERT_EQUAL_INT(BME280_OK, bme280_read(&sensor, &out));
    TEST_ASSERT_EQUAL_INT32(2508, out.temperature_centi_c);
    TEST_ASSERT_EQUAL_UINT32(25767233, out.pressure_pa_q24_8);
    TEST_ASSERT_EQUAL_UINT32(67367, out.humidity_rh_q22_10);
}

static void errors_and_configuration(void)
{
    uint8_t id;
    fake.regs[0xd0] = 0x58;
    TEST_ASSERT_EQUAL_INT(BME280_BAD_ID, bme280_check_id(&sensor, &id));
    TEST_ASSERT_EQUAL_HEX8(0x58, id);
    fake.fail_read = true;
    TEST_ASSERT_EQUAL_INT(BME280_BUS_ERROR, bme280_check_id(&sensor, &id));
    fake.fail_read = false;
    fake.fail_write = true;
    TEST_ASSERT_EQUAL_INT(BME280_BUS_ERROR, bme280_soft_reset(&sensor));
    fake.fail_write = false;
    set16(0x8e, 0);
    TEST_ASSERT_EQUAL_INT(BME280_BAD_CALIBRATION, bme280_load_calibration(&sensor));
    const bme280_config_t invalid = {6, 1, 1, 0, 0, 3};
    TEST_ASSERT_EQUAL_INT(BME280_BAD_ARGUMENT, bme280_configure(&sensor, &invalid));
    const bme280_config_t config = {2, 3, 4, 2, 5, 1};
    fake.regs[0xf4] = 0x27;
    fake.regs[0xf3] = 8;
    TEST_ASSERT_EQUAL_INT(BME280_BUSY, bme280_configure(&sensor, &config));
    TEST_ASSERT_EQUAL_HEX8(0x24, fake.regs[0xf4]);
    fake.regs[0xf3] = 0;
    TEST_ASSERT_EQUAL_INT(BME280_OK, bme280_configure(&sensor, &config));
    TEST_ASSERT_EQUAL_HEX8(4, fake.regs[0xf2]);
    TEST_ASSERT_EQUAL_HEX8(0xa8, fake.regs[0xf5]);
    TEST_ASSERT_EQUAL_HEX8(0x4d, fake.regs[0xf4]);
}

static void signed_humidity_and_invalid_data(void)
{
    fake.regs[0xe4] = 0xfe; fake.regs[0xe5] = 0xf1; fake.regs[0xe6] = 0xff; fake.regs[0xe7] = 0xfe;
    TEST_ASSERT_EQUAL_INT(BME280_OK, bme280_load_calibration(&sensor));
    TEST_ASSERT_EQUAL_INT16(-31, sensor.calibration.h4);
    TEST_ASSERT_EQUAL_INT16(-1, sensor.calibration.h5);
    TEST_ASSERT_EQUAL_INT8(-2, sensor.calibration.h6);
    bme280_reading_t result;
    TEST_ASSERT_EQUAL_INT(BME280_NO_DATA, bme280_read(&sensor, &result));
    const bme280_config_t config = {1, 1, 1, 0, 0, 3};
    TEST_ASSERT_EQUAL_INT(BME280_OK, bme280_configure(&sensor, &config));
    fake.fail_read = true;
    TEST_ASSERT_EQUAL_INT(BME280_BUS_ERROR, bme280_read(&sensor, &result));
}

static void humidity_boundaries_and_midpoint(void)
{
    TEST_ASSERT_EQUAL_INT(BME280_OK, bme280_load_calibration(&sensor));
    const bme280_config_t config = {1, 1, 1, 0, 0, 3};
    TEST_ASSERT_EQUAL_INT(BME280_OK, bme280_configure(&sensor, &config));
    bme280_reading_t out;
    set_raw(415148, 519888, 0);
    TEST_ASSERT_EQUAL_INT(BME280_OK, bme280_read(&sensor, &out));
    TEST_ASSERT_EQUAL_UINT32(0, out.humidity_rh_q22_10);
    set_raw(415148, 519888, 32768);
    TEST_ASSERT_EQUAL_INT(BME280_OK, bme280_read(&sensor, &out));
    set_raw(415148, 519888, 65535);
    TEST_ASSERT_EQUAL_INT(BME280_OK, bme280_read(&sensor, &out));
    TEST_ASSERT_EQUAL_UINT32(102400, out.humidity_rh_q22_10);
}

static uint32_t random_value(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void arbitrary_calibration_does_not_overflow(void)
{
    uint32_t state = 1234;
    const bme280_config_t config = {1, 1, 1, 0, 0, 3};
    TEST_ASSERT_EQUAL_INT(BME280_OK, bme280_configure(&sensor, &config));
    for (unsigned int n = 0; n < 20000; ++n) {
        for (unsigned int i = 0x88; i <= 0xa1; ++i) fake.regs[i] = (uint8_t)(random_value(&state) >> 24);
        for (unsigned int i = 0xe1; i <= 0xe7; ++i) fake.regs[i] = (uint8_t)(random_value(&state) >> 24);
        bme280_result_t loaded = bme280_load_calibration(&sensor);
        if (loaded != BME280_OK) continue;
        set_raw(random_value(&state) & 0xfffff, random_value(&state) & 0xfffff,
                (uint16_t)random_value(&state));
        bme280_reading_t out;
        bme280_result_t result = bme280_read(&sensor, &out);
        TEST_ASSERT_TRUE(result == BME280_OK || result == BME280_BAD_CALIBRATION);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(reference_compensation_from_fake_registers);
    RUN_TEST(errors_and_configuration);
    RUN_TEST(signed_humidity_and_invalid_data);
    RUN_TEST(humidity_boundaries_and_midpoint);
    RUN_TEST(arbitrary_calibration_does_not_overflow);
    return UNITY_END();
}
