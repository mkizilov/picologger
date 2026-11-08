#ifndef PICOLOGGER_COMMAND_H
#define PICOLOGGER_COMMAND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COMMAND_LINE_MAX 80u
#define COMMAND_FILENAME_MAX 32u

typedef enum {
    COMMAND_STATUS, COMMAND_START, COMMAND_STOP, COMMAND_RATE, COMMAND_STREAM,
    COMMAND_LS, COMMAND_DUMP, COMMAND_REBOOT, COMMAND_HELP, COMMAND_COUNT
} command_kind_t;

typedef struct {
    command_kind_t kind;
    uint32_t rate_hz;
    bool enabled;
    char filename[COMMAND_FILENAME_MAX + 1];
} command_request_t;

typedef enum {
    COMMAND_MORE, COMMAND_READY, COMMAND_UNKNOWN, COMMAND_BAD_ARGUMENT,
    COMMAND_TOO_LONG
} command_parse_result_t;

typedef struct {
    char line[COMMAND_LINE_MAX + 1];
    size_t length;
    bool too_long, invalid, skip_lf;
} command_parser_t;

typedef bool (*command_handler_t)(void *context, const command_request_t *request);
typedef struct { command_handler_t handler[COMMAND_COUNT]; } command_dispatch_t;

void command_parser_init(command_parser_t *parser);
/* Feed one byte. Errors are reported once at the end of the line. */
command_parse_result_t command_feed(command_parser_t *parser, uint8_t byte, command_request_t *request);
/* The indexed handler table is application-owned; false means unavailable or rejected. */
bool command_dispatch(const command_dispatch_t *dispatch, void *context, const command_request_t *request);
bool command_filename_valid(const char *name);

#endif
