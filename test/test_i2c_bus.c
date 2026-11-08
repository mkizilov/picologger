#include "unity.h"
#include "i2c_bus.h"
#include "hardware/i2c.h"
#include "pico/error.h"
#include "pico/stdlib.h"

i2c_inst_t fake_i2c0;
static int write_result, read_result;
static uint8_t written[2], used_address;
static size_t write_length, read_length;
static bool write_nostop, read_nostop;
static unsigned int baud, pin_mask, pullups, write_calls, read_calls;
static unsigned int reclock_result;

unsigned int i2c_init(i2c_inst_t *i2c, unsigned int rate)
{
    TEST_ASSERT_EQUAL_PTR(i2c0, i2c);
    baud = rate;
    return rate;
}
unsigned int i2c_set_baudrate(i2c_inst_t *i2c, unsigned int rate)
{
    TEST_ASSERT_EQUAL_PTR(i2c0, i2c);
    baud = rate;
    return reclock_result;
}
void gpio_set_function(unsigned int pin, unsigned int function)
{
    TEST_ASSERT_EQUAL_INT(GPIO_FUNC_I2C, function);
    pin_mask |= 1u << pin;
}
void gpio_pull_up(unsigned int pin) { pullups |= 1u << pin; }
int i2c_write_timeout_us(i2c_inst_t *i2c, uint8_t address, const uint8_t *data,
                         size_t length, bool nostop, unsigned int timeout)
{
    TEST_ASSERT_EQUAL_PTR(i2c0, i2c);
    TEST_ASSERT_EQUAL_UINT(5000, timeout);
    used_address = address; write_length = length; write_nostop = nostop; ++write_calls;
    for (size_t i = 0; i < length && i < sizeof(written); ++i) written[i] = data[i];
    return write_result;
}
int i2c_read_timeout_us(i2c_inst_t *i2c, uint8_t address, uint8_t *data,
                        size_t length, bool nostop, unsigned int timeout)
{
    TEST_ASSERT_EQUAL_PTR(i2c0, i2c);
    TEST_ASSERT_EQUAL_UINT(5000, timeout);
    used_address = address; read_length = length; read_nostop = nostop; ++read_calls;
    if (read_result > 0) data[0] = 0x60;
    return read_result;
}
void setUp(void)
{
    baud = pin_mask = pullups = write_calls = read_calls = 0;
    write_result = 1; read_result = 1;
    reclock_result = 100000;
}
void tearDown(void) {}

static void configures_i2c0_and_pins(void)
{
    i2c_bus_init();
    TEST_ASSERT_EQUAL_UINT(100000, baud);
    TEST_ASSERT_EQUAL_UINT((1u << 4) | (1u << 5), pin_mask);
    TEST_ASSERT_EQUAL_UINT(pin_mask, pullups);
    TEST_ASSERT_TRUE(i2c_bus_reclock());
    TEST_ASSERT_EQUAL_UINT(100000, baud);
    reclock_result = 0;
    TEST_ASSERT_FALSE(i2c_bus_reclock());
}

static void register_access_and_failures(void)
{
    bme280_bus_t bus = i2c_bus_interface();
    uint8_t value = 0;
    TEST_ASSERT_EQUAL_INT(0, bus.read(bus.context, 0x76, 0xd0, &value, 1));
    TEST_ASSERT_EQUAL_UINT(0x60, value);
    TEST_ASSERT_EQUAL_UINT(0x76, used_address);
    TEST_ASSERT_EQUAL_UINT(1, write_length);
    TEST_ASSERT_EQUAL_UINT(0xd0, written[0]);
    TEST_ASSERT_TRUE(write_nostop);
    TEST_ASSERT_FALSE(read_nostop);
    TEST_ASSERT_EQUAL_UINT(1, read_length);
    write_result = 2;
    TEST_ASSERT_EQUAL_INT(0, bus.write(bus.context, 0x77, 0xe0, 0xb6));
    TEST_ASSERT_EQUAL_UINT(0x77, used_address);
    TEST_ASSERT_EQUAL_UINT(2, write_length);
    TEST_ASSERT_EQUAL_UINT(0xe0, written[0]);
    TEST_ASSERT_EQUAL_UINT(0xb6, written[1]);
    TEST_ASSERT_FALSE(write_nostop);
    write_result = PICO_ERROR_TIMEOUT;
    unsigned int reads_before = read_calls;
    TEST_ASSERT_EQUAL_INT(-1, bus.read(bus.context, 0x76, 0xd0, &value, 1));
    TEST_ASSERT_EQUAL_UINT(reads_before, read_calls);
    TEST_ASSERT_EQUAL_INT(-1, bus.write(bus.context, 0x76, 0xe0, 0xb6));
    write_result = 1;
    read_result = PICO_ERROR_TIMEOUT;
    TEST_ASSERT_EQUAL_INT(-1, bus.read(bus.context, 0x76, 0xd0, &value, 1));
}

static void scan_distinguishes_ack_nack_and_timeout(void)
{
    TEST_ASSERT_EQUAL_INT(1, i2c_bus_probe(0x76));
    read_result = PICO_ERROR_GENERIC;
    TEST_ASSERT_EQUAL_INT(0, i2c_bus_probe(0x77));
    read_result = PICO_ERROR_TIMEOUT;
    TEST_ASSERT_EQUAL_INT(-1, i2c_bus_probe(0x20));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(configures_i2c0_and_pins);
    RUN_TEST(register_access_and_failures);
    RUN_TEST(scan_distinguishes_ack_nack_and_timeout);
    return UNITY_END();
}
