#!/usr/bin/env python3
"""Control PicoLogger over USB CDC, plot live readings, or verify a download."""

import argparse
import binascii
from collections import deque
import math
import os
import re
import sys
import time
from pathlib import Path

import serial

from receive_dump import receive_lines


class ProtocolError(Exception):
    pass


class Logger:
    def __init__(self, port, timeout):
        self.port = port
        self.timeout = timeout
        self.buffer = bytearray()
        self.frames = deque()

    def send(self, command):
        data = (command + "\n").encode("ascii")
        if self.port.write(data) != len(data):
            raise ProtocolError("short serial write")

    def poll(self):
        count = min(max(self.port.in_waiting, 1), 4096)
        self.buffer.extend(self.port.read(count))
        while b"\n" in self.buffer:
            line, _, rest = self.buffer.partition(b"\n")
            self.buffer = bytearray(rest)
            line = line.strip(b"\r")
            if len(line) > 4096:
                raise ProtocolError("serial line exceeds 4096 bytes")
            if line.startswith(b"@"):
                try:
                    self.frames.append(line.decode("ascii"))
                except UnicodeDecodeError as exc:
                    raise ProtocolError("non-ASCII protocol frame") from exc
        if len(self.buffer) > 4096:
            raise ProtocolError("serial line exceeds 4096 bytes")

    def next_frame(self, deadline):
        while not self.frames:
            if time.monotonic() >= deadline:
                raise ProtocolError("timed out waiting for logger")
            self.poll()
        frame = self.frames.popleft()
        if frame.startswith("@ERR "):
            raise ProtocolError(frame[1:])
        return frame

    def command(self, command, end=None, ok_prefix="@OK "):
        self.send(command)
        deadline = time.monotonic() + self.timeout
        lines = []
        while True:
            frame = self.next_frame(deadline)
            if end:
                if frame == end:
                    return lines
                if frame.startswith("@" + end[5:] + " "):
                    lines.append(frame)
            elif frame.startswith(ok_prefix):
                return [frame]

    def download(self, filename, output):
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,32}", filename) or filename.startswith(".") or filename.endswith(".") or ".." in filename:
            raise ValueError("invalid logger filename")
        if output.exists() or output.is_symlink():
            raise ValueError("output already exists")
        self.send("dump " + filename)

        def lines():
            deadline = time.monotonic() + self.timeout
            while True:
                frame = self.next_frame(deadline)
                if frame.startswith(("@BEGIN DUMP ", "@DATA ", "@END DUMP ")):
                    deadline = time.monotonic() + self.timeout
                    yield frame
                    if frame.startswith("@END DUMP "):
                        return

        receive_lines(lines(), output, expected_name=filename)


def parse_sample(frame):
    if not frame.startswith("@CSV,"):
        return None
    fields = frame.split(",")
    if len(fields) != 6:
        raise ProtocolError("invalid CSV frame")
    try:
        sequence, timestamp, temperature, pressure, humidity = map(int, fields[1:])
    except ValueError as exc:
        raise ProtocolError("invalid CSV value") from exc
    if not 0 <= sequence <= 0xffffffff or not 0 <= timestamp <= 0xffffffffffffffff or any(
            value < -0x80000000 or value > 0x7fffffff for value in (temperature, pressure, humidity)):
        raise ProtocolError("CSV value out of range")
    return sequence, timestamp / 1_000_000, temperature / 100, pressure / 100, humidity / 1024


class RollingPlot:
    def __init__(self, window):
        os.environ.setdefault("MPLCONFIGDIR", str(Path(__file__).resolve().parents[1] / ".agent-local" / "matplotlib"))
        import matplotlib.pyplot as plt
        self.plt = plt
        self.samples = deque(maxlen=window)
        self.figure, self.axes = plt.subplots(3, 1, sharex=True)
        self.lines = [axis.plot([], [])[0] for axis in self.axes]
        for axis, label in zip(self.axes, ("Temperature (°C)", "Pressure (hPa)", "Humidity (%RH)")):
            axis.set_ylabel(label)
        self.axes[-1].set_xlabel("Time since boot (s)")
        self.figure.tight_layout()
        plt.ion()
        plt.show(block=False)

    def add(self, sample):
        self.samples.append(sample)

    def update(self):
        if self.samples:
            x = [sample[1] for sample in self.samples]
            for index, (axis, line) in enumerate(zip(self.axes, self.lines), start=2):
                line.set_data(x, [sample[index] for sample in self.samples])
                axis.relim()
                axis.autoscale_view()
            self.figure.canvas.draw_idle()
        self.figure.canvas.flush_events()

    def open(self):
        manager = self.figure.canvas.manager
        return manager is not None and self.plt.fignum_exists(manager.num)


def stream(logger, window, plot=True):
    graph = RollingPlot(window) if plot else None
    try:
        logger.command("stream on", ok_prefix="@OK stream=on")
        last_draw = 0.0
        while graph is None or graph.open():
            logger.poll()
            while logger.frames:
                frame = logger.frames.popleft()
                if frame.startswith("@ERR "):
                    raise ProtocolError(frame[1:])
                sample = parse_sample(frame)
                if sample is not None:
                    if graph:
                        graph.add(sample)
                    else:
                        print(frame[1:], flush=True)
            if graph and time.monotonic() - last_draw >= 0.1:
                graph.update()
                last_draw = time.monotonic()
    finally:
        try:
            logger.command("stream off", ok_prefix="@OK stream=off")
        except (ProtocolError, serial.SerialException, OSError) as exc:
            print(f"warning: could not disable stream: {exc}", file=sys.stderr)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="USB CDC serial port")
    parser.add_argument("--timeout", type=float, default=15, help="inactivity timeout in seconds")
    commands = parser.add_subparsers(dest="action", required=True)
    commands.add_parser("status")
    config = commands.add_parser("config")
    config_commands = config.add_subparsers(dest="operation", required=True)
    config_commands.add_parser("get")
    config_commands.add_parser("reset")
    setting = config_commands.add_parser("set")
    setting.add_argument("key", choices=("rate", "channels", "rotate_bytes", "start_on_boot"))
    setting.add_argument("value", type=int)
    live = commands.add_parser("stream")
    live.add_argument("--window", type=int, default=300, help="number of samples retained")
    live.add_argument("--no-plot", action="store_true", help="print CSV instead of opening a window")
    download = commands.add_parser("download")
    download.add_argument("filename")
    download.add_argument("output", type=Path)
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0 or (args.action == "stream" and not 1 <= args.window <= 100000):
        parser.error("timeout must be positive and window must be 1–100000")
    try:
        with serial.Serial(args.port, 115200, timeout=0.05, write_timeout=1) as port:
            logger = Logger(port, args.timeout)
            if args.action == "status":
                lines = logger.command("status", "@END STATUS")
            elif args.action == "config":
                command = "config " + args.operation
                if args.operation == "set":
                    command += f" {args.key} {args.value}"
                lines = logger.command(command, "@END CONFIG" if args.operation == "get" else None,
                                       ok_prefix="@OK config ")
            elif args.action == "download":
                logger.download(args.filename, args.output)
                lines = [f"verified dump written to {args.output}"]
            else:
                stream(logger, args.window, not args.no_plot)
                lines = []
            for line in lines:
                print(line)
    except (OSError, ValueError, binascii.Error, ProtocolError, serial.SerialException) as exc:
        print(f"logger error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main())
