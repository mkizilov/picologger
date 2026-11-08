#include "unity.h"
#include "adc_capture.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/regs/adc.h"
#include "hardware/regs/dma.h"

#include <string.h>

adc_hw_t fake_adc_hw;
static dma_channel_hw_t channels[2];
static dma_channel_config configs[2];
static uint16_t *destinations[2];
static bool pending[2], busy[2], running, irq_enabled;
static uint32_t enabled_mask, started_mask;
static uint32_t unclaimed_mask, fake_clock = 48000000;
static unsigned int claim_count, claim_limit = 2, aborts, barriers, inject_late, inject_dma_error, inject_delay;
static uint64_t now;
static float divider;
static void (*handler)(void);

uint64_t time_us_64(void) { return now; }
uint32_t time_us_32(void) { return (uint32_t)now; }
void fake_compiler_barrier(void)
{
    ++barriers;
    if (inject_late && barriers == 2) pending[0] = true;
    if (inject_dma_error && barriers == 2) channels[1].ctrl_trig = DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS;
    if (inject_delay && barriers == 2) now += 50000;
}
int dma_claim_unused_channel(bool required)
{
    TEST_ASSERT_FALSE(required);
    return claim_count < claim_limit ? (int)claim_count++ : -1;
}
void dma_channel_unclaim(unsigned int channel) { unclaimed_mask |= 1u << channel; }
dma_channel_config dma_channel_get_default_config(unsigned int channel)
{
    return (dma_channel_config){.enabled = true, .chain = channel, .dreq = DREQ_FORCE};
}
dma_channel_config dma_get_channel_config(unsigned int channel) { return configs[channel]; }
void channel_config_set_enable(dma_channel_config *c, bool enabled) { c->enabled = enabled; }
void dma_channel_set_config(unsigned int channel, const dma_channel_config *c, bool trigger)
{
    TEST_ASSERT_FALSE(trigger);
    configs[channel] = *c;
}
void channel_config_set_transfer_data_size(dma_channel_config *c, enum dma_channel_transfer_size size) { c->size = size; }
void channel_config_set_read_increment(dma_channel_config *c, bool enabled) { c->read_inc = enabled; }
void channel_config_set_write_increment(dma_channel_config *c, bool enabled) { c->write_inc = enabled; }
void channel_config_set_ring(dma_channel_config *c, bool write, unsigned int bits) { c->ring_write = write; c->ring_bits = bits; }
void channel_config_set_dreq(dma_channel_config *c, unsigned int dreq) { c->dreq = dreq; }
void channel_config_set_chain_to(dma_channel_config *c, unsigned int target) { c->chain = target; }
void channel_config_set_irq_quiet(dma_channel_config *c, bool quiet) { c->quiet = quiet; }
void dma_channel_configure(unsigned int channel, const dma_channel_config *config, volatile void *dst,
                           const volatile void *src, unsigned int count, bool trigger)
{
    TEST_ASSERT_TRUE(channel < 2);
    TEST_ASSERT_FALSE(trigger);
    if (channel == 0) {
        TEST_ASSERT_EQUAL_PTR(&adc_hw->fifo, src);
        TEST_ASSERT_EQUAL_UINT(ADC_BLOCK_SAMPLES, count);
        TEST_ASSERT_EQUAL_UINT(0, (uintptr_t)dst % 1024u);
        destinations[0] = (uint16_t *)dst;
        destinations[1] = destinations[0] + ADC_BLOCK_SAMPLES;
        channels[0].write_addr = (uintptr_t)dst;
    } else {
        TEST_ASSERT_EQUAL_PTR(&channels[0].al1_transfer_count_trig, dst);
        TEST_ASSERT_EQUAL_UINT(1, count);
        TEST_ASSERT_EQUAL_UINT(ADC_BLOCK_SAMPLES, *(const uint32_t *)src);
    }
    configs[channel] = *config;
}
void dma_channel_acknowledge_irq0(unsigned int channel) { pending[channel] = false; }
void dma_set_irq0_channel_mask_enabled(uint32_t mask, bool enabled)
{
    if (enabled) enabled_mask |= mask;
    else enabled_mask &= ~mask;
}
bool dma_channel_get_irq0_status(unsigned int channel) { return pending[channel] && (enabled_mask & (1u << channel)); }
bool dma_channel_is_busy(unsigned int channel) { return busy[channel]; }
dma_channel_hw_t *dma_channel_hw_addr(unsigned int channel) { return &channels[channel]; }
void dma_start_channel_mask(uint32_t mask) { started_mask = mask; busy[0] = true; }
void dma_channel_abort(unsigned int channel)
{
    TEST_ASSERT_FALSE(configs[0].enabled);
    TEST_ASSERT_FALSE(configs[1].enabled);
    busy[channel] = false;
    ++aborts;
}
void adc_init(void) { fake_adc_hw.fcs = 0; }
void adc_gpio_init(unsigned int pin) { TEST_ASSERT_EQUAL_UINT(26, pin); }
void adc_select_input(unsigned int input) { TEST_ASSERT_EQUAL_UINT(0, input); }
void adc_set_round_robin(unsigned int mask) { TEST_ASSERT_EQUAL_UINT(0, mask); }
void adc_fifo_setup(bool enabled, bool dreq, uint16_t threshold, bool error, bool shift)
{
    TEST_ASSERT_TRUE(enabled); TEST_ASSERT_TRUE(dreq); TEST_ASSERT_EQUAL_UINT(1, threshold);
    TEST_ASSERT_TRUE(error); TEST_ASSERT_FALSE(shift);
    fake_adc_hw.fcs = ADC_FCS_EN_BITS | ADC_FCS_DREQ_EN_BITS | ADC_FCS_ERR_BITS | (1u << 24);
}
void adc_fifo_drain(void) {}
void adc_set_clkdiv(float value) { divider = value; }
void adc_run(bool value)
{
    running = value;
    if (value) fake_adc_hw.fcs &= ~(ADC_FCS_OVER_BITS | ADC_FCS_UNDER_BITS);
}
uint32_t clock_get_hz(int clock) { TEST_ASSERT_EQUAL_INT(clk_adc, clock); return fake_clock; }
void irq_add_shared_handler(unsigned int irq, void (*fn)(void), unsigned char priority)
{
    TEST_ASSERT_EQUAL_UINT(DMA_IRQ_0, irq);
    TEST_ASSERT_EQUAL_UINT(PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY, priority);
    handler = fn;
}
void irq_set_enabled(unsigned int irq, bool enabled)
{
    TEST_ASSERT_EQUAL_UINT(DMA_IRQ_0, irq);
    irq_enabled = enabled;
}
void setUp(void) {}
void tearDown(void) {}

