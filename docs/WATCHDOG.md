# Watchdog and HardFault reporting

The RP2040 hardware watchdog is enabled early in `main` with a 2000 ms timeout.
It pauses when a debugger halts a core. The main loop is the only normal feeder;
it feeds only after all currently required progress sources have advanced:

* Each enabled, expected BME280 and ADC sampling stream must produce successful
  samples/blocks. The BME280 source is exempt while explicitly stopped or
  disabled; ADC is exempt only when disabled in boot settings.
* The storage core must report a heartbeat, including while SD is offline or
  logging is stopped. Successful reads during a long recovery scan also count as
  storage-core progress. A blocked storage call stops the heartbeat.
* While BME logging is running and SD is online, the worker must accept records
  into its write buffer and must also have completed a successful FatFs
  `f_write` (the synced header counts). This is not proof that the card has
  physically committed the data. Batches are submitted after at most about 100 ms.
  Worker, sampling and accepted-record freshness use a 1.5 s window. The
  physical-write window is `ceil(128 / rate_hz) + 2` seconds because a 4 KiB
  chunk contains 128 records; this is 5 s at 50 Hz and 130 s at 1 Hz. A blocked
  `f_write` is still caught by the worker heartbeat. This policy does not claim
  a physical SD write occurs every two seconds.

An absent card has no possible writes, so the write checks are explicitly exempt
while offline; the worker heartbeat and enabled sampling checks remain required.
The watchdog timeout starts from the last feed, so a stalled source normally
causes reset after its freshness window plus up to two seconds, not exactly two
seconds after the stall. `status` reports `watchdog_missing` as bits: 1 BME,
2 ADC, 4 storage worker, 8 accepted records, 16 physical write. The last mask
observed before a watchdog timeout is retained when possible; zero means unknown
or no recorded missing source, not proof of a healthy prior run.

Initialization happens under the watchdog. An enabled but missing/failing
sensor, a slow SD operation, long recovery, or a flash operation lasting longer
than the watchdog interval can cause repeated resets. Card and flash timing,
the progress thresholds, and reset behavior are **not measured on hardware**.
`SETTINGS.md` describes the deliberate ADC pause during flash updates. Slow
cards may need a different policy after measurement; do not silently disable the
watchdog or claim lossless logging.

## Retained fault information

The Cortex-M0+ HardFault handler selects the stacked MSP/PSP frame, checks that
it lies in SRAM, records PC, LR, xPSR, frame address, EXC_RETURN, core number,
ICSR and SHCSR, then triggers a watchdog reset. The RP2040 does **not** provide
the M33 CFSR/HFSR fault-address registers. ICSR and SHCSR are state registers,
not a complete fault cause. If the stack address is invalid, PC/LR/xPSR are
reported as unavailable; a corrupted handler stack, nested fault or hardware
failure can prevent any record from being saved.

Per-core fault records, acknowledgments and the last watchdog mask are kept in
an SDK `.uninitialized_data` NOLOAD section. The firmware excludes SRAM banks
from watchdog reset selection and validates records before using them. Pending
HardFault reports remain across warm resets until a log with a synced v3 header
has been created; another fault on the same core can replace an older pending
record. No SRAM retention is guaranteed across power loss, and the record CRC32
cannot detect every possible corruption. If logging is disabled or the
card is missing, the fault can be printed on USB and remains pending only while
RAM and watchdog scratch survive.

The boot counter uses watchdog scratch registers 0–2 with a magic/complement
check, and register 3 marks retained faults. It increments across supported warm
resets but can reset to 1 on power loss or an invalid/interrupted counter update;
it is not a lifetime boot counter. These registers are reserved for PicoLogger.
The boot banner prints the counter, reset code, raw watchdog reason and any
pending fault details. New logs include the same data in the v3 header;
`tools/decode_log.py` prints it. V1 and v2 logs remain supported.

## Manual checklist (not performed)

1. Build and flash the Pico UF2, connect the BME280 and an expendable SD card,
   then open USB CDC. Check `status`, `watchdog_missing`, sampling/SD progress,
   the boot counter and reset reason. No timing or hardware result is claimed.
2. With no config write pending, type the hidden command `crash`. It deliberately
   executes an undefined instruction and resets abruptly; it is omitted from
   `help` and may lose unsynced log data. Reconnect and check for a HardFault
   line with core, PC, LR, stack-valid flag, ICSR/SHCSR and incremented warm boot
   counter. Reconnect again if the nonblocking banner was truncated.
3. Wait for SD online and a new file, then send `stop` and wait for `@OK stopped`.
   Use `ls`/`dump`, `tools/receive_dump.py`, and `tools/decode_log.py` as in
   `USB_COMMANDS.md`. Confirm that the v3 header reports the reset and the same
   fault details. If SD is absent or boot logging is disabled, enable logging
   before expecting a report on disk.
4. To test a watchdog timeout without shorting wires, use a **disposable test
   build** with an enabled BME channel: in `watchdog_monitor.c`, temporarily
   replace the `bme_samples` observation with `time_us_64() < 6000000 ?
   sensor.samples : 0`. Build/flash that test image, observe the resulting reset
   and missing-BME bit, then restore the unmodified source and firmware. A
   similar temporary freeze of `worker_progress` exercises the storage-worker
   bit. This injects stale observations; it does not prove a real hardware hang.
5. Test stopped/disabled channels and a missing card separately to check for
   unwanted reboot loops, and measure actual flash/SD timing before deployment.
   A debugger halt pauses this watchdog, so it is not a valid timeout test.
