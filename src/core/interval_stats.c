#include "interval_stats.h"

void interval_stats_observe(interval_stats_t *stats, uint64_t timestamp_us)
{
    if (stats->have_previous) {
        uint64_t interval = timestamp_us - stats->previous;
        if (!stats->count || interval < stats->minimum) stats->minimum = interval;
        if (!stats->count || interval > stats->maximum) stats->maximum = interval;
        stats->sum += interval;
        ++stats->count;
    }
    stats->previous = timestamp_us;
    stats->have_previous = true;
}

void interval_stats_reset_window(interval_stats_t *stats)
{
    stats->minimum = stats->maximum = stats->sum = stats->count = 0;
}
