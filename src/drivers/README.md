# Drivers

`bme280.c` is a portable I²C BME280 driver; it only accesses registers through
the injected bus callbacks. See `docs/BME280.md` for configuration, reference
vectors and hardware checks. SD access uses the pinned external FatFs/SPI
library through `src/hal/sd_storage.c`; see `docs/SD_LOGGING.md`.
