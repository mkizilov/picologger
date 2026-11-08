import struct
import tempfile
from copy import deepcopy
from pathlib import Path
from unittest.mock import patch

import pytest

from tests.hil.helpers import ROOT, Device, banner_is_valid, check_rate, parse_fields, parse_settings, validate_log
from tests.hil import conftest as hil_fixtures
from tests.hil import test_board as hil_tests
from tools.decode_log import crc16


def make_log(sequences, rate=25, version=3):
    size = 512 if version == 3 else 60
    header = bytearray(size)
    struct.pack_into("<4sHH", header, 0, b"PLOG", version, size)
    header[8:16] = b"fixture\0"
    struct.pack_into("<IB", header, 40, rate, 1)
    struct.pack_into("<H", header, 48, 1)
    if version == 3:
        struct.pack_into("<H", header, 510, crc16(header[:510]))
    data = bytearray(header)
    for index, sequence in enumerate(sequences):
        record = struct.pack("<IQ4iH", sequence, 1000000 + index * 40000, index, 0, 0, 0, 0)
        data.extend(record + struct.pack("<H", crc16(record)))
    return data


def test_banner_status_and_settings_parsing():
    text = "PicoLogger version: abc\r\nBoard ID: 0123456789abcdef\r\nReset reason: watchdog\r\n"
    assert banner_is_valid(text)
    assert not banner_is_valid("PicoLogger version: abc\nBoard ID: 0123\nReset reason: x\n")
    assert not banner_is_valid("PicoLogger version: abc\nBoard ID: 0123456789abcdef\nReset reason:\nOther")
    assert parse_fields(["@STATUS sampling=1 rate_hz=10", "@STATUS log=running sd=1 file=LOG_0001.BIN"],
                        "@STATUS")["rate_hz"] == "10"
    lines = ["@CONFIG saved rate=10 channels=3", "@CONFIG saved rotate_bytes=8192",
             "@CONFIG saved start_on_boot=1", "@CONFIG active rate=10 channels=3",
             "@CONFIG active rotate_bytes=8192", "@CONFIG active start_on_boot=1"]
    assert parse_settings(lines)["active"]["rotate_bytes"] == 8192
    with pytest.raises(ValueError, match="incomplete"):
        parse_settings(lines[:-1])


def test_stream_rates_require_continuity_and_one_percent_on_both_clocks():
    samples = [((i, i / 10, 20, 1000, 50), i / 10) for i in range(101)]
    assert check_rate(samples, 10) == pytest.approx((10, 10))
    bad = list(samples)
    bad[50] = ((51, 5.0, 20, 1000, 50), 5.0)
    with pytest.raises(ValueError, match="sequence"):
        check_rate(bad, 10)
    slow_host = [(sample, host * 1.02) for sample, host in samples]
    with pytest.raises(ValueError, match="outside 1%"):
        check_rate(slow_host, 10)
    slow_device = [((sample[0], sample[1] * 1.02, *sample[2:]), host) for sample, host in samples]
    with pytest.raises(ValueError, match="outside 1%"):
        check_rate(slow_device, 10)
    wrap = [((0xffffffff, 1.0, 0, 0, 0), 1.0), ((0, 1.1, 0, 0, 0), 1.1)]
    assert check_rate(wrap, 10) == pytest.approx((10, 10))


def test_log_validation_checks_all_crcs_sequences_and_truncation():
    workspace = ROOT / ".agent-local"
    workspace.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="hil-unit-", dir=workspace) as directory:
        path = Path(directory) / "test.bin"
        data = make_log([0xfffffffe, 0xffffffff, 0, 1])
        path.write_bytes(data)
        assert validate_log(path, 25, 4) == 4
        corrupted = bytearray(data)
        corrupted[-3] ^= 1
        path.write_bytes(corrupted)
        with pytest.raises(ValueError, match="CRC failure at record 3"):
            validate_log(path, 25, 4)
        path.write_bytes(make_log([1, 2, 4, 5]))
        with pytest.raises(ValueError, match="sequence gap at record 2"):
            validate_log(path, 25, 4)
        path.write_bytes(data[:-1])
        with pytest.raises(ValueError, match="truncated record"):
            validate_log(path, 25, 4)
        path.write_bytes(make_log([1, 2], version=1))
        assert validate_log(path, 25, 2) == 2
        with pytest.raises(ValueError, match="log rate"):
            validate_log(path, 10, 2)


