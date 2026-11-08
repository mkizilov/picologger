# Implementation and verification status

This is a snapshot of the local review, not a hardware certification.

| Area | Implemented / evidence | Partial or unverified |
| --- | --- | --- |
| Build and host tests | `scripts/check.sh` builds Pico/Pico W UF2s; 69 CTest cases run with host C binaries instrumented by ASan/UBSan, clang-tidy checks 37 project C files plus Pico W variants, and Python CLI/HIL-helper tests run. | Sanitizers and fakes cannot prove hardware timing. The selected clang-tidy profile excludes the Annex K checker that rejects bounded `snprintf`; see `HOST_TOOLS.md`. |
| BME280 and timing | Driver, 1–50 Hz timer, GP15 pulse, counters and fake-bus tests exist. | No physical rate/jitter measurement. Humidity uses a synthetic reference rather than a verified complete vendor worked example. |
| ADC / DMA | Nominal 10 kHz capture, bounded queue and fault handling exist; fake-DMA tests pass. | Raw ADC blocks are diagnostics-only, not logged or exported. Effective rate, analog accuracy and losslessness are unmeasured. |
| Dual-core storage | Core 1 owns FatFs, drains the SPSC ring and rotates v3 logs. Host stress and storage simulations pass; GCC acquire/release instructions were inspected. | Target contention and SD latency are unmeasured. A full ring drops records; rapid card swaps may be missed. |
| Recovery and settings | V1–v3 decoding, CRCs, tail recovery and dual-sector settings have host corruption/interruption tests. Both firmware targets link. | FAT/card atomicity, physical flash interruption, endurance and exact data survival are not established. Final partial RAM chunks are not periodically flushed. |
| Watchdog and faults | Progress gating, retained HardFault fields and host simulations are implemented; the linked Pico fault vector was inspected. | No physical reset/retention or threshold timing was measured. Missing sensors or slow operations can cause repeated resets; boot count is warm-reset-only. |
| USB and power | CDC commands, host CLI, checksum-verified dumps and guarded low-rate clock/WFI paths have fake/headless tests. | Actual USB, GUI, clock switching, current and wake timing are unmeasured. Pico W uses normal clocks and does not enter the low-rate WFI path. USB commands are not authenticated. |
| HIL | Six explicit-port cases and helper/restoration simulations are present. Default local checks report them skipped. | No board was connected, flashed or tested. HIL changes settings, creates logs and reboots; restoration is best effort. |
| Licensing and publication | Project-owned code has an MIT license; initial upstream notices and pinned revisions are catalogued in `../THIRD_PARTY_NOTICES.md`. | The SD library identifies SDIO as ZuluSCSI-derived and includes a GPL-3.0-or-later notice; SDIO symbols appear in both linked ELFs. Applicable provenance and public-source/binary distribution terms require independent review. Pico W driver terms are not MIT. |

## Latest local checks

The 2026-11-08 pre-publication review reran `scripts/setup.sh` and
`scripts/check.sh`: both Pico and Pico W firmware builds passed;
all 69 CTest cases passed with host C binaries under ASan/UBSan; clang-tidy passed; six CLI tests
passed; and pytest reported seven HIL-helper tests passed and six hardware tests
skipped. `pip check` reported no broken requirements. The installed picotool is
pinned but has no USB support. Matplotlib emitted Pyparsing deprecation warnings.
No board was accessed and no current, latency, jitter or throughput result was
measured.

The available Pyright language-server diagnostics reported no errors for 17
project Python files. AFT's C diagnostics lacked a root compilation database;
the check script instead ran clang-tidy against the generated per-target
databases, and both actual Arm firmware builds passed.

An additional host end-to-end simulation generated a rotated v3 log through the
sanitized C logger, transferred its framed dump through the Python CLI's fake
serial port, verified the transport checksum and decoded 240 records with zero
CRC failures or sequence gaps. This did not use a physical USB or SD device.

The initial DNS restriction was repaired: GitHub's Unity tag resolved, the
PyPI pyserial index returned HTTP 200, and project-local setup and pinned
dependency downloads succeeded. A targeted audit of reachable repository blobs
found no matches for the checked common private-key/token patterns and no
tracked cache/build artifacts. This is not an exhaustive secret scan. Historical
`AGENTS.md` versions beginning at `befbfc1` contain a developer-local absolute
path; the current file no longer does, but the preserved history still does.
The publication owner should review that disclosure before pushing; history has
not been rewritten.

For independent checks, run `scripts/setup.sh`, `scripts/check.sh`, and, only
with operator authorization and prepared hardware, the explicit-port procedure
in `HIL.md`. The roadmap and its estimates are in `../ROADMAP.md`.
