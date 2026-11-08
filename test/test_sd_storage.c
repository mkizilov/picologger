#include "unity.h"
#include "sd_storage.h"
#include "ff.h"
#include "hw_config.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char name[20];
    uint8_t data[9000];
    size_t length;
    bool directory;
} entry_t;

static entry_t entries[12];
static unsigned int entry_count, truncates, syncs, closed_dirs, driver_calls, mount_calls;
static bool driver_ok, present, fail_sync, fail_close, fail_rename, rename_exists, fail_final_open, fail_write;
static bool fail_read, fail_readdir, fail_truncate;
static int partial_header;
static uint64_t fake_time;
static unsigned int recovery_progress;
static sd_card_t card;

static bool same_name(const char *a, const char *b)
{
    while (*a && *b) {
        if (toupper((unsigned char)*a++) != toupper((unsigned char)*b++)) return false;
    }
    return *a == *b;
}
static int find_entry(const char *name)
{
    for (unsigned int i = 0; i < entry_count; ++i) if (same_name(entries[i].name, name)) return (int)i;
    return -1;
}
static bool test_card(sd_card_t *value) { return value == &card && present; }
bool sd_init_driver(void) { ++driver_calls; card.sd_test_com = test_card; return driver_ok; }
sd_card_t *sd_get_by_num(size_t number) { return number == 0 ? &card : NULL; }
uint64_t time_us_64(void) { fake_time += 7; return fake_time; }

FRESULT f_mount(FATFS *fs, const char *path, unsigned char immediate)
{
    TEST_ASSERT_EQUAL_STRING("0:", path);
    if (fs) { TEST_ASSERT_EQUAL_UINT(1, immediate); ++mount_calls; }
    return FR_OK;
}
FRESULT f_stat(const char *path, FILINFO *info)
{
    (void)info;
    return find_entry(path) >= 0 ? FR_OK : FR_NO_FILE;
}
FRESULT f_open(FIL *target, const char *path, unsigned char flags)
{
    int index = find_entry(path);
    if (flags & FA_CREATE_NEW) {
        if (index >= 0) return FR_EXIST;
        if (entry_count == 12) return FR_DISK_ERR;
        index = (int)entry_count++;
        (void)snprintf(entries[index].name, sizeof(entries[index].name), "%s", path);
    } else {
        if (index < 0) return FR_NO_FILE;
        if (fail_final_open && flags == FA_WRITE) return FR_DISK_ERR;
    }
    target->index = index;
    target->position = 0;
    target->open = true;
    return FR_OK;
}
FRESULT f_write(FIL *target, const void *data, UINT size, UINT *written)
{
    entry_t *e = &entries[target->index];
    *written = partial_header >= 0 && size == LOG_HEADER_V2_SIZE ? (UINT)partial_header : size;
    if (target->position + *written > sizeof(e->data)) return FR_DISK_ERR;
    memcpy(e->data + target->position, data, *written);
    target->position += *written;
    if (target->position > e->length) e->length = (size_t)target->position;
    return fail_write ? FR_DISK_ERR : FR_OK;
}
FRESULT f_read(FIL *target, void *data, UINT size, UINT *read)
{
    if (fail_read) return FR_DISK_ERR;
    entry_t *e = &entries[target->index];
    if (target->position > e->length) return FR_DISK_ERR;
    if (size > e->length - target->position) size = (UINT)(e->length - target->position);
    memcpy(data, e->data + target->position, size);
    target->position += size;
    *read = size;
    return FR_OK;
}
FRESULT f_sync(FIL *target) { (void)target; ++syncs; return fail_sync ? FR_DISK_ERR : FR_OK; }
FRESULT f_close(FIL *target) { target->open = false; return fail_close ? FR_DISK_ERR : FR_OK; }
FRESULT f_lseek(FIL *target, FSIZE_t offset)
{
    if (offset > entries[target->index].length) return FR_DISK_ERR;
    target->position = offset;
    return FR_OK;
}
FRESULT f_truncate(FIL *target)
{
    ++truncates;
    if (fail_truncate) return FR_DISK_ERR;
    entries[target->index].length = (size_t)target->position;
    return FR_OK;
}
FSIZE_t fake_f_size(FIL *target) { return entries[target->index].length; }
FRESULT f_opendir(DIR *dir, const char *path)
{
    TEST_ASSERT_EQUAL_STRING("0:/", path);
    dir->position = 0;
    return FR_OK;
}
FRESULT f_readdir(DIR *dir, FILINFO *info)
{
    if (fail_readdir) return FR_DISK_ERR;
    memset(info, 0, sizeof(*info));
    if (dir->position == entry_count) return FR_OK;
    entry_t *e = &entries[dir->position++];
    (void)snprintf(info->fname, sizeof(info->fname), "%s", e->name + 3);
    info->fattrib = e->directory ? AM_DIR : 0;
    info->fsize = e->length;
    return FR_OK;
}
FRESULT f_closedir(DIR *dir) { (void)dir; ++closed_dirs; return FR_OK; }
FRESULT f_rename(const char *old_name, const char *new_name)
{
    if (fail_rename) return FR_DISK_ERR;
    if (rename_exists) return FR_EXIST;
    if (find_entry(new_name) >= 0) return FR_EXIST;
    int index = find_entry(old_name);
    if (index < 0) return FR_NO_FILE;
    (void)snprintf(entries[index].name, sizeof(entries[index].name), "%s", new_name);
    return FR_OK;
}

