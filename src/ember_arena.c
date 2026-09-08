#include "ember_arena.h"
#include "ember_common.h"

#include <stdlib.h>
#include <string.h>

#define EMBER_ARENA_DEFAULT_BLOCK_SIZE (64 * 1024)

static size_t align_up(size_t n, size_t alignment) {
    return (n + (alignment - 1)) & ~(alignment - 1);
}

static ember_arena_block *block_new(size_t capacity) {
    ember_arena_block *block = malloc(sizeof(ember_arena_block) + capacity);
    if (!block) return NULL;
    block->next = NULL;
    block->capacity = capacity;
    block->used = 0;
    return block;
}

int ember_arena_init(ember_arena *arena, size_t default_block_size) {
    if (default_block_size == 0) default_block_size = EMBER_ARENA_DEFAULT_BLOCK_SIZE;

    ember_arena_block *block = block_new(default_block_size);
    if (!block) return EMBER_ERR_OOM;

    arena->head = block;
    arena->first = block;
    arena->default_block_size = default_block_size;
    arena->total_allocated = 0;
    arena->total_capacity = default_block_size;
    return EMBER_OK;
}

void *ember_arena_alloc_aligned(ember_arena *arena, size_t size, size_t alignment) {
    EMBER_ASSERT((alignment & (alignment - 1)) == 0, "alignment must be a power of two");

    ember_arena_block *block = arena->head;
    size_t aligned_offset = align_up(block->used, alignment);

    if (aligned_offset + size > block->capacity) {
        /* Doesn't fit in the current block. Allocate a new one big enough
         * for this request (at least the default block size, so a stream
         * of small allocations keeps amortized cost low). */
        size_t new_capacity = EMBER_MAX(arena->default_block_size, size + alignment);
        ember_arena_block *new_block = block_new(new_capacity);
        if (!new_block) return NULL;

        new_block->next = arena->head;
        arena->head = new_block;
        arena->total_capacity += new_capacity;

        block = new_block;
        aligned_offset = align_up(block->used, alignment);
    }

    void *ptr = block->data + aligned_offset;
    block->used = aligned_offset + size;
    arena->total_allocated += size;
    return ptr;
}

void *ember_arena_alloc(ember_arena *arena, size_t size) {
    return ember_arena_alloc_aligned(arena, size, sizeof(void *));
}

void ember_arena_reset(ember_arena *arena) {
    /* Free every block except the first, then rewind the first block's
     * bump pointer so the arena can be reused without touching the OS
     * allocator again for the common case. */
    ember_arena_block *block = arena->head;
    while (block && block != arena->first) {
        ember_arena_block *next = block->next;
        free(block);
        block = next;
    }

    arena->first->used = 0;
    arena->first->next = NULL;
    arena->head = arena->first;
    arena->total_allocated = 0;
    arena->total_capacity = arena->first->capacity;
}

void ember_arena_destroy(ember_arena *arena) {
    ember_arena_block *block = arena->head;
    while (block) {
        ember_arena_block *next = block->next;
        free(block);
        block = next;
    }
    memset(arena, 0, sizeof(*arena));
}

size_t ember_arena_bytes_used(const ember_arena *arena) {
    return arena->total_allocated;
}

size_t ember_arena_bytes_reserved(const ember_arena *arena) {
    return arena->total_capacity;
}