static void simulate_completion(unsigned int channel, uint16_t value)
{
    for (unsigned int i = 0; i < ADC_BLOCK_SAMPLES; ++i) destinations[channel][i] = (uint16_t)(value + i);
    now += 25600;
    pending[0] = true;
    channels[0].write_addr = (uintptr_t)destinations[channel ^ 1u];
    handler();
}

static void configuration_and_alternating_blocks(void)
{
    TEST_ASSERT_TRUE(adc_capture_start());
    TEST_ASSERT_TRUE(running); TEST_ASSERT_TRUE(irq_enabled);
    TEST_ASSERT_EQUAL_UINT(3, enabled_mask); TEST_ASSERT_EQUAL_UINT(1, started_mask);
    TEST_ASSERT_TRUE(divider == 4799.0f);
    TEST_ASSERT_EQUAL_UINT(DMA_SIZE_16, configs[0].size);
    TEST_ASSERT_FALSE(configs[0].read_inc); TEST_ASSERT_TRUE(configs[0].write_inc);
    TEST_ASSERT_TRUE(configs[0].ring_write); TEST_ASSERT_EQUAL_UINT(10, configs[0].ring_bits);
    TEST_ASSERT_EQUAL_UINT(DREQ_ADC, configs[0].dreq);
    TEST_ASSERT_EQUAL_UINT(1, configs[0].chain);
    TEST_ASSERT_EQUAL_UINT(DMA_SIZE_32, configs[1].size);
    TEST_ASSERT_FALSE(configs[1].read_inc); TEST_ASSERT_FALSE(configs[1].write_inc);
    TEST_ASSERT_EQUAL_UINT(DREQ_FORCE, configs[1].dreq);
    TEST_ASSERT_EQUAL_UINT(1, configs[1].chain);
    TEST_ASSERT_TRUE(configs[1].quiet);
    adc_block_t block;
    TEST_ASSERT_FALSE(adc_capture_pending());
    simulate_completion(0, 100);
    TEST_ASSERT_TRUE(adc_capture_pending());
    TEST_ASSERT_TRUE(adc_capture_take(&block));
    TEST_ASSERT_FALSE(adc_capture_pending());
    TEST_ASSERT_EQUAL_UINT32(0, block.sequence);
    TEST_ASSERT_EQUAL_UINT64(25600, block.completed_us);
    TEST_ASSERT_EQUAL_UINT16(100, block.samples[0]);
    TEST_ASSERT_EQUAL_UINT16(355, block.samples[255]);
    simulate_completion(1, 200);
    TEST_ASSERT_TRUE(adc_capture_take(&block));
    TEST_ASSERT_EQUAL_UINT32(1, block.sequence);
    TEST_ASSERT_EQUAL_UINT16(200, block.samples[0]);
    TEST_ASSERT_FALSE(adc_capture_take(&block));
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_OK, adc_capture_fault());
}

