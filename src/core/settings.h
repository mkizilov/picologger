#ifndef PICOLOGGER_SETTINGS_H
#define PICOLOGGER_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SETTINGS_PAGE_SIZE 256u
#define SETTINGS_CHANNEL_BME280 1u
#define SETTINGS_CHANNEL_ADC 2u
#define SETTINGS_CHANNEL_MASK (SETTINGS_CHANNEL_BME280 | SETTINGS_CHANNEL_ADC)

typedef struct {
    uint32_t rate_hz;
    uint32_t channels;
    uint32_t rotate_bytes;
    bool start_on_boot;
} settings_t;

typedef struct {
    void *context;
    bool (*read)(void *, unsigned int slot, uint8_t *data, size_t length);
    bool (*erase)(void *, unsigned int slot);
    bool (*program)(void *, unsigned int slot, const uint8_t *data, size_t length);
} settings_flash_t;

typedef struct {
    settings_t values;
    uint32_t revision;
    int active_slot;
} settings_snapshot_t;

bool settings_valid(const settings_t *settings);
bool settings_encode(uint8_t page[SETTINGS_PAGE_SIZE], const settings_t *settings, uint32_t revision);
bool settings_decode(const uint8_t page[SETTINGS_PAGE_SIZE], settings_t *settings, uint32_t *revision);
/* False means a read failed: do not save until both slots can be read. */
bool settings_load(const settings_flash_t *flash, const settings_t *defaults, settings_snapshot_t *snapshot);
/* snapshot must be the most recent successful load/save, with no other writer.
 * Erases only the inactive sector, programs it, and verifies before accepting it.
 * A false return can mean the new page was partly or fully programmed. */
bool settings_save(const settings_flash_t *flash, const settings_t *values, settings_snapshot_t *snapshot);

#endif
