#ifndef PICOLOGGER_INTERVAL_STATS_H
#define PICOLOGGER_INTERVAL_STATS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t previous, minimum, maximum, sum, count;
    bool have_previous;
} interval_stats_t;

void interval_stats_observe(interval_stats_t *stats, uint64_t timestamp_us);
/* Preserve the last trigger so an interval crossing a reporting window is kept. */
void interval_stats_reset_window(interval_stats_t *stats);

#endif
