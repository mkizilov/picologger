#!/usr/bin/env python3
"""Decode PicoLogger v1/v2/v3 files to CSV without third-party dependencies."""

import argparse
import csv
import struct
import sys
from pathlib import Path

HEADER_SIZE = 60
HEADER_V2_SIZE = 512
RECORD_SIZE = 32


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xFFFF
    return crc


def read_header(data):
    if len(data) < HEADER_SIZE:
        raise ValueError("truncated file header")
    magic, version, size = struct.unpack_from("<4sHH", data)
    if magic != b"PLOG" or (version, size) not in ((1, HEADER_SIZE), (2, HEADER_V2_SIZE), (3, HEADER_V2_SIZE)):
        raise ValueError("unsupported magic, version or header size")
    if len(data) != size:
        raise ValueError("truncated file header")
    diagnostics = None
    if version in (2, 3):
        stored_crc, = struct.unpack_from("<H", data, HEADER_V2_SIZE - 2)
        if crc16(data[:-2]) != stored_crc:
            raise ValueError("header CRC failure")
        if any(data[(HEADER_SIZE if version == 2 else 148):-2]):
            raise ValueError("invalid header reserved bytes")
        if version == 3:
            reset_reason, watchdog_reason, fault_mask = struct.unpack_from("<III", data, 60)
            if reset_reason > 3 or fault_mask & ~3:
                raise ValueError("invalid reset or fault metadata")
            faults = []
            for core in range(2):
                fields = struct.unpack_from("<8I", data, 72 + core * 32)
                origin, = struct.unpack_from("<I", data, 136 + core * 4)
                if fault_mask & (1 << core):
                    if fields[0] & ~1:
                        raise ValueError("invalid fault flags")
                    faults.append((core, origin, fields))
                elif any(fields) or origin:
                    raise ValueError("invalid header reserved bytes")
            watchdog_missing, = struct.unpack_from("<I", data, 144)
            diagnostics = (reset_reason, watchdog_reason, watchdog_missing, faults)
    firmware = data[8:40]
    end = firmware.find(b"\0")
    if end <= 0 or any(firmware[end + 1:]) or any(c < 32 or c > 126 for c in firmware[:end]):
        raise ValueError("invalid firmware version")
    rate, count = struct.unpack_from("<IB", data, 40)
    channels = struct.unpack_from("<4H", data, 48)
    if (rate == 0 or not 1 <= count <= 4 or any(data[45:48]) or
            any(channels[count:]) or len(set(channels[:count])) != count):
        raise ValueError("invalid channel list, sample rate or reserved bytes")
    boot, = struct.unpack_from("<I", data, 56)
    return firmware[:end].decode("ascii"), rate, channels[:count], boot, diagnostics


def decode(input_path, output_path):
    if input_path.resolve() == output_path.resolve() or (
            output_path.exists() and input_path.samefile(output_path)):
        raise ValueError("input and output must be different files")
    with input_path.open("rb") as source:
        data = source.read(HEADER_SIZE)
        if len(data) == HEADER_SIZE and data[:8] in (b"PLOG\x02\x00\x00\x02", b"PLOG\x03\x00\x00\x02"):
            data += source.read(HEADER_V2_SIZE - HEADER_SIZE)
        firmware, rate, channels, boot, diagnostics = read_header(data)
        print(f"firmware={firmware} sample_rate_hz={rate} boot_counter={boot} "
              f"channels={','.join(map(str, channels))}", file=sys.stderr)
        if diagnostics is not None:
            reason, watchdog, missing, faults = diagnostics
            print(f"reset_reason={reason} watchdog_reason=0x{watchdog:08x} watchdog_missing=0x{missing:08x}", file=sys.stderr)
            for core, origin, (flags, pc, lr, xpsr, sp, exc_lr, icsr, shcsr) in faults:
                print(f"hardfault core={core} origin_boot={origin} stack_valid={flags & 1} pc=0x{pc:08x} lr=0x{lr:08x} "
                      f"xpsr=0x{xpsr:08x} sp=0x{sp:08x} exc_return=0x{exc_lr:08x} "
                      f"icsr=0x{icsr:08x} shcsr=0x{shcsr:08x}", file=sys.stderr)
        failures = gaps = records = 0
        previous = None
        with output_path.open("w", encoding="utf-8", newline="") as output:
            writer = csv.writer(output)
            writer.writerow(["sequence", "timestamp_us"] +
                            [f"channel_{channel}" for channel in channels] + ["flags"])
            while True:
                data = source.read(RECORD_SIZE)
                if not data:
                    break
                if len(data) != RECORD_SIZE:
                    raise ValueError(f"truncated record at index {records} ({len(data)} bytes)")
                stored_crc, = struct.unpack_from("<H", data, 30)
                if crc16(data[:30]) != stored_crc:
                    failures += 1
                    print(f"CRC failure at record {records}", file=sys.stderr)
                    records += 1
                    continue
                sequence, timestamp, *rest = struct.unpack("<IQ4iHH", data)
                if previous is not None:
                    expected = (previous + 1) & 0xFFFFFFFF
                    if sequence != expected:
                        gaps += 1
                        missing = (sequence - expected) & 0xFFFFFFFF
                        print(f"sequence gap at record {records}: expected {expected}, "
                              f"got {sequence} (forward distance {missing})", file=sys.stderr)
                writer.writerow([sequence, timestamp] + rest[:len(channels)] + [rest[4]])
                previous = sequence
                records += 1
        print(f"records={records} crc_failures={failures} sequence_gaps={gaps}", file=sys.stderr)
        return 2 if failures or gaps else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="binary log")
    parser.add_argument("output", type=Path, help="destination CSV")
    args = parser.parse_args()
    try:
        return decode(args.input, args.output)
    except (OSError, ValueError) as exc:
        print(f"decode error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
