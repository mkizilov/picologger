#ifndef PICOLOGGER_LOGGER_CHANNEL_H
#define PICOLOGGER_LOGGER_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

/* Two bounded queues reserve the hardware inter-core FIFO for flash lockout. */
bool logger_channel_init(void);
bool logger_channel_to_worker(uint32_t value);
bool logger_channel_from_worker(uint32_t *value);
void logger_channel_to_main(uint32_t value);
bool logger_channel_from_main(uint32_t *value);

#endif
