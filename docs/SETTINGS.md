# Persistent flash settings

PicoLogger stores two versioned, CRC-32-protected settings slots in the **last
two 4 KiB sectors** of the configured RP2040 flash: slot 0 is the penultimate
sector and slot 1 is the final sector. This intentionally uses one additional
sector beyond the requested last sector. Both slots in a single erase sector
cannot preserve the old slot while that sector is erased. The firmware copies
the SDK's RP2040 linker script at configure time and asserts that the image
ends before the reserved sectors; the flash adapter also checks the link address
against `PICO_FLASH_SIZE_BYTES` and refuses writes on a mismatch. Use the Pico
or Pico W's correct flash-size configuration. The linker/firmware build has not
been verified in this environment.

Each slot occupies one programmed 256-byte page in its own sector. It contains
magic, format version, a 32-bit revision, values and CRC32; remaining bytes are
erased (`0xff`). On update, only the inactive sector is erased and programmed,
then the page is read back and compared before success is reported. Boot reads
both slots and selects the valid newer revision using modulo-2³² serial-number
ordering; equal or exactly half-range-apart revisions select slot 0. If neither
slot validates, compiled defaults are used. A read failure also uses defaults
but disables saves rather than risking an unknown active slot.

This protects the previous slot **assuming erase/program operations do not
damage the other sector**. CRC collisions, out-of-spec supply behavior, flash
wear, hardware failure and corruption of both sectors remain possible; an
absolute power-loss guarantee is not physically established. A flash API
timeout can occur after programming completed, so a failed save is *uncertain*:
run `config get` after the result and again after reboot. Avoid frequent writes;
this design alternates sectors but is not a high-endurance wear-leveling log.
Whole-chip erase or a flashing tool's erase-all option also destroys settings.

## Commands and values

Use the USB CDC interface described in `USB_COMMANDS.md`:

```text
config get
config set rate 25
config set channels 3
config set rotate_bytes 8192
config set start_on_boot 0
config reset
```

`rate` is 1–50 Hz. `channels` is a decimal bitmask: bit 0 enables the BME280
group (temperature, pressure and humidity together); bit 1 enables ADC0/GP26.
Values 0–3 are valid. The BME280 bus scan still runs when the group is disabled,
but it is not configured or sampled. ADC capture is disabled at boot when its
bit is clear. ADC remains diagnostics-only and is not written to the log. If
BME280 is disabled there is no loggable stream, so logging starts stopped even
when `start_on_boot` is 1. `rotate_bytes` is 8192–4294967295, and
`start_on_boot` is 0 or 1. `config reset` saves the compiled defaults (50 Hz,
both groups, 16777216 bytes, start enabled unless the build overrides the rate
or rotation defaults).

Changes are **saved immediately but applied at the next reboot**. `config get`
shows the selected saved slot/revision and the settings applied at boot; the
ordinary `status` command shows the current runtime sampling rate, which can
also be changed temporarily with `rate` while stopped. A first `config set` or
`config reset` writes a slot even if the requested values equal compiled
defaults. An unchanged already-persisted value does not erase flash.

For a real write, first send `stop` and wait for `@OK stopped`; finish or cancel
any `ls`/`dump`. The settings operation asynchronously quiesces the storage
core, temporarily stops ADC and its DMA, checks that **all** DMA channels are
idle inside the safe zone, then uses Pico SDK 2.1.1 `flash_safe_execute` with
the other core's lockout handler and interrupts disabled. The inter-core message
transport uses bounded SDK queues; the hardware FIFO is reserved for lockout.
Programming is also refused unless its source page lies in RP2040 SRAM. If the
guard, DMA check or SDK safe execution fails, no unsafe fallback is used.
ADC is restarted afterward if it had been running. Its in-flight partial block
is discarded and `status` reports `adc_flash_pauses`; exact lost samples and
pause duration are **unmeasured**. This intentional interruption is an exception
to normal nonblocking commands, and flash erase/program itself can stall the
CPU/USB for the flash device's duration. BME280 sampling is stopped by the
required `stop` beforehand.

The terminal reports `PENDING config; ADC will pause`, then either
`OK config saved; reboot to apply` or an error. `ERR ADC resume failed` requires
a reboot. If releasing the storage-core guard times out, further config and
storage operations stay blocked; retain diagnostics and power-cycle if needed.
Do not issue a reboot while a config operation is pending. No physical flash,
power-loss, DMA or timing test has been performed.

## Manual checklist (not performed)

1. With power off, connect the documented BME280 and an expendable FAT32 card;
   build and flash the Pico UF2 as in `FLASHING.md`. Connect USB, send
   `config get` and `status`, and record the saved slot/revision, values and
   current ADC fault/pause count. Do not use a whole-chip erase if settings must
   be retained.
2. Send `stop` and wait for `@OK stopped`. Set `rate 25`, `rotate_bytes 8192`
   and `start_on_boot 0` with separate `config set` commands, waiting for each
   final result. `config get` should show saved values distinct from boot-active
   values; `adc_flash_pauses` should increase if ADC was running.
3. Send `reboot`. After reconnect, expect saved values to be active and BME280
   logging stopped. Send `start`, then `status`; verify the new log uses the
   selected rate. Send `stop`, set `channels 1`, reboot, and confirm ADC capture
   reports disabled while BME280 remains enabled.
4. On expendable hardware only, repeatedly interrupt power during a pending
   configuration update. After each reboot, `config get` should select either
   the previous or the fully written new settings, never a mixed value. Record
   all failures; this test can require external recovery and cannot prove
   behavior for every flash/power failure.
5. Send `stop`, `config reset`, wait for the result, then `reboot`. Confirm the
   compiled defaults return and both channels behave as configured. Measure
   actual flash pause and sampling discontinuities separately; none are known.
