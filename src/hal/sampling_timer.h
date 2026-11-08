#ifndef PICOLOGGER_SAMPLING_TIMER_H
#define PICOLOGGER_SAMPLING_TIMER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t timestamp_us;
    uint32_t sequence;
} sampling_tick_t;

/* Timer ISR and consumer must execute on the same core. */
bool sampling_timer_start(uint32_t rate_hz);
bool sampling_timer_stop(void);
bool sampling_timer_take(sampling_tick_t *tick);
void sampling_pulse_begin(void);
void sampling_pulse_end(void);

#endif
