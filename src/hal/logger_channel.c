#include "logger_channel.h"

#include "pico/util/queue.h"

static queue_t to_worker, to_main;

bool logger_channel_init(void)
{
    queue_init(&to_worker, sizeof(uint32_t), 8);
    if (!to_worker.data) return false;
    queue_init(&to_main, sizeof(uint32_t), 8);
    if (!to_main.data) { queue_free(&to_worker); return false; }
    return true;
}

bool logger_channel_to_worker(uint32_t value) { return queue_try_add(&to_worker, &value); }
bool logger_channel_from_worker(uint32_t *value) { return queue_try_remove(&to_main, value); }
void logger_channel_to_main(uint32_t value) { queue_add_blocking(&to_main, &value); }
bool logger_channel_from_main(uint32_t *value) { return queue_try_remove(&to_worker, value); }
