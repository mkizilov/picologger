#include "unity.h"
#include "power_clock.h"
#include "hardware/clocks.h"
#include "hardware/regs/clocks.h"

#include <string.h>

test_clocks_hw_t fake_clocks_hw;
static uint32_t frequencies[CLK_COUNT];
static unsigned int low_calls, high_calls, peri_calls;
static bool can_restore, fail_low, fail_high;

uint32_t clock_get_hz(clock_handle_t clock) { return frequencies[clock]; }
void clock_configure_undivided(clock_handle_t clock, uint32_t src, uint32_t aux, uint32_t frequency)
{
    TEST_ASSERT_EQUAL_INT(clk_peri, clock);
    TEST_ASSERT_EQUAL_UINT(0, src);
    TEST_ASSERT_EQUAL_UINT(CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, aux);
    TEST_ASSERT_EQUAL_UINT32(48000000, frequency);
    frequencies[clk_peri] = frequency;
    fake_clocks_hw.clk[clk_peri].ctrl = aux << CLOCKS_CLK_PERI_CTRL_AUXSRC_LSB;
    ++peri_calls;
}
bool check_sys_clock_khz(uint32_t khz, uint *vco, uint *p1, uint *p2)
{
    TEST_ASSERT_EQUAL_UINT32(125000, khz);
    *vco = 1500000000u; *p1 = 6; *p2 = 2;
    return can_restore;
}
bool set_sys_clock_khz(uint32_t khz, bool required)
{
    TEST_ASSERT_FALSE(required);
    ++high_calls;
    if (fail_high) return false;
    frequencies[clk_sys] = khz * 1000u;
    return true;
}
void set_sys_clock_48mhz(void)
{
    ++low_calls;
    if (!fail_low) frequencies[clk_sys] = 48000000;
    frequencies[clk_peri] = 48000000;
    fake_clocks_hw.clk[clk_peri].ctrl = 0;
}

void setUp(void)
{
    memset(&fake_clocks_hw, 0, sizeof(fake_clocks_hw));
    frequencies[clk_sys] = frequencies[clk_peri] = 125000000;
    frequencies[clk_usb] = frequencies[clk_adc] = 48000000;
    frequencies[clk_ref] = 12000000;
    low_calls = high_calls = peri_calls = 0;
    can_restore = true;
    fail_low = fail_high = false;
}
void tearDown(void) {}

#ifndef CYW43_WL_GPIO_LED_PIN
static void low_and_normal_preserve_usb_adc_and_peripheral_clock(void)
{
    power_clock_init(10);
    TEST_ASSERT_TRUE(power_clock_supported());
    TEST_ASSERT_TRUE(power_clock_low());
    TEST_ASSERT_EQUAL_UINT32(48000000, power_clock_sys_hz());
    TEST_ASSERT_EQUAL_UINT32(48000000, frequencies[clk_usb]);
    TEST_ASSERT_EQUAL_UINT32(48000000, frequencies[clk_adc]);
    TEST_ASSERT_EQUAL_UINT(1, low_calls);
    TEST_ASSERT_TRUE(power_clock_select(false));
    TEST_ASSERT_FALSE(power_clock_low());
    TEST_ASSERT_EQUAL_UINT32(125000000, power_clock_sys_hz());
    TEST_ASSERT_EQUAL_UINT32(48000000, frequencies[clk_peri]);
    TEST_ASSERT_EQUAL_UINT(1, high_calls);
    TEST_ASSERT_TRUE(power_clock_select(false));
    TEST_ASSERT_EQUAL_UINT(1, high_calls);
}

static void boundary_and_failure_are_fail_closed(void)
{
    power_clock_init(11);
    TEST_ASSERT_TRUE(power_clock_supported());
    TEST_ASSERT_FALSE(power_clock_low());
    TEST_ASSERT_EQUAL_UINT(0, low_calls);
    fail_low = true;
    TEST_ASSERT_FALSE(power_clock_select(true));
    TEST_ASSERT_TRUE(power_clock_faulted());
    TEST_ASSERT_TRUE(power_clock_supported());
    TEST_ASSERT_FALSE(power_clock_low());
    TEST_ASSERT_EQUAL_UINT32(125000000, power_clock_sys_hz());
    TEST_ASSERT_EQUAL_UINT(1, high_calls);
    fail_low = false;
    TEST_ASSERT_TRUE(power_clock_select(true));
    fail_high = true;
    TEST_ASSERT_FALSE(power_clock_select(false));
    TEST_ASSERT_FALSE(power_clock_supported());
}

static void unexpected_clock_sources_are_not_changed(void)
{
    fake_clocks_hw.clk[clk_adc].ctrl = 1u << CLOCKS_CLK_ADC_CTRL_AUXSRC_LSB;
    power_clock_init(1);
    TEST_ASSERT_FALSE(power_clock_supported());
    TEST_ASSERT_FALSE(power_clock_faulted());
    TEST_ASSERT_EQUAL_UINT(0, peri_calls);
    TEST_ASSERT_EQUAL_UINT(0, low_calls);
    fake_clocks_hw.clk[clk_adc].ctrl = 0;
    can_restore = false;
    power_clock_init(10);
    TEST_ASSERT_FALSE(power_clock_supported());
    TEST_ASSERT_EQUAL_UINT(0, peri_calls);
}
#else
static void wireless_board_keeps_original_clocks(void)
{
    power_clock_init(1);
    TEST_ASSERT_FALSE(power_clock_supported());
    TEST_ASSERT_FALSE(power_clock_low());
    TEST_ASSERT_FALSE(power_clock_select(true));
    TEST_ASSERT_EQUAL_UINT32(125000000, power_clock_sys_hz());
    TEST_ASSERT_EQUAL_UINT(0, peri_calls);
}
#endif

int main(void)
{
    UNITY_BEGIN();
#ifndef CYW43_WL_GPIO_LED_PIN
    RUN_TEST(low_and_normal_preserve_usb_adc_and_peripheral_clock);
    RUN_TEST(boundary_and_failure_are_fail_closed);
    RUN_TEST(unexpected_clock_sources_are_not_changed);
#else
    RUN_TEST(wireless_board_keeps_original_clocks);
#endif
    return UNITY_END();
}
