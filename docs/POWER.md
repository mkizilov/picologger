# Low-power operation and current measurement

For BME280 rates of **1–10 Hz**, the Pico firmware selects a 48 MHz system
clock and uses `__wfi()` between foreground work while logging. Above 10 Hz it
uses the original boot system clock (normally 125 MHz) and does not enter this
low-rate WFI path. This is ordinary interrupt sleep, not dormant mode: USB,
the reference timer, ADC and DMA remain active. USB can wake the core and the
CDC command interface remains enabled. The ADC, if enabled, still runs at its
nominal 10 kHz; it can dominate wakeups and power. Actual USB behavior, clocks,
ADC continuity, wake latency and power savings have **not been measured**.

At boot the firmware checks the USB and ADC clock sources and keeps their PLL
running. It fixes `clk_peri` to 48 MHz before I²C and SPI initialization, so
SPI's requested 12.5 MHz may be realized at a lower rate. I²C timing is
derived from `clk_sys` and is reprogrammed to approximately 100 kHz after a
runtime clock change. `status` reports `power=low`, `normal`, `unavailable`, or
`error`, and `clk_sys` as the SDK's configured value, not an independent
frequency measurement. If the expected clock sources are unavailable, the
firmware keeps normal operation and reports `unavailable`. For Pico W, clock
changes and WFI are disabled pending CYW43 validation; low-rate storage batching
still applies. The specified target for this project remains the Pico/RP2040.

Use `stop`, wait for `OK stopped`, then issue `rate <1–50>`. Crossing the 10 Hz
boundary returns `PENDING rate` until the storage core has quiesced, the clock
has changed and I²C has been retimed; wait for `OK rate=...` before `start`.
Clock or I²C retiming failure is reported and prevents a new start until reboot.
An initialization-time clock fault prevents acquisition startup; the watchdog
can then reset the device because the storage worker is not running.
The watchdog remains enabled; a stalled clock transition or storage operation
can cause a reset. Existing flash-setting writes still deliberately pause ADC.

Core 1 normally drains records into a 4 KiB RAM chunk and writes a complete
chunk at a time. At 1–10 Hz, periodic FatFs sync and card-presence checks run
about every **30 seconds**, rather than every five; the worker also lengthens
its idle poll from 1 ms to 10 ms outside active transfers. This can reduce card
and CPU activity but is not a measured energy claim. Rotation and `stop` still
flush and sync. Periodic `f_sync` does **not** flush an incomplete RAM chunk;
at 1 Hz it can hold up to 128 records (about 128 seconds) before filling. The
longer sync interval also increases the potential loss window for submitted
but unsynced data and delays removal detection. FAT/card power-loss guarantees
remain limited as described in `SD_LOGGING.md` and `POWER_LOSS.md`.

## Manual USB power-meter procedure (not performed)

1. Build and flash the Pico UF2 using `FLASHING.md`. With power off, connect the
   BME280 on GP4/GP5 and an expendable FAT32 microSD module on GP10–GP13; wire
   GP26 only with a safe 0–3.3 V source if ADC is enabled. Do not hot-wire pins.
2. Put a suitable **data-capable USB power meter** in series between a stable
   5 V host/supply and the Pico. Confirm its current range, resolution and USB
   data pass-through. The meter measures the whole USB-powered assembly,
   including the Pico regulator, LEDs, sensor and card, not just the RP2040.
3. Open the CDC terminal and record firmware version, `config get`, `status`,
   enabled channels, card model, supply voltage, USB connection state and meter
   details. Saved settings can override the compiled defaults. Compare identical
   wiring, card, supply and enabled channels in each run.
4. Stop logging and wait for acknowledgement. Select 50 Hz and start; after
   settling, record the meter's average and observed peak over at least two
   minutes. Repeat at 10 Hz and 1 Hz, waiting for each pending rate transition
   and start acknowledgement. Confirm `status` reports the expected mode and
   `clk_sys`; issue `status` and `help` while connected to check CDC operation.
5. Repeat with the USB host disconnected only if the meter and supply support
   that setup, and separately with ADC disabled via saved settings and reboot if
   desired. Record both configurations rather than assuming equivalent loads.
   Observe sampling misses, ADC faults, watchdog resets, SD drops and the meter
   during a full 4 KiB write and sync; short peaks may exceed meter bandwidth.
6. Send `stop` and wait for `OK stopped` before powering down. Decode the log and
   check CRCs and sequence gaps. Compare readings and timing only after recording
   their conditions; no value below has been observed by the agent.

| Mode and conditions | USB connected? | ADC enabled? | Average current (mA) | Peak current (mA) | Notes |
| --- | --- | --- | --- | --- | --- |
| 50 Hz, normal clock |  |  |  |  |  |
| 10 Hz, low-rate mode |  |  |  |  |  |
| 1 Hz, low-rate mode |  |  |  |  |  |
| Other / stopped |  |  |  |  |  |
