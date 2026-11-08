#include "command.h"

#include <string.h>

typedef enum { ARG_NONE, ARG_RATE, ARG_SWITCH, ARG_FILE, ARG_CONFIG } argument_t;
typedef struct { const char *name; command_kind_t kind; argument_t argument; } command_entry_t;

static const command_entry_t commands[] = {
    {"status", COMMAND_STATUS, ARG_NONE}, {"start", COMMAND_START, ARG_NONE},
    {"stop", COMMAND_STOP, ARG_NONE}, {"rate", COMMAND_RATE, ARG_RATE},
    {"stream", COMMAND_STREAM, ARG_SWITCH}, {"ls", COMMAND_LS, ARG_NONE},
    {"dump", COMMAND_DUMP, ARG_FILE}, {"reboot", COMMAND_REBOOT, ARG_NONE},
    {"help", COMMAND_HELP, ARG_NONE}, {"config", COMMAND_CONFIG_GET, ARG_CONFIG}
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

static bool number(const char *text, uint32_t *out)
{
    if (!*text) return false;
    uint32_t value = 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        uint32_t digit = (uint32_t)(*p - '0');
        if (value > (UINT32_MAX - digit) / 10u) return false;
        value = value * 10u + digit;
    }
    *out = value;
    return true;
}

static command_parse_result_t parse_line(char *line, command_request_t *request)
{
    char *tokens[4];
    size_t count = 0;
    while (*line) {
        while (whitespace(*line)) ++line;
        if (!*line) break;
        if (count == 4) return COMMAND_BAD_ARGUMENT;
        tokens[count++] = line;
        while (*line && !whitespace(*line)) ++line;
        if (*line) *line++ = '\0';
    }
    if (!count) return COMMAND_MORE;
    const command_entry_t *entry = NULL;
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (!strcmp(tokens[0], commands[i].name)) { entry = &commands[i]; break; }
    }
    if (!entry) return COMMAND_UNKNOWN;
    command_request_t parsed = {.kind = entry->kind};
    if (entry->argument == ARG_CONFIG) {
        if (count == 2 && !strcmp(tokens[1], "get")) parsed.kind = COMMAND_CONFIG_GET;
        else if (count == 2 && !strcmp(tokens[1], "reset")) parsed.kind = COMMAND_CONFIG_RESET;
        else if (count == 4 && !strcmp(tokens[1], "set")) {
            parsed.kind = COMMAND_CONFIG_SET;
            if (!strcmp(tokens[2], "rate")) parsed.config_key = CONFIG_RATE;
            else if (!strcmp(tokens[2], "channels")) parsed.config_key = CONFIG_CHANNELS;
            else if (!strcmp(tokens[2], "rotate_bytes")) parsed.config_key = CONFIG_ROTATE_BYTES;
            else if (!strcmp(tokens[2], "start_on_boot")) parsed.config_key = CONFIG_START_ON_BOOT;
            else return COMMAND_BAD_ARGUMENT;
            if (!number(tokens[3], &parsed.config_value) ||
                (parsed.config_key == CONFIG_RATE && (parsed.config_value < 1 || parsed.config_value > 50)) ||
                (parsed.config_key == CONFIG_CHANNELS && parsed.config_value > 3) ||
                (parsed.config_key == CONFIG_ROTATE_BYTES && parsed.config_value < 8192) ||
                (parsed.config_key == CONFIG_START_ON_BOOT && parsed.config_value > 1)) return COMMAND_BAD_ARGUMENT;
        } else return COMMAND_BAD_ARGUMENT;
    } else if (entry->argument == ARG_NONE) {
        if (count != 1) return COMMAND_BAD_ARGUMENT;
    } else if (count != 2) return COMMAND_BAD_ARGUMENT;
    else if (entry->argument == ARG_RATE) {
        if (!number(tokens[1], &parsed.rate_hz) || parsed.rate_hz < 1 || parsed.rate_hz > 50)
            return COMMAND_BAD_ARGUMENT;
    } else if (entry->argument == ARG_SWITCH) {
        if (!strcmp(tokens[1], "on")) parsed.enabled = true;
        else if (strcmp(tokens[1], "off")) return COMMAND_BAD_ARGUMENT;
    } else {
        if (!command_filename_valid(tokens[1])) return COMMAND_BAD_ARGUMENT;
        memcpy(parsed.filename, tokens[1], strlen(tokens[1]) + 1);
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
