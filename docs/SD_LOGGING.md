# microSD logging

The firmware logs the **BME280 sample-record stream** in the v3 format
described by `LOG_FORMAT.md`; v1/v2 decoding and recovery remain supported.
ADC DMA blocks continue to be sampled and
reported, but are **not written to SD**: the fixed sample records do not define a
mixed-rate ADC block format. The header's boot counter tracks warm resets but
does not reliably survive a power cycle. Header
channel IDs 1, 2 and 3 contain temperature in centi-°C, pressure in whole Pa,
and relative humidity in %RH × 1024. Sequence numbers are timer trigger
numbers, so missed triggers and failed sensor reads appear as sequence gaps.

The firmware FetchContent dependency is carlk3's
[no-OS-FatFS-SD-SDIO-SPI-RPi-Pico](https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico),
pinned to the v3.6.2 commit `6c644cfc3ab03c161fee2dd7be4877e5b832fa71`.
It uses SPI1 SCK GP10, MOSI GP11, MISO GP12 and CS GP13, initially requesting
12.5 MHz after card initialization. Use a 3.3 V-compatible module and FAT32
card. No card-detect pin is assumed.

## Buffering, rotation and failures

Core 0 publishes BME280 records into a 1024-record cross-core SPSC ring. Core 1
drains it, encodes records and exclusively owns FatFs, mounts, writes and syncs;
its potentially long SD operations do not directly block the acquisition main
loop. Bounded SDK multicore queues carry control and status, not sample data.
The RP2040 hardware FIFO is reserved for SDK flash lockout; do not use it for
application data or other messages. Interrupts, bus contention,
serial printing and finite buffering can still cause sampling or ADC losses;
no real-time guarantee has been measured.

On each mount, the highest-numbered finalized `LOG_####.BIN` is opened and
recovered before the first unused `LOG_0001.BIN` … `LOG_9999.BIN` is created.
`FA_CREATE_NEW` prevents overwriting existing files. A new CRC-protected v3
header occupies its own 512-byte sector: it is written and synced to a `.TMP`
file, closed, renamed to `.BIN`, and reopened before data writes. Subsequent
data writes start after that sector. Interrupted `.TMP` files are ignored and
preserved, not automatically deleted or promoted.

Records are accumulated in a 512-byte-aligned buffer and normally submitted in
4096-byte chunks from sector-aligned offsets. The initial 512-byte header write
and final shorter rotation or USB-stop write are deliberate exceptions. Both v3 header
and normal chunks end on record boundaries. The compiled default rotation limit
is 16 MiB; saved settings can override it. To change the build default:

```sh
cmake --preset firmware -DPICOLOGGER_LOG_ROTATE_BYTES=8192
cmake --build --preset firmware
```

The configured integer must be between 8192 and 4294967295 bytes. A file's
actual length is at most that limit, rounded down to a complete 32-byte record
after its 512-byte header. Existing `.BIN` or `.TMP` names are skipped. If all
9999 names exist, logging goes offline and retries; it does not reuse files.

`f_sync` is attempted about every five seconds, and before rotation. It syncs
bytes already submitted to FatFs, **not** the incomplete 4 KiB RAM chunk or
records still in the shared ring. At low sample rates a partial chunk
can remain in RAM much longer than five seconds. USB `stop` flushes and closes
the current log, but the volume remains mounted and there is no eject command;
powering down or removing a card can still lose buffered data,
leave a partial trailing record or an orphan `.TMP`. Use the decoder's
truncation/CRC diagnostics and do not assume a recent sync means every sample
is durable.

## Recovery and limits

On boot and after a detected remount, recovery validates the newest `.BIN`
header, scans backward through record-aligned positions for the last valid
record CRC, truncates the trailing partial or invalid records, and syncs the
new length. A header-only file is valid. An invalid header or failed read is
not truncated. If truncation or its sync fails, the new length may already have
been applied; logging remains offline and retries. Older logs are not changed.
Earlier corruption before the last valid
record is not repaired, and CRC-16 collisions are possible. V1 headers have no
CRC, so plausible corruption in them cannot always be detected.
Scanning a long corrupt tail can take substantial time; finite acquisition
buffers may fill and drop records while recovery runs on the storage core.

