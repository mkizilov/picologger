#ifndef TEST_DMA_H
#define TEST_DMA_H
#include <stdbool.h>
#include <stdint.h>
enum dma_channel_transfer_size { DMA_SIZE_16 = 1, DMA_SIZE_32 = 2 };
enum { DREQ_ADC = 36, DREQ_FORCE = 63 };
typedef struct { unsigned int size, read_inc, write_inc, ring_bits, chain, dreq; bool ring_write, enabled, quiet; } dma_channel_config;
typedef struct { uint32_t ctrl_trig; uintptr_t write_addr; uint32_t al1_transfer_count_trig; } dma_channel_hw_t;
int dma_claim_unused_channel(bool required);
void dma_channel_unclaim(unsigned int channel);
dma_channel_config dma_channel_get_default_config(unsigned int channel);
dma_channel_config dma_get_channel_config(unsigned int channel);
void channel_config_set_enable(dma_channel_config *c, bool enabled);
void dma_channel_set_config(unsigned int channel, const dma_channel_config *c, bool trigger);
void channel_config_set_transfer_data_size(dma_channel_config *c, enum dma_channel_transfer_size size);
void channel_config_set_read_increment(dma_channel_config *c, bool enabled);
void channel_config_set_write_increment(dma_channel_config *c, bool enabled);
void channel_config_set_ring(dma_channel_config *c, bool write, unsigned int bits);
void channel_config_set_dreq(dma_channel_config *c, unsigned int dreq);
void channel_config_set_chain_to(dma_channel_config *c, unsigned int target);
void channel_config_set_irq_quiet(dma_channel_config *c, bool quiet);
void dma_channel_configure(unsigned int channel, const dma_channel_config *c, volatile void *dst,
                           const volatile void *src, unsigned int count, bool trigger);
void dma_channel_acknowledge_irq0(unsigned int channel);
void dma_set_irq0_channel_mask_enabled(uint32_t mask, bool enabled);
bool dma_channel_get_irq0_status(unsigned int channel);
bool dma_channel_is_busy(unsigned int channel);
dma_channel_hw_t *dma_channel_hw_addr(unsigned int channel);
void dma_start_channel_mask(uint32_t mask);
void dma_channel_abort(unsigned int channel);
#endif
