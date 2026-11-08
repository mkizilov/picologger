#include "power_clock.h"
#include "power_policy.h"

#include "hardware/clocks.h"
#include "hardware/regs/clocks.h"
#include "pico/stdlib.h"

static bool supported, low_selected, faulted;
static uint32_t normal_hz, adc_hz;

#ifndef CYW43_WL_GPIO_LED_PIN
static bool independent_sources(void)
{
    return ((clocks_hw->clk[clk_usb].ctrl & CLOCKS_CLK_USB_CTRL_AUXSRC_BITS) >> CLOCKS_CLK_USB_CTRL_AUXSRC_LSB) ==
               CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB &&
           ((clocks_hw->clk[clk_adc].ctrl & CLOCKS_CLK_ADC_CTRL_AUXSRC_BITS) >> CLOCKS_CLK_ADC_CTRL_AUXSRC_LSB) ==
               CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB;
}

static void peripheral_from_usb_pll(void)
{
    clock_configure_undivided(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, 48000000u);
}

static bool domains_valid(uint32_t sys_hz)
{
    return clock_get_hz(clk_sys) == sys_hz && clock_get_hz(clk_peri) == 48000000u &&
           clock_get_hz(clk_usb) == 48000000u && clock_get_hz(clk_adc) == adc_hz &&
           clock_get_hz(clk_ref) == XOSC_HZ && independent_sources() &&
           ((clocks_hw->clk[clk_peri].ctrl & CLOCKS_CLK_PERI_CTRL_AUXSRC_BITS) >> CLOCKS_CLK_PERI_CTRL_AUXSRC_LSB) ==
               CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB;
}

static bool restore_normal(void)
{
    if (!set_sys_clock_khz(normal_hz / 1000u, false)) return false;
    peripheral_from_usb_pll();
    return domains_valid(normal_hz);
}
#endif

void power_clock_init(uint32_t rate_hz)
{
    supported = low_selected = faulted = false;
    normal_hz = clock_get_hz(clk_sys);
    adc_hz = clock_get_hz(clk_adc);
#ifdef CYW43_WL_GPIO_LED_PIN
    (void)rate_hz;
#else
    uint vco, p1, p2;
    if (!normal_hz || normal_hz % 1000u || clock_get_hz(clk_usb) != 48000000u ||
        clock_get_hz(clk_ref) != XOSC_HZ || !adc_hz || !independent_sources() ||
        !check_sys_clock_khz(normal_hz / 1000u, &vco, &p1, &p2)) return;
    peripheral_from_usb_pll();
    if (!domains_valid(normal_hz)) { faulted = true; return; }
    supported = true;
    if (rate_hz >= 1 && rate_hz <= PICOLOGGER_LOW_RATE_MAX_HZ) (void)power_clock_select(true);
#endif
}

bool power_clock_select(bool low)
{
#ifdef CYW43_WL_GPIO_LED_PIN
    (void)low;
    return false;
#else
    if (!supported) return false;
    if (low == low_selected) return true;
    if (low) set_sys_clock_48mhz();
    else if (!set_sys_clock_khz(normal_hz / 1000u, false)) {
        faulted = true;
        supported = false;
        return false;
    }
    peripheral_from_usb_pll();
    if (!domains_valid(low ? 48000000u : normal_hz)) {
        faulted = true;
        supported = restore_normal();
        low_selected = false;
        return false;
    }
    low_selected = low;
    return true;
#endif
}

bool power_clock_supported(void) { return supported; }
bool power_clock_faulted(void) { return faulted; }
bool power_clock_low(void) { return supported && low_selected; }
uint32_t power_clock_sys_hz(void) { return clock_get_hz(clk_sys); }
