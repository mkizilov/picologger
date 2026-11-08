# Drivers

`bme280.c` is a portable I²C BME280 driver; it only accesses registers through
the injected bus callbacks. See `docs/BME280.md` for configuration, reference
vectors and hardware checks. No SD driver is implemented yet.