static void clear_fake(void)
{
    memset(entries, 0, sizeof(entries));
    entry_count = truncates = syncs = closed_dirs = mount_calls = 0;
    fail_sync = fail_close = fail_rename = rename_exists = fail_final_open = fail_write = false;
    fail_read = fail_readdir = fail_truncate = false;
    partial_header = -1;
}
void setUp(void)
{
    clear_fake();
    recovery_progress = 0;
    sd_storage_set_progress_hook(NULL);
    driver_ok = present = true;
    card.sd_test_com = test_card;
}
static void note_recovery_progress(void) { ++recovery_progress; }
void tearDown(void) {}

static void add_log(const char *name, bool v2, unsigned int records, unsigned int tail)
{
    entry_t *e = &entries[entry_count++];
    (void)snprintf(e->name, sizeof(e->name), "%s", name);
    const log_header_t header = {.firmware = "test", .sample_rate_hz = 50,
                                 .channel_count = 1, .channel_ids = {1}};
    size_t header_size = v2 ? LOG_HEADER_V2_SIZE : LOG_HEADER_SIZE;
    TEST_ASSERT_EQUAL_INT(LOG_OK, v2 ? log_encode_header_v2(e->data, sizeof(e->data), &header) :
                                     log_encode_header(e->data, sizeof(e->data), &header));
    for (unsigned int i = 0; i < records; ++i) {
        log_record_t record = {.sequence = i};
        TEST_ASSERT_EQUAL_INT(LOG_OK, log_encode_record(e->data + header_size + i * LOG_RECORD_SIZE,
                                                        LOG_RECORD_SIZE, &record));
    }
    e->length = header_size + records * LOG_RECORD_SIZE + tail;
}

static void adapter_checks_presence_and_publishes_synced_header(void)
{
    log_storage_t ops = sd_storage_interface();
    driver_ok = false;
    TEST_ASSERT_FALSE(ops.mount(ops.context));
    driver_ok = true; present = false;
    TEST_ASSERT_FALSE(ops.mount(ops.context));
    present = true;
    TEST_ASSERT_TRUE(ops.mount(ops.context));
    TEST_ASSERT_EQUAL_UINT(1, mount_calls);
    bool found = true;
    log_recovery_result_t recovered;
    TEST_ASSERT_TRUE(ops.recover_newest(ops.context, &found, &recovered));
    TEST_ASSERT_FALSE(found);
    const log_header_t h = {.firmware = "test", .sample_rate_hz = 50, .channel_count = 1, .channel_ids = {1}};
    uint8_t bytes[LOG_HEADER_V2_SIZE];
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_encode_header_v2(bytes, sizeof(bytes), &h));
    uint64_t write_us, sync_us;
    TEST_ASSERT_EQUAL_INT(0, ops.create(ops.context, "0:/LOG_0001.BIN", bytes, sizeof(bytes), &write_us, &sync_us));
    TEST_ASSERT_EQUAL_UINT64(7, write_us);
    TEST_ASSERT_EQUAL_UINT64(7, sync_us);
    TEST_ASSERT_EQUAL_UINT(1, syncs);
    TEST_ASSERT_EQUAL_INT(-1, find_entry("0:/LOG_0001.TMP"));
    TEST_ASSERT_EQUAL_INT(0, find_entry("0:/LOG_0001.BIN"));
    TEST_ASSERT_EQUAL_MEMORY(bytes, entries[0].data, sizeof(bytes));
    uint8_t data[4096] = {0};
    size_t written;
    TEST_ASSERT_TRUE(ops.write(ops.context, data, sizeof(data), &written));
    TEST_ASSERT_EQUAL_UINT(sizeof(data), written);
    TEST_ASSERT_EQUAL_UINT(LOG_HEADER_V2_SIZE + sizeof(data), entries[0].length);
    TEST_ASSERT_TRUE(ops.sync(ops.context));
    TEST_ASSERT_TRUE(ops.close(ops.context));
    ops.unmount(ops.context);
    TEST_ASSERT_TRUE((card.state.m_Status & STA_NOINIT) != 0);
}

