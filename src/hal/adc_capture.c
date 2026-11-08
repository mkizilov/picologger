#include "adc_capture.h"

#include <string.h>

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/regs/adc.h"
#include "hardware/regs/dma.h"
#include "pico/stdlib.h"

#define ADC_RATE_HZ 10000u
#define ADC_STALL_US 200000u
/* Two nominal 256-sample periods are 51.2 ms; fail conservatively sooner. */
#define ADC_LATE_US 50000u

/* One ADC-paced channel wraps across two 512-byte halves. */
_Alignas(1024) static uint16_t dma_buffers[2][ADC_BLOCK_SAMPLES];
static const uint32_t dma_reload_count = ADC_BLOCK_SAMPLES;
static adc_blocks_t completed;
static int dma_channels[2] = {-1, -1};
static uint32_t dma_mask;
static volatile uint32_t last_irq_us;
static bool started, paused;

static bool dma_is_writing_other_half(unsigned int completed_half)
{
    uintptr_t next = (uintptr_t)dma_channel_hw_addr((unsigned int)dma_channels[0])->write_addr;
    uintptr_t base = (uintptr_t)dma_buffers;
    return next >= base && next < base + sizeof(dma_buffers) &&
           (next - base) / sizeof(dma_buffers[0]) == (completed_half ^ 1u);
}

static void disable_capture(void)
{
    dma_set_irq0_channel_mask_enabled(dma_mask, false);
    adc_run(false);
    /* Prevent either chained channel from retriggering the other during abort. */
    for (unsigned int i = 0; i < 2; ++i) {
        unsigned int channel = (unsigned int)dma_channels[i];
        dma_channel_config config = dma_get_channel_config(channel);
        channel_config_set_enable(&config, false);
        dma_channel_set_config(channel, &config, false);
    }
    dma_channel_abort((unsigned int)dma_channels[0]);
    dma_channel_abort((unsigned int)dma_channels[1]);
    dma_channel_acknowledge_irq0((unsigned int)dma_channels[0]);
    dma_channel_acknowledge_irq0((unsigned int)dma_channels[1]);
}

static void stop_capture(adc_capture_fault_t fault)
{
    adc_blocks_fail(&completed, fault);
    disable_capture();
}

static bool dma_has_error(void)
{
    return (dma_channel_hw_addr((unsigned int)dma_channels[0])->ctrl_trig & DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS) ||
           (dma_channel_hw_addr((unsigned int)dma_channels[1])->ctrl_trig & DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS);
}

static void dma_complete_irq(void)
{
    unsigned int data_channel = (unsigned int)dma_channels[0];
    unsigned int control_channel = (unsigned int)dma_channels[1];
    bool pending_data = dma_channel_get_irq0_status(data_channel);
    bool pending_control = dma_channel_get_irq0_status(control_channel);
    if (!pending_data && !pending_control) return;
    if (pending_control || dma_has_error()) {
        stop_capture(ADC_CAPTURE_DMA_ERROR);
        return;
    }
    dma_channel_acknowledge_irq0(data_channel);
    uint64_t now = time_us_64();
    uint32_t interval = (uint32_t)((uint32_t)now - last_irq_us);
    last_irq_us = (uint32_t)now;
    bool overrun = (adc_hw->fcs & ADC_FCS_OVER_BITS) != 0;
    if (adc_hw->fcs & ADC_FCS_UNDER_BITS) { stop_capture(ADC_CAPTURE_FIFO_UNDERRUN); return; }
    unsigned int index = completed.expected_channel;
    /* A coalesced interrupt could conceal whole revolutions of the DMA ring;
     * fail closed if two block periods elapsed or the next half is wrong. */
    if (interval >= ADC_LATE_US || !dma_is_writing_other_half(index)) {
        stop_capture(overrun ? ADC_CAPTURE_FIFO_OVERRUN : ADC_CAPTURE_DMA_LATE);
        return;
    }
    adc_block_t *slot = adc_blocks_reserve(&completed, (uint8_t)index, overrun, now);
    if (!slot) { stop_capture((adc_capture_fault_t)completed.fault); return; }
    /* DMA may continue while this IRQ copies. Do not publish if the next
     * half completed and DMA could have started overwriting this one. */
    __compiler_memory_barrier();
    if (dma_has_error()) { stop_capture(ADC_CAPTURE_DMA_ERROR); return; }
    memcpy(slot->samples, dma_buffers[index], sizeof(slot->samples));
    __compiler_memory_barrier();
    if (dma_has_error()) { stop_capture(ADC_CAPTURE_DMA_ERROR); return; }
    if (adc_hw->fcs & ADC_FCS_OVER_BITS) { stop_capture(ADC_CAPTURE_FIFO_OVERRUN); return; }
    if (adc_hw->fcs & ADC_FCS_UNDER_BITS) { stop_capture(ADC_CAPTURE_FIFO_UNDERRUN); return; }
    if ((uint32_t)(time_us_32() - (uint32_t)now) >= ADC_LATE_US ||
        dma_channel_get_irq0_status(data_channel) || !dma_is_writing_other_half(index)) {
        stop_capture(ADC_CAPTURE_DMA_LATE);
        return;
    }
    adc_blocks_commit(&completed);
}

