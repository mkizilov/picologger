import csv
from pathlib import Path
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
    if "records=254 crc_failures=0 sequence_gaps=0" not in decoded.stderr:
        raise AssertionError(decoded.stderr)
    with output.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.reader(stream))
    if len(rows) != 255 or rows[1][0] != "10" or rows[-1][0] != "263":
        raise AssertionError("incorrect records in rotated log")
print("rotated logger output decoded without CRC failures or gaps")
