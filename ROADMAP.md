# PicoLogger roadmap

This is a prioritized plan, not a claim that the work or hardware measurements
have been performed. The current implementation and evidence are summarized in
`docs/VERIFICATION.md`. Correctness, safety and incomplete acquisition/storage
work precede optional product features. Physical tests below require an operator,
a suitable Pico/RP2040 setup and expendable media; agents must not access or
flash hardware.

The 2026-11-08 publication pass rechecked GitHub/PyPI access, pinned local
setup, both firmware builds and the host test/analyzer suite. Initial dependency
notices were preserved, but SDIO provenance and binary distribution terms remain
unresolved; step 13 now covers that outstanding review rather than repeating the
completed local checks. No HIL or performance result was obtained.

Effort estimates include implementation, review, host tests and documentation,
plus the stated supervised test preparation. They exclude hardware procurement,
shipping, long unattended soak time and external certification. Estimates are
engineering hours, not elapsed time, and are uncertain by roughly ±50%; work
may overlap but the total is not reduced for concurrency.

## 1. Establish a physical baseline

- **Scope:** Run the existing opt-in HIL and wiring checklists on a documented Pico, BME280 and expendable SD card.
- **Rationale:** USB, sensor, SD, watchdog and the 1% rate checks exist but have never been exercised on a board.
- **Dependencies:** Operator access to hardware; `docs/HIL.md` safety and backup procedure.
- **Acceptance:** Record firmware hash, setup, test output and failures; verify settings restoration and decode captured logs. No simulated result is labeled hardware evidence.
- **Effort:** 16 hours.

## 2. Bound watchdog recovery and reset loops

- **Scope:** Evaluate missing sensors, long SD operations, recovery and flash pauses against the two-second watchdog; improve the progress/retry policy where needed.
- **Rationale:** An enabled missing BME280 or slow operation can currently cause repeated resets instead of a recoverable degraded state.
- **Dependencies:** Step 1 baseline; existing watchdog and fault-report tests.
- **Acceptance:** Host fault simulations and supervised hardware trials distinguish real stalls from expected offline/slow cases, preserve fault diagnostics and document reset bounds without disabling the watchdog silently.
- **Effort:** 24 hours.

## 3. Characterize SD durability and recovery

- **Scope:** Exercise rotation, short/torn writes, sync failures, FAT damage and the documented ten power-cut cycles on expendable cards; improve recovery where evidence supports it.
- **Rationale:** Recovery is best effort, scans only the newest log's tail, and cannot guarantee FAT/card atomicity or preserve the unsynced RAM chunk.
- **Dependencies:** Steps 1–2; card images and controlled power interruption by an operator.
- **Acceptance:** Preserve pre/post images and decode every resulting file; account for CRC/gap failures, `.TMP` remnants, lost/uncertain records and any unmountable filesystem. Document remaining non-guarantees.
- **Effort:** 32 hours.

## 4. Validate ADC/DMA integrity on target

- **Scope:** Measure the GP26 10 kHz path, DMA chaining, interrupt latency and fault behavior under sensor, USB and SD load.
- **Rationale:** Fake-DMA tests and a target build do not establish physical FIFO headroom or gapless sample capture.
- **Dependencies:** Step 1 and a safe calibrated signal source/logic analyzer.
- **Acceptance:** Record effective rate, FIFO flags, sequence/error counters and latency across normal and stressed runs; demonstrate each injected fault is detected or explicitly documented.
- **Effort:** 24 hours.

## 5. Verify BME280 compensation against authoritative data

- **Scope:** Obtain a traceable complete BME280 temperature/pressure/humidity vector and compare compensation against an independent reference.
- **Rationale:** Current host coverage uses a shared BMP280 example and a synthetic humidity vector, not a verified complete Bosch worked example.
- **Dependencies:** Public vendor reference or reproducible independent vector; step 1 for physical comparison.
- **Acceptance:** Add provenance and regression vectors covering signed calibration, boundaries and rounding; investigate discrepancies rather than changing expected values blindly.
- **Effort:** 12 hours.

## 6. Define and enforce the USB trust boundary

- **Scope:** Threat-model physical USB access to config, dump, reboot and the hidden crash command; introduce an explicit deployment-appropriate authorization or disablement policy.
- **Rationale:** The current local CDC protocol is unauthenticated and includes destructive or data-exposing commands.
- **Dependencies:** Deployment requirements and operator-approved access model; no assumption of a network service.
- **Acceptance:** Document attacker assumptions and safe defaults; test denied, allowed and interrupted control paths without introducing embedded secrets or weakening diagnostics.
- **Effort:** 24 hours.

## 7. Exercise settings updates under real power interruption

- **Scope:** Validate dual-sector selection, linker reservation, flash lockout, ADC pause and recovery across interrupted erase/program cycles.
- **Rationale:** Host cut-point tests cannot establish flash-sector isolation, supply behavior, SRAM safety or endurance on a real unit.
- **Dependencies:** Steps 1–3; expendable hardware and a documented recovery procedure.
- **Acceptance:** Record old/new slot outcomes and pause/fault counters across controlled interruptions; verify the reserved region and document wear and any failures without claiming absolute atomicity.
- **Effort:** 16 hours.

## 8. Verify clock switching, WFI and USB together

