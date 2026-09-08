#include "ember_db.h"
#include "test_common.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void test_set_get(void) {
    ember_db *db = ember_db_create();
    ember_db_set(db, "k", 1, "hello", 5, 0);

    const char *val; size_t len;
    ASSERT_EQ(ember_db_get(db, "k", 1, &val, &len), EMBER_OK);
    ASSERT_EQ(len, 5u);
    ASSERT_EQ(memcmp(val, "hello", 5), 0);

    ASSERT_EQ(ember_db_get(db, "missing", 7, &val, &len), EMBER_ERR_NOTFOUND);

    ember_db_destroy(db);
}

static void test_del_and_exists(void) {
    ember_db *db = ember_db_create();
    ember_db_set(db, "k", 1, "v", 1, 0);

    ASSERT_TRUE(ember_db_exists(db, "k", 1));
    ASSERT_TRUE(ember_db_del(db, "k", 1));
    ASSERT_FALSE(ember_db_exists(db, "k", 1));
    ASSERT_FALSE(ember_db_del(db, "k", 1));

    ember_db_destroy(db);
}

static void test_expiry(void) {
    ember_db *db = ember_db_create();
    ember_db_set(db, "k", 1, "v", 1, 20); /* 20ms TTL */

    ASSERT_TRUE(ember_db_exists(db, "k", 1));
    usleep(40 * 1000);
    ASSERT_FALSE(ember_db_exists(db, "k", 1));
    ASSERT_EQ(ember_db_size(db), 0u); /* lazily evicted */

    ember_db_destroy(db);
}

static void test_expire_persist_ttl(void) {
    ember_db *db = ember_db_create();
    ember_db_set(db, "k", 1, "v", 1, 0);

    ASSERT_EQ(ember_db_ttl_ms(db, "k", 1), 0); /* no ttl */

    ASSERT_TRUE(ember_db_expire(db, "k", 1, 10000));
    int64_t ttl = ember_db_ttl_ms(db, "k", 1);
    ASSERT_TRUE(ttl > 0 && ttl <= 10000);

    ASSERT_TRUE(ember_db_persist(db, "k", 1));
    ASSERT_EQ(ember_db_ttl_ms(db, "k", 1), 0);

    ASSERT_FALSE(ember_db_expire(db, "nope", 4, 1000));

    ember_db_destroy(db);
}

static void test_incrby(void) {
    ember_db *db = ember_db_create();
    int64_t result;

    /* Missing key behaves like starting from 0. */
    ASSERT_EQ(ember_db_incrby(db, "counter", 7, 5, &result), EMBER_OK);
    ASSERT_EQ(result, 5);

    ASSERT_EQ(ember_db_incrby(db, "counter", 7, -2, &result), EMBER_OK);
    ASSERT_EQ(result, 3);

    ember_db_set(db, "notanum", 7, "abc", 3, 0);
    ASSERT_EQ(ember_db_incrby(db, "notanum", 7, 1, &result), EMBER_ERR_TYPE);

    ember_db_destroy(db);
}

static void test_append_and_strlen(void) {
    ember_db *db = ember_db_create();
    size_t newlen;

    ASSERT_EQ(ember_db_append(db, "s", 1, "foo", 3, &newlen), EMBER_OK);
    ASSERT_EQ(newlen, 3u);
    ASSERT_EQ(ember_db_append(db, "s", 1, "bar", 3, &newlen), EMBER_OK);
    ASSERT_EQ(newlen, 6u);

    const char *val; size_t len;
    ember_db_get(db, "s", 1, &val, &len);
    ASSERT_EQ(memcmp(val, "foobar", 6), 0);

    size_t slen;
    ember_db_strlen(db, "s", 1, &slen);
    ASSERT_EQ(slen, 6u);

    ember_db_strlen(db, "missing", 7, &slen);
    ASSERT_EQ(slen, 0u);

    ember_db_destroy(db);
}

static void test_flushall_and_size(void) {
    ember_db *db = ember_db_create();
    ember_db_set(db, "a", 1, "1", 1, 0);
    ember_db_set(db, "b", 1, "2", 1, 0);
    ASSERT_EQ(ember_db_size(db), 2u);

    ember_db_flushall(db);
    ASSERT_EQ(ember_db_size(db), 0u);
    ASSERT_FALSE(ember_db_exists(db, "a", 1));

    ember_db_destroy(db);
}

static void test_active_expire_cycle_reclaims_memory(void) {
    ember_db *db = ember_db_create();
    char key[32];
    for (int i = 0; i < 50; i++) {
        int n = snprintf(key, sizeof(key), "k%d", i);
        ember_db_set(db, key, (size_t)n, "v", 1, 5); /* 5ms TTL, all expire fast */
    }
    ASSERT_EQ(ember_db_size(db), 50u);

    usleep(20 * 1000);

    size_t total_evicted = 0;
    /* Sweep enough times to cover the whole table at least once. */
    for (int i = 0; i < 20; i++) {
        total_evicted += ember_db_active_expire_cycle(db, 8);
    }

    ASSERT_EQ(total_evicted, 50u);
    ASSERT_EQ(ember_db_size(db), 0u);

    ember_db_destroy(db);
}

static void test_iteration_skips_expired(void) {
    ember_db *db = ember_db_create();
    ember_db_set(db, "keep1", 5, "v", 1, 0);
    ember_db_set(db, "keep2", 5, "v", 1, 0);
    ember_db_set(db, "gone", 4, "v", 1, 5);

    usleep(20 * 1000);

    int seen = 0;
    ember_db_iter it = ember_db_iter_start(db);
    const char *k; size_t klen;
    while (ember_db_iter_next(&it, &k, &klen)) {
        ASSERT_TRUE(klen == 5);
        seen++;
    }
    ASSERT_EQ(seen, 2);

    ember_db_destroy(db);
}

int main(void) {
    RUN_TEST(test_set_get);
    RUN_TEST(test_del_and_exists);
    RUN_TEST(test_expiry);
    RUN_TEST(test_expire_persist_ttl);
    RUN_TEST(test_incrby);
    RUN_TEST(test_append_and_strlen);
    RUN_TEST(test_flushall_and_size);
    RUN_TEST(test_active_expire_cycle_reclaims_memory);
    RUN_TEST(test_iteration_skips_expired);
    TEST_REPORT_AND_EXIT();
}
