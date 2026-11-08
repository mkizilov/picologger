# PicoLogger design

The target is a Raspberry Pi Pico (RP2040), with no RTOS. The current firmware
blinks its onboard LED, samples the BME280 from timer triggers, captures GP26
ADC blocks by DMA and writes BME280 records to SD. A repeating hardware timer toggles the
Pico LED every 500 ms. On Pico W the timer changes the desired state and the main
loop applies it to CYW43, avoiding wireless-driver calls in interrupt context.
The banner is sent on each USB CDC connection. RP2040 reset information can
distinguish watchdog-related resets, but not reliably identify all other causes.

## Dual-core data path

```text
CORE 0                                      CORE 1
timer IRQ -> timestamp mailbox              flash-lockout victim IRQ (control)
ADC/DMA IRQ -> ADC diagnostic queue          storage worker
main: BME280 I2C + USB CDC commands           |  pop BME record; encode CRC
      ADC diagnostics + watchdog feed        |  mount/recover/list/dump
      publish completed BME record --SPSC--> |  FatFs: 4 KiB writes + sync
      control/status <----- SDK queues ----> |  stop/start/health/fault signals
                                             v
                                        SPI1 microSD
hardware inter-core FIFO: SDK launch and flash-lockout control only
```

Core 0 is the sole ring producer; core 1 is the sole consumer and FatFs owner.
Core 1 copies each record out before calling FatFs, so it never holds a ring
slot or a shared lock during a card operation. A slow write cannot directly
wait or lock the acquisition core. The 1024-record ring is bounded: if it fills,
new records are dropped and counted while sampling continues. Shared SRAM/bus
contention, foreground I²C/USB work, flash lockout, ADC faults and finite
buffering can still affect timing or data integrity; there is no measured or
absolute hardware jitter guarantee.

### Ring memory ordering

`src/core/spsc_ring.c` implements the common bounded SPSC algorithm. Only the
producer writes `head`; only the consumer writes `tail`. The producer acquires
`tail` before slot reuse, copies the whole record, then releases `head`. The
consumer acquires `head` before copying a record and releases `tail` only after
the copy. Unsigned counters wrap modulo 2³²; occupancy never exceeds the
power-of-two capacity. A full ring rejects rather than overwrites.

The host adapter uses C11 atomic acquire/release operations and rejects a
non-lock-free implementation. RP2040's SDK C11 atomic fallback may use a
spinlock, so `src/hal/spsc_index_rp2040.c` instead uses aligned 32-bit ARM
`ldr`/`str` and `dmb` with compiler memory clobbers. RP2040 cores share SRAM;
aligned word accesses are indivisible and there is no SRAM data cache to
invalidate. The release barrier orders payload before index publication; the
acquire barrier orders the observed index before payload access. This is an
ARM/GNU compiler implementation contract, not a claim that `volatile` alone or
portable C data races are safe. Neither ring endpoint runs in an ISR, and the
indices have exactly one writer each. Stop-control publication is ordered after
the last producer head update; core 1 drains before acknowledging stop.

Native pthread/ThreadSanitizer tests exercise the C11 equivalent. Both the
pinned Arm GCC firmware build and Clang Cortex-M0 probe compile the adapter;
inspection of the GCC-linked acquire/release functions shows `ldr`/`str` and
`dmb` without atomic helper calls. Actual dual-core ordering remains unverified
on hardware. See `docs/DUAL_CORE.md` for comparison checks.

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
The BME280 path uses the separate target-specific SPSC index adapter above.
The available Apple Clang Cortex-M0 cross-target lowered 32-bit C11 atomic
loads/stores to `__atomic_*` helper calls; that is not evidence about the pinned
Arm GNU compiler. Before integrating the queue with RP2040 interrupts, check
that the actual toolchain reports the ring's atomics lock-free and inspect its
generated code for helper calls or interrupt masking. If it cannot meet that
requirement, a separate target-specific design is needed rather than bypassing
the initialization check.

The USB serial command interface configures and inspects logging without an
RTOS; see `docs/USB_COMMANDS.md` for bounded work and transport limits.
Persistent settings use two separate erase sectors and SDK flash lockout; see
`docs/SETTINGS.md` for the deliberate ADC interruption and remaining risks.
At 1–10 Hz, the Pico uses a guarded 48 MHz system clock and foreground WFI;
`docs/POWER.md` covers clock domains, USB, batching and unmeasured current.
`src/core/` contains portable logic, `src/hal/` wraps Pico hardware,
`src/drivers/` contains device protocols, and `src/app/` assembles the system.
Host tests compile the portable core and selected application/HAL modules with
fakes, independent of the SDK hardware.

The portable binary header and record layout, CRC and streaming CSV decoder are
specified in [docs/LOG_FORMAT.md](docs/LOG_FORMAT.md). The current storage path
encodes BME280 records in v3 format, with a separately synced header sector and
boot/fault metadata; ADC blocks are not yet stored. The boot recovery scanner supports v1–v3,
but cannot guarantee FAT metadata survives arbitrary power loss.

## Measurements

Heartbeat timing/jitter, sampling rate, ring-buffer capacity, SD throughput and power
consumption: **not measured**. No board has been confirmed attached.
