# Flashing and USB serial

These are manual instructions; no board or flashing has been tested by the
agent. Use a data-capable USB cable. The default is the Pico/RP2040; choose the
Pico W build only for a Pico W. See `BME280.md` for sensor wiring and checks;
leave storage disconnected.

## Build

After `scripts/setup.sh`, run `cmake --preset firmware` and
`cmake --build --preset firmware`. The output is
`build/firmware/picologger.uf2`. For a Pico W use a separate build directory:

```sh
cmake -S . -B build/firmware-w -DPICO_BOARD=pico_w \
  -DPICO_SDK_PATH="$PWD/third_party/pico-sdk" \
  -DPICO_TOOLCHAIN_PATH="$PWD/.toolchain/arm-gnu" \
  -DPICOTOOL_FORCE_FETCH_FROM_GIT=ON
cmake --build build/firmware-w --parallel 2
```

The W output is `build/firmware-w/picologger.uf2`. The version is obtained at
build time with `git describe --tags --always --dirty`, or `unknown` if Git or
repository metadata is unavailable. `-dirty` indicates tracked modifications.

## Drag-and-drop UF2

1. Disconnect USB. Hold BOOTSEL while connecting the Pico by USB, then release
   BOOTSEL. The `RPI-RP2` mass-storage volume should appear.
2. Copy the matching `picologger.uf2` to the volume. It should eject as the Pico
   reboots into the firmware. This replaces the existing firmware.

## picotool

With a USB-capable project-local picotool (the SDK-built copy may lack USB
support if libusb was unavailable when built), enter BOOTSEL as above and run:

```sh
/path/to/project-local/picotool load -v -x build/firmware/picologger.uf2
```

Replace the path and UF2 with the actual local picotool and matching build. The
`-v` option verifies the write and `-x` starts the program. A picotool built
without libusb does not support `load`; use drag-and-drop or build a USB-enabled
copy with project-local libusb. OS permissions may need configuration by the
operator; the agent does not change them.

## Serial console

After the firmware starts, identify the new USB serial device. On macOS run
`ls /dev/cu.usbmodem*` and open it with `screen /dev/cu.usbmodem... 115200`.
On Linux check `/dev/ttyACM*`, then use `screen /dev/ttyACM0 115200` with the
correct device name. Close screen with `Ctrl-A` then `K`, and confirm. The baud
rate is conventional for USB CDC and does not control the USB transfer rate.
If access is denied, use an appropriately configured account/device permission.

The banner is emitted when a CDC terminal connects and again on reconnect:

```text
PicoLogger version: <git description>
Board ID: <16 hexadecimal digits>
Reset reason: <reason>
```

RP2040's watchdog information cannot reliably distinguish power-on from RUN
reset or all other causes. UF2 loading may be reported as a watchdog/software
reset. If LED initialization or timer registration fails, an additional
`Heartbeat initialization failed` line is printed.

## Manual checklist (not performed)

1. Build for the exact Pico or Pico W, then flash its UF2 using one method above.
   For sensor wiring and checks, follow the separate `BME280.md` checklist.
2. Confirm the onboard LED alternates about 500 ms on and 500 ms off without a
   serial terminal connected. This timing has not been measured.
3. Open a serial terminal and confirm the banner has a git version, 16-digit
   board ID and reset reason. Close and reopen it and confirm the banner repeats.
4. Power-cycle or use RUN reset and reconnect; confirm a plausible reset reason.
   Follow `WATCHDOG.md` for the hidden deliberate HardFault test and watchdog limits.
5. If a Pico W is available, repeat with the Pico W-specific build and check its
   onboard CYW43 LED and CDC banner. No wireless network is configured.
6. For timer sampling and GP15 logic-analyzer checks, follow `SAMPLING.md`.
7. For GP26 ADC wiring and DMA integrity checks, follow `ADC_DMA.md`.
8. For microSD wiring, logging and failure checks, follow `SD_LOGGING.md`.
9. For ten power-loss trials and log recovery checks, follow `POWER_LOSS.md`.
10. For USB commands and checksummed file transfer, follow `USB_COMMANDS.md`.
11. For persistent settings and flash safety, follow `SETTINGS.md`.
12. For watchdog and fault-report checks, follow `WATCHDOG.md`.
13. For dual-core jitter comparison, follow `DUAL_CORE.md`.
