# USB CDC command interface

Connect a USB serial terminal as described in `FLASHING.md`. Commands are
lowercase ASCII lines terminated by LF, CR or CRLF; the line limit is 80 bytes
excluding the terminator. Replies begin with `@` on their own line; a leading
blank separator keeps them distinguishable from legacy diagnostics. Empty lines
are ignored. Unknown commands, invalid arguments and overlong lines receive an
`@ERR ...` response. Filenames are root-directory basenames of at most 32 ASCII
letters, digits, `.`, `_` or `-`; paths, spaces, leading/trailing dots and
consecutive dots are rejected.

| Command | Behavior |
| --- | --- |
| `help` | List commands. |
| `status` | Uptime, sampling and storage counters, rate, power mode and configured system clock, ADC status, SD state and output drops. |
| `stop` | Stop BME280 timer sampling, drain already queued BME280 records, then flush, sync and close the current log on the storage core. Replies `PENDING stop`, followed by `OK stopped` or `ERR stop flush failed`. ADC DMA continues. |
| `rate <hz>` | Set BME280 rate to 1–50 Hz while stopped. Crossing the low-power boundary can reply `PENDING rate` before `OK rate=...` or an error. Runtime-only; resets to saved setting or build default at reboot. |
| `start` | Open a new log with the selected rate in its header and restart BME280 sampling. Replies `PENDING start`, followed by `OK started` or an error. If SD is absent, sampling can start while storage retries and counts drops. |
| `stream on` / `stream off` | Enable/disable live best-effort BME280 CSV frames. ADC samples are not streamed. |
| `ls` | List root entries asynchronously as `FILE name size` or `DIR name size`, ending with `END LS`. Unsupported filenames are reported and skipped. |
| `dump <file>` | Read a root file asynchronously in base64 chunks with a final CRC-32. Stop first to dump the current log; another completed file can be dumped while logging. |
| `reboot` | Request a stop/flush, then schedule a watchdog reboot. After ten seconds it reboots even if storage has not acknowledged; unsynced data may be lost. |
| `config get` / `config set <key> <value>` / `config reset` | Inspect or save settings for the next boot; real writes require stopped logging and intentionally pause ADC. See `SETTINGS.md`. |

`stop`, `start`, and clock-changing `rate` requests are asynchronous; wait for their final `OK`/`ERR` before
issuing `rate` or another state transition. `status` reflects the most recently
received storage-core state, so file and SD status can briefly lag. A failed
stop can leave unsynced records; check `uncertain` and `recovery_errors` in the
periodic SD diagnostics. There is no explicit unmount/eject command.
The ring high-water mark is a conservative producer-side BME280 occupancy snapshot since boot;
sample counts are acquired/delivered counts, not a count of durable records.
`watchdog_missing` is the current missing-progress bitmask described in
`WATCHDOG.md`.
Low-rate clock, USB sleep and measurement limitations are in `POWER.md`.

## Streaming and dump protocol

Live frames are `@CSV,sequence,timestamp_us,T_centi,P_Pa,RH_q1024`. Stream
frames are dropped rather than blocking if the bounded USB output queue fills;
`status` reports `stream_dropped`. `output_dropped` counts rejected command
output frames. Pending output is discarded on USB disconnect, and streaming
is disabled until explicitly re-enabled.

A dump produces frames of the form:

```text
@BEGIN DUMP LOG_0001.BIN
@DATA 0 <base64 of up to 24 bytes>
@DATA 24 <base64 of the next bytes>
@END DUMP <total_bytes> <8-digit CRC32>
```

Offsets are decimal. The checksum is standard CRC-32/ISO-HDLC, equivalent to
Python's `binascii.crc32` over the complete binary file. `@ERR ...` terminates
an unsuccessful operation. Legacy diagnostics or CSV frames may appear between
dump frames; filter for the dump frames. Disconnecting USB cancels an in-flight
listing/dump; there is no resume. A stalled host can delay the transfer but the
storage worker continues servicing logging when it is not inside a filesystem
call. Directory contents can change while listing.

Capture the terminal text to a file, then reconstruct and validate it on the
computer with:

```sh
python3 tools/receive_dump.py capture.txt recovered.bin
python3 tools/decode_log.py recovered.bin recovered.csv
```

The receiver verifies offsets, base64, length and CRC32 before publishing the
output, refuses to overwrite an existing file, and removes its temporary file
on error. The output directory must support hard links. It needs Python 3.8+
and only standard-library modules.

## Nonblocking scope and limitations

Except for intentional flash-save pauses described in `SETTINGS.md`, the
acquisition core consumes at most 16 input bytes and one complete command
per poll, and sends at most one 64-byte output frame without waiting for USB
space. The SDK's stdout/deadlock timeouts are set to zero so existing debug
prints may be truncated rather than stall when the host stops reading. A short
interrupt-masked section protects each direct TinyUSB FIFO operation from the
SDK USB background IRQ; its actual duration and resulting jitter have **not**
been measured. Filesystem work remains on core 1, including listing, reads,
stop/flush and file creation. A slow card or stalled transfer can still fill
finite logging buffers and cause counted drops; CPU/bus contention and legacy
diagnostics can still affect timing. No physical USB, card or board test has
been performed, so this is not a hard real-time guarantee.

Direct CDC access assumes USB stdio and the SDK USB background IRQ run on core
0 and that no other core or library concurrently calls TinyUSB CDC APIs. Do
not enable storage-core USB debug printing or add another CDC consumer without
reworking this synchronization.

## Manual checklist (not performed)

1. Build and flash the matching Pico/Pico W UF2 as in `FLASHING.md`; connect the
   BME280 and an expendable FAT32 SD card as in `SD_LOGGING.md`. Open the USB
   CDC terminal and send `help`, then `status`. Expect the fields listed above,
   an SD state and the current filename when mounted.
2. Send `stop` and wait for `@OK stopped`. Send `rate 25`, then `start` and wait
   for `@OK started`; verify `status` reports 25 Hz and a new filename. Confirm
   ADC diagnostics continue during the BME280 stop.
3. Send `stream on`; expect `@CSV,...` rows. Send `stream off`. Stall or pause
   terminal reading, then resume and inspect `stream_dropped`, sampling misses,
   ring high-water and ADC fault counters. Timing and drops are unmeasured here.
4. Send `ls`, then `stop` and wait for completion. Send `dump <filename>` for a
   listed log; save the terminal text and run `tools/receive_dump.py`, then
   `tools/decode_log.py`. Expect matching dump length/CRC and valid log records;
   storage/media errors must instead produce `@ERR`.
5. Send `reboot`, observe the pending/acknowledgment and reconnect after the
   device resets. If a flush error or timeout occurs, retain the card and check
   recovery diagnostics rather than assuming all records were saved.
