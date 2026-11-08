#include "settings_flash.h"

#include <string.h>

#include "hardware/flash.h"
#include "hardware/dma.h"
#include "hardware/regs/addressmap.h"
#include "pico/error.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

#define SETTINGS_SECTOR_COUNT 2u
#define SETTINGS_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - SETTINGS_SECTOR_COUNT * FLASH_SECTOR_SIZE)

_Static_assert(PICO_FLASH_SIZE_BYTES >= SETTINGS_SECTOR_COUNT * FLASH_SECTOR_SIZE, "flash too small for settings");
_Static_assert(SETTINGS_PAGE_SIZE == FLASH_PAGE_SIZE, "settings page size mismatch");
_Static_assert((PICO_FLASH_SIZE_BYTES % FLASH_SECTOR_SIZE) == 0, "flash size must be sector aligned");

typedef struct {
    uint32_t offset;
    const uint8_t *data;
    bool erase, completed;
} flash_operation_t;

static bool guarded;

#ifdef PICOLOGGER_FLASH_TEST
uintptr_t settings_test_link_start(void);
uintptr_t settings_test_image_end(void);
#define LINK_SETTINGS_START settings_test_link_start()
#define LINK_IMAGE_END settings_test_image_end()
#else
extern uint8_t __picologger_settings_start[], __flash_binary_end[];
#define LINK_SETTINGS_START ((uintptr_t)__picologger_settings_start)
#define LINK_IMAGE_END ((uintptr_t)__flash_binary_end)
#endif

static bool layout_valid(void)
{
    uintptr_t expected = (uintptr_t)XIP_BASE + SETTINGS_FLASH_OFFSET;
    return LINK_SETTINGS_START == expected && LINK_IMAGE_END <= expected;
}

static void __not_in_flash_func(run_flash_operation)(void *context)
{
    flash_operation_t *operation = context;
    /* A chained or peripheral-paced DMA may read XIP even with both cores parked. */
    for (unsigned int i = 0; i < NUM_DMA_CHANNELS; ++i)
        if (dma_channel_is_busy(i)) return;
    if (operation->erase) flash_range_erase(operation->offset, FLASH_SECTOR_SIZE);
    else flash_range_program(operation->offset, operation->data, FLASH_PAGE_SIZE);
    operation->completed = true;
}

static bool read_slot(void *context, unsigned int slot, uint8_t *data, size_t length)
{
    (void)context;
    if (!layout_valid() || slot >= SETTINGS_SECTOR_COUNT || !data || length != SETTINGS_PAGE_SIZE) return false;
    const uint8_t *source = (const uint8_t *)(XIP_BASE + SETTINGS_FLASH_OFFSET + slot * FLASH_SECTOR_SIZE);
    memcpy(data, source, length);
    return true;
}

static bool execute(unsigned int slot, const uint8_t *data, bool erase)
{
    if (!guarded || !layout_valid() || slot >= SETTINGS_SECTOR_COUNT || (!erase && !data)) return false;
    flash_operation_t operation = {SETTINGS_FLASH_OFFSET + slot * FLASH_SECTOR_SIZE, data, erase, false};
    return flash_safe_execute(run_flash_operation, &operation, 1000) == PICO_OK && operation.completed;
}

static bool erase_slot(void *context, unsigned int slot)
{
    (void)context;
    return execute(slot, NULL, true);
}

static bool program_slot(void *context, unsigned int slot, const uint8_t *data, size_t length)
{
    (void)context;
    uintptr_t address = (uintptr_t)data;
    return length == SETTINGS_PAGE_SIZE && address >= (uintptr_t)SRAM_BASE &&
           address <= (uintptr_t)SRAM_END - SETTINGS_PAGE_SIZE && execute(slot, data, false);
}

void settings_flash_set_guard(bool armed) { guarded = armed; }

settings_flash_t settings_flash_interface(void)
{
    return (settings_flash_t){NULL, read_slot, erase_slot, program_slot};
}
