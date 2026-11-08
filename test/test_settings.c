#include "unity.h"
#include "settings.h"

#include <string.h>

#define SECTOR_SIZE 4096u
typedef struct {
    uint8_t sector[2][SECTOR_SIZE];
    int erase_cut, program_cut;
    bool fail_read, corrupt_after_program;
} fake_flash_t;

static fake_flash_t fake;
static settings_flash_t flash;
static const settings_t defaults = {50, 3, 16777216, true};
static const settings_t changed = {25, 1, 8192, false};

static bool fake_read(void *context, unsigned int slot, uint8_t *data, size_t length)
{
    fake_flash_t *f = context;
    if (f->fail_read || slot > 1 || length > SECTOR_SIZE) return false;
    memcpy(data, f->sector[slot], length);
    return true;
}
static bool fake_erase(void *context, unsigned int slot)
{
    fake_flash_t *f = context;
    size_t n = f->erase_cut >= 0 ? (size_t)f->erase_cut : SECTOR_SIZE;
    memset(f->sector[slot], 0xff, n);
    return f->erase_cut < 0;
}
static bool fake_program(void *context, unsigned int slot, const uint8_t *data, size_t length)
{
    fake_flash_t *f = context;
    size_t n = f->program_cut >= 0 ? (size_t)f->program_cut : length;
    for (size_t i = 0; i < n; ++i) f->sector[slot][i] &= data[i];
    if (f->corrupt_after_program) f->sector[slot][12] ^= 1;
    return f->program_cut < 0;
}
void setUp(void)
{
    memset(&fake, 0, sizeof(fake));
    memset(fake.sector, 0xff, sizeof(fake.sector));
    fake.erase_cut = fake.program_cut = -1;
    flash = (settings_flash_t){&fake, fake_read, fake_erase, fake_program};
}
void tearDown(void) {}

static void serialization_and_validation(void)
{
    uint8_t page[SETTINGS_PAGE_SIZE];
    settings_t decoded;
    uint32_t revision;
    TEST_ASSERT_TRUE(settings_encode(page, &changed, UINT32_C(0x12345678)));
    TEST_ASSERT_EQUAL_MEMORY("CFG1\1\0\x20\0\x78\x56\x34\x12", page, 12);
    TEST_ASSERT_TRUE(settings_decode(page, &decoded, &revision));
    TEST_ASSERT_EQUAL_UINT32(UINT32_C(0x12345678), revision);
    TEST_ASSERT_EQUAL_UINT32(changed.rate_hz, decoded.rate_hz);
    TEST_ASSERT_EQUAL_UINT32(changed.channels, decoded.channels);
    TEST_ASSERT_EQUAL_UINT32(changed.rotate_bytes, decoded.rotate_bytes);
    TEST_ASSERT_FALSE(decoded.start_on_boot);
    page[12] ^= 1;
    TEST_ASSERT_FALSE(settings_decode(page, &decoded, &revision));
    page[12] ^= 1; page[200] = 0;
    TEST_ASSERT_FALSE(settings_decode(page, &decoded, &revision));
    settings_t invalid = defaults; invalid.rate_hz = 0;
    TEST_ASSERT_FALSE(settings_encode(page, &invalid, 1));
    invalid = defaults; invalid.channels = 4;
    TEST_ASSERT_FALSE(settings_valid(&invalid));
    invalid = defaults; invalid.rotate_bytes = 8191;
    TEST_ASSERT_FALSE(settings_valid(&invalid));
}

