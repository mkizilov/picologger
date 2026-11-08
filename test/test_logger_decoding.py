import csv
import binascii
import base64
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

generator = Path(sys.argv[1]).resolve()
work_dir = Path(sys.argv[2]).resolve()
decoder = Path(__file__).resolve().parents[1] / "tools" / "decode_log.py"
receiver = Path(__file__).resolve().parents[1] / "tools" / "receive_dump.py"
with tempfile.TemporaryDirectory(dir=work_dir) as folder:
    log = Path(folder) / "LOG_0001.BIN"
    output = Path(folder) / "out.csv"
    subprocess.run([str(generator), str(log)], check=True, capture_output=True, text=True)
    decoded = subprocess.run([sys.executable, str(decoder), str(log), str(output)],
                             capture_output=True, text=True, check=True)
    if "records=240 crc_failures=0 sequence_gaps=0" not in decoded.stderr:
        raise AssertionError(decoded.stderr)
    if "reset_reason=3 watchdog_reason=0x00000002 watchdog_missing=0x00000005" not in decoded.stderr or \
            "hardfault core=1 origin_boot=8 stack_valid=1 pc=0x10001234 lr=0x10005678" not in decoded.stderr:
        raise AssertionError(f"missing persisted fault metadata: {decoded.stderr}")
    with output.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.reader(stream))
    if len(rows) != 241 or rows[1][0] != "10" or rows[-1][0] != "249":
        raise AssertionError("incorrect records in rotated log")
    original = log.read_bytes()
    lines = Path(str(log) + ".dump").read_text(encoding="ascii").splitlines()
    if not lines or lines[0] != "BEGIN DUMP LOG_0001.BIN":
        raise AssertionError(f"bad dump start: {lines[:1]}")
    rebuilt = bytearray()
    for line in lines[1:-1]:
        fields = line.split(" ")
        if len(fields) != 3 or fields[0] != "DATA" or int(fields[1]) != len(rebuilt):
            raise AssertionError(f"bad dump chunk: {line}")
        rebuilt.extend(base64.b64decode(fields[2], validate=True))
    end = lines[-1].split(" ")
    if len(end) != 4 or end[:2] != ["END", "DUMP"]:
        raise AssertionError(f"bad dump trailer: {lines[-1]}")
    if int(end[2]) != len(original) or int(end[3], 16) != binascii.crc32(original) or rebuilt != original:
        raise AssertionError("dump bytes, length, or checksum differ")
    capture = Path(folder) / "serial.txt"
    capture.write_text("diagnostic\n" + "\n".join("@" + line for line in lines) + "\n", encoding="ascii")
    received = Path(folder) / "received.bin"
    subprocess.run([sys.executable, str(receiver), str(capture), str(received)], check=True, capture_output=True, text=True)
    if received.read_bytes() != original:
        raise AssertionError("receiver produced different bytes")
    result = subprocess.run([sys.executable, str(receiver), str(capture), str(received)], capture_output=True, text=True)
    if result.returncode != 1 or received.read_bytes() != original:
        raise AssertionError("receiver overwrote an existing output")
    bad_crc = ["@" + line for line in lines]
    bad_crc[-1] = bad_crc[-1][:-8] + ("ffffffff" if bad_crc[-1].endswith("00000000") else "00000000")
    capture.write_text("\n".join(bad_crc) + "\n", encoding="ascii")
    bad_output = Path(folder) / "bad-crc.bin"
    result = subprocess.run([sys.executable, str(receiver), str(capture), str(bad_output)], capture_output=True, text=True)
    if result.returncode != 1 or bad_output.exists():
        raise AssertionError("receiver accepted a bad CRC")
    damaged = ["@" + line for line in lines]
    damaged[2] = damaged[2].replace("DATA ", "DATA 1", 1)
    capture.write_text("\n".join(damaged) + "\n", encoding="ascii")
    failed = Path(folder) / "failed.bin"
    result = subprocess.run([sys.executable, str(receiver), str(capture), str(failed)], capture_output=True, text=True)
    if result.returncode != 1 or failed.exists() or list(Path(folder).glob(".picologger-dump-*")):
        raise AssertionError("receiver failed to reject or clean up damaged transfer")
    corrupted = bytearray(original)
    corrupted[40] ^= 1
    log.write_bytes(corrupted)
    result = subprocess.run([sys.executable, str(decoder), str(log), str(output)], capture_output=True, text=True)
    if result.returncode != 1 or "header CRC failure" not in result.stderr:
        raise AssertionError(f"invalid header was not rejected: {result.stderr}")
    corrupted = bytearray(original)
    corrupted[100] = 1
    struct.pack_into("<H", corrupted, 510, binascii.crc_hqx(corrupted[:510], 0xffff))
    log.write_bytes(corrupted)
    result = subprocess.run([sys.executable, str(decoder), str(log), str(output)], capture_output=True, text=True)
    if result.returncode != 1 or "invalid header reserved bytes" not in result.stderr:
        raise AssertionError(f"invalid reserved bytes were not rejected: {result.stderr}")
    log.write_bytes(original[:511])
    result = subprocess.run([sys.executable, str(decoder), str(log), str(output)], capture_output=True, text=True)
    if result.returncode != 1 or "truncated file header" not in result.stderr:
        raise AssertionError(f"truncated header was not rejected: {result.stderr}")
print("rotated logger output decoded without CRC failures or gaps")
