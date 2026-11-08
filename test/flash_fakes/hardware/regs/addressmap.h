#ifndef TEST_FLASH_ADDRESSMAP_H
#define TEST_FLASH_ADDRESSMAP_H
#include <stdint.h>
extern uint8_t fake_xip[16384];
uintptr_t settings_test_ram_start(void);
uintptr_t settings_test_ram_end(void);
#define XIP_BASE ((uintptr_t)fake_xip)
#define SRAM_BASE settings_test_ram_start()
#define SRAM_END settings_test_ram_end()
#endif
