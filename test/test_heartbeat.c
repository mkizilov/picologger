#include "unity.h"
#include "heartbeat.h"

#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

static void heartbeat_switches_at_half_second(void)
{
    TEST_ASSERT_TRUE(heartbeat_led_on(0));
    TEST_ASSERT_TRUE(heartbeat_led_on(499));
    TEST_ASSERT_FALSE(heartbeat_led_on(500));
    TEST_ASSERT_FALSE(heartbeat_led_on(999));
    TEST_ASSERT_TRUE(heartbeat_led_on(1000));
}

static void heartbeat_accepts_maximum_clock_value(void)
{
    TEST_ASSERT_TRUE(heartbeat_led_on(UINT32_MAX));
    TEST_ASSERT_TRUE(heartbeat_led_on(0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(heartbeat_switches_at_half_second);
    RUN_TEST(heartbeat_accepts_maximum_clock_value);
    return UNITY_END();
}
