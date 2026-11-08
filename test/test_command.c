#include "unity.h"
#include "command.h"
#include "command_io.h"

#include <string.h>

static command_parser_t parser;
static command_request_t request;
static unsigned int calls;
static command_kind_t last_kind;

void setUp(void) { command_parser_init(&parser); memset(&request, 0, sizeof(request)); calls = 0; }
void tearDown(void) {}

static command_parse_result_t line(const char *text)
{
    command_parse_result_t result = COMMAND_MORE;
    while (*text) result = command_feed(&parser, (uint8_t)*text++, &request);
    return result;
}

static bool handle(void *context, const command_request_t *input)
{
    TEST_ASSERT_EQUAL_PTR(&calls, context);
    ++calls;
    last_kind = input->kind;
    return true;
}

static void accepts_every_command_and_dispatches(void)
{
    static const struct { const char *text; command_kind_t kind; } cases[] = {
        {"status\n", COMMAND_STATUS}, {"start\n", COMMAND_START}, {"stop\n", COMMAND_STOP},
        {"rate 50\n", COMMAND_RATE}, {"stream on\n", COMMAND_STREAM}, {"ls\n", COMMAND_LS},
        {"dump LOG_0001.BIN\n", COMMAND_DUMP}, {"reboot\n", COMMAND_REBOOT}, {"help\n", COMMAND_HELP}
    };
    command_dispatch_t dispatch = {0};
    for (unsigned int i = 0; i < COMMAND_COUNT; ++i) dispatch.handler[i] = handle;
    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        TEST_ASSERT_EQUAL_INT(COMMAND_READY, line(cases[i].text));
        TEST_ASSERT_EQUAL_INT(cases[i].kind, request.kind);
        TEST_ASSERT_TRUE(command_dispatch(&dispatch, &calls, &request));
        TEST_ASSERT_EQUAL_INT(cases[i].kind, last_kind);
    }
    TEST_ASSERT_EQUAL_UINT(COMMAND_COUNT, calls);
    dispatch.handler[COMMAND_HELP] = NULL;
    TEST_ASSERT_FALSE(command_dispatch(&dispatch, &calls, &request));
    request.kind = COMMAND_COUNT;
    TEST_ASSERT_FALSE(command_dispatch(&dispatch, &calls, &request));
    TEST_ASSERT_FALSE(command_dispatch(NULL, &calls, &request));
}

