import subprocess
import sys

result = subprocess.run([sys.argv[1]], capture_output=True, text=True, check=True)
expected = ("Sampling interval_us count=498 min=20000 max=40000 "
            "mean=20040 missed=1 ring_overflows=0 read_errors=0")
if expected not in result.stdout:
    raise AssertionError(f"missing expected statistics: {result.stdout}")
if "BME280 read unavailable (status 1)" not in result.stdout:
    raise AssertionError(f"missing I2C failure report: {result.stdout}")
error_window = ("Sampling interval_us count=1 min=20000 max=20000 mean=20000 "
                "missed=0 ring_overflows=0 read_errors=1")
if error_window not in result.stdout:
    raise AssertionError(f"missing failure count: {result.stdout}")
empty_window = ("Sampling interval_us count=0 min=0 max=0 mean=0 "
                "missed=0 ring_overflows=0 read_errors=0")
if empty_window not in result.stdout:
    raise AssertionError(f"missing empty window: {result.stdout}")
print("sampling report, missed trigger and I2C error checks passed")
