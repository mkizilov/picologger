# microSD logging

The firmware logs the **BME280 sample-record stream** in the existing v1
format described by `LOG_FORMAT.md`. ADC DMA blocks continue to be sampled and
reported, but are **not written to SD**: the v1 records do not define a
mixed-rate ADC block format. Firmware has no persistent boot counter yet, so
the header's boot counter is `0` (unknown), not a measured boot count. Header
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

Core 0 drains a 256-record single-context sampling ring into four bounded
inter-core batches. Core 1 exclusively owns FatFs, mounts, writes and syncs;
its potentially long SD operations do not directly block the acquisition main
loop. Batch ownership passes through the RP2040 multicore FIFO with memory
barriers. This reserves core 1 and the inter-core FIFO; do not use SDK multicore
lockout or another FIFO consumer concurrently. Interrupts, bus contention,
serial printing and finite buffering can still cause sampling or ADC losses;
no real-time guarantee has been measured.

The writer creates the first unused `LOG_0001.BIN` … `LOG_9999.BIN` on each
mount, including after a reboot or detected removal; `FA_CREATE_NEW` prevents
overwriting an existing file. It accumulates the v1 header and records as a
byte stream in a 512-byte-aligned buffer, submitting normal writes in 4096-byte
chunks from sector-aligned file offsets. A record may straddle chunk boundaries.
On rotation, a final shorter write preserves the v1 record boundary; it starts
at a sector-aligned offset. The default rotation limit is 16 MiB. To change it:

```sh
cmake --preset firmware -DPICOLOGGER_LOG_ROTATE_BYTES=8192
cmake --build --preset firmware
```

The configured integer must be between 8192 and 4294967295 bytes. A file's
actual length is at most that limit, rounded down to a complete 32-byte record
after its 60-byte header. If all 9999 names exist, logging goes offline and
retries; it does not delete or reuse files.

`f_sync` is attempted about every five seconds, and before rotation. It syncs
bytes already submitted to FatFs, **not** the incomplete 4 KiB RAM chunk or
records still in the core-0 ring/batches. At low sample rates a partial chunk
can remain in RAM much longer than five seconds. There is currently no clean
stop/eject command; powering down or removing a card can lose buffered data,
leave an empty file or leave a partial trailing record. Use the decoder's
truncation/CRC diagnostics and do not assume a recent sync means every sample
is durable.

The storage core polls the card after sync, invalidates the mount on errors and
retries roughly every three seconds. Without card-detect hardware, removal and
reinsertion between checks may not be detected and can corrupt a filesystem;
avoid rapid hot-swaps. If testing removal, allow the console to show offline
before reinsertion and use an expendable card. Driver timeouts, power faults,
and untested electrical behavior can still affect recovery; no physical removal
test has been performed.

Every approximately ten seconds the console reports:

```text
SD online=... dropped=... uncertain=... worst_write_us=... worst_sync_us=...
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