static void validates_rate_switch_file_and_argument_count(void)
{
    TEST_ASSERT_EQUAL_INT(COMMAND_READY, line("  rate\t1 \r"));
    TEST_ASSERT_EQUAL_UINT32(1, request.rate_hz);
    TEST_ASSERT_EQUAL_INT(COMMAND_MORE, line("\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_READY, line("rate 00050\n"));
    TEST_ASSERT_EQUAL_UINT32(50, request.rate_hz);
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, line("rate 0\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, line("rate 51\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, line("rate 4294967296\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, line("rate -1\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, line("rate 2x\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, line("rate\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_READY, line("stream off\n"));
    TEST_ASSERT_FALSE(request.enabled);
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, line("stream yes\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, line("status extra\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, line("rate 2 extra\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, line("dump ../file\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, line("dump ..\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, line("dump LOG_0001.BIN.\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, line("dump x..bin\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, line("dump 0:LOG.BIN\n"));
    TEST_ASSERT_FALSE(command_filename_valid(NULL));
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, command_feed(NULL, '\n', &request));
    TEST_ASSERT_EQUAL_INT(COMMAND_READY, line("dump log-2.bin\n"));
    TEST_ASSERT_EQUAL_STRING("log-2.bin", request.filename);
    TEST_ASSERT_EQUAL_INT(COMMAND_READY, line("dump 12345678901234567890123456789012\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, line("dump 123456789012345678901234567890123\n"));
}

static void drains_overlong_and_invalid_input_then_recovers(void)
{
    TEST_ASSERT_EQUAL_INT(COMMAND_UNKNOWN, line("unknown\n"));
    for (unsigned int i = 0; i < COMMAND_LINE_MAX + 100; ++i)
        TEST_ASSERT_EQUAL_INT(COMMAND_MORE, command_feed(&parser, 'x', &request));
    TEST_ASSERT_EQUAL_INT(COMMAND_TOO_LONG, command_feed(&parser, '\n', &request));
    TEST_ASSERT_EQUAL_INT(COMMAND_READY, line("help\r"));
    TEST_ASSERT_EQUAL_INT(COMMAND_MORE, line("\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_MORE, command_feed(&parser, 'a', &request));
    TEST_ASSERT_EQUAL_INT(COMMAND_MORE, command_feed(&parser, 0, &request));
    TEST_ASSERT_EQUAL_INT(COMMAND_BAD_ARGUMENT, command_feed(&parser, '\n', &request));
    TEST_ASSERT_EQUAL_INT(COMMAND_READY, line("status\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_MORE, line(" \t\n"));
}

static void output_queue_and_transfer_encoding(void)
{
    static command_output_t output;
    memset(&output, 0, sizeof(output));
    static char large[COMMAND_OUTPUT_CAPACITY];
    memset(large, 'x', sizeof(large));
    TEST_ASSERT_TRUE(command_output_write(&output, large, sizeof(large)));
    TEST_ASSERT_FALSE(command_output_write(&output, "a", 1));
    TEST_ASSERT_EQUAL_UINT32(1, output.dropped);
    const char *part;
    TEST_ASSERT_EQUAL_UINT(COMMAND_OUTPUT_CAPACITY, command_output_peek(&output, &part));
    command_output_consume(&output, COMMAND_OUTPUT_CAPACITY - 2);
    TEST_ASSERT_TRUE(command_output_write(&output, "abcd", 4));
    TEST_ASSERT_EQUAL_UINT(2, command_output_peek(&output, &part));
    command_output_consume(&output, 2);
    TEST_ASSERT_EQUAL_UINT(4, command_output_peek(&output, &part));
    TEST_ASSERT_EQUAL_MEMORY("abcd", part, 4);
    command_output_reset(&output);
    TEST_ASSERT_EQUAL_UINT(0, command_output_peek(&output, &part));
    TEST_ASSERT_TRUE(command_output_write(&output, large, COMMAND_OUTPUT_CAPACITY - 2));
    command_output_consume(&output, COMMAND_OUTPUT_CAPACITY - 2);
    TEST_ASSERT_TRUE(command_output_write(&output, "\n@OK\n", 5));
    char frame[64];
    TEST_ASSERT_EQUAL_UINT(5, command_output_frame(&output, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_MEMORY("\n@OK\n", frame, 5);
    char encoded[16];
    TEST_ASSERT_EQUAL_UINT(4, command_base64((const uint8_t *)"M", 1, encoded, sizeof(encoded)));
    TEST_ASSERT_EQUAL_STRING("TQ==", encoded);
    TEST_ASSERT_EQUAL_UINT(4, command_base64((const uint8_t *)"Ma", 2, encoded, sizeof(encoded)));
    TEST_ASSERT_EQUAL_STRING("TWE=", encoded);
    TEST_ASSERT_EQUAL_UINT(4, command_base64((const uint8_t *)"Man", 3, encoded, sizeof(encoded)));
    TEST_ASSERT_EQUAL_STRING("TWFu", encoded);
    TEST_ASSERT_EQUAL_UINT(0, command_base64((const uint8_t *)"Man", 3, encoded, 4));
    uint32_t crc = command_crc32_update(UINT32_MAX, (const uint8_t *)"1234", 4);
    crc = command_crc32_update(crc, (const uint8_t *)"56789", 5) ^ UINT32_MAX;
    TEST_ASSERT_EQUAL_UINT32(UINT32_C(0xcbf43926), crc);
}

static void arbitrary_input_is_bounded_and_parser_recovers(void)
{
    uint32_t state = 12345;
    for (unsigned int i = 0; i < 100000; ++i) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        (void)command_feed(&parser, (uint8_t)(state >> 24), &request);
    }
    (void)command_feed(&parser, '\n', &request);
    TEST_ASSERT_EQUAL_INT(COMMAND_READY, line("help\n"));
    TEST_ASSERT_EQUAL_INT(COMMAND_HELP, request.kind);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(accepts_every_command_and_dispatches);
    RUN_TEST(validates_rate_switch_file_and_argument_count);
    RUN_TEST(drains_overlong_and_invalid_input_then_recovers);
    RUN_TEST(output_queue_and_transfer_encoding);
    RUN_TEST(arbitrary_input_is_bounded_and_parser_recovers);
    return UNITY_END();
}
