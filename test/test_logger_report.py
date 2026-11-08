import subprocess
import sys

result = subprocess.run([sys.argv[1]], capture_output=True, text=True, check=True)
expected = "SD online=1 dropped=5 uncertain=2 worst_write_us=12000 worst_sync_us=6000"
if expected not in result.stdout:
    raise AssertionError(f"missing SD status: {result.stdout}")
print("SD batch backpressure and loss reporting checks passed")
