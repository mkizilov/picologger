#ifndef PICOLOGGER_COMMAND_IO_H
#define PICOLOGGER_COMMAND_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COMMAND_OUTPUT_CAPACITY 4096u

typedef struct {
    char bytes[COMMAND_OUTPUT_CAPACITY];
    size_t head, tail, count;
    uint32_t dropped;
} command_output_t;

/* A line is accepted atomically or dropped entirely; no allocation or wait. */
bool command_output_write(command_output_t *output, const char *data, size_t length);
size_t command_output_peek(const command_output_t *output, const char **data);
/* Frames start with a separator newline and end with a second newline. */
size_t command_output_frame(const command_output_t *output, char *frame, size_t capacity);
void command_output_consume(command_output_t *output, size_t length);
void command_output_reset(command_output_t *output);
size_t command_base64(const uint8_t *input, size_t length, char *output, size_t capacity);
uint32_t command_crc32_update(uint32_t crc, const uint8_t *data, size_t length);

#endif
