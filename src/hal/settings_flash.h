#ifndef PICOLOGGER_SETTINGS_FLASH_H
#define PICOLOGGER_SETTINGS_FLASH_H

#include "settings.h"

settings_flash_t settings_flash_interface(void);
/* Arm only after storage is quiescent and ADC DMA is stopped. */
void settings_flash_set_guard(bool armed);

#endif
