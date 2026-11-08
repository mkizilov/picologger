# PicoLogger binary log format, version 1

All integers are little-endian. No C structure layout or padding is written to
disk. Files consist of one 60-byte header followed by zero or more 32-byte
records. Firmware does not yet write logs; the format and host decoder are ready
for later integration. Channel IDs and value units are application-defined.

## Header (60 bytes)

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `PLOG` |
| 4 | 2 | Version, 1 |
| 6 | 2 | Header size, 60 |
| 8 | 32 | Nonempty printable ASCII firmware version, NUL-terminated and zero-padded (maximum 31 bytes) |
| 40 | 4 | Unsigned sample rate in Hz, nonzero |
| 44 | 1 | Channel count, 1–4 |
| 45 | 3 | Reserved, zero |
| 48 | 8 | Four unsigned 16-bit channel IDs; active IDs are unique, unused IDs are zero |
| 56 | 4 | Unsigned boot counter |

## Record (32 bytes)

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | Unsigned sequence number, wraps modulo 2^32 |
| 4 | 8 | Unsigned microsecond timestamp |
| 12 | 16 | Four signed two's-complement 32-bit channel values in header order; inactive slots should be zero |
| 28 | 2 | Unsigned flags |
| 30 | 2 | CRC-16/CCITT-FALSE of bytes 0–29, stored little-endian |

The CRC uses polynomial `0x1021`, initial value `0xffff`, no reflection and no
final XOR. The ASCII vector `123456789` yields `0x29b1`. The header is not
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