static void slot_selection_corruption_and_counter_wrap(void)
{
    settings_snapshot_t snapshot;
    TEST_ASSERT_TRUE(settings_load(&flash, &defaults, &snapshot));
    TEST_ASSERT_EQUAL_INT(-1, snapshot.active_slot);
    TEST_ASSERT_TRUE(settings_save(&flash, &defaults, &snapshot));
    TEST_ASSERT_EQUAL_INT(0, snapshot.active_slot);
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.revision);
    TEST_ASSERT_TRUE(settings_save(&flash, &changed, &snapshot));
    TEST_ASSERT_EQUAL_INT(1, snapshot.active_slot);
    TEST_ASSERT_EQUAL_UINT32(2, snapshot.revision);
    fake.sector[1][12] ^= 1;
    TEST_ASSERT_TRUE(settings_load(&flash, &defaults, &snapshot));
    TEST_ASSERT_EQUAL_INT(0, snapshot.active_slot);
    TEST_ASSERT_EQUAL_UINT32(50, snapshot.values.rate_hz);
    fake.sector[1][12] ^= 1; fake.sector[0][12] ^= 1;
    TEST_ASSERT_TRUE(settings_load(&flash, &defaults, &snapshot));
    TEST_ASSERT_EQUAL_INT(1, snapshot.active_slot);
    TEST_ASSERT_EQUAL_UINT32(25, snapshot.values.rate_hz);
    TEST_ASSERT_TRUE(settings_encode(fake.sector[0], &defaults, UINT32_MAX));
    TEST_ASSERT_TRUE(settings_encode(fake.sector[1], &changed, 0));
    TEST_ASSERT_TRUE(settings_load(&flash, &defaults, &snapshot));
    TEST_ASSERT_EQUAL_INT(1, snapshot.active_slot);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.revision);
}

static void interrupted_erase_and_program_preserve_a_valid_slot(void)
{
    TEST_ASSERT_TRUE(settings_encode(fake.sector[0], &defaults, 7));
    uint8_t old[SETTINGS_PAGE_SIZE];
    memcpy(old, fake.sector[0], sizeof(old));
    for (unsigned int cut = 0; cut <= SECTOR_SIZE; ++cut) {
        memset(fake.sector[1], 0, SECTOR_SIZE);
        fake.erase_cut = (int)cut;
        settings_snapshot_t s = {defaults, 7, 0};
        TEST_ASSERT_FALSE(settings_save(&flash, &changed, &s));
        TEST_ASSERT_EQUAL_MEMORY(old, fake.sector[0], sizeof(old));
        TEST_ASSERT_TRUE(settings_load(&flash, &defaults, &s));
        TEST_ASSERT_EQUAL_INT(0, s.active_slot);
        TEST_ASSERT_EQUAL_UINT32(50, s.values.rate_hz);
    }
    fake.erase_cut = -1;
    for (unsigned int cut = 0; cut <= SETTINGS_PAGE_SIZE; ++cut) {
        memset(fake.sector[1], 0, SECTOR_SIZE);
        fake.program_cut = (int)cut;
        settings_snapshot_t s = {defaults, 7, 0};
        TEST_ASSERT_FALSE(settings_save(&flash, &changed, &s));
        TEST_ASSERT_EQUAL_MEMORY(old, fake.sector[0], sizeof(old));
        TEST_ASSERT_TRUE(settings_load(&flash, &defaults, &s));
        TEST_ASSERT_TRUE((s.active_slot == 0 && s.values.rate_hz == 50) ||
                         (s.active_slot == 1 && s.values.rate_hz == 25));
    }
}

static void read_and_verification_failures_are_reported(void)
{
    settings_snapshot_t s;
    fake.fail_read = true;
    TEST_ASSERT_FALSE(settings_load(&flash, &defaults, &s));
    fake.fail_read = false;
    TEST_ASSERT_TRUE(settings_load(&flash, &defaults, &s));
    fake.corrupt_after_program = true;
    TEST_ASSERT_FALSE(settings_save(&flash, &changed, &s));
    TEST_ASSERT_EQUAL_INT(-1, s.active_slot);
}

static void every_single_byte_corruption_falls_back_to_the_other_slot(void)
{
    TEST_ASSERT_TRUE(settings_encode(fake.sector[0], &defaults, 10));
    TEST_ASSERT_TRUE(settings_encode(fake.sector[1], &changed, 11));
    for (unsigned int slot = 0; slot < 2; ++slot) {
        for (unsigned int offset = 0; offset < SETTINGS_PAGE_SIZE; ++offset) {
            fake.sector[slot][offset] ^= 1;
            settings_snapshot_t snapshot;
            TEST_ASSERT_TRUE(settings_load(&flash, &defaults, &snapshot));
            TEST_ASSERT_EQUAL_INT((int)(slot ^ 1u), snapshot.active_slot);
            fake.sector[slot][offset] ^= 1;
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(serialization_and_validation);
    RUN_TEST(slot_selection_corruption_and_counter_wrap);
    RUN_TEST(interrupted_erase_and_program_preserve_a_valid_slot);
    RUN_TEST(read_and_verification_failures_are_reported);
    RUN_TEST(every_single_byte_corruption_falls_back_to_the_other_slot);
    return UNITY_END();
}
