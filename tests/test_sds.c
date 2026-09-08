#include "ember_sds.h"
#include "test_common.h"

#include <string.h>

static void test_new_and_len(void) {
    sds s = sds_new("hello");
    ASSERT_EQ(sds_len(s), 5u);
    ASSERT_STREQ(s, "hello");
    sds_free(s);
}

static void test_empty(void) {
    sds s = sds_empty();
    ASSERT_EQ(sds_len(s), 0u);
    ASSERT_STREQ(s, "");
    sds_free(s);
}

static void test_cat_grows_and_preserves_content(void) {
    sds s = sds_new("foo");
    s = sds_cat(s, "bar");
    s = sds_cat(s, "baz");
    ASSERT_STREQ(s, "foobarbaz");
    ASSERT_EQ(sds_len(s), 9u);
    sds_free(s);
}

static void test_cat_len_is_binary_safe(void) {
    char data[] = {'a', '\0', 'b', 'c'};
    sds s = sds_new_len(data, sizeof(data));
    ASSERT_EQ(sds_len(s), 4u);
    ASSERT_EQ(memcmp(s, data, 4), 0);
    sds_free(s);
}

static void test_cat_printf(void) {
    sds s = sds_new("count=");
    s = sds_cat_printf(s, "%d/%d", 3, 10);
    ASSERT_STREQ(s, "count=3/10");
    sds_free(s);
}

static void test_dup_and_cmp(void) {
    sds a = sds_new("abc");
    sds b = sds_dup(a);
    ASSERT_EQ(sds_cmp(a, b), 0);

    sds c = sds_new("abd");
    ASSERT_TRUE(sds_cmp(a, c) < 0);

    sds_free(a);
    sds_free(b);
    sds_free(c);
}

static void test_clear(void) {
    sds s = sds_new("something");
    sds_clear(s);
    ASSERT_EQ(sds_len(s), 0u);
    ASSERT_STREQ(s, "");
    sds_free(s);
}

static void test_advance_drops_prefix(void) {
    sds s = sds_new("hello world");
    sds_advance(s, 6);
    ASSERT_STREQ(s, "world");
    ASSERT_EQ(sds_len(s), 5u);

    sds_advance(s, 100); /* past the end: clamps to empty */
    ASSERT_STREQ(s, "");
    ASSERT_EQ(sds_len(s), 0u);

    sds_free(s);
}

static void test_many_small_appends(void) {
    sds s = sds_empty();
    for (int i = 0; i < 10000; i++) {
        s = sds_cat(s, "x");
    }
    ASSERT_EQ(sds_len(s), 10000u);
    sds_free(s);
}

int main(void) {
    RUN_TEST(test_new_and_len);
    RUN_TEST(test_empty);
    RUN_TEST(test_cat_grows_and_preserves_content);
    RUN_TEST(test_cat_len_is_binary_safe);
    RUN_TEST(test_cat_printf);
    RUN_TEST(test_dup_and_cmp);
    RUN_TEST(test_clear);
    RUN_TEST(test_advance_drops_prefix);
    RUN_TEST(test_many_small_appends);
    TEST_REPORT_AND_EXIT();
}
