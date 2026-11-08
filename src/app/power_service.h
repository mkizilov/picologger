#ifndef PICOLOGGER_POWER_SERVICE_H
#define PICOLOGGER_POWER_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { POWER_RATE_READY, POWER_RATE_PENDING, POWER_RATE_BUSY, POWER_RATE_ERROR } power_rate_result_t;

void power_service_init(uint32_t rate_hz);
power_rate_result_t power_service_prepare_rate(uint32_t rate_hz);
void power_service_poll(void);
bool power_service_take_result(bool *ok);
bool power_service_busy(void);
bool power_service_can_start(void);
const char *power_service_mode(void);
uint32_t power_service_sys_hz(void);
void power_service_idle(void);

#endif
