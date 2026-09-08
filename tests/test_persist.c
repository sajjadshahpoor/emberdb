#include "ember_persist.h"
#include "ember_commands.h"
#include "ember_server.h"
#include "test_common.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dispatch_and_discard(ember_server *srv, const char *first, ...) {
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

    sds reply = sds_empty();
    ember_command_dispatch(srv, &cmd, &reply);
    sds_free(reply);
    ember_command_free(&cmd);
}

static void test_aof_replay_reproduces_state(void) {
    const char *path = "/tmp/emberdb_test_aof.log";
    remove(path);

    ember_server srv;
    memset(&srv, 0, sizeof(srv));
    srv.db = ember_db_create();
    srv.aof = ember_aof_open(path);
    ASSERT_NOT_NULL(srv.aof);

    dispatch_and_discard(&srv, "SET", "foo", "bar", NULL);
    dispatch_and_discard(&srv, "INCR", "counter", NULL);
    dispatch_and_discard(&srv, "INCR", "counter", NULL);
    dispatch_and_discard(&srv, "APPEND", "foo", "baz", NULL);
    dispatch_and_discard(&srv, "DEL", "nonexistent", NULL);

    ember_aof_close(srv.aof);
    ember_db_destroy(srv.db);

    ember_db *replayed = ember_db_create();
    ASSERT_EQ(ember_aof_load(path, replayed), EMBER_OK);

    const char *val;
    size_t len;
    ASSERT_EQ(ember_db_get(replayed, "foo", 3, &val, &len), EMBER_OK);
    ASSERT_EQ(len, 6u);
    ASSERT_EQ(memcmp(val, "barbaz", 6), 0);

    ASSERT_EQ(ember_db_get(replayed, "counter", 7, &val, &len), EMBER_OK);
    ASSERT_EQ(memcmp(val, "2", 1), 0);

    ember_db_destroy(replayed);
    remove(path);
}

static void test_aof_load_missing_file_is_ok(void) {
    ember_db *db = ember_db_create();
    ASSERT_EQ(ember_aof_load("/tmp/emberdb_definitely_missing.log", db), EMBER_OK);
    ASSERT_EQ(ember_db_size(db), 0u);
    ember_db_destroy(db);
}

static void test_snapshot_roundtrip(void) {
    const char *path = "/tmp/emberdb_test_snapshot.rdb";
    remove(path);

    ember_db *db = ember_db_create();
    ember_db_set(db, "a", 1, "1", 1, 0);
    ember_db_set(db, "b", 1, "2", 1, 0);
    ember_db_set(db, "ttlkey", 6, "v", 1, 60000);

    ASSERT_EQ(ember_snapshot_save(db, path), EMBER_OK);
    ember_db_destroy(db);

    ember_db *loaded = ember_db_create();
    ASSERT_EQ(ember_snapshot_load(path, loaded), EMBER_OK);
    ASSERT_EQ(ember_db_size(loaded), 3u);

    const char *val;
    size_t len;
    ASSERT_EQ(ember_db_get(loaded, "a", 1, &val, &len), EMBER_OK);
    ASSERT_EQ(memcmp(val, "1", 1), 0);

    int64_t ttl = ember_db_ttl_ms(loaded, "ttlkey", 6);
    ASSERT_TRUE(ttl > 0 && ttl <= 60000);

    ember_db_destroy(loaded);
    remove(path);
}

static void test_snapshot_load_missing_file_is_ok(void) {
    ember_db *db = ember_db_create();
    ASSERT_EQ(ember_snapshot_load("/tmp/emberdb_definitely_missing.rdb", db), EMBER_OK);
    ASSERT_EQ(ember_db_size(db), 0u);
    ember_db_destroy(db);
}

int main(void) {
    RUN_TEST(test_aof_replay_reproduces_state);
    RUN_TEST(test_aof_load_missing_file_is_ok);
    RUN_TEST(test_snapshot_roundtrip);
    RUN_TEST(test_snapshot_load_missing_file_is_ok);
    TEST_REPORT_AND_EXIT();
}
