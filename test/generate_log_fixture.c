#include "log_format.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    FILE *file = fopen(argv[1], "wb");
    if (file == NULL) return 1;
    log_header_t header = {.firmware = "fixture-v1", .sample_rate_hz = 1000,
                           .channel_count = 2, .channel_ids = {4, 26}, .boot_counter = 42};
    uint8_t bytes[LOG_HEADER_SIZE];
    if (log_encode_header(bytes, sizeof(bytes), &header) != LOG_OK ||
        fwrite(bytes, 1, LOG_HEADER_SIZE, file) != LOG_HEADER_SIZE) {
        fclose(file);
        return 1;
    }
    const uint32_t sequences[] = {0, 1, 3, 4};
    for (unsigned int i = 0; i < 4; ++i) {
        log_record_t record = {.sequence = sequences[i], .timestamp_us = 1000000u + 1000u * i,
                               .values = {(int32_t)i, -(int32_t)i, 0, 0}, .flags = (uint16_t)i};
        if (log_encode_record(bytes, sizeof(bytes), &record) != LOG_OK) {
            fclose(file);
            return 1;
        }
        if (i == 1) bytes[12] ^= 0x80;
        if (fwrite(bytes, 1, LOG_RECORD_SIZE, file) != LOG_RECORD_SIZE) {
            fclose(file);
            return 1;
        }
    }
    return fclose(file) == 0 ? 0 : 1;
}
