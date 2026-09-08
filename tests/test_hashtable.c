#include "ember_hashtable.h"
#include "test_common.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_set_get_basic(void) {
    ember_hashtable *ht = ember_hashtable_create(0);

    void *old = NULL;
    ASSERT_TRUE(ember_hashtable_set(ht, "foo", 3, (void *)1, &old));
    ASSERT_NULL(old);

    void *v = NULL;
    ASSERT_TRUE(ember_hashtable_get(ht, "foo", 3, &v));
    ASSERT_EQ(v, (void *)1);

    ASSERT_FALSE(ember_hashtable_get(ht, "bar", 3, &v));
    ASSERT_EQ(ember_hashtable_size(ht), 1u);

    ember_hashtable_destroy(ht, NULL);
}

static void test_overwrite_returns_old_value(void) {
    ember_hashtable *ht = ember_hashtable_create(0);
    ember_hashtable_set(ht, "k", 1, (void *)10, NULL);

    void *old = NULL;
    ember_hashtable_set(ht, "k", 1, (void *)20, &old);
    ASSERT_EQ(old, (void *)10);
    ASSERT_EQ(ember_hashtable_size(ht), 1u);

    void *v;
    ember_hashtable_get(ht, "k", 1, &v);
    ASSERT_EQ(v, (void *)20);

    ember_hashtable_destroy(ht, NULL);
}

static void test_delete(void) {
    ember_hashtable *ht = ember_hashtable_create(0);
    ember_hashtable_set(ht, "a", 1, (void *)1, NULL);
    ember_hashtable_set(ht, "b", 1, (void *)2, NULL);

    void *old = NULL;
    ASSERT_TRUE(ember_hashtable_delete(ht, "a", 1, &old));
    ASSERT_EQ(old, (void *)1);
    ASSERT_FALSE(ember_hashtable_contains(ht, "a", 1));
    ASSERT_TRUE(ember_hashtable_contains(ht, "b", 1));
    ASSERT_EQ(ember_hashtable_size(ht), 1u);

    ASSERT_FALSE(ember_hashtable_delete(ht, "missing", 7, NULL));

    ember_hashtable_destroy(ht, NULL);
}

static void test_binary_safe_keys(void) {
    ember_hashtable *ht = ember_hashtable_create(0);
    char k1[] = {'a', '\0', 'b'};
    char k2[] = {'a', '\0', 'c'};

    ember_hashtable_set(ht, k1, 3, (void *)1, NULL);
    ember_hashtable_set(ht, k2, 3, (void *)2, NULL);
    ASSERT_EQ(ember_hashtable_size(ht), 2u);

    void *v;
    ASSERT_TRUE(ember_hashtable_get(ht, k1, 3, &v));
    ASSERT_EQ(v, (void *)1);
    ASSERT_TRUE(ember_hashtable_get(ht, k2, 3, &v));
    ASSERT_EQ(v, (void *)2);

    ember_hashtable_destroy(ht, NULL);
}

/* Insert enough entries to force several resizes, then verify every key is
 * still reachable and every value is exactly right - this is the test most
 * likely to catch a broken Robin Hood swap or a bad rehash. */
static void test_resize_preserves_all_entries(void) {
    ember_hashtable *ht = ember_hashtable_create(4);
    const int N = 5000;

    char key[32];
    for (int i = 0; i < N; i++) {
        int len = snprintf(key, sizeof(key), "key-%d", i);
        ember_hashtable_set(ht, key, (size_t)len, (void *)(intptr_t)i, NULL);
    }
    ASSERT_EQ(ember_hashtable_size(ht), (size_t)N);

    for (int i = 0; i < N; i++) {
        int len = snprintf(key, sizeof(key), "key-%d", i);
        void *v;
        ASSERT_TRUE(ember_hashtable_get(ht, key, (size_t)len, &v));
        ASSERT_EQ((intptr_t)v, i);
    }

    ember_hashtable_destroy(ht, NULL);
}

static void test_delete_then_reinsert_and_iterate(void) {
    ember_hashtable *ht = ember_hashtable_create(0);
    const int N = 200;
    char key[32];

    for (int i = 0; i < N; i++) {
        int len = snprintf(key, sizeof(key), "k%d", i);
        ember_hashtable_set(ht, key, (size_t)len, (void *)(intptr_t)i, NULL);
    }
    /* Delete every third key to exercise backward-shift deletion across
     * many overlapping probe chains. */
    for (int i = 0; i < N; i += 3) {
        int len = snprintf(key, sizeof(key), "k%d", i);
        ASSERT_TRUE(ember_hashtable_delete(ht, key, (size_t)len, NULL));
    }

    size_t expected = 0;
    for (int i = 0; i < N; i++) if (i % 3 != 0) expected++;
    ASSERT_EQ(ember_hashtable_size(ht), expected);

    /* Every surviving key must still be findable ... */
    for (int i = 0; i < N; i++) {
        int len = snprintf(key, sizeof(key), "k%d", i);
        bool found = ember_hashtable_contains(ht, key, (size_t)len);
        ASSERT_EQ(found, (i % 3 != 0));
    }

    /* ... and iteration must see exactly the surviving count, matching
     * ember_hashtable_size(). */
    size_t seen = 0;
    ember_ht_iter it = ember_hashtable_iter(ht);
    const char *k; size_t klen; void *v;
    while (ember_ht_iter_next(&it, &k, &klen, &v)) {
        seen++;
    }
    ASSERT_EQ(seen, expected);

    ember_hashtable_destroy(ht, NULL);
}

static void test_clear(void) {
    ember_hashtable *ht = ember_hashtable_create(0);
    ember_hashtable_set(ht, "a", 1, (void *)1, NULL);
    ember_hashtable_set(ht, "b", 1, (void *)2, NULL);

    ember_hashtable_clear(ht, NULL);
    ASSERT_EQ(ember_hashtable_size(ht), 0u);
    ASSERT_FALSE(ember_hashtable_contains(ht, "a", 1));

    ember_hashtable_set(ht, "c", 1, (void *)3, NULL);
    ASSERT_EQ(ember_hashtable_size(ht), 1u);

    ember_hashtable_destroy(ht, NULL);
}

int main(void) {
    RUN_TEST(test_set_get_basic);
    RUN_TEST(test_overwrite_returns_old_value);
    RUN_TEST(test_delete);
    RUN_TEST(test_binary_safe_keys);
    RUN_TEST(test_resize_preserves_all_entries);
    RUN_TEST(test_delete_then_reinsert_and_iterate);
    RUN_TEST(test_clear);
    TEST_REPORT_AND_EXIT();
}
