import csv
import binascii
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

generator = Path(sys.argv[1]).resolve()
work_dir = Path(sys.argv[2]).resolve()
decoder = Path(__file__).resolve().parents[1] / "tools" / "decode_log.py"
with tempfile.TemporaryDirectory(dir=work_dir) as folder:
    log = Path(folder) / "LOG_0001.BIN"
    output = Path(folder) / "out.csv"
    subprocess.run([str(generator), str(log)], check=True, capture_output=True, text=True)
    decoded = subprocess.run([sys.executable, str(decoder), str(log), str(output)],
                             capture_output=True, text=True, check=True)
    if "records=240 crc_failures=0 sequence_gaps=0" not in decoded.stderr:
        raise AssertionError(decoded.stderr)
    with output.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.reader(stream))
    if len(rows) != 241 or rows[1][0] != "10" or rows[-1][0] != "249":
        raise AssertionError("incorrect records in rotated log")
    original = log.read_bytes()
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
