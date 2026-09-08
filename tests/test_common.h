/*
 * test_common.h - tiny hand-rolled test harness.
 *
 * No external dependency is pulled in for something this small: each test
 * file is its own standalone `main`, and failures print a file:line plus
 * the failing expression so they read like compiler errors.
 */
#ifndef EMBER_TEST_COMMON_H
#define EMBER_TEST_COMMON_H

#include <stdio.h>
#include <string.h>

static int ember_test_failures = 0;
static int ember_test_count = 0;

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                     \
        ember_test_count++;                                                  \
        if (!(cond)) {                                                       \
            ember_test_failures++;                                           \
            fprintf(stderr, "  FAIL %s:%d: ASSERT_TRUE(%s)\n", __FILE__,     \
                    __LINE__, #cond);                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b)                                                      \
    do {                                                                     \
        ember_test_count++;                                                  \
        if ((a) != (b)) {                                                    \
            ember_test_failures++;                                           \
            fprintf(stderr, "  FAIL %s:%d: ASSERT_EQ(%s, %s)\n", __FILE__,   \
                    __LINE__, #a, #b);                                       \
        }                                                                    \
    } while (0)

#define ASSERT_STREQ(a, b)                                                   \
    do {                                                                     \
        ember_test_count++;                                                  \
        if (strcmp((a), (b)) != 0) {                                         \
            ember_test_failures++;                                           \
            fprintf(stderr, "  FAIL %s:%d: ASSERT_STREQ(%s, %s) -> \"%s\" != \"%s\"\n", \
                    __FILE__, __LINE__, #a, #b, (a), (b));                   \
        }                                                                    \
    } while (0)

#define ASSERT_NULL(p) ASSERT_TRUE((p) == NULL)
#define ASSERT_NOT_NULL(p) ASSERT_TRUE((p) != NULL)

#define RUN_TEST(fn)                                                         \
    do {                                                                     \
        printf("RUN  %s\n", #fn);                                           \
        fn();                                                                \
    } while (0)

#define TEST_REPORT_AND_EXIT()                                               \
    do {                                                                     \
        printf("%d assertions, %d failed\n", ember_test_count,               \
               ember_test_failures);                                         \
        return ember_test_failures == 0 ? 0 : 1;                             \
    } while (0)

#endif /* EMBER_TEST_COMMON_H */
