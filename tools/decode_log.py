#!/usr/bin/env python3
"""Decode PicoLogger format-v1 files to CSV without third-party dependencies."""

import argparse
import csv
import struct
import sys
from pathlib import Path

HEADER_SIZE = 60
RECORD_SIZE = 32


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xFFFF
    return crc


def read_header(data):
    if len(data) != HEADER_SIZE:
        raise ValueError("truncated file header")
    magic, version, size = struct.unpack_from("<4sHH", data)
    if magic != b"PLOG" or version != 1 or size != HEADER_SIZE:
        raise ValueError("unsupported magic, version or header size")
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
    return firmware[:end].decode("ascii"), rate, channels[:count], boot


def decode(input_path, output_path):
    if input_path.resolve() == output_path.resolve() or (
            output_path.exists() and input_path.samefile(output_path)):
        raise ValueError("input and output must be different files")
    with input_path.open("rb") as source:
        firmware, rate, channels, boot = read_header(source.read(HEADER_SIZE))
        print(f"firmware={firmware} sample_rate_hz={rate} boot_counter={boot} "
              f"channels={','.join(map(str, channels))}", file=sys.stderr)
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
