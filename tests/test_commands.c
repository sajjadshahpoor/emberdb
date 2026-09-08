#include "ember_commands.h"
#include "ember_server.h"
#include "test_common.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* Builds an ember_command the same way the real protocol parser would
 * (each argv entry independently heap-allocated), so tests exercise
 * dispatch exactly as a live connection would and can free it with the
 * real ember_command_free. */
static ember_command make_cmd(const char *first, ...) {
    va_list ap;
    int argc = 0;
    va_start(ap, first);
    for (const char *s = first; s; s = va_arg(ap, const char *)) argc++;
    va_end(ap);

    ember_command cmd;
    cmd.argc = argc;
    cmd.argv = malloc(sizeof(char *) * (size_t)argc);
    cmd.argvlen = malloc(sizeof(size_t) * (size_t)argc);

    va_start(ap, first);
    const char *s = first;
    for (int i = 0; i < argc; i++) {
        size_t len = strlen(s);
        cmd.argv[i] = malloc(len + 1);
        memcpy(cmd.argv[i], s, len + 1);
        cmd.argvlen[i] = len;
        s = va_arg(ap, const char *);
    }
    va_end(ap);
    return cmd;
}

static ember_server *make_server(void) {
    ember_server *s = calloc(1, sizeof(ember_server));
    s->db = ember_db_create();
    return s;
}

static void free_server(ember_server *s) {
    ember_db_destroy(s->db);
    free(s);
}

static void test_set_get_via_dispatch(void) {
    ember_server *srv = make_server();
    sds reply = sds_empty();

    ember_command cmd = make_cmd("SET", "foo", "bar", NULL);
    ASSERT_TRUE(ember_command_dispatch(srv, &cmd, &reply));
    ASSERT_STREQ(reply, "+OK\r\n");
    ember_command_free(&cmd);
    sds_clear(reply);

    cmd = make_cmd("GET", "foo", NULL);
    ember_command_dispatch(srv, &cmd, &reply);
    ASSERT_STREQ(reply, "$3\r\nbar\r\n");
    ember_command_free(&cmd);

    sds_free(reply);
    free_server(srv);
}

static void test_get_missing_key_is_null_bulk(void) {
    ember_server *srv = make_server();
    sds reply = sds_empty();

    ember_command cmd = make_cmd("GET", "missing", NULL);
    ember_command_dispatch(srv, &cmd, &reply);
    ASSERT_STREQ(reply, "$-1\r\n");

    ember_command_free(&cmd);
    sds_free(reply);
    free_server(srv);
}

static void test_unknown_command_is_an_error(void) {
    ember_server *srv = make_server();
    sds reply = sds_empty();

    ember_command cmd = make_cmd("NOTACOMMAND", NULL);
    ember_command_dispatch(srv, &cmd, &reply);
    ASSERT_TRUE(strncmp(reply, "-ERR", 4) == 0);

    ember_command_free(&cmd);
    sds_free(reply);
    free_server(srv);
}

static void test_wrong_arity_is_an_error(void) {
    ember_server *srv = make_server();
    sds reply = sds_empty();

    ember_command cmd = make_cmd("SET", "onlykey", NULL); /* SET needs key+value */
    ember_command_dispatch(srv, &cmd, &reply);
    ASSERT_TRUE(strncmp(reply, "-ERR", 4) == 0);

    ember_command_free(&cmd);
    sds_free(reply);
    free_server(srv);
}

static void test_incr_on_non_integer_is_type_error(void) {
    ember_server *srv = make_server();
    sds reply = sds_empty();

    ember_command cmd = make_cmd("SET", "s", "notanumber", NULL);
    ember_command_dispatch(srv, &cmd, &reply);
    ember_command_free(&cmd);
    sds_clear(reply);

    cmd = make_cmd("INCR", "s", NULL);
    ember_command_dispatch(srv, &cmd, &reply);
    ASSERT_TRUE(strncmp(reply, "-ERR", 4) == 0);

    ember_command_free(&cmd);
    sds_free(reply);
    free_server(srv);
}

static void test_quit_signals_connection_close(void) {
    ember_server *srv = make_server();
    sds reply = sds_empty();

    ember_command cmd = make_cmd("QUIT", NULL);
    bool keep_open = ember_command_dispatch(srv, &cmd, &reply);
    ASSERT_FALSE(keep_open);
    ASSERT_STREQ(reply, "+OK\r\n");

    ember_command_free(&cmd);
    sds_free(reply);
    free_server(srv);
}

static void test_expire_ttl_del_flow(void) {
    ember_server *srv = make_server();
    sds reply = sds_empty();

    ember_command cmd = make_cmd("SET", "k", "v", NULL);
    ember_command_dispatch(srv, &cmd, &reply);
    ember_command_free(&cmd);
    sds_clear(reply);

    cmd = make_cmd("EXPIRE", "k", "100", NULL);
    ember_command_dispatch(srv, &cmd, &reply);
    ASSERT_STREQ(reply, ":1\r\n");
    ember_command_free(&cmd);
    sds_clear(reply);

    cmd = make_cmd("TTL", "k", NULL);
    ember_command_dispatch(srv, &cmd, &reply);
    ASSERT_EQ(reply[0], ':');
    ember_command_free(&cmd);
    sds_clear(reply);

    cmd = make_cmd("DEL", "k", NULL);
    ember_command_dispatch(srv, &cmd, &reply);
    ASSERT_STREQ(reply, ":1\r\n");
    ember_command_free(&cmd);

    sds_free(reply);
    free_server(srv);
}

static void test_dbsize_and_flushall(void) {
    ember_server *srv = make_server();
    sds reply = sds_empty();

    ember_command cmd = make_cmd("SET", "a", "1", NULL);
    ember_command_dispatch(srv, &cmd, &reply);
    ember_command_free(&cmd);
    sds_clear(reply);

    cmd = make_cmd("SET", "b", "2", NULL);
    ember_command_dispatch(srv, &cmd, &reply);
    ember_command_free(&cmd);
    sds_clear(reply);

    cmd = make_cmd("DBSIZE", NULL);
    ember_command_dispatch(srv, &cmd, &reply);
    ASSERT_STREQ(reply, ":2\r\n");
    ember_command_free(&cmd);
    sds_clear(reply);

    cmd = make_cmd("FLUSHALL", NULL);
    ember_command_dispatch(srv, &cmd, &reply);
    ember_command_free(&cmd);
    sds_clear(reply);

    cmd = make_cmd("DBSIZE", NULL);
    ember_command_dispatch(srv, &cmd, &reply);
    ASSERT_STREQ(reply, ":0\r\n");
    ember_command_free(&cmd);

    sds_free(reply);
    free_server(srv);
}

int main(void) {
    RUN_TEST(test_set_get_via_dispatch);
    RUN_TEST(test_get_missing_key_is_null_bulk);
    RUN_TEST(test_unknown_command_is_an_error);
    RUN_TEST(test_wrong_arity_is_an_error);
    RUN_TEST(test_incr_on_non_integer_is_type_error);
    RUN_TEST(test_quit_signals_connection_close);
    RUN_TEST(test_expire_ttl_del_flow);
    RUN_TEST(test_dbsize_and_flushall);
    TEST_REPORT_AND_EXIT();
}
