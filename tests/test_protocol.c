#include "ember_protocol.h"
#include "test_common.h"

#include <string.h>

static void test_parse_multibulk_basic(void) {
    const char *wire = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
    ember_command cmd;
    size_t consumed;

    int status = ember_protocol_parse(wire, strlen(wire), &cmd, &consumed);
    ASSERT_EQ(status, EMBER_OK);
    ASSERT_EQ(consumed, strlen(wire));
    ASSERT_EQ(cmd.argc, 3);
    ASSERT_STREQ(cmd.argv[0], "SET");
    ASSERT_STREQ(cmd.argv[1], "foo");
    ASSERT_STREQ(cmd.argv[2], "bar");

    ember_command_free(&cmd);
}

static void test_parse_incomplete_returns_again(void) {
    const char *wire = "*3\r\n$3\r\nSET\r\n$3\r\nfo"; /* cut mid-argument */
    ember_command cmd;
    size_t consumed;

    int status = ember_protocol_parse(wire, strlen(wire), &cmd, &consumed);
    ASSERT_EQ(status, EMBER_ERR_AGAIN);
    ASSERT_EQ(consumed, 0u);
}

static void test_parse_arrives_byte_by_byte(void) {
    const char *wire = "*2\r\n$3\r\nGET\r\n$1\r\nk\r\n";
    size_t total = strlen(wire);

    ember_command cmd;
    size_t consumed;
    int status = EMBER_ERR_AGAIN;

    /* Feed one extra byte at a time until the parser reports success,
     * simulating a slow/fragmented TCP stream. */
    for (size_t n = 1; n <= total; n++) {
        status = ember_protocol_parse(wire, n, &cmd, &consumed);
        if (status == EMBER_OK) {
            ASSERT_EQ(n, total);
            break;
        }
        ASSERT_EQ(status, EMBER_ERR_AGAIN);
    }
    ASSERT_EQ(status, EMBER_OK);
    ASSERT_EQ(cmd.argc, 2);
    ASSERT_STREQ(cmd.argv[0], "GET");
    ASSERT_STREQ(cmd.argv[1], "k");

    ember_command_free(&cmd);
}

static void test_parse_two_commands_back_to_back(void) {
    const char *wire = "*1\r\n$4\r\nPING\r\n*1\r\n$4\r\nPING\r\n";
    ember_command cmd;
    size_t consumed;

    int status = ember_protocol_parse(wire, strlen(wire), &cmd, &consumed);
    ASSERT_EQ(status, EMBER_OK);
    ASSERT_EQ(cmd.argc, 1);
    ASSERT_STREQ(cmd.argv[0], "PING");
    ember_command_free(&cmd);

    status = ember_protocol_parse(wire + consumed, strlen(wire) - consumed, &cmd, &consumed);
    ASSERT_EQ(status, EMBER_OK);
    ASSERT_STREQ(cmd.argv[0], "PING");
    ember_command_free(&cmd);
}

static void test_parse_inline_command(void) {
    const char *wire = "SET foo bar\r\n";
    ember_command cmd;
    size_t consumed;

    int status = ember_protocol_parse(wire, strlen(wire), &cmd, &consumed);
    ASSERT_EQ(status, EMBER_OK);
    ASSERT_EQ(consumed, strlen(wire));
    ASSERT_EQ(cmd.argc, 3);
    ASSERT_STREQ(cmd.argv[0], "SET");
    ASSERT_STREQ(cmd.argv[1], "foo");
    ASSERT_STREQ(cmd.argv[2], "bar");

    ember_command_free(&cmd);
}

static void test_parse_inline_extra_whitespace(void) {
    const char *wire = "  PING   \r\n";
    ember_command cmd;
    size_t consumed;

    ASSERT_EQ(ember_protocol_parse(wire, strlen(wire), &cmd, &consumed), EMBER_OK);
    ASSERT_EQ(cmd.argc, 1);
    ASSERT_STREQ(cmd.argv[0], "PING");
    ember_command_free(&cmd);
}

static void test_parse_binary_safe_bulk(void) {
    /* strlen would stop at an embedded NUL, so this is built byte-by-byte
     * instead of as a string literal. */
    char buf[64];
    size_t n = 0;
    memcpy(buf, "*2\r\n$3\r\nSET\r\n$3\r\n", 17); n = 17;
    buf[n++] = 'a'; buf[n++] = '\0'; buf[n++] = 'b';
    memcpy(buf + n, "\r\n", 2); n += 2;

    ember_command cmd;
    size_t consumed;
    ASSERT_EQ(ember_protocol_parse(buf, n, &cmd, &consumed), EMBER_OK);
    ASSERT_EQ(cmd.argc, 2);
    ASSERT_EQ(cmd.argvlen[1], 3u);
    ASSERT_EQ(memcmp(cmd.argv[1], "a\0b", 3), 0);

    ember_command_free(&cmd);
}

static void test_parse_malformed_is_protocol_error(void) {
    const char *wire = "*2\r\n$3\r\nSET\r\nnotadollar\r\n";
    ember_command cmd;
    size_t consumed;
    ASSERT_EQ(ember_protocol_parse(wire, strlen(wire), &cmd, &consumed), EMBER_ERR_PROTOCOL);
}

static void test_reply_serialization(void) {
    sds buf = sds_empty();

    buf = ember_reply_simple_string(buf, "OK");
    ASSERT_STREQ(buf, "+OK\r\n");
    sds_clear(buf);

    buf = ember_reply_error(buf, "ERR bad thing");
    ASSERT_STREQ(buf, "-ERR bad thing\r\n");
    sds_clear(buf);

    buf = ember_reply_integer(buf, 42);
    ASSERT_STREQ(buf, ":42\r\n");
    sds_clear(buf);

    buf = ember_reply_bulk_string(buf, "hi", 2);
    ASSERT_STREQ(buf, "$2\r\nhi\r\n");
    sds_clear(buf);

    buf = ember_reply_null_bulk(buf);
    ASSERT_STREQ(buf, "$-1\r\n");
    sds_clear(buf);

    buf = ember_reply_array_header(buf, 2);
    buf = ember_reply_bulk_string(buf, "a", 1);
    buf = ember_reply_bulk_string(buf, "bb", 2);
    ASSERT_STREQ(buf, "*2\r\n$1\r\na\r\n$2\r\nbb\r\n");

    sds_free(buf);
}

int main(void) {
    RUN_TEST(test_parse_multibulk_basic);
    RUN_TEST(test_parse_incomplete_returns_again);
    RUN_TEST(test_parse_arrives_byte_by_byte);
    RUN_TEST(test_parse_two_commands_back_to_back);
    RUN_TEST(test_parse_inline_command);
    RUN_TEST(test_parse_inline_extra_whitespace);
    RUN_TEST(test_parse_binary_safe_bulk);
    RUN_TEST(test_parse_malformed_is_protocol_error);
    RUN_TEST(test_reply_serialization);
    TEST_REPORT_AND_EXIT();
}
