#include "unity.h"
#include "board.h"
#include "pico/stdlib.h"

static repeating_timer_callback_t callback;
static struct repeating_timer *registered_timer;
static bool timer_succeeds;
static bool led;
static unsigned int writes;
static bool watchdog_reset;
static bool watchdog_timeout;
#ifdef CYW43_WL_GPIO_LED_PIN
static int cyw43_result;
int cyw43_arch_init(void) { return cyw43_result; }
void cyw43_arch_gpio_put(unsigned int pin, bool value)
{
    TEST_ASSERT_EQUAL_UINT(CYW43_WL_GPIO_LED_PIN, pin);
    led = value;
    ++writes;
}
#else
void gpio_init(unsigned int pin) { TEST_ASSERT_EQUAL_UINT(PICO_DEFAULT_LED_PIN, pin); }
void gpio_set_dir(unsigned int pin, bool output)
{
    TEST_ASSERT_EQUAL_UINT(PICO_DEFAULT_LED_PIN, pin);
    TEST_ASSERT_TRUE(output);
}
void gpio_put(unsigned int pin, bool value)
{
    TEST_ASSERT_EQUAL_UINT(PICO_DEFAULT_LED_PIN, pin);
    led = value;
    ++writes;
}
#endif
bool add_repeating_timer_ms(int32_t delay, repeating_timer_callback_t fn,
                            void *user_data, struct repeating_timer *timer)
{
    TEST_ASSERT_EQUAL_INT32(-500, delay);
    TEST_ASSERT_NULL(user_data);
    callback = fn;
    registered_timer = timer;
    return timer_succeeds;
}
bool watchdog_caused_reboot(void) { return watchdog_reset; }
bool watchdog_enable_caused_reboot(void) { return watchdog_timeout; }
void setUp(void) {}
void tearDown(void) {}

static void timer_drives_heartbeat(void)
{
    timer_succeeds = true;
    TEST_ASSERT_TRUE(board_init());
    TEST_ASSERT_FALSE(led);
    TEST_ASSERT_NOT_NULL(callback);
    unsigned int before = writes;
    TEST_ASSERT_TRUE(callback(registered_timer));
#ifdef CYW43_WL_GPIO_LED_PIN
    TEST_ASSERT_EQUAL_UINT(before, writes);
    board_poll();
#else
    (void)before;
#endif
    TEST_ASSERT_TRUE(led);
    TEST_ASSERT_TRUE(callback(registered_timer));
    board_poll();
    TEST_ASSERT_FALSE(led);
}

static void reports_initialization_failures(void)
{
#ifdef CYW43_WL_GPIO_LED_PIN
    cyw43_result = -1;
    TEST_ASSERT_FALSE(board_init());
    cyw43_result = 0;
#endif
    timer_succeeds = false;
    TEST_ASSERT_FALSE(board_init());
}

static void reports_available_reset_information(void)
{
    watchdog_reset = false;
    watchdog_timeout = false;
    TEST_ASSERT_EQUAL_STRING("power-on, RUN reset, or unknown", board_reset_reason());
    watchdog_reset = true;
    TEST_ASSERT_EQUAL_STRING("watchdog or software reset", board_reset_reason());
    watchdog_timeout = true;
    TEST_ASSERT_EQUAL_STRING("watchdog timeout", board_reset_reason());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(timer_drives_heartbeat);
    RUN_TEST(reports_initialization_failures);
    RUN_TEST(reports_available_reset_information);
    return UNITY_END();
}
