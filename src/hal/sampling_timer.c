#include "sampling_timer.h"

#include "pico/stdlib.h"

#define TIMING_PIN 15

static struct repeating_timer timer;
/* RP2040 aligned 32-bit loads/stores are indivisible on one core. The ISR is
 * the only writer; volatile keeps the publication and payload accesses ordered.
 * This mailbox is not a cross-core or C-thread synchronization primitive. */
static volatile uint32_t publication, timestamp_low, timestamp_high, sequence;
static uint32_t last_sequence;
static bool running;

static bool timer_callback(struct repeating_timer *ignored)
{
    (void)ignored;
    ++publication;
    uint64_t timestamp = time_us_64();
    timestamp_low = (uint32_t)timestamp;
    timestamp_high = (uint32_t)(timestamp >> 32);
    ++sequence;
    ++publication;
    return true;
}

bool sampling_timer_start(uint32_t rate_hz)
{
    if (running || rate_hz == 0 || rate_hz > 50) return false;
    gpio_init(TIMING_PIN);
    gpio_set_dir(TIMING_PIN, GPIO_OUT);
    gpio_put(TIMING_PIN, false);
    running = add_repeating_timer_us(-(int64_t)(1000000u / rate_hz), timer_callback, NULL, &timer);
    return running;
}

bool sampling_timer_stop(void)
{
    if (!running) return true;
    if (!cancel_repeating_timer(&timer)) return false;
    running = false;
    return true;
}

bool sampling_timer_take(sampling_tick_t *tick)
{
    uint32_t before, after, lo, hi, count;
    do {
        before = publication;
        lo = timestamp_low;
        hi = timestamp_high;
        count = sequence;
        after = publication;
    } while ((before & 1u) || before != after);
    if (count == last_sequence) return false;
    tick->sequence = count;
    tick->timestamp_us = ((uint64_t)hi << 32) | lo;
    last_sequence = count;
    return true;
}

bool sampling_timer_pending(void) { return running && sequence != last_sequence; }

void sampling_pulse_begin(void) { gpio_put(TIMING_PIN, true); }
void sampling_pulse_end(void) { gpio_put(TIMING_PIN, false); }
