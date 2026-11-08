#include "unity.h"
#include "sampling_timer.h"
#include "pico/stdlib.h"

static uint64_t now;
static int64_t delay;
static repeating_timer_callback_t callback;
static struct repeating_timer *saved_timer;
static bool timer_ok, pin_state;
static bool cancel_ok = true;
static unsigned int pin_writes;

uint64_t time_us_64(void) { return now; }
void gpio_init(unsigned int pin) { TEST_ASSERT_EQUAL_UINT(15, pin); }
void gpio_set_dir(unsigned int pin, bool output)
{
    TEST_ASSERT_EQUAL_UINT(15, pin);
    TEST_ASSERT_TRUE(output);
}
void gpio_put(unsigned int pin, bool value)
{
    TEST_ASSERT_EQUAL_UINT(15, pin);
    pin_state = value;
    ++pin_writes;
}
bool add_repeating_timer_us(int64_t us, repeating_timer_callback_t fn,
                            void *user_data, struct repeating_timer *timer)
{
    TEST_ASSERT_NULL(user_data);
    delay = us; callback = fn; saved_timer = timer;
    return timer_ok;
}
bool cancel_repeating_timer(struct repeating_timer *timer)
{
    TEST_ASSERT_EQUAL_PTR(saved_timer, timer);
    return cancel_ok;
}
void setUp(void) {}
void tearDown(void) {}

static void timer_publishes_latest_tick_and_pulses(void)
{
    TEST_ASSERT_FALSE(sampling_timer_start(0));
    TEST_ASSERT_FALSE(sampling_timer_start(51));
    TEST_ASSERT_FALSE(sampling_timer_start(50));
    timer_ok = true;
    TEST_ASSERT_TRUE(sampling_timer_start(50));
    TEST_ASSERT_EQUAL_INT64(-20000, delay);
    TEST_ASSERT_FALSE(pin_state);
    unsigned int idle_writes = pin_writes;
    sampling_tick_t tick;
    TEST_ASSERT_FALSE(sampling_timer_take(&tick));
    TEST_ASSERT_FALSE(sampling_timer_pending());
    now = UINT64_C(0x100001234);
    TEST_ASSERT_TRUE(callback(saved_timer));
    TEST_ASSERT_TRUE(sampling_timer_pending());
    TEST_ASSERT_TRUE(sampling_timer_take(&tick));
    TEST_ASSERT_FALSE(sampling_timer_pending());
    TEST_ASSERT_EQUAL_UINT32(1, tick.sequence);
    TEST_ASSERT_EQUAL_UINT64(now, tick.timestamp_us);
    TEST_ASSERT_FALSE(sampling_timer_take(&tick));
    now += 20000;
    TEST_ASSERT_TRUE(callback(saved_timer));
    now += 20000;
    TEST_ASSERT_TRUE(callback(saved_timer));
    TEST_ASSERT_TRUE(sampling_timer_take(&tick));
    TEST_ASSERT_EQUAL_UINT32(3, tick.sequence);
    TEST_ASSERT_EQUAL_UINT64(now, tick.timestamp_us);
    TEST_ASSERT_EQUAL_UINT(idle_writes, pin_writes);
    unsigned int before = pin_writes;
    sampling_pulse_begin();
    TEST_ASSERT_TRUE(pin_state);
    sampling_pulse_end();
    TEST_ASSERT_FALSE(pin_state);
    TEST_ASSERT_EQUAL_UINT(before + 2, pin_writes);
    now += 20000;
    TEST_ASSERT_TRUE(callback(saved_timer));
    cancel_ok = false;
    TEST_ASSERT_FALSE(sampling_timer_stop());
    TEST_ASSERT_FALSE(sampling_timer_start(25));
    cancel_ok = true;
    TEST_ASSERT_TRUE(sampling_timer_stop());
    TEST_ASSERT_FALSE(sampling_timer_pending());
    TEST_ASSERT_TRUE(sampling_timer_take(&tick));
    TEST_ASSERT_EQUAL_UINT32(4, tick.sequence);
    TEST_ASSERT_TRUE(sampling_timer_start(25));
    TEST_ASSERT_EQUAL_INT64(-40000, delay);
    now += 40000;
    TEST_ASSERT_TRUE(callback(saved_timer));
    TEST_ASSERT_TRUE(sampling_timer_take(&tick));
    TEST_ASSERT_EQUAL_UINT32(5, tick.sequence);
    TEST_ASSERT_TRUE(sampling_timer_stop());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(timer_publishes_latest_tick_and_pulses);
    return UNITY_END();
}
