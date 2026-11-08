# Hardware-in-the-loop tests

The tests in `tests/hil/` are **opt-in**. Without `--hil-port` they skip without
opening or discovering any serial port. Supplying a port explicitly enables
real hardware operations; an inaccessible or incorrect port is a failure, not
a skip. No board was attached or tested by the agent.

## Preparation and side effects

1. Build with `scripts/check.sh` and manually flash the correct UF2 as described
   in `FLASHING.md`. Connect the BME280 and an expendable FAT32 SD card using
   `BME280.md` and `SD_LOGGING.md`; use a stable supply and data-capable USB.
2. Back up the card and current settings. Confirm `config get` reports identical
   saved and active settings, with the BME channel enabled, and that logging is
   either running or stopped, not transitioning. Disable any pre-existing live
   stream: the suite leaves streaming off because its previous state is not
   reported by firmware.
3. The tests create new numbered SD logs, change runtime rate, write the saved
   `rate` and `start_on_boot` settings, and reboot at least once. Restoration
   attempts to restore saved settings, active settings, runtime rate and the
   original running/stopped state, including a second reboot when needed. Flash
   wear and log files remain. If settings initially used compiled defaults with
   no valid flash slot, restoring their values creates a slot; the prior absence
   of a slot cannot be restored through the command interface.
4. Interruptions, USB loss, power failure or a failed cleanup can prevent
   restoration. Inspect `config get` and `status` afterward and manually restore
   from the backup if necessary. Do not remove power or the card during writes.

## Run

```sh
.venv/bin/python -m pytest -q tests/hil
.venv/bin/python -m pytest -q -s tests/hil --hil-port /dev/cu.usbmodem...
```

Replace the example with the exact port selected by the operator. The first
command skips all six HIL cases. The second validates the boot banner; runs 5,
10 and 25 Hz streaming for at least 20 seconds each; checks contiguous sequence
numbers and both device-timestamp and host-receive rates within 1%; starts and
stops logging; downloads a newly closed log with CRC32 transport validation and
checks **every** record CRC16, timestamp and sequence; then changes two saved
settings, reboots by command and verifies active and saved settings. Host USB
scheduling or dropped stream frames can cause a rate failure even if the timer
itself is accurate. Jitter, throughput, write latency and power remain unmeasured
until observed on the actual setup.

Default `scripts/check.sh` runs helper tests and verifies that the HIL cases
skip; it never supplies a port. HIL download scratch files are created under
`.agent-local/` and removed on normal completion or error.
