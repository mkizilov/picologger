#include "command.h"

#include <string.h>

typedef enum { ARG_NONE, ARG_RATE, ARG_SWITCH, ARG_FILE } argument_t;
typedef struct { const char *name; command_kind_t kind; argument_t argument; } command_entry_t;

static const command_entry_t commands[] = {
    {"status", COMMAND_STATUS, ARG_NONE}, {"start", COMMAND_START, ARG_NONE},
    {"stop", COMMAND_STOP, ARG_NONE}, {"rate", COMMAND_RATE, ARG_RATE},
    {"stream", COMMAND_STREAM, ARG_SWITCH}, {"ls", COMMAND_LS, ARG_NONE},
    {"dump", COMMAND_DUMP, ARG_FILE}, {"reboot", COMMAND_REBOOT, ARG_NONE},
    {"help", COMMAND_HELP, ARG_NONE}
};

void command_parser_init(command_parser_t *parser) { memset(parser, 0, sizeof(*parser)); }

static bool whitespace(char c) { return c == ' ' || c == '\t'; }

bool command_filename_valid(const char *name)
{
    if (!name) return false;
    size_t length = strlen(name);
    if (!length || length > COMMAND_FILENAME_MAX || name[0] == '.' || name[length - 1] == '.') return false;
    for (size_t i = 0; i < length; ++i) {
        char c = name[i];
        if (c == '.' && i && name[i - 1] == '.') return false;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    }
    return true;
}

static command_parse_result_t parse_line(char *line, command_request_t *request)
{
    while (whitespace(*line)) ++line;
    if (!*line) return COMMAND_MORE;
    char *name = line;
    while (*line && !whitespace(*line)) ++line;
    if (*line) *line++ = '\0';
    while (whitespace(*line)) ++line;
    char *argument = line;
    while (*line && !whitespace(*line)) ++line;
    if (*line) *line++ = '\0';
    while (whitespace(*line)) ++line;
    if (*line) return COMMAND_BAD_ARGUMENT;
    const command_entry_t *entry = NULL;
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (!strcmp(name, commands[i].name)) { entry = &commands[i]; break; }
    }
    if (!entry) return COMMAND_UNKNOWN;
    command_request_t parsed = {.kind = entry->kind};
    if (entry->argument == ARG_NONE) {
        if (*argument) return COMMAND_BAD_ARGUMENT;
    } else if (!*argument) return COMMAND_BAD_ARGUMENT;
    else if (entry->argument == ARG_RATE) {
        uint32_t value = 0;
        for (const char *p = argument; *p; ++p) {
            if (*p < '0' || *p > '9') return COMMAND_BAD_ARGUMENT;
            uint32_t digit = (uint32_t)(*p - '0');
            if (value > (UINT32_MAX - digit) / 10u) return COMMAND_BAD_ARGUMENT;
            value = value * 10u + digit;
        }
        if (value < 1 || value > 50) return COMMAND_BAD_ARGUMENT;
        parsed.rate_hz = value;
    } else if (entry->argument == ARG_SWITCH) {
        if (!strcmp(argument, "on")) parsed.enabled = true;
        else if (strcmp(argument, "off")) return COMMAND_BAD_ARGUMENT;
    } else {
        if (!command_filename_valid(argument)) return COMMAND_BAD_ARGUMENT;
        memcpy(parsed.filename, argument, strlen(argument) + 1);
    }
    *request = parsed;
    return COMMAND_READY;
}

command_parse_result_t command_feed(command_parser_t *parser, uint8_t byte, command_request_t *request)
{
    if (!parser || !request) return COMMAND_BAD_ARGUMENT;
    if (byte == '\n' && parser->skip_lf) { parser->skip_lf = false; return COMMAND_MORE; }
    if (byte == '\r' || byte == '\n') {
        parser->skip_lf = byte == '\r';
        command_parse_result_t result;
        if (parser->too_long) result = COMMAND_TOO_LONG;
        else if (parser->invalid) result = COMMAND_BAD_ARGUMENT;
        else { parser->line[parser->length] = '\0'; result = parse_line(parser->line, request); }
        parser->length = 0;
        parser->too_long = parser->invalid = false;
        return result;
    }
    parser->skip_lf = false;
    if ((byte < 32 && byte != '\t') || byte > 126) parser->invalid = true;
    if (parser->length == COMMAND_LINE_MAX) parser->too_long = true;
    else if (!parser->too_long) parser->line[parser->length++] = (char)byte;
    return COMMAND_MORE;
}

bool command_dispatch(const command_dispatch_t *dispatch, void *context, const command_request_t *request)
{
    if (!dispatch || !request || (unsigned int)request->kind >= COMMAND_COUNT || !dispatch->handler[request->kind])
        return false;
    return dispatch->handler[request->kind](context, request);
}
