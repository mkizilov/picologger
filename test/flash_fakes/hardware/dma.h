#ifndef TEST_FLASH_DMA_H
#define TEST_FLASH_DMA_H
#include <stdbool.h>
#define NUM_DMA_CHANNELS 12u
bool dma_channel_is_busy(unsigned int channel);
#endif
