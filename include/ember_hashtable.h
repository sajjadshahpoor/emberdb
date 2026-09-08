/*
 * ember_hashtable.h - open-addressing hash table using Robin Hood hashing.
 *
 * Standard open addressing with linear probing suffers from long probe
 * chains once the table starts filling up, because a bucket "far from
 * home" (many collisions) sits right next to a bucket that could have
 * been placed there directly. Robin Hood hashing fixes this by tracking
 * each entry's "distance from its ideal bucket" (DIB) and, during
 * insertion, swapping a new entry into a slot if the entry sitting there
 * has a *smaller* DIB (i.e. it is "richer" / closer to home than the one
 * being inserted). This bounds the worst-case probe length to roughly the
 * average, and keeps probe-length variance low, which is exactly the
 * property you want for predictable p99 latency in a KV store.
 *
 * Deletion uses backward-shift deletion instead of tombstones: when an
 * entry is removed, subsequent entries in the probe chain are shifted back
 * by one slot (decrementing their DIB) until an entry with DIB 0 or an
 * empty slot is reached. This keeps the table tombstone-free, which keeps
 * lookups fast indefinitely even under heavy churn (delete-heavy
 * workloads), unlike naive open addressing where tombstones accumulate and
 * degrade probe lengths over time.
 *
 * Keys are arbitrary byte strings (binary-safe, not required to be
 * NUL-terminated) and are copied into the table. Values are opaque
 * pointers owned by the caller.
 */
#ifndef EMBER_HASHTABLE_H
#define EMBER_HASHTABLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint64_t hash;
    char *key;
    size_t keylen;
    void *value;
    int32_t dib; /* -1 means the slot is empty */
} ember_ht_entry;

typedef struct ember_hashtable {
    ember_ht_entry *entries;
    size_t capacity;   /* always a power of two */
    size_t count;
} ember_hashtable;

typedef struct {
    const ember_hashtable *ht;
    size_t index;
} ember_ht_iter;

ember_hashtable *ember_hashtable_create(size_t initial_capacity);
void ember_hashtable_destroy(ember_hashtable *ht, void (*free_value)(void *));

/* Inserts or overwrites `key`. If a value already existed for this key, it
 * is returned via *old_value (if old_value is non-NULL) so the caller can
 * decide how to free it; the hash table never inspects or frees values. */
bool ember_hashtable_set(ember_hashtable *ht, const char *key, size_t keylen,
                          void *value, void **old_value);

bool ember_hashtable_get(const ember_hashtable *ht, const char *key, size_t keylen,
                          void **out_value);

bool ember_hashtable_contains(const ember_hashtable *ht, const char *key, size_t keylen);

/* Removes `key`. If found, *old_value (when non-NULL) receives the value
 * so the caller can free it. Returns whether the key was present. */
bool ember_hashtable_delete(ember_hashtable *ht, const char *key, size_t keylen,
                             void **old_value);

size_t ember_hashtable_size(const ember_hashtable *ht);

/* Removes every entry, invoking free_value on each value if non-NULL. */
void ember_hashtable_clear(ember_hashtable *ht, void (*free_value)(void *));

/* Iteration in unspecified order, safe to use for KEYS/SCAN-style commands.
 * Do not mutate the table while iterating. */
ember_ht_iter ember_hashtable_iter(const ember_hashtable *ht);
bool ember_ht_iter_next(ember_ht_iter *it, const char **key, size_t *keylen, void **value);

uint64_t ember_hash_bytes(const void *data, size_t len);

#endif /* EMBER_HASHTABLE_H */
