#ifndef PICOLOGGER_HEARTBEAT_H
#define PICOLOGGER_HEARTBEAT_H

#include <stdbool.h>
#include <stdint.h>

/* True during the first half of each one-second interval. */
bool heartbeat_led_on(uint32_t milliseconds);

#endif
