# PicoLogger design

The target is a Raspberry Pi Pico (RP2040), with no RTOS. The current firmware
blinks its onboard LED, samples the BME280 from timer triggers, captures GP26
ADC blocks by DMA and writes BME280 records to SD. A repeating hardware timer toggles the
Pico LED every 500 ms. On Pico W the timer changes the desired state and the main
loop applies it to CYW43, avoiding wireless-driver calls in interrupt context.
The banner is sent on each USB CDC connection. RP2040 reset information can
distinguish watchdog-related resets, but not reliably identify all other causes.

## Planned data path

Free-running 10 kHz ADC sampling uses one ADC-paced DMA channel and one chained
control channel to fill alternating halves; completed blocks enter a bounded
IRQ-to-main queue. See `docs/ADC_DMA.md` for integrity checks and limitations.
The main loop currently consumes ADC blocks for diagnostics, not storage.
`src/core/ring_buffer` supplies an
SPSC fixed-record queue with caller-owned storage, power-of-two capacity, and
release/acquire publication. A full queue rejects each new push and counts it
modulo 2^32; retries count as further rejected attempts. Initialize it before
enabling interrupts; it supports exactly one producer and one consumer, and
storage and record arguments must not overlap. The atomic indices must be
lock-free: initialization fails if the C implementation cannot guarantee that.
In particular, lock-free behavior with the pinned RP2040 toolchain has not yet
been verified, so do not use it in an ISR until it has been checked. The ring is
not connected to sampling or storage. Physical buffer sizing and recovery from
card errors remain unverified. The BME280 is an I2C sensor; its once-per-second
printing is independent of ADC DMA sampling.

The Pico SDK's RP2040 atomic helpers are not lock-free and can mask interrupts.
The BME280 sampling path instead keeps completed records in a main-loop-only
ring, fed by a single-core timer mailbox; the main loop drains those records
into batches for a core-1 FatFs writer. Its limitations are in
`docs/SAMPLING.md` and `docs/SD_LOGGING.md`.
The available Apple Clang Cortex-M0 cross-target lowered 32-bit C11 atomic
loads/stores to `__atomic_*` helper calls; that is not evidence about the pinned
Arm GNU compiler. Before integrating the queue with RP2040 interrupts, check
that the actual toolchain reports the ring's atomics lock-free and inspect its
generated code for helper calls or interrupt masking. If it cannot meet that
requirement, a separate target-specific design is needed rather than bypassing
the initialization check.

The USB serial command interface will configure and inspect logging without an
RTOS. `src/core/` contains portable logic, `src/hal/` wraps Pico hardware,
`src/drivers/` contains device protocols, and `src/app/` assembles the system.
Host tests compile core code only, independent of the SDK.

The portable binary header and record layout, CRC and streaming CSV decoder are
specified in [docs/LOG_FORMAT.md](docs/LOG_FORMAT.md). The current storage path
encodes BME280 records in v1 format; ADC blocks are not yet stored.

## Measurements

Heartbeat timing/jitter, sampling rate, ring-buffer capacity, SD throughput and power
consumption: **not measured**. No board has been confirmed attached.
