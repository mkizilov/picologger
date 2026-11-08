# PicoLogger design

The target is a Raspberry Pi Pico (RP2040), with no RTOS. The current firmware
blinks its onboard LED, scans I²C, samples the BME280 from timer triggers and prints readings over USB serial; it
does not yet sample the ADC or write storage. A repeating hardware timer toggles the
Pico LED every 500 ms. On Pico W the timer changes the desired state and the main
loop applies it to CYW43, avoiding wireless-driver calls in interrupt context.
The banner is sent on each USB CDC connection. RP2040 reset information can
distinguish watchdog-related resets, but not reliably identify all other causes.

## Planned data path

Timer- and DMA-driven sampling will place ADC samples into a bounded ring
buffer. The main loop will drain completed data to the microSD card, keeping
storage latency away from the sampling path. `src/core/ring_buffer` supplies an
SPSC fixed-record queue with caller-owned storage, power-of-two capacity, and
release/acquire publication. A full queue rejects each new push and counts it
modulo 2^32; retries count as further rejected attempts. Initialize it before
enabling interrupts; it supports exactly one producer and one consumer, and
storage and record arguments must not overlap. The atomic indices must be
lock-free: initialization fails if the C implementation cannot guarantee that.
In particular, lock-free behavior with the pinned RP2040 toolchain has not yet
been verified, so do not use it in an ISR until it has been checked. The ring is
not yet connected to sampling or storage. Buffer sizing and recovery from card
errors remain design work. The BME280 is an I2C sensor; its current once-per-second
printing is independent of the planned ADC DMA sampling and log storage.

The Pico SDK's RP2040 atomic helpers are not lock-free and can mask interrupts.
The BME280 sampling path instead keeps completed records in a main-loop-only
ring, fed by a single-core timer mailbox; it drains those records for display,
not SD. Its limitations and timing measurement are in `docs/SAMPLING.md`.
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
specified in [docs/LOG_FORMAT.md](docs/LOG_FORMAT.md). They are not yet wired
to the firmware's sampling or storage paths.

## Measurements

Heartbeat timing/jitter, sampling rate, ring-buffer capacity, SD throughput and power
consumption: **not measured**. No board has been confirmed attached.
