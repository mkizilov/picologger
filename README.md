# PicoLogger

C11 Raspberry Pi Pico firmware and native portable-code tests. Run
`scripts/setup.sh` once, then `scripts/check.sh`. See [AGENTS.md](AGENTS.md)
for prerequisites, exact build commands and hardware pin assignments, and
[DESIGN.md](DESIGN.md) for the planned architecture. The SDK is pinned to
2.1.1; Unity 2.6.1 and the SDK's matching picotool are fetched at build time.
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

## Initial environment verification

The initial sandbox could not resolve `github.com`, did not provide CMake on
PATH, and denied nested shell directory traversal and system linker libraries.
Consequently setup, SDK submodule initialization, Unity tests, picotool and the
firmware build have not been verified here. The SDK gitlink records release
2.1.1 but its checkout and nested submodules still need `scripts/setup.sh` in an
environment where those operations are permitted. The portable heartbeat source
was compiled to an object with the host compiler and strict warning flags; it
was not linked or executed in this sandbox.

An empty `third_party/pico-sdk/` directory preserves a clean Git status for the
uninitialized submodule; it is not an SDK checkout. If the directory is missing,
Git may report the gitlink as deleted. Recreate the directory or initialize the
submodule rather than committing that deletion. A subsequent check in the same
sandbox again found GitHub DNS resolution unavailable and CMake absent.

## Manual hardware checklist (not performed)

1. Build and flash `build/firmware/picologger.uf2` to the Pico using the guide. Connect USB; follow the separate BME280 checklist if connecting a sensor.
2. Expect the onboard LED to alternate approximately 500 ms on and off. Open the USB serial console and expect version, board ID and reset reason lines; reconnect the console to see them again.
3. For Pico W, separately build for `pico_w`, flash that UF2 and repeat the LED and serial checks. No physical board has been tested.
4. Follow [docs/BME280.md](docs/BME280.md) for sensor wiring, scan, readings and chip-ID troubleshooting.
5. Follow [docs/SAMPLING.md](docs/SAMPLING.md) for the GP15 timing trace and interval-statistics checks.
6. Follow [docs/ADC_DMA.md](docs/ADC_DMA.md) for safe analog input wiring and ADC/DMA checks.
7. Follow [docs/SD_LOGGING.md](docs/SD_LOGGING.md) for SD wiring, rotation and removal checks.
8. Follow [docs/POWER_LOSS.md](docs/POWER_LOSS.md) for ten manual power-loss trials.
