#include "unity.h"
#include "log_writer.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t data[3][9000];
    size_t size[3];
    char names[3][20];
    int current, files;
    bool media, fail_write, short_write, fail_sync, all_exist, fail_recovery, recovery_found;
    unsigned int mounts, syncs, closes, writes, unmounts, recoveries, presence_checks;
    size_t write_lengths[8];
    uint64_t now, write_delay, sync_delay;
} fake_fs_t;

static fake_fs_t fs;
static log_writer_t writer;
static uint8_t records[LOG_RECORDS_PER_BATCH * LOG_RECORD_SIZE];

static bool fs_mount(void *ctx) { fake_fs_t *f = ctx; ++f->mounts; return f->media; }
static bool fs_recover(void *ctx, bool *found, log_recovery_result_t *result)
{
    fake_fs_t *f = ctx;
    ++f->recoveries;
    *found = f->recovery_found;
    if (*found) *result = (log_recovery_result_t){1000, 993};
    return !f->fail_recovery;
}
static int fs_create(void *ctx, const char *name, const uint8_t *header, size_t size,
                     uint64_t *write_us, uint64_t *sync_us)
{
    fake_fs_t *f = ctx;
    if (f->all_exist) return 1;
    if (!strcmp(name, "0:/LOG_0001.BIN")) return 1;
    for (int i = 0; i < f->files; ++i) if (!strcmp(f->names[i], name)) return 1;
    if (!f->media || f->files == 3) return -1;
    f->current = f->files++;
    (void)snprintf(f->names[f->current], sizeof(f->names[0]), "%s", name);
    TEST_ASSERT_EQUAL_UINT(LOG_HEADER_V2_SIZE, size);
    memcpy(f->data[f->current], header, size);
    f->size[f->current] = size;
    *write_us = f->write_delay;
    *sync_us = f->sync_delay;
    f->now += *write_us + *sync_us;
    return 0;
}
static bool fs_write(void *ctx, const uint8_t *data, size_t length, size_t *written)
{
    fake_fs_t *f = ctx;
    TEST_ASSERT_EQUAL_UINT(0, (uintptr_t)data % 512u);
    TEST_ASSERT_TRUE(f->writes < 8);
    f->write_lengths[f->writes++] = length;
    f->now += f->write_delay;
    if (!f->media || f->fail_write) { *written = 0; return false; }
    *written = f->short_write ? length - 1 : length;
    TEST_ASSERT_TRUE(f->size[f->current] + *written <= sizeof(f->data[0]));
    memcpy(f->data[f->current] + f->size[f->current], data, *written);
    f->size[f->current] += *written;
    return true;
}
static bool fs_sync(void *ctx) { fake_fs_t *f = ctx; ++f->syncs; f->now += f->sync_delay; return f->media && !f->fail_sync; }
static bool fs_close(void *ctx) { fake_fs_t *f = ctx; ++f->closes; return f->media; }
static void fs_unmount(void *ctx) { fake_fs_t *f = ctx; ++f->unmounts; }
static bool fs_present(void *ctx) { fake_fs_t *f = ctx; ++f->presence_checks; return f->media; }
static uint64_t fs_now(void *ctx) { return ((fake_fs_t *)ctx)->now; }

void setUp(void)
{
    memset(&fs, 0, sizeof(fs));
    fs.media = true;
    fs.write_delay = 37000;
    fs.sync_delay = 12000;
    const log_header_t header = {.firmware = "test", .sample_rate_hz = 50,
        .channel_count = 3, .channel_ids = {1, 2, 3}, .boot_counter = 0};
    const log_storage_t ops = {&fs, fs_mount, fs_recover, fs_create, fs_write, fs_sync, fs_close, fs_unmount, fs_present, fs_now};
    TEST_ASSERT_TRUE(log_writer_init(&writer, &ops, &header, 8192));
    for (unsigned int i = 0; i < LOG_RECORDS_PER_BATCH; ++i) {
        log_record_t record = {.sequence = i, .timestamp_us = i * 20000u, .values = {(int32_t)i, 100000, 50, 0}};
        TEST_ASSERT_EQUAL_INT(LOG_OK, log_encode_record(records + i * LOG_RECORD_SIZE, LOG_RECORD_SIZE, &record));
    }
}
void tearDown(void) {}

