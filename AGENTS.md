# PicoLogger: project boundaries and working agreement

- Work only in `/Users/mk-rune/test_projects/11-picologger`. This is one of fourteen independent repositories.
- Do not read, modify, clean up, or run commands in sibling repositories or the user's configuration.
- Work autonomously: choose sensible defaults and document assumptions; do not ask questions or delegate.
- Never install anything globally or use sudo, Homebrew, launchd, host credentials, or host Docker sockets.
- Dependencies, toolchains, caches, browser binaries, SDKs, temporary files and test data stay in this repository. Preserve the supplied project-local environment. Python packages go in `.venv`.
- `.agent-local/` is ignored, project-local build/runtime state. `.opencode/` is managed orchestration configuration; do not change or remove it or bypass the sandbox.
- Use existing system tools read-only, or install pinned tools under this repository. Do not change shell profiles, user settings, Keychain, or global Git settings.
- Bind test servers to loopback; use ports 22000–22099. Limit build/test workers to two unless a required benchmark explicitly needs more.
- Docker-dependent integration tests must remain reproducible but be reported as unverified here if they require access outside this repository. Do not bypass this boundary.
- Do not touch attached hardware, enroll services, flash firmware, install agents, or alter OS settings. Supply manual checklists for such steps.
- Implement only the current numbered prompt, preserving prior behavior. Run appropriate checks, fix failures, and report genuine limitations without fabricating test or benchmark results.
- Before finishing every numbered turn: stop its servers/watchers, remove scratch outputs, update relevant documentation, and make a local conventional-commit Git commit. Do not push.
- End each turn with a short summary of changes, checks actually run, remaining limitations, and commit hash. The orchestrator will provide the next prompt only after this turn ends.

Extend this file with the project's language-specific architecture and verification commands while retaining these boundaries.

## Hardware context

| Item | Connection |
| --- | --- |
| Board | Raspberry Pi Pico, RP2040 (no confirmed attached board) |
| BME280 | I2C0: SDA GP4, SCL GP5 |
| microSD | SPI1: SCK GP10, MOSI GP11, MISO GP12, CS GP13 |
| Timing output | GP15 |
| ADC input | GP26 / ADC0 |

## C11 architecture and checks

- `src/core/` is portable C11 and must never include pico-sdk headers; host tests compile only this portable code. `src/hal/` wraps the hardware; `src/drivers/` holds device drivers and `src/app/` assembles the firmware.
- Run `scripts/setup.sh` to initialize the pinned pico-sdk and its submodules, install Arm GNU Toolchain 13.3.Rel1 and picotool 2.1.1 locally in `.toolchain/`, and create `.venv`. Network access, CMake 3.21+, Git, Python 3, curl, tar, a native compiler and make are required. Unity and the SD/FatFs library are fetched during CMake configuration/build.
- Project-local CMake can be installed in `.venv`; prepend `.venv/bin` to PATH for the checks. The SDK-bearing firmware target uses GNU C extensions required by the pinned SDK; portable core and host code remain strict C11.
- Host: `cmake --preset host`, `cmake --build --preset host`, `ctest --preset host`. Host project code is built with `-Wall -Wextra -Werror`.
- Firmware: `cmake --preset firmware`, `cmake --build --preset firmware`. The output is `build/firmware/picologger.uf2`.
- Compiled default sampling rate is set with `-DPICOLOGGER_SAMPLE_RATE_HZ=<integer>` from 1 to 50 (default 50); persisted settings can override it. `docs/SAMPLING.md` describes the GP15 signal and manual checks.
- GP26 ADC/DMA capture runs at a nominal 10 kHz; `docs/ADC_DMA.md` documents block integrity, fault handling and safe manual input checks.
- SD logging and rotation are documented in `docs/SD_LOGGING.md`; the compiled maximum file size is set with `-DPICOLOGGER_LOG_ROTATE_BYTES=<integer>` (default 16777216) and can be overridden by saved settings.
- Best-effort log recovery and the ten-cycle power-loss checklist are documented in `docs/POWER_LOSS.md`; do not claim FAT or card-level atomicity.
- USB CDC commands, asynchronous stop/start, rate changes and checksummed dumps are documented in `docs/USB_COMMANDS.md`.
- Persistent settings reserve the final two flash sectors; `docs/SETTINGS.md` documents flash lockout, intentional ADC pauses and manual checks.
- Watchdog progress gating and retained HardFault reports are described in `docs/WATCHDOG.md`; reset timing and SRAM retention are unmeasured on hardware.
- Core 0 publishes BME records to a 1024-record SPSC ring; core 1 drains it and owns FatFs. `docs/DUAL_CORE.md` covers ordering and unmeasured jitter comparison.
- Low-rate sleep, clock changes and unmeasured power checks are documented in `docs/POWER.md`; Pico W falls back to normal clocks.
- Run `scripts/check.sh` to build both targets and run host tests, with at most two workers.
- Agents cannot flash or observe the board. Every task touching hardware ends with a numbered manual test checklist stating what to flash, connect, and expect; never claim unobserved results.
- Definition of done for each future task: `scripts/check.sh` passes, relevant docs are updated, provide a short summary, and create a local conventional-commit Git commit. Report genuine environment limitations instead of claiming checks passed.