This is best-effort recovery, **not a guarantee of survival at any instant**.
FAT metadata and rename operations are not journaled or guaranteed atomic,
storage devices can tear writes or misreport durability, and an unmountable
filesystem cannot be repaired by this scanner. Records still in RAM or not
synced may be lost; `.TMP` remnants may need manual inspection. See
`POWER_LOSS.md` for the ten-cycle manual test.

The storage core polls the card after sync, invalidates the mount on errors and
retries roughly every three seconds. Without card-detect hardware, removal and
reinsertion between checks may not be detected and can corrupt a filesystem;
avoid rapid hot-swaps. If testing removal, allow the console to show offline
before reinsertion and use an expendable card. Driver timeouts, power faults,
and untested electrical behavior can still affect recovery; no physical removal
test has been performed.

Every approximately ten seconds the console reports:

```text
SD online=... dropped=... uncertain=... worst_write_us=... worst_sync_us=... recovered_bytes=... recovery_errors=...
```

Counters are cumulative since boot. `dropped` counts records rejected by the
core-0 ring or explicitly rejected by the storage worker while offline or
after an I/O failure. `uncertain` counts records not proven synced when a
storage error occurs; some may actually have reached the card, so it is an
upper bound, not an additional confirmed loss count. In-flight records are
accounted for when the worker processes them. Sudden power loss cannot be
counted. BME280 trigger misses and read failures are reported separately by
sampling statistics; ADC integrity is reported separately by ADC diagnostics.

`worst_write_us` measures completed `f_write` calls, including failed calls and
the rotation tail; `worst_sync_us` measures completed `f_sync` calls. Neither
includes mount, open, presence checks, close or an operation still blocked.
Values are capped at 268435455 µs in inter-core telemetry and a `+` marks the
cap. These latency fields are **unmeasured on hardware**. Use both alongside
drop counters when sizing the ring; neither is a guarantee about future cards.
`recovered_bytes` counts trailing bytes removed by fully successful recovery
attempts, and `recovery_errors` counts failed attempts since this boot. If a
truncate succeeds but a later sync/close fails, the byte count can omit it.
Header write and sync times are
included in the respective worst-latency fields; scan and rename are not.

## Manual checklist (not performed)

1. With power off, connect a 3.3 V-compatible SD module to SPI1: SCK GP10,
   MOSI GP11, MISO GP12, CS GP13, 3V3 and common GND. Wire the BME280 as in
   `BME280.md`. Check that no module drives Pico pins above 3.3 V.
2. Insert an expendable FAT32 card, build and flash the matching UF2 as in
   `FLASHING.md`, then open USB serial. Expect sensor/ADC diagnostics and
   eventually `SD online=1`; allow several 4 KiB writes and sync periods.
3. For a rotation test, build with `PICOLOGGER_LOG_ROTATE_BYTES=8192`; allow
   enough samples for multiple files. Inspect the card on a computer and expect
   distinct `LOG_0001.BIN`, `LOG_0002.BIN`, etc. Reboot with the card in place
   and confirm existing files are not overwritten. Decode completed files with
   `python3 tools/decode_log.py LOG_0001.BIN out.csv`; inspect CRC/gap output.
4. With an expendable card, test missing-card startup and removal. Sampling
   diagnostics should continue, `SD online` should become 0, and `dropped` or
   `uncertain` should rise as applicable. Wait for offline before reinserting;
   expect mounting to retry and a new filename to be created. Review the loss
   caveats above; this test can damage filesystem contents.
5. Record `worst_write_us`, `worst_sync_us`, BME280 missed-trigger counts and
   ADC fault/FIFO fields over a representative run. These values and physical
   integrity are not yet measured; zero counters alone do not prove losslessness.
