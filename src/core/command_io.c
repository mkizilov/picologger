#include "command_io.h"

#include <string.h>

bool command_output_write(command_output_t *output, const char *data, size_t length)
{
    if (length > COMMAND_OUTPUT_CAPACITY - output->count) { ++output->dropped; return false; }
    size_t first = COMMAND_OUTPUT_CAPACITY - output->head;
    if (first > length) first = length;
    memcpy(output->bytes + output->head, data, first);
    memcpy(output->bytes, data + first, length - first);
    output->head = (output->head + length) % COMMAND_OUTPUT_CAPACITY;
    output->count += length;
    return true;
}

size_t command_output_peek(const command_output_t *output, const char **data)
{
    *data = output->bytes + output->tail;
    size_t length = COMMAND_OUTPUT_CAPACITY - output->tail;
    return length < output->count ? length : output->count;
}

size_t command_output_frame(const command_output_t *output, char *frame, size_t capacity)
{
    size_t limit = output->count < capacity ? output->count : capacity;
    for (size_t i = 0; i < limit; ++i) {
        frame[i] = output->bytes[(output->tail + i) % COMMAND_OUTPUT_CAPACITY];
        if (i && frame[i] == '\n') return i + 1;
    }
    return 0;
}

void command_output_consume(command_output_t *output, size_t length)
{
    if (length > output->count) length = output->count;
    output->tail = (output->tail + length) % COMMAND_OUTPUT_CAPACITY;
    output->count -= length;
}

void command_output_reset(command_output_t *output)
{
    output->head = output->tail = output->count = 0;
}

size_t command_base64(const uint8_t *input, size_t length, char *output, size_t capacity)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (length > (SIZE_MAX - 2u) / 3u) return 0;
    size_t groups = (length + 2u) / 3u;
    if (groups > SIZE_MAX / 4u) return 0;
    size_t required = groups * 4u;
    if (required == SIZE_MAX || capacity <= required) return 0;
    size_t out = 0;
    for (size_t i = 0; i < length; i += 3) {
        uint32_t value = (uint32_t)input[i] << 16;
        if (i + 1 < length) value |= (uint32_t)input[i + 1] << 8;
        if (i + 2 < length) value |= input[i + 2];
        output[out++] = table[(value >> 18) & 63u];
        output[out++] = table[(value >> 12) & 63u];
        output[out++] = i + 1 < length ? table[(value >> 6) & 63u] : '=';
        output[out++] = i + 2 < length ? table[value & 63u] : '=';
    }
    output[out] = '\0';
    return out;
}

uint32_t command_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1u));
    }
    return crc;
}
