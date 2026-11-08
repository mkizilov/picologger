#ifndef TEST_FLASH_H
#define TEST_FLASH_H
#include <stddef.h>
#include <stdint.h>
#define PICO_FLASH_SIZE_BYTES 16384u
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE 256u
void flash_range_erase(uint32_t offset, size_t count);
void flash_range_program(uint32_t offset, const uint8_t *data, size_t count);
#endif
