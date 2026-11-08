#ifndef PICOLOGGER_I2C_BUS_H
#define PICOLOGGER_I2C_BUS_H

#include "bme280.h"

void i2c_bus_init(void);
bme280_bus_t i2c_bus_interface(void);
/* 1 = acknowledged, 0 = NACK, -1 = bus timeout/error. */
int i2c_bus_probe(uint8_t address);

#endif
