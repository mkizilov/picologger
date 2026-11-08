#ifndef PICOLOGGER_SENSOR_H
#define PICOLOGGER_SENSOR_H

#include <stdbool.h>

#include "log_format.h"

void sensor_start(void);
void sensor_configure_boot(uint32_t rate_hz, bool enabled, bool start);
void sensor_report_boot(void);
void sensor_poll(void);

typedef struct {
    uint64_t samples, missed, read_errors;
    uint32_t ring_high_water, rate_hz;
    bool running, available, expected;
} sensor_status_t;

bool sensor_set_running(bool running);
bool sensor_set_rate(uint32_t rate_hz);
void sensor_get_status(sensor_status_t *status);

#endif
