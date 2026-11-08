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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(crc_vectors);
    RUN_TEST(header_round_trip_and_validation);
    RUN_TEST(record_round_trip_and_corruption);
    return UNITY_END();
}