static void configure_dma(void)
{
    unsigned int data_channel = (unsigned int)dma_channels[0];
    unsigned int control_channel = (unsigned int)dma_channels[1];
    dma_channel_config data = dma_channel_get_default_config(data_channel);
    channel_config_set_transfer_data_size(&data, DMA_SIZE_16);
    channel_config_set_read_increment(&data, false);
    channel_config_set_write_increment(&data, true);
    channel_config_set_ring(&data, true, 10);
    channel_config_set_dreq(&data, DREQ_ADC);
    channel_config_set_chain_to(&data, control_channel);
    dma_channel_configure(data_channel, &data, dma_buffers, &adc_hw->fifo, ADC_BLOCK_SAMPLES, false);

    /* Only the data channel consumes DREQ_ADC. */
    dma_channel_config control = dma_channel_get_default_config(control_channel);
    channel_config_set_transfer_data_size(&control, DMA_SIZE_32);
    channel_config_set_read_increment(&control, false);
    channel_config_set_write_increment(&control, false);
    channel_config_set_dreq(&control, DREQ_FORCE);
    channel_config_set_irq_quiet(&control, true);
    dma_channel_configure(control_channel, &control, &dma_channel_hw_addr(data_channel)->al1_transfer_count_trig,
                          &dma_reload_count, 1, false);
    dma_channel_acknowledge_irq0(data_channel);
    dma_channel_acknowledge_irq0(control_channel);
}

static void clear_fifo_flags(void)
{
    adc_hw->fcs = (adc_hw->fcs & (ADC_FCS_EN_BITS | ADC_FCS_DREQ_EN_BITS |
                   ADC_FCS_ERR_BITS | ADC_FCS_SHIFT_BITS | ADC_FCS_THRESH_BITS)) |
                  ADC_FCS_OVER_BITS | ADC_FCS_UNDER_BITS;
}

