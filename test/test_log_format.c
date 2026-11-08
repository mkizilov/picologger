#include "unity.h"
#include "log_format.h"

#include <limits.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void crc_vectors(void)
{
    const uint8_t input[] = "123456789";
    TEST_ASSERT_EQUAL_HEX16(0x29b1, log_crc16(input, 9));
    TEST_ASSERT_EQUAL_HEX16(0xffff, log_crc16(input, 0));
}

static void header_round_trip_and_validation(void)
{
    log_header_t in = {.firmware = "v1.2.3-abc", .sample_rate_hz = 1000,
                       .channel_count = 2, .channel_ids = {7, 513}, .boot_counter = 0x12345678};
    uint8_t bytes[LOG_HEADER_SIZE];
    log_header_t out;
    TEST_ASSERT_EQUAL_INT(LOG_INVALID_FORMAT, log_encode_header(NULL, sizeof(bytes), &in));
    TEST_ASSERT_EQUAL_INT(LOG_SHORT_BUFFER, log_encode_header(bytes, sizeof(bytes) - 1, &in));
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_encode_header(bytes, sizeof(bytes), &in));
    TEST_ASSERT_EQUAL_MEMORY("PLOG\1\0\x3c\0", bytes, 8);
    TEST_ASSERT_EQUAL_HEX8(0xe8, bytes[40]);
    TEST_ASSERT_EQUAL_HEX8(0x03, bytes[41]);
    TEST_ASSERT_EQUAL_HEX8(0x01, bytes[50]);
    TEST_ASSERT_EQUAL_HEX8(0x02, bytes[51]);
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_header(&out, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL_STRING(in.firmware, out.firmware);
    TEST_ASSERT_EQUAL_UINT32(in.sample_rate_hz, out.sample_rate_hz);
    TEST_ASSERT_EQUAL_UINT8(in.channel_count, out.channel_count);
    TEST_ASSERT_EQUAL_UINT16(in.channel_ids[1], out.channel_ids[1]);
    TEST_ASSERT_EQUAL_UINT32(in.boot_counter, out.boot_counter);
    TEST_ASSERT_EQUAL_INT(LOG_SHORT_BUFFER, log_decode_header(&out, bytes, sizeof(bytes) - 1));
    bytes[45] = 1;
    TEST_ASSERT_EQUAL_INT(LOG_INVALID_FORMAT, log_decode_header(&out, bytes, sizeof(bytes)));
    bytes[45] = 0;
    bytes[55] = 1;
    TEST_ASSERT_EQUAL_INT(LOG_INVALID_FORMAT, log_decode_header(&out, bytes, sizeof(bytes)));
    bytes[55] = 0;
    bytes[20] = 1;
    TEST_ASSERT_EQUAL_INT(LOG_INVALID_FORMAT, log_decode_header(&out, bytes, sizeof(bytes)));
    in.channel_ids[1] = in.channel_ids[0];
    TEST_ASSERT_EQUAL_INT(LOG_INVALID_FORMAT, log_encode_header(bytes, sizeof(bytes), &in));
}

