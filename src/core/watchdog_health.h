#ifndef PICOLOGGER_WATCHDOG_HEALTH_H
#define PICOLOGGER_WATCHDOG_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#define WATCHDOG_PROGRESS_WINDOW_US UINT64_C(1500000)
enum { WATCHDOG_MISSING_BME = 1u, WATCHDOG_MISSING_ADC = 2u,
       WATCHDOG_MISSING_WORKER = 4u, WATCHDOG_MISSING_WRITER = 8u, WATCHDOG_MISSING_PHYSICAL = 16u };

typedef struct {
    uint64_t bme_samples, adc_samples;
    uint64_t physical_at_us;
    uint32_t worker_progress, writer_progress, physical_progress, rate_hz;
    bool bme_required, adc_required, writer_required, physical_observed;
} watchdog_observation_t;

typedef struct {
    uint64_t last_value[5], changed_at[5];
    uint64_t physical_window;
    uint32_t last_rate;
    bool required[5], seen[5];
} watchdog_health_t;

void watchdog_health_init(watchdog_health_t *health);
/* Returns missing progress bits. The storage worker is always required. */
uint32_t watchdog_health_observe(watchdog_health_t *health, uint64_t now,
                                 const watchdog_observation_t *observation);

#endif