static void incomplete_header_is_never_published(void)
{
    log_storage_t ops = sd_storage_interface();
    const log_header_t h = {.firmware = "test", .sample_rate_hz = 50, .channel_count = 1, .channel_ids = {1}};
    uint8_t bytes[LOG_HEADER_V2_SIZE];
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_encode_header_v2(bytes, sizeof(bytes), &h));
    for (unsigned int cut = 0; cut < LOG_HEADER_V2_SIZE; ++cut) {
        clear_fake();
        partial_header = (int)cut;
        uint64_t write_us, sync_us;
        TEST_ASSERT_EQUAL_INT(-1, ops.create(ops.context, "0:/LOG_0001.BIN", bytes, sizeof(bytes), &write_us, &sync_us));
        TEST_ASSERT_EQUAL_INT(-1, find_entry("0:/LOG_0001.BIN"));
        TEST_ASSERT_EQUAL_INT(0, find_entry("0:/LOG_0001.TMP"));
    }
    clear_fake(); fail_sync = true;
    uint64_t write_us, sync_us;
    TEST_ASSERT_EQUAL_INT(-1, ops.create(ops.context, "0:/LOG_0001.BIN", bytes, sizeof(bytes), &write_us, &sync_us));
    TEST_ASSERT_EQUAL_INT(-1, find_entry("0:/LOG_0001.BIN"));
    clear_fake(); fail_write = true;
    TEST_ASSERT_EQUAL_INT(-1, ops.create(ops.context, "0:/LOG_0001.BIN", bytes, sizeof(bytes), &write_us, &sync_us));
    TEST_ASSERT_EQUAL_INT(-1, find_entry("0:/LOG_0001.BIN"));
    clear_fake(); fail_rename = true;
    TEST_ASSERT_EQUAL_INT(-1, ops.create(ops.context, "0:/LOG_0001.BIN", bytes, sizeof(bytes), &write_us, &sync_us));
    TEST_ASSERT_EQUAL_INT(-1, find_entry("0:/LOG_0001.BIN"));
    clear_fake(); rename_exists = true;
    TEST_ASSERT_EQUAL_INT(1, ops.create(ops.context, "0:/LOG_0001.BIN", bytes, sizeof(bytes), &write_us, &sync_us));
    TEST_ASSERT_EQUAL_INT(-1, find_entry("0:/LOG_0001.BIN"));
    TEST_ASSERT_EQUAL_INT(0, find_entry("0:/LOG_0001.TMP"));
    clear_fake(); fail_close = true;
    TEST_ASSERT_EQUAL_INT(-1, ops.create(ops.context, "0:/LOG_0001.BIN", bytes, sizeof(bytes), &write_us, &sync_us));
    TEST_ASSERT_EQUAL_INT(-1, find_entry("0:/LOG_0001.BIN"));
    clear_fake(); fail_final_open = true;
    TEST_ASSERT_EQUAL_INT(-1, ops.create(ops.context, "0:/LOG_0001.BIN", bytes, sizeof(bytes), &write_us, &sync_us));
    TEST_ASSERT_EQUAL_INT(0, find_entry("0:/LOG_0001.BIN"));
    log_header_t out;
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_header(&out, entries[0].data, entries[0].length));
}