static void record_round_trip_and_corruption(void)
{
    log_record_t in = {.sequence = 0x12345678, .timestamp_us = UINT64_C(0x0123456789abcdef),
                       .values = {INT32_MIN, INT32_MAX, -1, 12345}, .flags = 0xa1b2};
    uint8_t bytes[LOG_RECORD_SIZE];
    log_record_t out;
    TEST_ASSERT_EQUAL_INT(LOG_INVALID_FORMAT, log_encode_record(bytes, sizeof(bytes), NULL));
    TEST_ASSERT_EQUAL_INT(LOG_SHORT_BUFFER, log_encode_record(bytes, sizeof(bytes) - 1, &in));
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_encode_record(bytes, sizeof(bytes), &in));
    TEST_ASSERT_EQUAL_HEX8(0x78, bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x12, bytes[3]);
    TEST_ASSERT_EQUAL_HEX8(0xef, bytes[4]);
    TEST_ASSERT_EQUAL_HEX8(0x01, bytes[11]);
    TEST_ASSERT_EQUAL_HEX8(0xb2, bytes[28]);
    TEST_ASSERT_EQUAL_HEX8(0xa1, bytes[29]);
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_record(&out, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL_UINT32(in.sequence, out.sequence);
    TEST_ASSERT_EQUAL_UINT64(in.timestamp_us, out.timestamp_us);
    TEST_ASSERT_EQUAL_INT32_ARRAY(in.values, out.values, LOG_MAX_CHANNELS);
    TEST_ASSERT_EQUAL_HEX16(in.flags, out.flags);
    TEST_ASSERT_EQUAL_INT(LOG_SHORT_BUFFER, log_decode_record(&out, bytes, sizeof(bytes) - 1));
    bytes[15] ^= 1;
    out.sequence = 99;
    TEST_ASSERT_EQUAL_INT(LOG_CRC_MISMATCH, log_decode_record(&out, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL_UINT32(99, out.sequence);
    bytes[15] ^= 1;
    bytes[31] ^= 1;
    TEST_ASSERT_EQUAL_INT(LOG_CRC_MISMATCH, log_decode_record(&out, bytes, sizeof(bytes)));
}

static void v2_header_crc_and_reserved_bytes(void)
{
    const log_header_t input = {.firmware = "v2-test", .sample_rate_hz = 50,
                                .channel_count = 1, .channel_ids = {1}};
    uint8_t bytes[LOG_HEADER_V2_SIZE];
    log_header_t output;
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_encode_header_v2(bytes, sizeof(bytes), &input));
    TEST_ASSERT_EQUAL_INT(LOG_SHORT_BUFFER, log_decode_header(&output, bytes, LOG_HEADER_SIZE));
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_header(&output, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL_STRING(input.firmware, output.firmware);
    bytes[100] ^= 1;
    TEST_ASSERT_EQUAL_INT(LOG_CRC_MISMATCH, log_decode_header(&output, bytes, sizeof(bytes)));
    uint16_t crc = log_crc16(bytes, LOG_HEADER_V2_SIZE - 2);
    bytes[LOG_HEADER_V2_SIZE - 2] = (uint8_t)crc;
    bytes[LOG_HEADER_V2_SIZE - 1] = (uint8_t)(crc >> 8);
    TEST_ASSERT_EQUAL_INT(LOG_INVALID_FORMAT, log_decode_header(&output, bytes, sizeof(bytes)));
}

static void v3_header_round_trips_reset_and_fault_details(void)
{
    log_header_t input = {.firmware = "v3-test", .sample_rate_hz = 50, .channel_count = 1,
                          .channel_ids = {1}, .boot_counter = 7, .reset_reason = BOOT_RESET_HARDFAULT,
                          .watchdog_reason = 2, .fault_mask = 2, .watchdog_missing = 5};
    input.fault[1] = (fault_detail_t){FAULT_STACK_VALID, 0x10001234, 0x10005678, 0x21000000,
                                     0x20041000, 0xfffffff9, 3, 0x8000};
    input.fault_boot_id[1] = 6;
    uint8_t bytes[LOG_HEADER_V2_SIZE];
    log_header_t output;
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_encode_header_v3(bytes, sizeof(bytes), &input));
    TEST_ASSERT_EQUAL_INT(LOG_OK, log_decode_header(&output, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL_UINT32(7, output.boot_counter);
    TEST_ASSERT_EQUAL_UINT32(BOOT_RESET_HARDFAULT, output.reset_reason);
    TEST_ASSERT_EQUAL_UINT32(2, output.watchdog_reason);
    TEST_ASSERT_EQUAL_UINT32(2, output.fault_mask);
    TEST_ASSERT_EQUAL_UINT32(5, output.watchdog_missing);
    TEST_ASSERT_EQUAL_UINT32(6, output.fault_boot_id[1]);
    TEST_ASSERT_EQUAL_UINT32(0x10001234, output.fault[1].pc);
    TEST_ASSERT_EQUAL_UINT32(0x10005678, output.fault[1].lr);
    bytes[108] ^= 1;
    TEST_ASSERT_EQUAL_INT(LOG_CRC_MISMATCH, log_decode_header(&output, bytes, sizeof(bytes)));
    input.fault[1].flags = 2;
    TEST_ASSERT_EQUAL_INT(LOG_INVALID_FORMAT, log_encode_header_v3(bytes, sizeof(bytes), &input));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(crc_vectors);
    RUN_TEST(header_round_trip_and_validation);
    RUN_TEST(record_round_trip_and_corruption);
    RUN_TEST(v2_header_crc_and_reserved_bytes);
    RUN_TEST(v3_header_round_trips_reset_and_fault_details);
    return UNITY_END();
}
