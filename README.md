# PicoLogger

C11 Raspberry Pi Pico firmware and native portable-code tests. Run
`scripts/setup.sh` once, then `scripts/check.sh`. See [AGENTS.md](AGENTS.md)
for prerequisites, exact build commands and hardware pin assignments, and
[DESIGN.md](DESIGN.md) for the planned architecture. The SDK is pinned to
2.1.1; Unity 2.6.1 is fetched at build time and SDK-compatible picotool 2.1.1
is installed locally by setup.
The firmware currently implements a timer-driven LED heartbeat and a USB CDC
boot banner. See [docs/FLASHING.md](docs/FLASHING.md) for build, flashing, serial
console and manual verification instructions. The firmware scans I²C0 and
prints BME280 readings; see [docs/BME280.md](docs/BME280.md) for wiring and
sensor-specific checks.
Timer sampling configuration and GP15 measurements are described in
[docs/SAMPLING.md](docs/SAMPLING.md).
The 10 kHz GP26 ADC/DMA path and its integrity checks are described in
[docs/ADC_DMA.md](docs/ADC_DMA.md).

The binary log format and Python CSV decoder are described in
[docs/LOG_FORMAT.md](docs/LOG_FORMAT.md); SD wiring, buffering and removal
limitations are in [docs/SD_LOGGING.md](docs/SD_LOGGING.md).
Best-effort power-loss recovery and its ten-cycle manual test are described in
[docs/POWER_LOSS.md](docs/POWER_LOSS.md).
The USB CDC commands, live stream and checksummed dump are documented in
[docs/USB_COMMANDS.md](docs/USB_COMMANDS.md).
Persistent settings and flash safety are documented in
[docs/SETTINGS.md](docs/SETTINGS.md).
Watchdog and retained HardFault reporting are documented in
[docs/WATCHDOG.md](docs/WATCHDOG.md).
Dual-core ownership and the before/after jitter procedure are in
[docs/DUAL_CORE.md](docs/DUAL_CORE.md).
Low-rate power behavior and an unmeasured current table are in
[docs/POWER.md](docs/POWER.md).
The serial CLI, verified downloads, live plot and local checks are documented in
[docs/HOST_TOOLS.md](docs/HOST_TOOLS.md).

## Local environment

The initial sandbox lacked GitHub DNS and CMake. After network access was
repaired, `scripts/setup.sh` initialized the pinned SDK and project-local Arm
toolchain; real Unity host tests and the Pico firmware build were run. CMake
3.31.10 was installed in `.venv` for these checks. For a local installation,
make it available with `PATH="$PWD/.venv/bin:$PATH"` before `scripts/check.sh`.
Picotool was built without USB support; UF2 drag-and-drop remains the documented
manual flashing method. No physical board or power measurements were tested.

## Manual hardware checklist (not performed)

1. Build and flash `build/firmware/picologger.uf2` to the Pico using the guide. Connect USB; follow the separate BME280 checklist if connecting a sensor.
2. Expect the onboard LED to alternate approximately 500 ms on and off. Open the USB serial console and expect version, board ID and reset reason lines; reconnect the console to see them again.
3. For Pico W, separately build for `pico_w`, flash that UF2 and repeat the LED and serial checks. No physical board has been tested.
4. Follow [docs/BME280.md](docs/BME280.md) for sensor wiring, scan, readings and chip-ID troubleshooting.
5. Follow [docs/SAMPLING.md](docs/SAMPLING.md) for the GP15 timing trace and interval-statistics checks.
6. Follow [docs/ADC_DMA.md](docs/ADC_DMA.md) for safe analog input wiring and ADC/DMA checks.
7. Follow [docs/SD_LOGGING.md](docs/SD_LOGGING.md) for SD wiring, rotation and removal checks.
8. Follow [docs/POWER_LOSS.md](docs/POWER_LOSS.md) for ten manual power-loss trials.
9. Follow [docs/USB_COMMANDS.md](docs/USB_COMMANDS.md) for command, stream and dump checks.
10. Follow [docs/SETTINGS.md](docs/SETTINGS.md) for configuration and flash checks.
11. Follow [docs/WATCHDOG.md](docs/WATCHDOG.md) for watchdog and HardFault checks.
12. Follow [docs/DUAL_CORE.md](docs/DUAL_CORE.md) for dual-core and jitter comparison checks.
13. Follow [docs/POWER.md](docs/POWER.md) for USB power-meter measurements and low-rate checks.
14. Follow [docs/HOST_TOOLS.md](docs/HOST_TOOLS.md) for host CLI checks.