static void aligned_chunks_rotate_without_overwriting(void)
{
    log_writer_maintain(&writer);
    TEST_ASSERT_TRUE(writer.online);
    TEST_ASSERT_EQUAL_STRING("0:/LOG_0002.BIN", fs.names[0]);
    log_writer_append(&writer, records, 128);
    log_writer_append(&writer, records, 128);
    TEST_ASSERT_EQUAL_UINT(2, fs.files);
    TEST_ASSERT_EQUAL_UINT64(4, writer.write_operations);
    TEST_ASSERT_EQUAL_UINT64(256, writer.accepted_records);
    TEST_ASSERT_EQUAL_STRING("0:/LOG_0003.BIN", fs.names[1]);
    TEST_ASSERT_EQUAL_UINT(2, fs.writes);
    TEST_ASSERT_EQUAL_UINT(LOG_WRITE_CHUNK, fs.write_lengths[0]);
    TEST_ASSERT_EQUAL_UINT(3584, fs.write_lengths[1]);
    TEST_ASSERT_EQUAL_UINT(8192, fs.size[0]);
    TEST_ASSERT_EQUAL_UINT(1, fs.syncs);
    TEST_ASSERT_EQUAL_UINT(1, fs.closes);
    TEST_ASSERT_EQUAL_UINT64(37000, writer.worst_write_us);
    TEST_ASSERT_EQUAL_UINT64(12000, writer.worst_sync_us);
    log_header_t header;
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_header(&header, fs.data[0], fs.size[0]));
    TEST_ASSERT_EQUAL_UINT32(50, header.sample_rate_hz);
    for (unsigned int i = 0; i < 240; ++i) {
        log_record_t record;
        TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_record(&record, fs.data[0] + LOG_HEADER_V2_SIZE + i * LOG_RECORD_SIZE,
                                                       LOG_RECORD_SIZE));
        TEST_ASSERT_EQUAL_UINT32(i % 128, record.sequence);
    }
    fs.now += LOG_SYNC_US;
    log_writer_maintain(&writer);
    TEST_ASSERT_EQUAL_UINT(2, fs.syncs);
    TEST_ASSERT_EQUAL_UINT(2, fs.writes);
    TEST_ASSERT_EQUAL_UINT64(0, writer.dropped);
    TEST_ASSERT_EQUAL_UINT64(0, writer.uncertain);
}

static void missing_card_retries_and_counts_drops(void)
{
    fs.media = false;
    log_writer_maintain(&writer);
    TEST_ASSERT_FALSE(writer.online);
    TEST_ASSERT_EQUAL_UINT(1, fs.mounts);
    log_writer_append(&writer, records, 3);
    TEST_ASSERT_EQUAL_UINT64(3, writer.dropped);
    fs.media = true;
    fs.now = LOG_RETRY_US - 1;
    log_writer_maintain(&writer);
    TEST_ASSERT_EQUAL_UINT(1, fs.mounts);
    fs.now = LOG_RETRY_US;
    log_writer_maintain(&writer);
    TEST_ASSERT_TRUE(writer.online);
    TEST_ASSERT_EQUAL_UINT(2, fs.mounts);
}

static void partial_write_is_uncertain_and_remaining_dropped(void)
{
    log_writer_maintain(&writer);
    fs.short_write = true;
    log_writer_append(&writer, records, LOG_RECORDS_PER_BATCH);
    TEST_ASSERT_FALSE(writer.online);
    TEST_ASSERT_EQUAL_UINT64(128, writer.uncertain);
    TEST_ASSERT_EQUAL_UINT64(0, writer.dropped);
    TEST_ASSERT_EQUAL_UINT64(37000, writer.worst_write_us);
    TEST_ASSERT_EQUAL_UINT(1, fs.unmounts);
}

static void removal_counts_only_records_not_proven_synced(void)
{
    log_writer_maintain(&writer);
    log_writer_append(&writer, records, LOG_RECORDS_PER_BATCH);
    log_writer_append(&writer, records, 2);
    fs.now += LOG_SYNC_US;
    log_writer_maintain(&writer);
    TEST_ASSERT_EQUAL_UINT(128, writer.synced_records);
    fs.media = false;
    fs.now += LOG_PRESENCE_US;
    log_writer_maintain(&writer);
    TEST_ASSERT_FALSE(writer.online);
    TEST_ASSERT_EQUAL_UINT64(2, writer.uncertain);
    TEST_ASSERT_EQUAL_UINT64(0, writer.dropped);
}

static void sync_failure_marks_all_unsynced_records_uncertain(void)
{
    log_writer_maintain(&writer);
    log_writer_append(&writer, records, LOG_RECORDS_PER_BATCH);
    fs.fail_sync = true;
    fs.now += LOG_SYNC_US;
    log_writer_maintain(&writer);
    TEST_ASSERT_FALSE(writer.online);
    TEST_ASSERT_EQUAL_UINT64(128, writer.uncertain);
}

