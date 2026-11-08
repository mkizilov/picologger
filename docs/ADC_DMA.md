# GP26 ADC and DMA capture

The firmware starts the RP2040 ADC on GP26 / ADC0 in free-running mode at a
requested 10,000 samples/s. The divider is calculated from `clk_adc` using the
SDK's `(divider + 1)` convention; with a 48 MHz ADC clock it is 4799. Samples
are 12-bit results carried in 16-bit FIFO words, with bit 15 retained as the
ADC conversion-error flag. Neither the actual rate nor analog accuracy has been
measured on hardware.

## Capture and integrity

One ADC-DREQ-paced DMA channel writes a 1024-byte aligned, hardware-wrapped
ring consisting of two 256-sample halves. On each half completion it chains
to a second, unpaced control DMA channel, which re-triggers the data channel.
**Only one channel uses DREQ_ADC**: the RP2040 datasheet section 2.5.3 warns
against connecting multiple DMA channels to the same DREQ. The programmed
transfer count reloads on every trigger, and hardware address wrapping keeps
both writes within the two halves even if the CPU is delayed.

The DMA-completion interrupt copies the finished half into an eight-block
queue for the main loop, with a block sequence and completion-IRQ timestamp.
It does not publish the copy if the next completion occurs during copying,
the DMA write pointer indicates the wrong half, or the interrupt arrives at
least 50 ms after the previous completion; a copy taking 50 ms is also rejected.
Two nominal blocks take 51.2 ms;
the conservative cutoff avoids treating coalesced completions as intact data.
This check assumes the configured ADC clock is close to its nominal value and
is not a substitute for validating the actual hardware clock.

The queue is single-producer IRQ / single-consumer foreground on the **same
core**; it uses aligned 32-bit volatile indices and C11 signal fences, not the
RP2040's spinlock-backed C11 atomic helpers. The IRQ copies into a queue slot
before publishing it, and the foreground releases the slot only after copying.
An ADC FIFO overrun/underrun, full queue, late or unexpected DMA event, DMA bus
error, or stalled capture latches a fault and stops ADC/DMA rather than
silently overwriting or continuing with unreliable data. Already published
blocks remain readable. Reset the board to restart after a fault.

Every approximately ten seconds, while USB CDC is connected, the firmware
prints `ADC blocks=... samples=... effective_hz=... seq_gaps=...` plus
`fifo_overrun`, `fifo_underrun`, `sample_errors`, ADC `min`, `max`, `last`,
and `fault`. `effective_hz` is the delivered-block rate estimated from IRQ
completion timestamps within the reporting window; the previous completion
timestamp carries across windows. Zero means insufficient timing data. Window
counters reset even while USB is disconnected. FIFO flags and capture faults
remain latched until reset. Raw min/max/last ignore words with bit 15 set.
`seq_gaps` counts discontinuities among delivered block sequence numbers; a
fault can end capture before a missing block's sequence can be observed, so
check the fault and FIFO flags too. Zero counters are necessary evidence, not
a proof that every analog conversion was captured under all possible timing
conditions. The blocks are currently processed for diagnostics and discarded;
they are not yet written to SD or exported as a waveform.

Fault codes: 0 = none; 1 = no DMA channel; 2 = ADC FIFO overrun; 3 = block
queue full; 4 = unexpected DMA order; 5 = late/coalesced DMA completion;
6 = DMA bus/control-channel error; 7 = stalled DMA; 8 = ADC FIFO underrun;
9 = invalid ADC clock for 10 kHz.
The sample sequence is 32-bit and wraps naturally. Interrupt latency, FIFO
headroom, the deadline margin and end-to-end integrity remain unmeasured on
the actual board.

## Manual checklist (not performed)

1. Build and flash the matching Pico/Pico W UF2 via `FLASHING.md`. With power
   disconnected, connect a suitable 10 kΩ potentiometer between 3V3 and GND,
   its wiper to GP26, and share ground. Never drive GP26 below GND or above the
   ADC reference supply; verify the board and signal levels before powering it.
2. Connect USB, open the CDC console and confirm `ADC GP26 requested=10000 Hz`
   and `capture=started`. After ten seconds check `effective_hz` is near
   10,000 and `seq_gaps`, both FIFO flags, `sample_errors`, and `fault` are zero.
   This is an unmeasured expectation, not a benchmark result.
3. Slowly move the potentiometer. Over successive reporting windows the raw
   ADC `min`, `max` and `last` should change across approximately 0–4095,
   subject to reference voltage and ADC accuracy; avoid forcing either rail.
4. Alternatively, use a current-limited function generator with common ground,
   e.g. 100 Hz sine, 1 V peak-to-peak with 1.65 V DC offset (approximately
   1.15–2.15 V). Verify the waveform with a meter/scope before connecting GP26.
   The reported min/max should respond to the signal; raw waveform export is
   not implemented, so this does not by itself prove per-sample continuity.
5. If any integrity flag or fault becomes nonzero, record the line and reset
   before retrying. A stopped capture will not restart automatically. GP15 is
   still the **BME280 foreground-read** timing pulse, not an ADC DMA marker.
