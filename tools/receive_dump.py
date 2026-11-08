#!/usr/bin/env python3
"""Verify one captured USB dump and reconstruct it without overwriting a file."""

import argparse
import base64
import binascii
import os
from pathlib import Path
import sys
import tempfile
import zlib


def receive(capture, output):
    if output.exists():
        raise ValueError("output already exists")
    temporary = None
    started = finished = False
    offset = checksum = 0
    try:
        with capture.open("r", encoding="utf-8", errors="replace") as source:
            with tempfile.NamedTemporaryFile(mode="wb", prefix=".picologger-dump-", dir=output.parent,
                                             delete=False) as target:
                temporary = Path(target.name)
                for raw in source:
                    line = raw.strip()
                    if line.startswith("@BEGIN DUMP "):
                        if started:
                            raise ValueError("multiple dump starts")
                        started = True
                    elif started and line.startswith("@ERR "):
                        raise ValueError(line[1:])
                    elif started and line.startswith("@DATA "):
                        fields = line.split(" ")
                        if len(fields) != 3 or int(fields[1]) != offset:
                            raise ValueError("missing or out-of-order chunk")
                        data = base64.b64decode(fields[2], validate=True)
                        if not data or len(data) > 24:
                            raise ValueError("invalid chunk size")
                        target.write(data)
                        checksum = zlib.crc32(data, checksum)
                        offset += len(data)
                    elif started and line.startswith("@END DUMP "):
                        fields = line.split(" ")
                        if len(fields) != 4 or int(fields[2]) != offset or int(fields[3], 16) != checksum:
                            raise ValueError("length or CRC32 mismatch")
                        finished = True
                        break
        if not finished:
            raise ValueError("incomplete dump")
        os.link(temporary, output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        receive(args.capture, args.output)
    except (OSError, ValueError, binascii.Error) as exc:
        print(f"dump error: {exc}", file=sys.stderr)
        return 1
    print(f"verified dump written to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