bool adc_capture_start(void)
{
    if (started) return false;
    adc_blocks_init(&completed);
    dma_channels[0] = dma_claim_unused_channel(false);
    if (dma_channels[0] < 0) { adc_blocks_fail(&completed, ADC_CAPTURE_NO_DMA); return false; }
    dma_channels[1] = dma_claim_unused_channel(false);
    if (dma_channels[1] < 0) {
        dma_channel_unclaim((unsigned int)dma_channels[0]);
        dma_channels[0] = -1;
        adc_blocks_fail(&completed, ADC_CAPTURE_NO_DMA);
        return false;
    }
    uint32_t adc_clock = clock_get_hz(clk_adc);
    if (adc_clock < 960000u || adc_clock > 655360000u) {
        dma_channel_unclaim((unsigned int)dma_channels[0]);
        dma_channel_unclaim((unsigned int)dma_channels[1]);
        dma_channels[0] = dma_channels[1] = -1;
        adc_blocks_fail(&completed, ADC_CAPTURE_BAD_CLOCK);
        return false;
    }
    dma_mask = (1u << dma_channels[0]) | (1u << dma_channels[1]);
    adc_init();
    adc_gpio_init(26);
    adc_select_input(0);
    adc_set_round_robin(0);
    adc_fifo_setup(true, true, 1, true, false);
    adc_fifo_drain();
    clear_fifo_flags();
    adc_set_clkdiv((float)adc_clock / (float)ADC_RATE_HZ - 1.0f);
    configure_dma();
    irq_add_shared_handler(DMA_IRQ_0, dma_complete_irq, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    dma_set_irq0_channel_mask_enabled(dma_mask, true);
    irq_set_enabled(DMA_IRQ_0, true);
    started = true;
    last_irq_us = time_us_32();
    dma_start_channel_mask(1u << dma_channels[0]);
    adc_run(true);
    return true;
}

void adc_capture_check(void)
{
    if (!started || paused || completed.fault != ADC_CAPTURE_OK) return;
    bool overrun = (adc_hw->fcs & ADC_FCS_OVER_BITS) != 0;
    bool underrun = (adc_hw->fcs & ADC_FCS_UNDER_BITS) != 0;
    uint32_t last = last_irq_us;
    bool stalled = (uint32_t)(time_us_32() - last) > ADC_STALL_US;
    if (overrun || underrun || stalled) {
        /* Mask our DMA IRQ sources before foreground fault handling. */
        dma_set_irq0_channel_mask_enabled(dma_mask, false);
        if (completed.fault == ADC_CAPTURE_OK) {
            overrun = (adc_hw->fcs & ADC_FCS_OVER_BITS) != 0;
            underrun = (adc_hw->fcs & ADC_FCS_UNDER_BITS) != 0;
            last = last_irq_us;
            stalled = (uint32_t)(time_us_32() - last) > ADC_STALL_US;
            if (overrun || underrun || stalled)
                stop_capture(overrun ? ADC_CAPTURE_FIFO_OVERRUN : underrun ? ADC_CAPTURE_FIFO_UNDERRUN : ADC_CAPTURE_STALLED);
            else dma_set_irq0_channel_mask_enabled(dma_mask, true);
        }
    }
}

bool adc_capture_pause(bool *was_running)
{
    if (!was_running) return false;
    *was_running = false;
    if (!started || paused || completed.fault != ADC_CAPTURE_OK) return true;
    dma_set_irq0_channel_mask_enabled(dma_mask, false);
    if (completed.fault != ADC_CAPTURE_OK) return true;
    if (adc_hw->fcs & (ADC_FCS_OVER_BITS | ADC_FCS_UNDER_BITS)) {
        stop_capture((adc_hw->fcs & ADC_FCS_OVER_BITS) ? ADC_CAPTURE_FIFO_OVERRUN : ADC_CAPTURE_FIFO_UNDERRUN);
        return true;
    }
    disable_capture();
    adc_fifo_drain();
    paused = true;
    *was_running = true;
    return true;
}

bool adc_capture_resume(void)
{
    if (!paused) return true;
    if (completed.fault != ADC_CAPTURE_OK) return false;
    adc_fifo_drain();
    clear_fifo_flags();
    completed.expected_channel = 0;
    configure_dma();
    dma_set_irq0_channel_mask_enabled(dma_mask, true);
    last_irq_us = time_us_32();
    dma_start_channel_mask(1u << dma_channels[0]);
    adc_run(true);
    paused = false;
    return true;
}

bool adc_capture_take(adc_block_t *block) { return adc_blocks_take(&completed, block); }
bool adc_capture_pending(void) { return completed.head != completed.tail; }
adc_capture_fault_t adc_capture_fault(void) { return (adc_capture_fault_t)completed.fault; }
bool adc_capture_fifo_overrun(void) { return started && (adc_hw->fcs & ADC_FCS_OVER_BITS) != 0; }
bool adc_capture_fifo_underrun(void) { return started && (adc_hw->fcs & ADC_FCS_UNDER_BITS) != 0; }
