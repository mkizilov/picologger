# PicoLogger design

The target is a Raspberry Pi Pico (RP2040), with no RTOS. The current firmware
only blinks its onboard LED and emits a startup message over USB serial; it does
not yet sample sensors or write storage. The first message can be missed when a
terminal connects after boot.

## Planned data path

Timer- and DMA-driven sampling will place ADC samples into a bounded ring
buffer. The main loop will drain completed data to the microSD card, keeping
storage latency away from the sampling path. Overflow handling, buffer sizing,
and recovery from card errors remain design work. The BME280 is an I2C sensor;
its slower measurements need scheduling separately from ADC DMA sampling.

The USB serial command interface will configure and inspect logging without an
RTOS. `src/core/` contains portable logic, `src/hal/` wraps Pico hardware,
`src/drivers/` contains device protocols, and `src/app/` assembles the system.
Host tests compile core code only, independent of the SDK.

## Measurements

Sampling rate, timing jitter, ring-buffer capacity, SD throughput and power
consumption: **not measured**. No board has been confirmed attached.
