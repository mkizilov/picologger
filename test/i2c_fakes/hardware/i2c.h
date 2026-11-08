#ifndef TEST_HARDWARE_I2C_H
#define TEST_HARDWARE_I2C_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { int unused; } i2c_inst_t;
extern i2c_inst_t fake_i2c0;
#define i2c0 (&fake_i2c0)
unsigned int i2c_init(i2c_inst_t *i2c, unsigned int baudrate);
unsigned int i2c_set_baudrate(i2c_inst_t *i2c, unsigned int baudrate);
int i2c_write_timeout_us(i2c_inst_t *i2c, uint8_t address, const uint8_t *data,
                         size_t length, bool nostop, unsigned int timeout_us);
int i2c_read_timeout_us(i2c_inst_t *i2c, uint8_t address, uint8_t *data,
                        size_t length, bool nostop, unsigned int timeout_us);
#endif
