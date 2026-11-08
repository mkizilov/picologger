#include "unity.h"
#include "adc_app.h"
#include "adc_capture.h"

static uint64_t now;
static adc_block_t input[3];
static unsigned int next_block;
static bool overrun;
static adc_capture_fault_t fault;

uint64_t time_us_64(void) { return now; }
bool stdio_usb_connected(void) { return true; }
bool adc_capture_start(void) { return true; }
void adc_capture_check(void) {}
bool adc_capture_take(adc_block_t *block)
{
    if (next_block == 3) return false;
    *block = input[next_block++];
    return true;
}
adc_capture_fault_t adc_capture_fault(void) { return fault; }
bool adc_capture_fifo_overrun(void) { return overrun; }
bool adc_capture_fifo_underrun(void) { return false; }
void setUp(void) {}
void tearDown(void) {}

static void reports_effective_rate_and_loss(void)
{
    for (unsigned int i = 0; i < 3; ++i) {
        input[i].sequence = i == 2 ? 3 : i;
        input[i].completed_us = i == 2 ? 102400 : (i + 1) * UINT64_C(25600);
        for (unsigned int j = 0; j < ADC_BLOCK_SAMPLES; ++j)
            input[i].samples[j] = (uint16_t)((i + 1) * 100);
    }
    input[0].samples[0] = 0x8000;
    adc_app_start();
    adc_app_report_boot();
    adc_app_poll();
    TEST_ASSERT_EQUAL_UINT(3, next_block);
    overrun = true;
    fault = ADC_CAPTURE_FIFO_OVERRUN;
    now = 10000000;
    adc_app_poll();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(reports_effective_rate_and_loss);
    return UNITY_END();
}