- **Scope:** Measure low-rate 48 MHz operation, USB enumeration/traffic, I²C timing and ADC continuity through runtime transitions and idle wakeups.
- **Rationale:** Host fakes and ELF inspection establish intended control flow, not physical clock, sleep or wake behavior; Pico W currently falls back to normal clocks.
- **Dependencies:** Steps 1–2 and timing/power instruments.
- **Acceptance:** Capture clocks, missed samples, USB results, ADC faults and watchdog events at 1, 10 and 11 Hz; retain Pico W fallback unless its own checks pass.
- **Effort:** 16 hours.

## 9. Resolve high-rate ADC storage requirements

- **Scope:** Decide whether raw 10 kHz ADC blocks must be persisted/exported and, if so, design a versioned mixed-rate format and bounded writer path.
- **Rationale:** ADC capture currently feeds diagnostics only; BME records alone are saved to SD, so no raw ADC waveform is available for later analysis.
- **Dependencies:** Steps 3–4 and an explicit retention/throughput requirement.
- **Acceptance:** Either document the accepted diagnostics-only scope or deliver a backward-compatible format, decoder and sustained-load loss accounting with host and hardware evidence.
- **Effort:** 32 hours.

## 10. Stress cross-core ownership and backpressure

- **Scope:** Exercise SPSC ordering, wraparound, ring saturation and control messages during SD/USB load and core stalls.
- **Rationale:** Host concurrency tests and GCC instruction inspection do not prove behavior on the physical dual-core bus or under all contention.
- **Dependencies:** Steps 1 and 4; instrumented test builds that do not alter normal firmware behavior.
- **Acceptance:** Demonstrate bounded rejection without overwrite, correct stop/drain ordering and watchdog response; record observed losses and limitations rather than claiming zero interference.
- **Effort:** 16 hours.

## 11. Harden HIL and serial-protocol coverage

- **Scope:** Expand fake-transport and operator-run HIL scenarios for reconnects, timeouts, interrupted restoration, slow hosts and malformed frames.
- **Rationale:** The HIL suite currently skips without an explicit port and its workflows have only been simulated.
- **Dependencies:** Steps 1 and 6; retain explicit opt-in and destructive-operation warnings.
- **Acceptance:** Automated fakes cover failure/cleanup paths; operator runs record actual results and verify settings, stream and logging restoration after both success and failure.
- **Effort:** 16 hours.

## 12. Improve card-removal and safe-eject handling

- **Scope:** Evaluate a card-detect input or a clearly specified unmount/eject protocol, including board pin and compatibility implications.
- **Rationale:** Five/30-second polling can miss rapid swaps; `stop` closes the file but leaves the volume mounted.
- **Dependencies:** Steps 2–3 and an approved hardware/command design.
- **Acceptance:** Test no-card boot, removal, reinsertion, explicit unmount and interrupted transfers with expendable media; report remaining FAT/card risks.
- **Effort:** 16 hours.

## 13. Strengthen reproducible dependency and build verification

- **Scope:** Resolve the documented SDIO/ZuluSCSI provenance question, audit remaining dependency/runtime terms and exercise clean project-local setup on supported hosts.
- **Rationale:** The project MIT license and initial notices are in place, but both linked ELFs contain SDIO symbols and the upstream dependency's license/provenance needs independent review before binary distribution.
- **Dependencies:** Independent clean environments and the repository's local-only boundaries.
- **Acceptance:** Resolve applicable SDIO and Pico W driver/firmware terms with appropriate review; record versions and hashes, verify clean setup and both UF2 builds, and document remaining issues without global installs.
- **Effort:** 12 hours.

## 14. Measure and publish performance envelopes

- **Scope:** Measure jitter, reliable rates, SD write/sync latency, sustained drops and current in normal and low-power modes.
- **Rationale:** README and power tables deliberately contain blanks; configured frequencies are not measurements.
- **Dependencies:** Steps 1–4 and 8; documented load, instrument resolution and measurement procedure.
- **Acceptance:** Publish raw conditions, duration, distributions/worst observed values and uncertainties; retain unmeasured labels for anything not observed.
- **Effort:** 16 hours.

## 15. Add explicit wall-clock time provenance

- **Scope:** Define optional time synchronization or RTC support with source and validity metadata, without changing existing monotonic sample semantics.
- **Rationale:** Current timestamps are microseconds since boot and the warm-reset boot counter is not a persistent real-time clock.
- **Dependencies:** Steps 9 and 14; approved hardware and time-source requirements.
- **Acceptance:** Specify rollover, missing/invalid time and format compatibility; test drift and reboot behavior with documented source quality.
- **Effort:** 24 hours.

## 16. Improve host operator workflow

- **Scope:** Refine CLI recovery, progress and plotting diagnostics based on real usage while preserving verified-download and non-overwrite behavior.
- **Rationale:** The present serial CLI and plot have fake/headless tests but no real-device or desktop-session validation.
- **Dependencies:** Steps 6 and 11 and operator feedback.
- **Acceptance:** Test disconnect/reconnect, backpressure, corrupt transfers and GUI shutdown without leaving streaming active or partial output; document platform limitations.
- **Effort:** 12 hours.

## Total

**308 engineering hours** across the 16 scoped steps, under the assumptions
above. This is a planning estimate, not a measured benchmark or delivery date.
