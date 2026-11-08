#include "adc_app.h"

#include <inttypes.h>
#include <stdio.h>

#include "adc_capture.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

static bool started, have_previous, have_values;
static bool boot_enabled = true;
static uint32_t flash_pauses;
static uint32_t expected_sequence, minimum, maximum, last_value;
static uint64_t report_at, previous_us, elapsed_us, intervals, blocks, sequence_gaps, sample_errors;
static uint64_t total_samples;
static adc_block_t block;

void adc_app_start(void)
{
    started = boot_enabled && adc_capture_start();
    report_at = time_us_64() + UINT64_C(10000000);
}

void adc_app_configure_boot(bool enabled) { boot_enabled = enabled; }

void adc_app_report_boot(void)
{
    printf("ADC GP26 requested=10000 Hz block_samples=%u capture=%s fault=%u\n",
           ADC_BLOCK_SAMPLES, started ? "started" : boot_enabled ? "unavailable" : "disabled", (unsigned int)adc_capture_fault());
}

static void process_block(const adc_block_t *record)
{
    if (record->sequence != expected_sequence) ++sequence_gaps;
    expected_sequence = record->sequence + 1u;
    if (have_previous) {
        elapsed_us += record->completed_us - previous_us;
        ++intervals;
    }
    previous_us = record->completed_us;
    have_previous = true;
    ++blocks;
    total_samples += ADC_BLOCK_SAMPLES;
    for (unsigned int i = 0; i < ADC_BLOCK_SAMPLES; ++i) {
        uint16_t raw = record->samples[i];
        if (raw & 0x8000u) { ++sample_errors; continue; }
        uint32_t value = raw & 0x0fffu;
        if (!have_values || value < minimum) minimum = value;
        if (!have_values || value > maximum) maximum = value;
        last_value = value;
        have_values = true;
    }
}

uint64_t adc_app_total_samples(void) { return total_samples; }
uint32_t adc_app_fault(void) { return (uint32_t)adc_capture_fault(); }
uint32_t adc_app_flash_pauses(void) { return flash_pauses; }
bool adc_app_watchdog_expected(void) { return boot_enabled; }

bool adc_app_pause_for_flash(bool *was_running)
{
    if (!adc_capture_pause(was_running)) return false;
    if (*was_running) ++flash_pauses;
    return true;
}

bool adc_app_resume_after_flash(bool was_running)
{
    if (!was_running) return true;
    have_previous = false;
    return adc_capture_resume();
}

void adc_app_poll(void)
{
    adc_capture_check();
    while (adc_capture_take(&block)) process_block(&block);
    uint64_t now = time_us_64();
    if (now < report_at) return;
    if (stdio_usb_connected()) {
        uint64_t rate = elapsed_us ? intervals * ADC_BLOCK_SAMPLES * UINT64_C(1000000) / elapsed_us : 0;
        printf("ADC blocks=%" PRIu64 " samples=%" PRIu64 " effective_hz=%" PRIu64
               " seq_gaps=%" PRIu64 " fifo_overrun=%u fifo_underrun=%u sample_errors=%" PRIu64
               " min=%" PRIu32 " max=%" PRIu32 " last=%" PRIu32 " fault=%u\n",
               blocks, blocks * ADC_BLOCK_SAMPLES, rate, sequence_gaps,
               adc_capture_fifo_overrun() ? 1u : 0u, adc_capture_fifo_underrun() ? 1u : 0u, sample_errors,
               have_values ? minimum : 0, have_values ? maximum : 0,
               have_values ? last_value : 0, (unsigned int)adc_capture_fault());
    }
    report_at = now + UINT64_C(10000000);
    elapsed_us = intervals = blocks = sequence_gaps = sample_errors = 0;
    have_values = false;
}
