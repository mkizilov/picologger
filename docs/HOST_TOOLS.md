# Host tools and local checks

Run `scripts/setup.sh` to install the pinned Python dependencies in `.venv`
(Python 3.9–3.12), the project-local SDK, Arm toolchain and picotool. The host
CLI requires pyserial and matplotlib; a graphical matplotlib backend is needed
for the plot. No board or serial device was accessed during development.

```sh
.venv/bin/python tools/logger_cli.py --port /dev/cu.usbmodem... status
.venv/bin/python tools/logger_cli.py --port /dev/cu.usbmodem... config get
.venv/bin/python tools/logger_cli.py --port /dev/cu.usbmodem... config set rate 10
.venv/bin/python tools/logger_cli.py --port /dev/cu.usbmodem... config reset
.venv/bin/python tools/logger_cli.py --port /dev/cu.usbmodem... stream --window 300
.venv/bin/python tools/logger_cli.py --port /dev/cu.usbmodem... download LOG_0001.BIN log.bin
```

Replace the example port and filename. Configuration writes require stopped
logging and apply after reboot; the CLI waits for the final acknowledgment and
does not stop or restart logging automatically. Streaming displays a rolling
temperature/pressure/humidity plot; close the window or use Ctrl-C to request
`stream off`. `stream --no-plot` prints CSV frames until interrupted. Plot x-axis
timestamps are seconds since device boot. Output is best-effort: consult device
`stream_dropped` and sample sequence numbers for loss. `--timeout` sets the
inactivity timeout for commands and downloads, not for an idle live stream.

Downloads validate filename, chunk offsets, base64, length and CRC32 before
atomically publishing the output. Existing files are never overwritten; an
incomplete or corrupt transfer removes its temporary file. The output directory
must support hard links. Stop logging before downloading the active file; see
`USB_COMMANDS.md` for the protocol and storage limitations.

Run `scripts/check.sh` after setup. It builds the host suite with AddressSanitizer
and UndefinedBehaviorSanitizer and runs all CTest cases (including the existing
Python decoder tests), builds `build/pico/picologger.uf2` and
`build/pico_w/picologger.uf2`, runs clang-tidy over every project C source and
both board-specific variants, and runs the Python CLI tests. At most two build,
test or analyzer workers are used. Clang-tidy uses the generated target compile
database and local Arm headers; core, Unix and security analyzer checks are
enabled, with warnings treated as errors. Its Annex K deprecated-buffer check is
excluded because newlib lacks those replacements and that checker flags bounded
`snprintf` calls. Compiler diagnostics and other enabled checks remain active.
The script also runs HIL helper tests and confirms the hardware cases skip
without an explicit port; see `HIL.md` before an operator enables them.

## Manual checklist (not performed)

1. Flash the matching UF2 using `FLASHING.md`, wire the BME280 and expendable
   SD card, and identify the correct USB CDC port.
2. Run `status` and `config get`; compare output with a serial terminal. With
   logging stopped, test `config set` and confirm the saved value after reboot.
3. Start logging, run `stream` and confirm the plot updates; close the window
   and check that streaming stops. Compare sequence/drop counters with `status`.
4. Stop logging and download a completed log to a new file. Decode it using
   `tools/decode_log.py` and inspect CRC and sequence-gap diagnostics. An active
   file, disconnected board, corrupted transfer or existing output must fail
   without overwriting data.
