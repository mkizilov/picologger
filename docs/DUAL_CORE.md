# Dual-core sampling and storage

`DESIGN.md` contains the core assignment and SPSC memory-ordering argument.
Before this change (`bac618a`), SD/FatFs was already on core 1, but core 0
drained a 256-record local ring, encoded records and transferred four batches
through shared buffers. Now core 0 only publishes complete BME280 records to a
1024-record shared SPSC ring; core 1 pops and encodes them before writing. The
RP2040 hardware FIFO remains reserved for SDK launch/flash-lockout control;
application queues carry control/status messages, not sample payloads.

The ring rejects new records when full and the producer counts those drops.
Its high-water value is a conservative producer-side snapshot: a concurrent
consumer advance can make it overstate the instantaneous occupancy. At a
nominal 50 records/s, 1024 empty slots represent roughly 20.48 seconds of
capacity if nothing drains them; that arithmetic is not a measured guarantee.
The actual target RAM footprint, SD stall distribution and achievable jitter are
unmeasured. ADC DMA blocks remain diagnostics-only and are not placed in this
BME record ring.

The target index adapter relies on aligned indivisible 32-bit RP2040 SRAM
accesses, `dmb` and GNU compiler memory clobbers; it does not call C11 atomic
fallback helpers or mask interrupts. The host adapter uses actual C11 atomics.
Native stress and ThreadSanitizer tests validate the host equivalent, not the
physical board or the pinned Arm GCC toolchain. Slow card I/O does not directly
hold a producer lock, but shared bus contention, CPU work, watchdog policy,
flash operations and finite buffers still prevent a claim of zero interference.

Core 0 is the only watchdog feeder: if it stalls, no feed occurs. Core 1 must
advance its heartbeat and drain the ring even while SD is offline; while online
and recording, accepted-record and rate-appropriate physical-write progress are
also required. Stalls can reset the device; stopped
channels and offline SD writes are explicitly handled as described in
`WATCHDOG.md`. During settings flash writes the ADC is intentionally paused and
both cores are locked out; this is not a normal SD-write operation.

## Before-and-after manual jitter comparison (not performed)

1. Use the same Pico, BME280, SD card, power supply and USB terminal for both
   runs. Wire GP4/GP5 for I²C, GP15 and ground to a 3.3 V-compatible logic
   analyzer, and SD SPI1 as in `SD_LOGGING.md`. No board is confirmed attached.
2. For a reproducible baseline, build/flash the previous revision `bac618a`.
   If its UF2 is unavailable, a disposable project-local worktree can be made
   with `git worktree add --detach .agent-local/jitter-before bac618a`; run that
   worktree's `scripts/setup.sh` and build inside it. Record firmware version,
   SD identity, saved config, rate and load. Saved settings can override CMake
   defaults, so use `config get`/`status` to confirm the actual rate.
3. Run for at least ten consecutive 10-second statistics windows at 50 Hz
   while logging. Save each `Sampling interval_us` line (`count`, `min`, `max`,
   `mean`, `missed`, `ring_overflows`, `read_errors`), SD drop/latency output,
   watchdog resets and ADC fault/gap diagnostics. Record GP15 rising-edge
   intervals independently. These values are not measured in this repository.
4. Build/flash the current revision with the same settings and repeat the same
   duration and workload. For an optional controlled slow-write comparison,
   use disposable test builds of **both** revisions with the same temporary
   `sleep_ms(200)` inserted in the core-1 `write_file` path; do not commit the
   injection. A sufficiently slow card or injection can trigger the watchdog.
5. Compare minimum of window minima, maximum of window maxima, and a
   count-weighted mean (`sum(mean × count) / sum(count)`), plus total misses,
   overflow/drop counts and ADC faults. Timer statistics use interrupt
   timestamps; GP15 pulses cover foreground I²C/compensation and also reflect
   scheduling latency. The old and new ring capacities differ, so drop counts
   are not a pure jitter comparison.

| Measurement | Before `bac618a` | After this change |
| --- | --- | --- |
| Window count / duration | unmeasured | unmeasured |
| Interval min / max / weighted mean | unmeasured | unmeasured |
| Misses / ring overflows / SD drops | unmeasured | unmeasured |
| GP15 edge intervals / ADC faults | unmeasured | unmeasured |

Afterward remove only disposable test worktrees and their local build data once
their contents are no longer needed; do not force-remove user work. Reflash the
intended non-instrumented firmware and recheck the saved settings.
