from pathlib import Path
import tempfile
import time

import pytest

from .helpers import ROOT, banner_is_valid, check_rate, validate_log

pytestmark = pytest.mark.hil


def test_boot_banner(device):
    assert banner_is_valid(device.banner), device.banner


@pytest.mark.parametrize("rate", [5, 10, 25])
def test_streaming_rate_within_one_percent(device, rate, record_property):
    device.command("stop", "@OK stopped")
    device.command(f"rate {rate}", f"@OK rate={rate}")
    device.command("start", "@OK started")
    samples = device.collect(rate, seconds=20)
    device_hz, host_hz = check_rate(samples, rate)
    record_property("device_hz", device_hz)
    record_property("host_hz", host_hz)


def test_start_stop_download_and_validate_every_record(device):
    previous = device.status().get("file", "-")
    device.command("stop", "@OK stopped")
    device.command("rate 25", "@OK rate=25")
    device.command("start", "@OK started")
    deadline = time.monotonic() + 20
    filename = None
    while time.monotonic() < deadline:
        status = device.status()
        candidate = status.get("file", "-")
        if status.get("sd") == "1" and candidate not in ("-", previous):
            filename = candidate
            break
        time.sleep(0.1)
    assert filename, "SD did not create a new log within 20 seconds"
    time.sleep(3)
    device.command("stop", "@OK stopped")
    workspace = ROOT / ".agent-local"
    workspace.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="hil-download-", dir=workspace) as directory:
        downloaded = Path(directory) / filename
        device.logger.download(filename, downloaded)
        assert validate_log(downloaded, expected_rate=25, minimum_records=40) >= 40


def test_settings_persist_across_command_reboot(device):
    device.command("stop", "@OK stopped")
    before = device.settings()
    rate = next(value for value in (10, 11, 12) if value not in (before["saved"]["rate"], before["active"]["rate"]))
    start = 1 - before["saved"]["start_on_boot"]
    device.command(f"config set rate {rate}", "@OK config ")
    device.command(f"config set start_on_boot {start}", "@OK config ")
    changed = device.settings()
    assert changed["saved"]["rate"] == rate
    assert changed["saved"]["start_on_boot"] == start
    assert changed["active"] == before["active"]
    device.reboot()
    assert banner_is_valid(device.banner)
    after = device.settings()
    assert after["saved"] == changed["saved"]
    assert after["active"] == changed["saved"]
