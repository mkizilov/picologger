#ifndef PICOLOGGER_LOG_FORMAT_H
#define PICOLOGGER_LOG_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#define LOG_FORMAT_VERSION 1u
#define LOG_HEADER_SIZE 60u
#define LOG_FORMAT_VERSION_V2 2u
#define LOG_HEADER_V2_SIZE 512u
#define LOG_RECORD_SIZE 32u
#define LOG_MAX_CHANNELS 4u
#define LOG_FIRMWARE_SIZE 32u

typedef enum {
    LOG_OK = 0,
    LOG_SHORT_BUFFER,
    LOG_INVALID_FORMAT,
    LOG_CRC_MISMATCH
} log_status_t;

typedef struct {
    char firmware[LOG_FIRMWARE_SIZE];
    uint32_t sample_rate_hz;
    uint8_t channel_count;
    uint16_t channel_ids[LOG_MAX_CHANNELS];
    uint32_t boot_counter;
} log_header_t;

typedef struct {
    uint32_t sequence;
    uint64_t timestamp_us;
    int32_t values[LOG_MAX_CHANNELS];
    uint16_t flags;
} log_record_t;

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xffff, no reflection or xorout. */
uint16_t log_crc16(const uint8_t *data, size_t length);
log_status_t log_encode_header(uint8_t *output, size_t length, const log_header_t *header);
/* V2 reserves a whole sector and protects the header with CRC-16. */
log_status_t log_encode_header_v2(uint8_t *output, size_t length, const log_header_t *header);
/* Decodes either v1 (60 bytes) or v2 (512 bytes). */
log_status_t log_decode_header(log_header_t *header, const uint8_t *input, size_t length);
log_status_t log_encode_record(uint8_t *output, size_t length, const log_record_t *record);
/* On a CRC or validation failure, the output object is unchanged. */
log_status_t log_decode_record(log_record_t *record, const uint8_t *input, size_t length);

#endif
