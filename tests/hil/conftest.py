import pytest

from .helpers import Device


@pytest.fixture(scope="session")
def device(hil_port):
    board = Device(hil_port)
    original = None
    original_status = None
    armed = False
    try:
        original = board.settings()
        original_status = board.status()
        if original["saved"] != original["active"]:
            pytest.fail("saved and active settings differ; resolve pending settings before HIL")
        if not original["active"]["channels"] & 1:
            pytest.fail("HIL requires BME280 enabled in the active settings")
        if original_status.get("log") not in ("running", "stopped") or "rate_hz" not in original_status:
            pytest.fail("incomplete initial status or logging transition in progress")
        armed = True
        yield board
    finally:
        try:
            if armed and original is not None and original_status is not None:
                if board.port is None or not board.port.is_open:
                    board.connect(30)
                board.command("stream off", "@OK stream=off")
                board.command("stop", "@OK stopped")
                current = board.settings()
                changed = False
                for key in ("rate", "start_on_boot"):
                    if current["saved"][key] != original["saved"][key]:
                        board.command(f"config set {key} {original['saved'][key]}", "@OK config ")
                        changed = True
                if changed or current["active"] != original["active"]:
                    board.reboot()
                    board.command("stop", "@OK stopped")
                    if board.settings() != original:
                        raise RuntimeError("original settings did not restore")
                board.command(f"rate {int(original_status['rate_hz'])}", "@OK rate=")
                if original_status["log"] == "running":
                    board.command("start", "@OK started")
        except Exception as exc:
            pytest.fail(f"HIL cleanup could not restore original state: {exc}", pytrace=False)
        finally:
            board.close()
