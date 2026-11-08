# PicoLogger

C11 Raspberry Pi Pico firmware and native portable-code tests. Run
`scripts/setup.sh` once, then `scripts/check.sh`. See [AGENTS.md](AGENTS.md)
for prerequisites, exact build commands and hardware pin assignments, and
[DESIGN.md](DESIGN.md) for the planned architecture. The SDK is pinned to
2.1.1; Unity 2.6.1 and the SDK's matching picotool are fetched at build time.
The firmware currently implements only an LED heartbeat and USB startup message.

## Initial environment verification

The initial sandbox could not resolve `github.com`, did not provide CMake on
PATH, and denied nested shell directory traversal and system linker libraries.
Consequently setup, SDK submodule initialization, Unity tests, picotool and the
firmware build have not been verified here. The SDK gitlink records release
2.1.1 but its checkout and nested submodules still need `scripts/setup.sh` in an
environment where those operations are permitted. The portable heartbeat source
was compiled to an object with the host compiler and strict warning flags; it
was not linked or executed in this sandbox.

## Manual hardware checklist (not performed)

1. Build the firmware and flash `build/firmware/picologger.uf2` to a Pico/RP2040 using its BOOTSEL mode. Connect the Pico to a computer over USB; no external sensor, SD module or ADC input is needed for this scaffold.
2. Expect the onboard LED to alternate approximately 500 ms on and 500 ms off. Connect a USB serial terminal and reset the board; `PicoLogger starting` may appear, although a terminal connecting after boot can miss it.
3. For later hardware work, connect the BME280, microSD and timing/ADC pins as listed in `AGENTS.md`; they are not driven or tested by this scaffold. Verify supply voltage and module wiring before powering them.
