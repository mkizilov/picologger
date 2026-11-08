#include "log_format.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>

static void put_le(uint8_t *out, uint64_t value, unsigned int bytes)
{
    for (unsigned int i = 0; i < bytes; ++i) {
        out[i] = (uint8_t)(value >> (i * 8));
    }
}

static uint64_t get_le(const uint8_t *input, unsigned int bytes)
{
    uint64_t value = 0;
    for (unsigned int i = 0; i < bytes; ++i) {
        value |= (uint64_t)input[i] << (i * 8);
    }
    return value;
}

static bool valid_header(const log_header_t *header)
{
    if (header->sample_rate_hz == 0 || header->channel_count == 0 ||
        header->channel_count > LOG_MAX_CHANNELS) return false;
    size_t i = 0;
    while (i < LOG_FIRMWARE_SIZE && header->firmware[i] != '\0') {
        unsigned char c = (unsigned char)header->firmware[i++];
        if (c < 32 || c > 126) return false;
    }
    if (i == 0 || i == LOG_FIRMWARE_SIZE) return false;
    for (unsigned int j = 0; j < header->channel_count; ++j) {
        for (unsigned int k = 0; k < j; ++k) {
            if (header->channel_ids[j] == header->channel_ids[k]) return false;
        }
    }
    return true;
}

uint16_t log_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = UINT16_C(0xffff);
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned int bit = 0; bit < 8; ++bit) {
            bool high_bit = (crc & 0x8000u) != 0;
            crc = (uint16_t)(crc << 1);
            if (high_bit) crc ^= UINT16_C(0x1021);
        }
    }
    return crc;
}

log_status_t log_encode_header(uint8_t *output, size_t length, const log_header_t *header)
{
    if (output == NULL || header == NULL) return LOG_INVALID_FORMAT;
    if (length < LOG_HEADER_SIZE) return LOG_SHORT_BUFFER;
    if (!valid_header(header)) return LOG_INVALID_FORMAT;
    memset(output, 0, LOG_HEADER_SIZE);
    memcpy(output, "PLOG", 4);
    put_le(output + 4, LOG_FORMAT_VERSION, 2);
    put_le(output + 6, LOG_HEADER_SIZE, 2);
    for (size_t i = 0; i < LOG_FIRMWARE_SIZE && header->firmware[i]; ++i) {
        output[8 + i] = (uint8_t)header->firmware[i];
    }
    put_le(output + 40, header->sample_rate_hz, 4);
    output[44] = header->channel_count;
    for (unsigned int i = 0; i < header->channel_count; ++i) {
        put_le(output + 48 + i * 2, header->channel_ids[i], 2);
    }
    put_le(output + 56, header->boot_counter, 4);
    return LOG_OK;
}

log_status_t log_decode_header(log_header_t *header, const uint8_t *input, size_t length)
{
    if (header == NULL || input == NULL) return LOG_INVALID_FORMAT;
    if (length < LOG_HEADER_SIZE) return LOG_SHORT_BUFFER;
    if (memcmp(input, "PLOG", 4) || get_le(input + 4, 2) != LOG_FORMAT_VERSION ||
        get_le(input + 6, 2) != LOG_HEADER_SIZE || input[45] || input[46] || input[47]) {
        return LOG_INVALID_FORMAT;
    }
    log_header_t decoded = {0};
    memcpy(decoded.firmware, input + 8, LOG_FIRMWARE_SIZE);
    decoded.sample_rate_hz = (uint32_t)get_le(input + 40, 4);
    decoded.channel_count = input[44];
    for (unsigned int i = 0; i < LOG_MAX_CHANNELS; ++i) {
        decoded.channel_ids[i] = (uint16_t)get_le(input + 48 + i * 2, 2);
    }
    decoded.boot_counter = (uint32_t)get_le(input + 56, 4);
    if (!valid_header(&decoded)) return LOG_INVALID_FORMAT;
    size_t end = strlen(decoded.firmware);
    for (size_t i = end + 1; i < LOG_FIRMWARE_SIZE; ++i) {
        if (decoded.firmware[i] != '\0') return LOG_INVALID_FORMAT;
    }
    for (unsigned int i = decoded.channel_count; i < LOG_MAX_CHANNELS; ++i) {
        if (decoded.channel_ids[i] != 0) return LOG_INVALID_FORMAT;
    }
    *header = decoded;
    return LOG_OK;
}

log_status_t log_encode_record(uint8_t *output, size_t length, const log_record_t *record)
{
    if (output == NULL || record == NULL) return LOG_INVALID_FORMAT;
    if (length < LOG_RECORD_SIZE) return LOG_SHORT_BUFFER;
    put_le(output, record->sequence, 4);
    put_le(output + 4, record->timestamp_us, 8);
    for (unsigned int i = 0; i < LOG_MAX_CHANNELS; ++i) {
        put_le(output + 12 + i * 4, (uint32_t)record->values[i], 4);
    }
    put_le(output + 28, record->flags, 2);
    put_le(output + 30, log_crc16(output, 30), 2);
    return LOG_OK;
}

log_status_t log_decode_record(log_record_t *record, const uint8_t *input, size_t length)
{
    if (record == NULL || input == NULL) return LOG_INVALID_FORMAT;
    if (length < LOG_RECORD_SIZE) return LOG_SHORT_BUFFER;
    if (get_le(input + 30, 2) != log_crc16(input, 30)) return LOG_CRC_MISMATCH;
    log_record_t decoded;
    decoded.sequence = (uint32_t)get_le(input, 4);
    decoded.timestamp_us = get_le(input + 4, 8);
    for (unsigned int i = 0; i < LOG_MAX_CHANNELS; ++i) {
        uint32_t value = (uint32_t)get_le(input + 12 + i * 4, 4);
        decoded.values[i] = value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
    }
    decoded.flags = (uint16_t)get_le(input + 28, 2);
    *record = decoded;
    return LOG_OK;
}
