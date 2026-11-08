#ifndef PICOLOGGER_SETTINGS_SERVICE_H
#define PICOLOGGER_SETTINGS_SERVICE_H

#include "settings.h"

typedef enum { SETTINGS_REQUEST_ACCEPTED, SETTINGS_REQUEST_UNCHANGED, SETTINGS_REQUEST_BUSY,
               SETTINGS_REQUEST_UNAVAILABLE, SETTINGS_REQUEST_INVALID } settings_request_t;

void settings_service_init(settings_flash_t flash, settings_t defaults);
void settings_service_get(settings_snapshot_t *snapshot, settings_t *active, bool *readable);
settings_request_t settings_service_request(const settings_t *values);
settings_request_t settings_service_reset(void);
void settings_service_poll(void);
bool settings_service_take_result(bool *saved, bool *adc_resumed);
bool settings_service_busy(void);

#endif
