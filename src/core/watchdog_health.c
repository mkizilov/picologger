#include "watchdog_health.h"
#include "log_writer.h"

#include <string.h>

void watchdog_health_init(watchdog_health_t *health) { memset(health, 0, sizeof(*health)); }

uint32_t watchdog_health_observe(watchdog_health_t *health, uint64_t now,
                                 const watchdog_observation_t *observation)
{
    const uint64_t values[5] = {observation->bme_samples, observation->adc_samples,
                                observation->worker_progress, observation->writer_progress, observation->physical_progress};
    const bool required[5] = {observation->bme_required, observation->adc_required,
                              true, observation->writer_required, observation->writer_required};
    uint32_t rate = observation->rate_hz >= 1 && observation->rate_hz <= 50 ? observation->rate_hz : 1;
    if (rate != health->last_rate) {
        health->last_rate = rate;
        health->physical_window = (uint64_t)((LOG_RECORDS_PER_BATCH + rate - 1u) / rate + 2u) * UINT64_C(1000000);
    }
    uint32_t missing = 0;
    for (unsigned int i = 0; i < 5; ++i) {
        if (!required[i]) { health->required[i] = false; health->seen[i] = false; continue; }
        if (!health->required[i]) {
            health->required[i] = true;
            health->last_value[i] = values[i];
            health->changed_at[i] = i == 4 && observation->physical_observed ? observation->physical_at_us : now;
            health->seen[i] = i == 4 && observation->physical_observed;
        } else if (health->last_value[i] != values[i]) {
            health->last_value[i] = values[i];
            health->changed_at[i] = now;
            health->seen[i] = true;
        }
        uint64_t window = i == 4 ? health->physical_window : WATCHDOG_PROGRESS_WINDOW_US;
        if (!health->seen[i] || now - health->changed_at[i] > window)
            missing |= 1u << i;
    }
    return missing;
}