def test_device_connection_waits_for_complete_fragmented_banner():
    class FakePort:
        def __init__(self):
            self.data = bytearray(b"PicoLogger version: abc\r\nBoard ID: 0123456789abcdef\r\nReset reason: watchdog\r\n")
            self.is_open = True

        @property
        def in_waiting(self):
            return len(self.data)

        def read(self, count):
            count = min(count, 3)
            result = self.data[:count]
            del self.data[:count]
            return bytes(result)

        def close(self):
            self.is_open = False

    fake = FakePort()
    with patch("tests.hil.helpers.serial.Serial", return_value=fake):
        device = Device("fake")
        assert banner_is_valid(device.banner)
        assert device.banner.endswith("watchdog\r") or device.banner.endswith("watchdog\r\n")
        device.close()
    assert not fake.is_open


def test_hil_scenarios_and_restore_against_simulated_board():
    class Port:
        is_open = True

        def close(self):
            self.is_open = False

    class SimulatedBoard:
        def __init__(self):
            self.saved = {"rate": 50, "channels": 3, "rotate_bytes": 8192, "start_on_boot": 1}
            self.active = deepcopy(self.saved)
            self.rate = 50
            self.log = "running"
            self.number = 1
            self.banner = "PicoLogger version: abc\nBoard ID: 0123456789abcdef\nReset reason: watchdog\n"
            self.port = Port()
            self.logger = self
            self.commands = []

        def settings(self):
            return {"saved": deepcopy(self.saved), "active": deepcopy(self.active)}

        def status(self):
            return {"log": self.log, "rate_hz": str(self.rate), "sd": "1", "file": f"LOG_{self.number:04}.BIN"}

        def command(self, text, prefix="@OK "):
            self.commands.append(text)
            parts = text.split()
            if text == "stop":
                self.log = "stopped"
            elif text == "start":
                self.log = "running"
                self.number += 1
            elif parts[0] == "rate":
                assert self.log == "stopped"
                self.rate = int(parts[1])
            elif parts[:2] == ["config", "set"]:
                assert self.log == "stopped"
                self.saved[parts[2]] = int(parts[3])
            return [prefix]

        def collect(self, rate, seconds):
            return [((i, i / rate, 20, 1000, 50), i / rate) for i in range(rate * seconds + 1)]

        def download(self, filename, output):
            output.write_bytes(make_log(list(range(75))))

        def reboot(self):
            self.active = deepcopy(self.saved)
            self.rate = self.active["rate"]
            self.log = "running" if self.active["start_on_boot"] else "stopped"
            if self.log == "running":
                self.number += 1

        def close(self):
            self.port.close()

    board = SimulatedBoard()
    original = board.settings()
    properties = []
    with patch.object(hil_fixtures, "Device", return_value=board):
        fixture = getattr(hil_fixtures.device, "__wrapped__")("fake")
        assert next(fixture) is board
        hil_tests.test_boot_banner(board)
        for rate in (5, 10, 25):
            hil_tests.test_streaming_rate_within_one_percent(board, rate, lambda key, value: properties.append((key, value)))
        with patch.object(hil_tests.time, "sleep"):
            hil_tests.test_start_stop_download_and_validate_every_record(board)
        hil_tests.test_settings_persist_across_command_reboot(board)
        with pytest.raises(StopIteration):
            next(fixture)
    assert len(properties) == 6
    assert board.settings() == original
    assert board.log == "running" and board.rate == 50
    assert not board.port.is_open


def test_fixture_refuses_pending_settings_without_changing_board():
    class PendingBoard:
        closed = False

        def settings(self):
            return {"saved": {"rate": 5}, "active": {"rate": 10}}

        def status(self):
            return {"log": "running", "rate_hz": "10"}

        def command(self, *args):
            raise AssertionError("unexpected state-changing command")

        def close(self):
            self.closed = True

    board = PendingBoard()
    with patch.object(hil_fixtures, "Device", return_value=board):
        fixture = getattr(hil_fixtures.device, "__wrapped__")("fake")
        with pytest.raises(pytest.fail.Exception, match="saved and active settings differ"):
            next(fixture)
    assert board.closed


def test_fixture_reports_restore_failure_and_closes_port():
    class BrokenBoard:
        closed = False
        port = type("Port", (), {"is_open": True})()
        values = {"rate": 10, "channels": 3, "rotate_bytes": 8192, "start_on_boot": 0}

        def settings(self):
            return {"saved": self.values.copy(), "active": self.values.copy()}

        def status(self):
            return {"log": "stopped", "rate_hz": "10"}

        def command(self, *args):
            raise RuntimeError("USB lost")

        def close(self):
            self.closed = True

    board = BrokenBoard()
    with patch.object(hil_fixtures, "Device", return_value=board):
        fixture = getattr(hil_fixtures.device, "__wrapped__")("fake")
        assert next(fixture) is board
        with pytest.raises(pytest.fail.Exception, match="could not restore original state: USB lost"):
            next(fixture)
    assert board.closed