static void rotation_tail_failure_preserves_prior_sync_boundary(void)
{
    log_writer_maintain(&writer);
    log_writer_append(&writer, records, LOG_RECORDS_PER_BATCH);
    fs.now += LOG_SYNC_US;
    log_writer_maintain(&writer);
    TEST_ASSERT_EQUAL_UINT(128, writer.synced_records);
    fs.short_write = true;
    log_writer_append(&writer, records, LOG_RECORDS_PER_BATCH);
    TEST_ASSERT_FALSE(writer.online);
    TEST_ASSERT_EQUAL_UINT64(112, writer.uncertain);
    TEST_ASSERT_EQUAL_UINT64(16, writer.dropped);
}

static void exhausted_names_do_not_overwrite(void)
{
    fs.all_exist = true;
    log_writer_maintain(&writer);
    TEST_ASSERT_FALSE(writer.online);
    TEST_ASSERT_EQUAL_UINT(0, fs.files);
    TEST_ASSERT_EQUAL_UINT(1, fs.unmounts);
    log_writer_append(&writer, records, 2);
    TEST_ASSERT_EQUAL_UINT64(2, writer.dropped);
}

static void rejects_invalid_writer_configuration(void)
{
    static log_writer_t other;
    const log_header_t header = {.firmware = "test", .sample_rate_hz = 50,
        .channel_count = 1, .channel_ids = {1}};
    TEST_ASSERT_FALSE(log_writer_init(&other, &writer.storage, &header, 8191));
    log_storage_t incomplete = writer.storage;
    incomplete.write = NULL;
    TEST_ASSERT_FALSE(log_writer_init(&other, &incomplete, &header, 8192));
    log_header_t invalid = header;
    invalid.sample_rate_hz = 0;
    TEST_ASSERT_FALSE(log_writer_init(&other, &writer.storage, &invalid, 8192));
}

static void recovery_failure_blocks_new_file_without_overwriting(void)
{
    fs.fail_recovery = true;
    log_writer_maintain(&writer);
    TEST_ASSERT_FALSE(writer.online);
    TEST_ASSERT_EQUAL_UINT(1, fs.recoveries);
    TEST_ASSERT_EQUAL_UINT(0, fs.files);
    TEST_ASSERT_EQUAL_UINT64(1, writer.recovery_errors);
    log_writer_append(&writer, records, 2);
    TEST_ASSERT_EQUAL_UINT64(2, writer.dropped);
}

static void successful_recovery_precedes_new_file(void)
{
    fs.recovery_found = true;
    log_writer_maintain(&writer);
    TEST_ASSERT_TRUE(writer.online);
    TEST_ASSERT_EQUAL_UINT(1, fs.recoveries);
    TEST_ASSERT_EQUAL_UINT64(7, writer.recovered_bytes);
    TEST_ASSERT_EQUAL_STRING("0:/LOG_0002.BIN", fs.names[0]);
}

static void pause_flushes_and_resume_publishes_new_rate(void)
{
    log_header_t boot = {.firmware = "test", .sample_rate_hz = 50, .channel_count = 3,
                         .channel_ids = {1, 2, 3}, .boot_counter = 7, .reset_reason = BOOT_RESET_HARDFAULT,
                         .fault_mask = 1};
    boot.fault[0] = (fault_detail_t){.flags = FAULT_STACK_VALID, .pc = 0x10001234, .lr = 0x10005678};
    boot.fault_boot_id[0] = 6;
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_encode_header_v3(writer.header, sizeof(writer.header), &boot));
    log_writer_maintain(&writer);
    log_writer_append(&writer, records, 3);
    TEST_ASSERT_TRUE(log_writer_pause(&writer));
    TEST_ASSERT_FALSE(writer.enabled);
    TEST_ASSERT_FALSE(writer.file_open);
    TEST_ASSERT_EQUAL_UINT(1, fs.closes);
    TEST_ASSERT_EQUAL_UINT(1, fs.syncs);
    TEST_ASSERT_EQUAL_UINT(3 * LOG_RECORD_SIZE, fs.write_lengths[0]);
    TEST_ASSERT_EQUAL_UINT(LOG_HEADER_V2_SIZE + 3 * LOG_RECORD_SIZE, fs.size[0]);
    log_writer_append(&writer, records, 1);
    TEST_ASSERT_EQUAL_UINT64(1, writer.dropped);
    TEST_ASSERT_FALSE(log_writer_resume(&writer, 51));
    TEST_ASSERT_TRUE(log_writer_resume(&writer, 25));
    TEST_ASSERT_TRUE(writer.enabled);
    TEST_ASSERT_TRUE(writer.file_open);
    TEST_ASSERT_EQUAL_UINT(3, writer.current_number);
    log_header_t header;
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_header(&header, fs.data[1], fs.size[1]));
    TEST_ASSERT_EQUAL_UINT32(25, header.sample_rate_hz);
    TEST_ASSERT_EQUAL_UINT32(7, header.boot_counter);
    TEST_ASSERT_EQUAL_UINT32(BOOT_RESET_HARDFAULT, header.reset_reason);
    TEST_ASSERT_EQUAL_UINT32(0x10001234, header.fault[0].pc);
}

