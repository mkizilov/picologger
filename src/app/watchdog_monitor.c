#include "watchdog_monitor.h"

#include "adc_app.h"
#include "fault_report.h"
#include "logger.h"
#include "sensor.h"
#include "watchdog_health.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

static watchdog_health_t health;
static uint32_t missing;
static uint32_t recorded_missing = UINT32_MAX;
static uint64_t next_feed;

void watchdog_monitor_start(void)
{
    watchdog_health_init(&health);
    missing = WATCHDOG_MISSING_WORKER;
    recorded_missing = UINT32_MAX;
    next_feed = 0;
    uint32_t irq_state = save_and_disable_interrupts();
    watchdog_enable(2000, true);
    fault_preserve_sram_on_watchdog();
    restore_interrupts(irq_state);
}

void watchdog_monitor_poll(void)
{
    sensor_status_t sensor;
    logger_status_t storage;
    sensor_get_status(&sensor);
    logger_get_status(&storage);
    watchdog_observation_t observation = {
        .bme_samples = sensor.samples,
        .adc_samples = adc_app_total_samples(),
        .worker_progress = storage.worker_progress,
        .writer_progress = storage.writer_progress,
        .physical_progress = storage.physical_progress,
        .physical_observed = storage.physical_observed,
        .physical_at_us = storage.physical_at_us,
        .rate_hz = sensor.rate_hz,
        .bme_required = sensor.expected,
        .adc_required = adc_app_watchdog_expected(),
        .writer_required = sensor.expected && storage.state == LOGGER_RUNNING && storage.online
    };
    uint64_t now = time_us_64();
    missing = watchdog_health_observe(&health, now, &observation);
    if (missing != recorded_missing) { fault_note_watchdog(missing); recorded_missing = missing; }
    if (!missing && now >= next_feed) {
        watchdog_update();
        next_feed = now + UINT64_C(100000);
    }
}

uint32_t watchdog_monitor_missing(void) { return missing; }
