import base64
import contextlib
import io
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
os.environ.setdefault("MPLBACKEND", "Agg")
os.environ.setdefault("MPLCONFIGDIR", str(ROOT / ".agent-local" / "matplotlib"))

from tools import logger_cli


class FakeSerial:
    def __init__(self, responses):
        self.responses = responses
        self.pending = bytearray(b"boot banner\n@READY type help\n")
        self.commands = []

    @property
    def in_waiting(self):
        return len(self.pending)

    def write(self, data):
        command = data.decode("ascii").strip()
        self.commands.append(command)
        self.pending.extend(self.responses.get(command, b""))
        return len(data)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        return False

    def read(self, count):
        n = min(count, 7, len(self.pending))
        data = self.pending[:n]
        del self.pending[:n]
        return bytes(data)


def dump_frames(data, checksum=None):
    crc = zlib.crc32(data) if checksum is None else checksum
    frames = [b"@PENDING dump\n@BEGIN DUMP LOG_0001.BIN\n"]
    for offset in range(0, len(data), 24):
        frames.append(b"noise\n@DATA %d %s\n" % (offset, base64.b64encode(data[offset:offset + 24])))
    frames.append(b"@END DUMP %d %08x\n" % (len(data), crc))
    return b"".join(frames)


class LoggerCliTests(unittest.TestCase):
    def test_status_config_and_device_errors(self):
        port = FakeSerial({"status": b"@STATUS rate_hz=10\n@CSV,1,1,2,3,4\n@END STATUS\n",
                           "config get": b"@CONFIG saved rate=10\n@END CONFIG\n",
                           "config set rate 10": b"@PENDING config\n@OK config saved; reboot to apply\n",
                           "config reset": b"@ERR stop first\n"})
        client = logger_cli.Logger(port, 1)
        self.assertEqual(client.command("status", "@END STATUS"), ["@STATUS rate_hz=10"])
        self.assertEqual(client.command("config get", "@END CONFIG"), ["@CONFIG saved rate=10"])
        self.assertEqual(client.command("config set rate 10"), ["@OK config saved; reboot to apply"])
        with self.assertRaisesRegex(logger_cli.ProtocolError, "stop first"):
            client.command("config reset")

    def test_download_verifies_crc_offsets_and_preserves_existing_output(self):
        data = bytes(range(100))
        base = ROOT / ".agent-local"
        base.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as folder:
            output = Path(folder) / "log.bin"
            port = FakeSerial({"dump LOG_0001.BIN": dump_frames(data)})
            client = logger_cli.Logger(port, 1)
            client.download("LOG_0001.BIN", output)
            self.assertEqual(output.read_bytes(), data)
            with self.assertRaisesRegex(ValueError, "already exists"):
                client.download("LOG_0001.BIN", output)
            self.assertEqual(port.commands, ["dump LOG_0001.BIN"])
            bad = Path(folder) / "bad.bin"
            port = FakeSerial({"dump LOG_0001.BIN": dump_frames(data, 0)})
            with self.assertRaisesRegex(ValueError, "CRC32"):
                logger_cli.Logger(port, 1).download("LOG_0001.BIN", bad)
            self.assertFalse(bad.exists())
            self.assertEqual(sorted(p.name for p in Path(folder).iterdir()), ["log.bin"])
            wrong = dump_frames(data).replace(b"@DATA 24 ", b"@DATA 25 ")
            with self.assertRaisesRegex(ValueError, "out-of-order"):
                logger_cli.Logger(FakeSerial({"dump LOG_0001.BIN": wrong}), 1).download("LOG_0001.BIN", bad)
            self.assertFalse(bad.exists())
            wrong_name = dump_frames(data).replace(b"BEGIN DUMP LOG_0001.BIN", b"BEGIN DUMP LOG_0002.BIN")
            with self.assertRaisesRegex(ValueError, "unexpected dump filename"):
                logger_cli.Logger(FakeSerial({"dump LOG_0001.BIN": wrong_name}), 1).download("LOG_0001.BIN", bad)
            self.assertFalse(bad.exists())

    def test_invalid_filename_timeout_and_incomplete_dump(self):
        client = logger_cli.Logger(FakeSerial({}), 0.001)
        with self.assertRaises(ValueError):
            client.download("../escape", Path("unused"))
        with self.assertRaisesRegex(logger_cli.ProtocolError, "timed out"):
            client.command("status", "@END STATUS")
        base = ROOT / ".agent-local"
        base.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as folder:
            target = Path(folder) / "missing.bin"
            port = FakeSerial({"dump LOG_0001.BIN": b"@BEGIN DUMP LOG_0001.BIN\n@DATA 0 TQ==\n"})
            with self.assertRaisesRegex(logger_cli.ProtocolError, "timed out"):
                logger_cli.Logger(port, 0.001).download("LOG_0001.BIN", target)
            self.assertEqual(list(Path(folder).iterdir()), [])

    def test_plot_rolls_and_stream_turns_off(self):
        sample = logger_cli.parse_sample("@CSV,7,2500000,2508,100653,67367")
        if sample is None:
            raise AssertionError("CSV sample was not parsed")
        self.assertEqual(sample[:3], (7, 2.5, 25.08))
        self.assertAlmostEqual(sample[3], 1006.53)
        with self.assertRaises(logger_cli.ProtocolError):
            logger_cli.parse_sample("@CSV,broken")
        with self.assertRaises(logger_cli.ProtocolError):
            logger_cli.parse_sample("@CSV,-1,0,0,0,0")
        with patch("matplotlib.pyplot.show"):
            graph = logger_cli.RollingPlot(2)
        for i in range(3):
            graph.add((i, float(i), 20, 1000, 50))
        graph.update()
        self.assertEqual([row[0] for row in graph.samples], [1, 2])
        self.assertTrue(graph.open())
        graph.plt.close(graph.figure)
        self.assertFalse(graph.open())

        class FakeGraph:
            instance = None

            def __init__(self, window):
                self.samples = []
                self.checks = 0
                FakeGraph.instance = self

            def open(self):
                self.checks += 1
                return self.checks < 20

            def add(self, value):
                self.samples.append(value)

            def update(self):
                pass

        port = FakeSerial({"stream on": b"@OK stream=on\n@CSV,7,2500000,2508,100653,67367\n",
                           "stream off": b"@OK stream=off\n"})
        with patch.object(logger_cli, "RollingPlot", FakeGraph):
            logger_cli.stream(logger_cli.Logger(port, 1), 2)
        self.assertEqual(port.commands, ["stream on", "stream off"])
        if FakeGraph.instance is None:
            raise AssertionError("plot was not created")
        self.assertEqual(FakeGraph.instance.samples, [sample])

    def test_main_uses_serial_and_returns_error_for_device_rejection(self):
        port = FakeSerial({"status": b"@STATUS rate_hz=10\n@END STATUS\n",
                           "config reset": b"@ERR stop first\n"})
        with patch.object(logger_cli.serial, "Serial", return_value=port) as opened:
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(logger_cli.main(["--port", "fake", "status"]), 0)
            opened.assert_called_with("fake", 115200, timeout=0.05, write_timeout=1)
            self.assertIn("@STATUS rate_hz=10", output.getvalue())
            with contextlib.redirect_stderr(io.StringIO()) as error:
                self.assertEqual(logger_cli.main(["--port", "fake", "config", "reset"]), 1)
            self.assertIn("stop first", error.getvalue())

    def test_stream_stop_is_attempted_if_start_wait_is_interrupted(self):
        class InterruptedLogger:
            def __init__(self):
                self.commands = []

            def command(self, text, **kwargs):
                self.commands.append(text)
                if text == "stream on":
                    raise KeyboardInterrupt

        client = InterruptedLogger()
        with self.assertRaises(KeyboardInterrupt):
            logger_cli.stream(client, 10, plot=False)
        self.assertEqual(client.commands, ["stream on", "stream off"])


if __name__ == "__main__":
    unittest.main()
