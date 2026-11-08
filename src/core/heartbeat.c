#include "heartbeat.h"

bool heartbeat_led_on(uint32_t milliseconds)
{
    return milliseconds % 1000u < 500u;
}
