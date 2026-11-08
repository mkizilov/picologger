#include "i2c_bus.h"

#include "hardware/i2c.h"
#include "pico/error.h"
#include "pico/stdlib.h"

#define BUS_TIMEOUT_US 5000u

void i2c_bus_init(void)
{
    i2c_init(i2c0, 100000);
    gpio_set_function(4, GPIO_FUNC_I2C);
    gpio_set_function(5, GPIO_FUNC_I2C);
    gpio_pull_up(4);
    gpio_pull_up(5);
}

bool i2c_bus_reclock(void)
{
    unsigned int actual = i2c_set_baudrate(i2c0, 100000u);
    return actual >= 90000u && actual <= 100000u;
}

static int read_register(void *context, uint8_t address, uint8_t reg, uint8_t *data, size_t length)
{
    (void)context;
    if (i2c_write_timeout_us(i2c0, address, &reg, 1, true, BUS_TIMEOUT_US) != 1) return -1;
    return i2c_read_timeout_us(i2c0, address, data, length, false, BUS_TIMEOUT_US) == (int)length ? 0 : -1;
}

static int write_register(void *context, uint8_t address, uint8_t reg, uint8_t value)
{
    (void)context;
    uint8_t bytes[] = {reg, value};
    return i2c_write_timeout_us(i2c0, address, bytes, sizeof(bytes), false, BUS_TIMEOUT_US) == 2 ? 0 : -1;
}

bme280_bus_t i2c_bus_interface(void)
{
    bme280_bus_t bus = {NULL, read_register, write_register};
    return bus;
}

int i2c_bus_probe(uint8_t address)
{
    uint8_t byte;
    int result = i2c_read_timeout_us(i2c0, address, &byte, 1, false, BUS_TIMEOUT_US);
    return result == 1 ? 1 : result == PICO_ERROR_GENERIC ? 0 : -1;
}
