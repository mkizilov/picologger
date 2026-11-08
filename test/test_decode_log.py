import csv
import binascii
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

FIXTURE_GENERATOR = Path(sys.argv[1]).resolve()
WORK_DIR = Path(sys.argv[2]).resolve()
DECODER = Path(__file__).resolve().parents[1] / "tools" / "decode_log.py"
del sys.argv[1:]


class DecoderTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=WORK_DIR)
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "fixture.bin"
        self.csv = Path(self.temp.name) / "output.csv"
        subprocess.run([str(FIXTURE_GENERATOR), str(self.path)], check=True)

    def run_decoder(self, *args):
        return subprocess.run([sys.executable, str(DECODER), *map(str, args)],
                              text=True, capture_output=True, check=False)

    def test_c_generated_file_reports_crc_and_gap(self):
        result = self.run_decoder(self.path, self.csv)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("CRC failure at record 1", result.stderr)
        self.assertIn("expected 1, got 3", result.stderr)
        self.assertIn("records=4 crc_failures=1 sequence_gaps=1", result.stderr)
        with self.csv.open(newline="", encoding="utf-8") as stream:
            rows = list(csv.reader(stream))
        self.assertEqual(rows, [["sequence", "timestamp_us", "channel_4", "channel_26", "flags"],
                                ["0", "1000000", "0", "0", "0"],
                                ["3", "1002000", "2", "-2", "2"],
                                ["4", "1003000", "3", "-3", "3"]])

    def test_truncation_and_invalid_header_are_errors(self):
        data = self.path.read_bytes()
        self.path.write_bytes(data[:-1])
        result = self.run_decoder(self.path, self.csv)
        self.assertEqual(result.returncode, 1)
        self.assertIn("truncated record", result.stderr)
        self.path.write_bytes(b"BAD!" + data[4:])
        result = self.run_decoder(self.path, self.csv)
        self.assertEqual(result.returncode, 1)
        self.assertIn("unsupported magic", result.stderr)

    def test_refuses_to_replace_input(self):
        before = self.path.read_bytes()
        result = self.run_decoder(self.path, self.path)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(self.path.read_bytes(), before)

    def test_clean_sequence_wrap(self):
        data = bytearray(self.path.read_bytes())
        for index, sequence in enumerate((0xfffffffe, 0xffffffff, 0, 1)):
            start = 60 + 32 * index
            struct.pack_into("<I", data, start, sequence)
            checksum = binascii.crc_hqx(data[start:start + 30], 0xffff)
            struct.pack_into("<H", data, start + 30, checksum)
        self.path.write_bytes(data)
        result = self.run_decoder(self.path, self.csv)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("records=4 crc_failures=0 sequence_gaps=0", result.stderr)
        with self.csv.open(newline="", encoding="utf-8") as stream:
            rows = list(csv.reader(stream))
        self.assertEqual([row[0] for row in rows[1:]], ["4294967294", "4294967295", "0", "1"])


if __name__ == "__main__":
    unittest.main()
