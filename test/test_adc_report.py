import subprocess
import sys

result = subprocess.run([sys.argv[1]], text=True, capture_output=True, check=True)
expected = ("ADC blocks=3 samples=768 effective_hz=6666 seq_gaps=1 "
            "fifo_overrun=1 fifo_underrun=0 sample_errors=1 min=100 max=300 last=300 fault=2")
if expected not in result.stdout:
    raise AssertionError(f"missing ADC rate/loss report: {result.stdout}")
print("ADC effective-rate and loss report checks passed")
