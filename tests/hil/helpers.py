"""Protocol and validation helpers; importing this module never opens hardware."""

from pathlib import Path
import re
import struct
import sys
import time

import serial

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from tools.decode_log import HEADER_SIZE, HEADER_V2_SIZE, RECORD_SIZE, crc16, read_header  # noqa: E402
from tools.logger_cli import Logger, ProtocolError, parse_sample  # noqa: E402


def parse_fields(lines, prefix):
    fields = {}
    for line in lines:
        words = line.split()
        if not words or words[0] != prefix:
            continue
        for item in words[1:]:
            key, separator, value = item.partition("=")
            if separator:
                fields[key] = value
    return fields


def parse_settings(lines):
    result = {"saved": {}, "active": {}}
    for line in lines:
        parts = line.split()
        if len(parts) >= 3 and parts[0] == "@CONFIG" and parts[1] in result:
            for item in parts[2:]:
                key, separator, value = item.partition("=")
                if separator:
                    result[parts[1]][key] = int(value)
    required = {"rate", "channels", "rotate_bytes", "start_on_boot"}
    if any(set(values) != required for values in result.values()):
        raise ValueError("incomplete settings response")
    return result


def check_rate(samples, expected):
    if len(samples) < 2:
        raise ValueError("not enough samples")
    for (previous, _), (current, _) in zip(samples, samples[1:]):
        if (current[0] - previous[0]) & 0xffffffff != 1:
            raise ValueError("stream sequence gap or duplicate")
        if current[1] <= previous[1]:
            raise ValueError("stream timestamps are not increasing")
    count = len(samples) - 1
    device_seconds = samples[-1][0][1] - samples[0][0][1]
    host_seconds = samples[-1][1] - samples[0][1]
    if host_seconds <= 0:
        raise ValueError("host timestamps are not increasing")
    device_hz, host_hz = count / device_seconds, count / host_seconds
    if abs(device_hz / expected - 1) > 0.01 or abs(host_hz / expected - 1) > 0.01:
        raise ValueError(f"rate outside 1%: device={device_hz:.6f} Hz host={host_hz:.6f} Hz expected={expected}")
    return device_hz, host_hz


def validate_log(path, expected_rate, minimum_records):
    with path.open("rb") as source:
        header = source.read(HEADER_SIZE)
        if len(header) == HEADER_SIZE and header[:8] in (b"PLOG\x02\x00\x00\x02", b"PLOG\x03\x00\x00\x02"):
            header += source.read(HEADER_V2_SIZE - HEADER_SIZE)
        _, rate, _, _, _ = read_header(header)
        if rate != expected_rate:
            raise ValueError(f"log rate {rate} does not match {expected_rate}")
        previous = None
        previous_time = None
        records = 0
        while True:
            record = source.read(RECORD_SIZE)
            if not record:
                break
            if len(record) != RECORD_SIZE:
                raise ValueError(f"truncated record {records}")
            if crc16(record[:30]) != struct.unpack_from("<H", record, 30)[0]:
                raise ValueError(f"CRC failure at record {records}")
            sequence, timestamp = struct.unpack_from("<IQ", record)
            if previous is not None and sequence != (previous + 1) & 0xffffffff:
                raise ValueError(f"sequence gap at record {records}")
            if previous_time is not None and timestamp <= previous_time:
                raise ValueError(f"timestamp did not advance at record {records}")
            previous, previous_time = sequence, timestamp
            records += 1
    if records < minimum_records:
        raise ValueError(f"only {records} records; expected at least {minimum_records}")
    return records


class Device:
    def __init__(self, port):
        self.name = port
        self.port = None
        self.logger = None
        self.banner = ""
        self.connect(30)

    def connect(self, timeout):
        deadline = time.monotonic() + timeout
        last_error = None
        while time.monotonic() < deadline:
            port = None
            try:
                port = serial.Serial(self.name, 115200, timeout=0.05, write_timeout=1)
                data = bytearray()
                banner_deadline = min(deadline, time.monotonic() + 3)
                while time.monotonic() < banner_deadline:
                    data.extend(port.read(min(max(port.in_waiting, 1), 4096)))
                    text = data.decode("ascii", errors="replace")
                    if banner_is_valid(text) and re.search(r"(?m)^Reset reason:[^\r\n]*[\r\n]", text):
                        self.port = port
                        self.logger = Logger(port, 20)
                        self.banner = text
                        return
                    if len(data) > 65536:
                        raise ProtocolError("boot banner not found")
                port.close()
                last_error = "boot banner not received"
            except (OSError, serial.SerialException, ProtocolError) as exc:
                last_error = str(exc)
                if port is not None and port.is_open:
                    port.close()
            time.sleep(0.25)
        raise RuntimeError(f"could not connect to {self.name}: {last_error}")

    def close(self):
        if self.port is not None:
            self.port.close()
        self.port = self.logger = None

    def command(self, text, prefix="@OK "):
        return self._client().command(text, ok_prefix=prefix)

    def _client(self):
        if self.logger is None:
            raise RuntimeError("serial connection is not open")
        return self.logger

    def status(self):
        return parse_fields(self._client().command("status", "@END STATUS"), "@STATUS")

    def settings(self):
        return parse_settings(self._client().command("config get", "@END CONFIG"))

    def reboot(self):
        failure = None
        try:
            self.command("reboot", "@OK reboot")
        except (OSError, serial.SerialException, ProtocolError) as exc:
            failure = exc
        finally:
            self.close()
        time.sleep(0.5)
        self.connect(30)
        if failure:
            raise RuntimeError(f"reboot acknowledgment failed: {failure}") from failure

    def collect(self, rate, seconds=20):
        try:
            self.command("stream on", "@OK stream=on")
            samples = []
            deadline = time.monotonic() + seconds + 15
            target = rate * seconds + 1
            while len(samples) < target:
                frame = self._client().next_frame(deadline)
                sample = parse_sample(frame)
                if sample is not None:
                    samples.append((sample, time.monotonic()))
            return samples
        finally:
            self.command("stream off", "@OK stream=off")


def banner_is_valid(text):
    return bool(re.search(r"(?m)^PicoLogger version:[ \t]*\S+", text) and
                re.search(r"(?m)^Board ID:[ \t]*[0-9a-fA-F]{16}\b", text) and
                re.search(r"(?m)^Reset reason:[ \t]*[^\s\r\n]+", text))
