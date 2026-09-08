#include "ember_arena.h"
#include "ember_common.h"
#include "test_common.h"

#include <stdint.h>
#include <string.h>

static void test_basic_alloc(void) {
    ember_arena arena;
    ASSERT_EQ(ember_arena_init(&arena, 0), EMBER_OK);

    int *a = ember_arena_alloc(&arena, sizeof(int));
    int *b = ember_arena_alloc(&arena, sizeof(int));
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    ASSERT_TRUE(a != b);

    *a = 42;
    *b = 7;
    ASSERT_EQ(*a, 42);
    ASSERT_EQ(*b, 7);

    ember_arena_destroy(&arena);
}

static void test_alignment(void) {
    ember_arena arena;
    ember_arena_init(&arena, 0);

    /* Force an odd offset, then request a pointer-aligned allocation and
     * confirm the returned address actually respects the alignment. */
    ember_arena_alloc(&arena, 1);
    void *p = ember_arena_alloc_aligned(&arena, sizeof(double), sizeof(double));
    ASSERT_EQ(((uintptr_t)p) % sizeof(double), 0);

    ember_arena_destroy(&arena);
}

static void test_growth_across_blocks(void) {
    ember_arena arena;
    /* Tiny block size to force several block allocations quickly. */
    ember_arena_init(&arena, 64);

    void *ptrs[100];
    for (int i = 0; i < 100; i++) {
        ptrs[i] = ember_arena_alloc(&arena, 32);
        ASSERT_NOT_NULL(ptrs[i]);
    }

    /* Every returned pointer must be distinct and writable. */
    for (int i = 0; i < 100; i++) {
        memset(ptrs[i], i & 0xFF, 32);
    }
    for (int i = 0; i < 100; i++) {
        unsigned char *bytes = ptrs[i];
        for (int j = 0; j < 32; j++) {
            ASSERT_EQ(bytes[j], (unsigned char)(i & 0xFF));
        }
    }

    ASSERT_TRUE(ember_arena_bytes_reserved(&arena) > 64);
    ember_arena_destroy(&arena);
}

static void test_reset_reuses_first_block(void) {
    ember_arena arena;
    ember_arena_init(&arena, 64);

    for (int i = 0; i < 20; i++) {
        ember_arena_alloc(&arena, 32);
    }
    size_t reserved_before = ember_arena_bytes_reserved(&arena);
    ASSERT_TRUE(reserved_before > 64);

    ember_arena_reset(&arena);
    ASSERT_EQ(ember_arena_bytes_used(&arena), 0);
    ASSERT_EQ(ember_arena_bytes_reserved(&arena), 64);

    void *p = ember_arena_alloc(&arena, 8);
    ASSERT_NOT_NULL(p);

    ember_arena_destroy(&arena);
}

int main(void) {
    RUN_TEST(test_basic_alloc);
    RUN_TEST(test_alignment);
    RUN_TEST(test_growth_across_blocks);
    RUN_TEST(test_reset_reuses_first_block);
    TEST_REPORT_AND_EXIT();
}
