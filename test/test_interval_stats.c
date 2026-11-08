#include "unity.h"
#include "interval_stats.h"

void setUp(void) {}
void tearDown(void) {}

static void measures_intervals_and_preserves_boundary(void)
{
    interval_stats_t stats = {0};
    interval_stats_observe(&stats, 0);
    TEST_ASSERT_EQUAL_UINT64(0, stats.count);
    interval_stats_observe(&stats, 20000);
    interval_stats_observe(&stats, 41000);
    interval_stats_observe(&stats, 60000);
    TEST_ASSERT_EQUAL_UINT64(3, stats.count);
    TEST_ASSERT_EQUAL_UINT64(19000, stats.minimum);
    TEST_ASSERT_EQUAL_UINT64(21000, stats.maximum);
    TEST_ASSERT_EQUAL_UINT64(20000, stats.sum / stats.count);
    interval_stats_reset_window(&stats);
    TEST_ASSERT_EQUAL_UINT64(0, stats.count);
    interval_stats_observe(&stats, 81000);
    TEST_ASSERT_EQUAL_UINT64(1, stats.count);
    TEST_ASSERT_EQUAL_UINT64(21000, stats.minimum);
    TEST_ASSERT_EQUAL_UINT64(21000, stats.maximum);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(measures_intervals_and_preserves_boundary);
    return UNITY_END();
}