static void existing_final_or_temporary_file_is_never_overwritten(void)
{
    log_storage_t ops = sd_storage_interface();
    const log_header_t h = {.firmware = "test", .sample_rate_hz = 50, .channel_count = 1, .channel_ids = {1}};
    uint8_t bytes[LOG_HEADER_V2_SIZE];
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_encode_header_v2(bytes, sizeof(bytes), &h));
    add_log("0:/LOG_0001.BIN", false, 1, 0);
    size_t length = entries[0].length;
    uint64_t write_us, sync_us;
    TEST_ASSERT_EQUAL_INT(1, ops.create(ops.context, "0:/LOG_0001.BIN", bytes, sizeof(bytes), &write_us, &sync_us));
    TEST_ASSERT_EQUAL_UINT(length, entries[0].length);
    clear_fake();
    add_log("0:/LOG_0001.TMP", false, 1, 0);
    length = entries[0].length;
    TEST_ASSERT_EQUAL_INT(1, ops.create(ops.context, "0:/LOG_0001.BIN", bytes, sizeof(bytes), &write_us, &sync_us));
    TEST_ASSERT_EQUAL_UINT(length, entries[0].length);
    TEST_ASSERT_EQUAL_INT(-1, find_entry("0:/LOG_0001.BIN"));
}

static void recovers_highest_numbered_final_log_only(void)
{
    log_storage_t ops = sd_storage_interface();
    add_log("0:/LOG_0002.BIN", false, 2, 3);
    add_log("0:/log_0005.bin", true, 3, 7);
    add_log("0:/LOG_9999.TMP", true, 1, 1);
    add_log("0:/LOG_9998.BIN", true, 1, 1);
    entries[3].directory = true;
    bool found;
    log_recovery_result_t result;
    TEST_ASSERT_TRUE(ops.recover_newest(ops.context, &found, &result));
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL_UINT64(LOG_HEADER_V2_SIZE + 3 * LOG_RECORD_SIZE + 7, result.original_size);
    TEST_ASSERT_EQUAL_UINT64(LOG_HEADER_V2_SIZE + 3 * LOG_RECORD_SIZE, result.recovered_size);
    TEST_ASSERT_EQUAL_UINT(LOG_HEADER_V2_SIZE + 3 * LOG_RECORD_SIZE, entries[1].length);
    TEST_ASSERT_EQUAL_UINT(LOG_HEADER_SIZE + 2 * LOG_RECORD_SIZE + 3, entries[0].length);
    TEST_ASSERT_EQUAL_UINT(1, truncates);
    TEST_ASSERT_EQUAL_UINT(1, syncs);
    TEST_ASSERT_EQUAL_UINT(1, closed_dirs);
}

static void invalid_newest_or_io_error_preserves_data(void)
{
    log_storage_t ops = sd_storage_interface();
    add_log("0:/LOG_0001.BIN", false, 1, 2);
    add_log("0:/LOG_0002.BIN", true, 1, 3);
    entries[1].data[40] ^= 1;
    bool found = false;
    log_recovery_result_t result;
    TEST_ASSERT_FALSE(ops.recover_newest(ops.context, &found, &result));
    TEST_ASSERT_EQUAL_UINT(0, truncates);
    entries[1].data[40] ^= 1;
    fail_read = true;
    TEST_ASSERT_FALSE(ops.recover_newest(ops.context, &found, &result));
    TEST_ASSERT_EQUAL_UINT(0, truncates);
    fail_read = false; fail_readdir = true;
    TEST_ASSERT_FALSE(ops.recover_newest(ops.context, &found, &result));
    TEST_ASSERT_EQUAL_UINT(3, closed_dirs);
    fail_readdir = false;
    fail_truncate = true;
    TEST_ASSERT_FALSE(ops.recover_newest(ops.context, &found, &result));
    TEST_ASSERT_EQUAL_UINT(LOG_HEADER_V2_SIZE + LOG_RECORD_SIZE + 3, entries[1].length);
    fail_truncate = false; fail_sync = true;
    TEST_ASSERT_FALSE(ops.recover_newest(ops.context, &found, &result));
    TEST_ASSERT_EQUAL_UINT(LOG_HEADER_V2_SIZE + LOG_RECORD_SIZE, entries[1].length);
}

