#include "settings.h"

#include "command_io.h"

#include <string.h>

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned int i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool settings_valid(const settings_t *s)
{
    return s && s->rate_hz >= 1 && s->rate_hz <= 50 &&
           (s->channels & ~SETTINGS_CHANNEL_MASK) == 0 && s->rotate_bytes >= 8192;
}

bool settings_encode(uint8_t page[SETTINGS_PAGE_SIZE], const settings_t *s, uint32_t revision)
{
    if (!page || !settings_valid(s)) return false;
    memset(page, 0xff, SETTINGS_PAGE_SIZE);
    memcpy(page, "CFG1", 4);
    page[4] = 1; page[5] = 0;
    page[6] = 32; page[7] = 0;
    put32(page + 8, revision);
    put32(page + 12, s->rate_hz);
    put32(page + 16, s->channels);
    put32(page + 20, s->rotate_bytes);
    page[24] = s->start_on_boot ? 1 : 0;
    page[25] = page[26] = page[27] = 0;
    put32(page + 28, command_crc32_update(UINT32_MAX, page, 28) ^ UINT32_MAX);
    return true;
}

bool settings_decode(const uint8_t page[SETTINGS_PAGE_SIZE], settings_t *out, uint32_t *revision)
{
    if (!page || !out || !revision || memcmp(page, "CFG1", 4) || page[4] != 1 || page[5] != 0 ||
        page[6] != 32 || page[7] != 0 || page[24] > 1 || page[25] || page[26] || page[27] ||
        get32(page + 28) != (command_crc32_update(UINT32_MAX, page, 28) ^ UINT32_MAX)) return false;
    for (size_t i = 32; i < SETTINGS_PAGE_SIZE; ++i) if (page[i] != 0xff) return false;
    settings_t decoded = {get32(page + 12), get32(page + 16), get32(page + 20), page[24] != 0};
    if (!settings_valid(&decoded)) return false;
    *revision = get32(page + 8);
    *out = decoded;
    return true;
}

bool settings_load(const settings_flash_t *flash, const settings_t *defaults, settings_snapshot_t *snapshot)
{
    if (!flash || !flash->read || !defaults || !settings_valid(defaults) || !snapshot) return false;
    uint8_t pages[2][SETTINGS_PAGE_SIZE];
    settings_t values[2];
    uint32_t revision[2];
    bool valid[2];
    for (unsigned int slot = 0; slot < 2; ++slot) {
        if (!flash->read(flash->context, slot, pages[slot], sizeof(pages[slot]))) return false;
        valid[slot] = settings_decode(pages[slot], &values[slot], &revision[slot]);
    }
    if (!valid[0] && !valid[1]) {
        *snapshot = (settings_snapshot_t){*defaults, 0, -1};
        return true;
    }
    unsigned int slot = valid[1] && (!valid[0] ||
        (revision[1] != revision[0] && (uint32_t)(revision[1] - revision[0]) < UINT32_C(0x80000000))) ? 1 : 0;
    *snapshot = (settings_snapshot_t){values[slot], revision[slot], (int)slot};
    return true;
}

bool settings_save(const settings_flash_t *flash, const settings_t *values, settings_snapshot_t *snapshot)
{
    if (!flash || !flash->read || !flash->erase || !flash->program || !snapshot || !settings_valid(values) ||
        snapshot->active_slot < -1 || snapshot->active_slot > 1) return false;
    unsigned int next = snapshot->active_slot == 0 ? 1u : 0u;
    uint32_t revision = snapshot->active_slot < 0 ? 1u : snapshot->revision + 1u;
    uint8_t page[SETTINGS_PAGE_SIZE], verify[SETTINGS_PAGE_SIZE];
    if (!settings_encode(page, values, revision) || !flash->erase(flash->context, next) ||
        !flash->program(flash->context, next, page, sizeof(page)) ||
        !flash->read(flash->context, next, verify, sizeof(verify)) || memcmp(page, verify, sizeof(page))) return false;
    *snapshot = (settings_snapshot_t){*values, revision, (int)next};
    return true;
}
