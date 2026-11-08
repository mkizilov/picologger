# Timer-driven BME280 sampling

The default is 50 Hz. Configure an integer rate from 1 through 50 Hz at CMake
configure time, or use the USB `rate` command after `stop` (see
`USB_COMMANDS.md`). The temporary runtime value resets at reboot to the saved
setting, or the compiled default if no valid setting exists. For example:

```sh
cmake --preset firmware -DPICOLOGGER_SAMPLE_RATE_HZ=25
cmake --build --preset firmware
```

The repeating timer period is `floor(1000000 / rate_hz)` microseconds; at 50 Hz
it is exactly 20,000 µs. The BME280 remains in normal mode with all three
channels at 1× oversampling and 0.5 ms standby. The 50 Hz limit leaves nominal
headroom for its approximately 9.3 ms maximum 1× conversion time plus standby;
the actual sensor rate, jitter and throughput have **not been measured**.

The timer callback records a 64-bit microsecond timestamp and increments a
sequence in a single-core, RP2040-specific volatile mailbox. Its publication
counter protects against torn snapshots of the two 32-bit timestamp halves.
The interrupt performs no I²C, formatting, ring access or GPIO pulse. Both the
default alarm-pool interrupt and foreground consumer must run on the same core;
this mailbox is not safe for another core or arbitrary host threads. The
sequence comparison assumes fewer than 2^32 timer callbacks between polls.
Only the latest pending trigger is retained; skipped callback sequences are
counted as `missed` rather than replayed with stale timestamps.

The main loop raises GP15 immediately before the BME280 burst read and integer
compensation and lowers it afterward, even if the read fails. A successful
sample is published by core 0 into a 1024-record SPSC ring and drained by the
core-1 storage worker. Record fields currently
hold trigger sequence, trigger timestamp, temperature in centi-°C, pressure in
whole Pa and humidity in %RH × 1024. The latest reading also feeds the
once-per-second display; see `SD_LOGGING.md` for storage and loss limitations.

The original C11 SPSC ring remains unchanged for contexts where its atomics
are actually lock-free. On RP2040 the Pico SDK's Cortex-M0 atomic helpers may
take a spinlock with interrupts masked and `atomic_is_lock_free` reports false;
the sampling path uses a separate RP2040 SPSC adapter with aligned word accesses
and explicit barriers. The timer ISR never touches the ring. Overflow is
reported; sustained storage backpressure can fill it and cause dropped records.
See `DUAL_CORE.md` for ordering and measurement limitations.

Every approximately ten seconds while the USB CDC console is connected, the
firmware prints `Sampling interval_us count=... min=... max=... mean=...`
along with `missed`, `ring_overflows`, and `read_errors`. The min, max and
integer mean are intervals between **processed ISR timestamps**, including a
long interval where intervening triggers were missed; the first trigger has no
interval. The preceding timestamp is retained across window boundaries. An
empty window reports zero for min/max/mean. Counters reset each window even
when USB is disconnected. These are not independent measurements of ideal timer
deadlines, nor do they count timer events the hardware/SDK never dispatched.

## Manual checklist (not performed)

1. With power off, wire the BME280 as in `BME280.md` (GP4 SDA, GP5 SCL, 3.3 V
   and common ground). Connect a 3.3 V-compatible logic analyzer input to GP15
   and analyzer ground to Pico GND. Leave unrelated I²C devices disconnected;
   the boot scan performs reads.
2. Build and flash the appropriate Pico/Pico W UF2 as in `FLASHING.md`, using
   the default or selected sampling rate. Open the USB console, check the I²C
   scan and successful BME280 selection, then capture GP15.
3. At 50 Hz expect rising edges roughly 20 ms apart. Each high pulse spans the
   main-loop I²C read plus compensation; pulse width is sample processing time,
   not the hardware timer's ISR duration. Variation in rising-edge spacing
   reflects end-to-end scheduling jitter; missed triggers can produce longer
   gaps. Actual pulse widths and jitter remain unmeasured.
4. After ten seconds compare the printed interval min/max/mean and missed count
   with the analyzer trace. They measure ISR timestamp spacing, whereas GP15
   includes foreground scheduling latency, so the two are not identical.
5. If there are no pulses, check the reported timer/sensor error, chip ID and
   I²C wiring per `BME280.md`. Do not infer success from an idle GP15 signal.