static void fifo_overrun_stops_without_publishing(void)
{
    TEST_ASSERT_TRUE(adc_capture_start());
    fake_adc_hw.fcs |= ADC_FCS_OVER_BITS;
    simulate_completion(0, 1);
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_FIFO_OVERRUN, adc_capture_fault());
    TEST_ASSERT_TRUE(adc_capture_fifo_overrun());
    TEST_ASSERT_FALSE(running); TEST_ASSERT_EQUAL_UINT(2, aborts);
    adc_block_t block;
    TEST_ASSERT_FALSE(adc_capture_take(&block));
}

static void fifo_underrun_stops_without_publishing(void)
{
    TEST_ASSERT_TRUE(adc_capture_start());
    fake_adc_hw.fcs |= ADC_FCS_UNDER_BITS;
    adc_capture_check();
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_FIFO_UNDERRUN, adc_capture_fault());
    TEST_ASSERT_TRUE(adc_capture_fifo_underrun());
    TEST_ASSERT_FALSE(running);
}

static void full_queue_stops_and_retains_prior_blocks(void)
{
    TEST_ASSERT_TRUE(adc_capture_start());
    for (unsigned int i = 0; i <= ADC_BLOCK_QUEUE_CAPACITY; ++i)
        simulate_completion(i & 1u, (uint16_t)i);
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_QUEUE_FULL, adc_capture_fault());
    TEST_ASSERT_FALSE(running);
    adc_block_t block;
    for (unsigned int i = 0; i < ADC_BLOCK_QUEUE_CAPACITY; ++i) {
        TEST_ASSERT_TRUE(adc_capture_take(&block));
        TEST_ASSERT_EQUAL_UINT32(i, block.sequence);
        TEST_ASSERT_EQUAL_UINT16(i, block.samples[0]);
    }
    TEST_ASSERT_FALSE(adc_capture_take(&block));
}

static void late_copy_is_not_published(void)
{
    TEST_ASSERT_TRUE(adc_capture_start());
    inject_late = 1;
    simulate_completion(0, 1);
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_DMA_LATE, adc_capture_fault());
    adc_block_t block;
    TEST_ASSERT_FALSE(adc_capture_take(&block));
    TEST_ASSERT_FALSE(running);
}

static void control_irq_is_a_fault(void)
{
    TEST_ASSERT_TRUE(adc_capture_start());
    pending[0] = pending[1] = true;
    handler();
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_DMA_ERROR, adc_capture_fault());
    TEST_ASSERT_FALSE(running);
}

static void coalesced_or_delayed_interrupt_is_a_fault(void)
{
    TEST_ASSERT_TRUE(adc_capture_start());
    now = 25600;
    simulate_completion(0, 1);
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_DMA_LATE, adc_capture_fault());
    adc_block_t block;
    TEST_ASSERT_FALSE(adc_capture_take(&block));
}

static void wrong_dma_half_is_a_fault(void)
{
    TEST_ASSERT_TRUE(adc_capture_start());
    simulate_completion(1, 1);
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_DMA_LATE, adc_capture_fault());
    adc_block_t block;
    TEST_ASSERT_FALSE(adc_capture_take(&block));
}

static void dma_bus_error_is_a_fault(void)
{
    TEST_ASSERT_TRUE(adc_capture_start());
    channels[0].ctrl_trig = DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS;
    simulate_completion(0, 1);
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_DMA_ERROR, adc_capture_fault());
    TEST_ASSERT_FALSE(running);
}

static void dma_error_during_copy_is_not_published(void)
{
    TEST_ASSERT_TRUE(adc_capture_start());
    inject_dma_error = 1;
    simulate_completion(0, 1);
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_DMA_ERROR, adc_capture_fault());
    adc_block_t block;
    TEST_ASSERT_FALSE(adc_capture_take(&block));
}

static void delayed_copy_is_not_published(void)
{
    TEST_ASSERT_TRUE(adc_capture_start());
    inject_delay = 1;
    simulate_completion(0, 1);
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_DMA_LATE, adc_capture_fault());
    adc_block_t block;
    TEST_ASSERT_FALSE(adc_capture_take(&block));
}