static void newest_v1_log_is_recovered(void)
{
    log_storage_t ops = sd_storage_interface();
    sd_storage_set_progress_hook(note_recovery_progress);
    add_log("0:/LOG_0004.BIN", true, 1, 0);
    add_log("0:/LOG_0005.BIN", false, 2, 9);
    bool found = false;
    log_recovery_result_t result;
    TEST_ASSERT_TRUE(ops.recover_newest(ops.context, &found, &result));
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL_UINT64(LOG_HEADER_SIZE + 2 * LOG_RECORD_SIZE + 9, result.original_size);
    TEST_ASSERT_EQUAL_UINT64(LOG_HEADER_SIZE + 2 * LOG_RECORD_SIZE, result.recovered_size);
    TEST_ASSERT_EQUAL_UINT(1, truncates);
    TEST_ASSERT_TRUE(recovery_progress >= 2);
}

static void list_and_dump_are_bounded_and_validate_names(void)
{
    add_log("0:/LOG_0001.BIN", true, 1, 0);
    add_log("0:/bad name.BIN", true, 0, 0);
    add_log("0:/DIRNAME", true, 0, 0);
    entries[2].directory = true;
    sd_list_entry_t entry;
    TEST_ASSERT_TRUE(sd_storage_list_open());
    TEST_ASSERT_FALSE(sd_storage_list_open());
    TEST_ASSERT_EQUAL_INT(1, sd_storage_list_next(&entry));
    TEST_ASSERT_EQUAL_STRING("LOG_0001.BIN", entry.name);
    TEST_ASSERT_EQUAL_UINT64(LOG_HEADER_V2_SIZE + LOG_RECORD_SIZE, entry.size);
    TEST_ASSERT_FALSE(entry.directory);
    TEST_ASSERT_EQUAL_INT(2, sd_storage_list_next(&entry));
    TEST_ASSERT_EQUAL_INT(1, sd_storage_list_next(&entry));
    TEST_ASSERT_TRUE(entry.directory);
    TEST_ASSERT_EQUAL_INT(0, sd_storage_list_next(&entry));
    TEST_ASSERT_TRUE(sd_storage_list_close());
    TEST_ASSERT_EQUAL_INT(-1, sd_storage_list_next(&entry));
    TEST_ASSERT_TRUE(sd_storage_list_open());
    fail_readdir = true;
    TEST_ASSERT_EQUAL_INT(-1, sd_storage_list_next(&entry));
    TEST_ASSERT_TRUE(sd_storage_list_close());
    fail_readdir = false;
    TEST_ASSERT_FALSE(sd_storage_dump_open("../LOG_0001.BIN"));
    TEST_ASSERT_TRUE(sd_storage_dump_open("LOG_0001.BIN"));
    TEST_ASSERT_FALSE(sd_storage_dump_open("LOG_0001.BIN"));
    uint8_t bytes[32];
    size_t count;
    TEST_ASSERT_FALSE(sd_storage_dump_read(bytes, 33, &count));
    TEST_ASSERT_TRUE(sd_storage_dump_read(bytes, sizeof(bytes), &count));
    TEST_ASSERT_EQUAL_UINT(sizeof(bytes), count);
    TEST_ASSERT_EQUAL_MEMORY(entries[0].data, bytes, sizeof(bytes));
    size_t offset = count;
    for (;;) {
        TEST_ASSERT_TRUE(sd_storage_dump_read(bytes, 24, &count));
        if (!count) break;
        TEST_ASSERT_EQUAL_MEMORY(entries[0].data + offset, bytes, count);
        offset += count;
    }
    TEST_ASSERT_EQUAL_UINT(entries[0].length, offset);
    TEST_ASSERT_TRUE(sd_storage_dump_close());
    TEST_ASSERT_FALSE(sd_storage_dump_read(bytes, sizeof(bytes), &count));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(adapter_checks_presence_and_publishes_synced_header);
    RUN_TEST(incomplete_header_is_never_published);
    RUN_TEST(existing_final_or_temporary_file_is_never_overwritten);
    RUN_TEST(recovers_highest_numbered_final_log_only);
    RUN_TEST(invalid_newest_or_io_error_preserves_data);
    RUN_TEST(newest_v1_log_is_recovered);
    RUN_TEST(list_and_dump_are_bounded_and_validate_names);
    return UNITY_END();
}