static void failed_pause_reports_uncertain_and_can_resume_offline(void)
{
    log_writer_maintain(&writer);
    log_writer_append(&writer, records, 3);
    fs.fail_sync = true;
    TEST_ASSERT_FALSE(log_writer_pause(&writer));
    TEST_ASSERT_FALSE(writer.enabled);
    TEST_ASSERT_FALSE(writer.online);
    TEST_ASSERT_EQUAL_UINT64(3, writer.uncertain);
    TEST_ASSERT_TRUE(log_writer_resume(&writer, 20));
    TEST_ASSERT_TRUE(writer.enabled);
    TEST_ASSERT_FALSE(writer.online);
    fs.fail_sync = false;
    fs.now += LOG_RETRY_US;
    log_writer_maintain(&writer);
    TEST_ASSERT_TRUE(writer.online);
    log_header_t header;
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_header(&header, fs.data[1], fs.size[1]));
    TEST_ASSERT_EQUAL_UINT32(20, header.sample_rate_hz);
}

static void low_rate_batches_and_reduces_sync_and_presence_checks(void)
{
    const log_header_t header = {.firmware = "test", .sample_rate_hz = 10,
        .channel_count = 1, .channel_ids = {1}};
    const log_storage_t ops = {&fs, fs_mount, fs_recover, fs_create, fs_write, fs_sync, fs_close, fs_unmount, fs_present, fs_now};
    TEST_ASSERT_TRUE(log_writer_init(&writer, &ops, &header, 8192));
    log_writer_maintain(&writer);
    TEST_ASSERT_EQUAL_UINT64(LOG_SYNC_LOW_US, writer.sync_interval);
    TEST_ASSERT_EQUAL_UINT64(LOG_SYNC_LOW_US, writer.presence_interval);
    log_writer_append(&writer, records, 4);
    fs.now = writer.next_sync - 1;
    log_writer_maintain(&writer);
    TEST_ASSERT_EQUAL_UINT(0, fs.syncs);
    TEST_ASSERT_EQUAL_UINT(0, fs.presence_checks);
    TEST_ASSERT_EQUAL_UINT(0, fs.writes);
    ++fs.now;
    log_writer_maintain(&writer);
    TEST_ASSERT_EQUAL_UINT(1, fs.syncs);
    TEST_ASSERT_EQUAL_UINT(1, fs.presence_checks);
    TEST_ASSERT_EQUAL_UINT(0, fs.writes); /* Partial 4 KiB chunk remains in RAM. */
    TEST_ASSERT_TRUE(log_writer_pause(&writer));
    TEST_ASSERT_EQUAL_UINT(1, fs.writes);
    TEST_ASSERT_TRUE(log_writer_resume(&writer, 11));
    TEST_ASSERT_EQUAL_UINT64(LOG_SYNC_US, writer.sync_interval);
    TEST_ASSERT_EQUAL_UINT64(LOG_PRESENCE_US, writer.presence_interval);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(aligned_chunks_rotate_without_overwriting);
    RUN_TEST(missing_card_retries_and_counts_drops);
    RUN_TEST(partial_write_is_uncertain_and_remaining_dropped);
    RUN_TEST(removal_counts_only_records_not_proven_synced);
    RUN_TEST(sync_failure_marks_all_unsynced_records_uncertain);
    RUN_TEST(rotation_tail_failure_preserves_prior_sync_boundary);
    RUN_TEST(exhausted_names_do_not_overwrite);
    RUN_TEST(rejects_invalid_writer_configuration);
    RUN_TEST(recovery_failure_blocks_new_file_without_overwriting);
    RUN_TEST(successful_recovery_precedes_new_file);
    RUN_TEST(pause_flushes_and_resume_publishes_new_rate);
    RUN_TEST(failed_pause_reports_uncertain_and_can_resume_offline);
    RUN_TEST(low_rate_batches_and_reduces_sync_and_presence_checks);
    return UNITY_END();
}
