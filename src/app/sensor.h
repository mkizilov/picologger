#ifndef PICOLOGGER_SENSOR_H
#define PICOLOGGER_SENSOR_H

#include <stdbool.h>

#include "log_format.h"

void sensor_start(void);
void sensor_report_boot(void);
void sensor_poll(void);
bool sensor_take_sample(log_record_t *record);

#endif
