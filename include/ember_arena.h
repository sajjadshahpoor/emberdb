/*
 * ember_arena.h - bump-pointer arena allocator.
 *
 * EmberDB uses arenas for allocations that share a lifetime (e.g. all the
 * temporary buffers built up while parsing one client request, or all the
 * nodes created while replaying the AOF at startup). Instead of freeing
 * objects one at a time, the whole arena is reset or destroyed in O(1),
 * which avoids the bookkeeping and fragmentation costs of malloc/free for
 * short-lived, high-churn allocations.
 *
 * The arena grows by allocating additional blocks ("chunks") on demand and
 * links them into a singly linked list; nothing already handed out ever
 * moves, so pointers returned by ember_arena_alloc remain valid until the
 * arena is reset or destroyed.
 */
#ifndef EMBER_ARENA_H
#define EMBER_ARENA_H

#include <stddef.h>

typedef struct ember_arena_block {
    struct ember_arena_block *next;
    size_t capacity;
    size_t used;
    /* raw bytes follow this header (flexible array member) */
    unsigned char data[];
} ember_arena_block;

typedef struct ember_arena {
    ember_arena_block *head;   /* block we are currently bumping into */
    ember_arena_block *first;  /* first block ever allocated, for reset/free */
    size_t default_block_size;
    size_t total_allocated;    /* bytes handed out to callers, for stats */
    size_t total_capacity;     /* bytes reserved from the OS across all blocks */
} ember_arena;

/* Initializes an arena. default_block_size is the size of each underlying
 * chunk (a sensible default is used if 0 is passed). Returns EMBER_OK or
 * EMBER_ERR_OOM. */
int ember_arena_init(ember_arena *arena, size_t default_block_size);

/* Allocates `size` bytes with `alignment` (must be a power of two) from the
 * arena. Returns NULL on allocation failure. */
void *ember_arena_alloc_aligned(ember_arena *arena, size_t size, size_t alignment);

/* Convenience wrapper aligned to sizeof(void*). */
void *ember_arena_alloc(ember_arena *arena, size_t size);

/* Releases all blocks back to the allocator but keeps `first` around so the
 * arena can be reused. */
void ember_arena_reset(ember_arena *arena);

/* Frees every block and zeroes the arena struct. */
void ember_arena_destroy(ember_arena *arena);

size_t ember_arena_bytes_used(const ember_arena *arena);
size_t ember_arena_bytes_reserved(const ember_arena *arena);

#endif /* EMBER_ARENA_H */