static void missing_dma_channel_is_reported(void)
{
    claim_limit = 1;
    TEST_ASSERT_FALSE(adc_capture_start());
    TEST_ASSERT_EQUAL_UINT(1, unclaimed_mask);
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_NO_DMA, adc_capture_fault());
    TEST_ASSERT_FALSE(running);
}

static void no_dma_channels_is_reported(void)
{
    claim_limit = 0;
    TEST_ASSERT_FALSE(adc_capture_start());
    TEST_ASSERT_EQUAL_UINT(0, unclaimed_mask);
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_NO_DMA, adc_capture_fault());
}

static void invalid_adc_clock_releases_channels(void)
{
    fake_clock = 0;
    TEST_ASSERT_FALSE(adc_capture_start());
    TEST_ASSERT_EQUAL_UINT(3, unclaimed_mask);
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_BAD_CLOCK, adc_capture_fault());
    TEST_ASSERT_FALSE(running);
}

static void stalled_dma_is_reported(void)
{
    TEST_ASSERT_TRUE(adc_capture_start());
    now = 200001;
    adc_capture_check();
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_STALLED, adc_capture_fault());
    TEST_ASSERT_FALSE(running);
}

static void capture_pauses_and_resumes_without_reusing_sequence(void)
{
    TEST_ASSERT_TRUE(adc_capture_start());
    simulate_completion(0, 100);
    adc_block_t block;
    TEST_ASSERT_FALSE(adc_capture_pause(NULL));
    bool was_running = false;
    TEST_ASSERT_TRUE(adc_capture_pause(&was_running));
    TEST_ASSERT_TRUE(was_running);
    TEST_ASSERT_FALSE(running);
    TEST_ASSERT_EQUAL_UINT(2, aborts);
    now += 1000000;
    adc_capture_check();
    TEST_ASSERT_EQUAL_INT(ADC_CAPTURE_OK, adc_capture_fault());
    TEST_ASSERT_TRUE(adc_capture_resume());
    TEST_ASSERT_TRUE(running);
    simulate_completion(0, 200);
    TEST_ASSERT_TRUE(adc_capture_take(&block));
    TEST_ASSERT_EQUAL_UINT32(0, block.sequence);
    TEST_ASSERT_EQUAL_UINT16(100, block.samples[0]);
    TEST_ASSERT_TRUE(adc_capture_take(&block));
    TEST_ASSERT_EQUAL_UINT32(1, block.sequence);
    TEST_ASSERT_EQUAL_UINT16(200, block.samples[0]);
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    if (argc != 2) return 1;
    if (!strcmp(argv[1], "normal")) RUN_TEST(configuration_and_alternating_blocks);
    else if (!strcmp(argv[1], "fifo")) RUN_TEST(fifo_overrun_stops_without_publishing);
    else if (!strcmp(argv[1], "underrun")) RUN_TEST(fifo_underrun_stops_without_publishing);
    else if (!strcmp(argv[1], "full")) RUN_TEST(full_queue_stops_and_retains_prior_blocks);
    else if (!strcmp(argv[1], "late")) RUN_TEST(late_copy_is_not_published);
    else if (!strcmp(argv[1], "simultaneous")) RUN_TEST(control_irq_is_a_fault);
    else if (!strcmp(argv[1], "coalesced")) RUN_TEST(coalesced_or_delayed_interrupt_is_a_fault);
    else if (!strcmp(argv[1], "wrong_half")) RUN_TEST(wrong_dma_half_is_a_fault);
    else if (!strcmp(argv[1], "dma_error")) RUN_TEST(dma_bus_error_is_a_fault);
    else if (!strcmp(argv[1], "copy_error")) RUN_TEST(dma_error_during_copy_is_not_published);
    else if (!strcmp(argv[1], "copy_delay")) RUN_TEST(delayed_copy_is_not_published);
    else if (!strcmp(argv[1], "no_dma")) RUN_TEST(missing_dma_channel_is_reported);
    else if (!strcmp(argv[1], "none")) RUN_TEST(no_dma_channels_is_reported);
    else if (!strcmp(argv[1], "bad_clock")) RUN_TEST(invalid_adc_clock_releases_channels);
    else if (!strcmp(argv[1], "stalled")) RUN_TEST(stalled_dma_is_reported);
    else if (!strcmp(argv[1], "pause")) RUN_TEST(capture_pauses_and_resumes_without_reusing_sequence);
    else return 1;
    return UNITY_END();
}
