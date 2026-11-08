# PicoLogger binary log formats, versions 1, 2 and 3

All integers are little-endian. No C structure layout or padding is written to
disk. Version 1 has a 60-byte header; versions 2 and 3 have 512-byte headers.
All are followed by zero or more identical 32-byte records. New firmware logs use v3;
the decoder and recovery code continue to accept v1 and v2. Channel IDs and units
for the BME280 files are documented in `SD_LOGGING.md`.

## Shared header fields (first 60 bytes)

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `PLOG` |
| 4 | 2 | Version, 1, 2 or 3 |
| 6 | 2 | Header size, 60 for v1 or 512 for v2/v3 |
| 8 | 32 | Nonempty printable ASCII firmware version, NUL-terminated and zero-padded (maximum 31 bytes) |
| 40 | 4 | Unsigned sample rate in Hz, nonzero |
| 44 | 1 | Channel count, 1–4 |
| 45 | 3 | Reserved, zero |
| 48 | 8 | Four unsigned 16-bit channel IDs; active IDs are unique, unused IDs are zero |
| 56 | 4 | Unsigned boot counter |

For v2, bytes 60–509 are reserved and zero. V3 uses bytes 60–147 as described
below and reserves bytes 148–509 as zero. For both, bytes 510–511 contain the
little-endian CRC-16/CCITT-FALSE of header bytes 0–509. Records begin at byte
512, so subsequent data writes need not rewrite the header sector. V1 records
begin at byte 60; the v1 header has no checksum.

## V3 reset and fault metadata

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 60 | 4 | Reset code: 0 unknown/power/RUN, 1 watchdog timeout, 2 forced watchdog/software, 3 HardFault |
| 64 | 4 | Raw RP2040 watchdog reason register |
| 68 | 4 | Fault mask: bit 0 core 0, bit 1 core 1 |
| 72 | 32 | Core 0 fault detail, eight little-endian 32-bit words |
| 104 | 32 | Core 1 fault detail, same layout |
| 136 | 8 | Originating boot counter for core 0 and core 1 faults |
| 144 | 4 | Last observed watchdog missing-progress bitmask, or zero if unavailable |

Each fault detail contains flags, stacked PC, stacked LR, stacked xPSR,
stack-frame address, EXC_RETURN, ICSR and SHCSR, in that order. Flag bit 0
means the exception frame was within SRAM; when clear, stacked PC/LR/xPSR are
unavailable. An absent core's detail and origin must be zero. The RP2040
Cortex-M0+ does not provide Cortex-M33 CFSR/HFSR fault-address registers.
The boot counter is a warm-reset counter, not a power-cycle-persistent count.
See `WATCHDOG.md` for retention and diagnostic limitations.

## Record (32 bytes)

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | Unsigned sequence number, wraps modulo 2^32 |
| 4 | 8 | Unsigned microsecond timestamp |
| 12 | 16 | Four signed two's-complement 32-bit channel values in header order; inactive slots should be zero |
| 28 | 2 | Unsigned flags |
| 30 | 2 | CRC-16/CCITT-FALSE of bytes 0–29, stored little-endian |

The CRC uses polynomial `0x1021`, initial value `0xffff`, no reflection and no
final XOR. The ASCII vector `123456789` yields `0x29b1`. V2 and v3 headers are
CRC-protected; invalid or unsupported header fields are rejected. The C API
does not require inactive record slots to be zero, because records are encoded
independently of a header. The decoder omits inactive slots from CSV.

## Decoder

```sh
python3 tools/decode_log.py input.bin output.csv
```

Only Python's standard library is needed; no package installation or NumPy is
required. The decoder streams records, writes valid ones as CSV and reports
header metadata, per-record CRC failures, and gaps between consecutive valid
sequence numbers on stderr. The first valid sequence has no assumed predecessor.
Sequence comparisons wrap at 2^32. A gap's reported forward distance cannot
distinguish missing records from duplicates or out-of-order records. Corrupt
records are skipped, so their sequence numbers are not trusted. An incomplete
header or record is an error; a partial CSV can remain after a record error.
Input and output must be different files. Exit status is 0 for clean logs, 2 for
CRC failures or gaps, and 1 for file, header or truncation errors.
