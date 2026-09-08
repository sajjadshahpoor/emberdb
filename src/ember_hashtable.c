#include "ember_hashtable.h"

#include <stdlib.h>
#include <string.h>

#define EMBER_HT_MIN_CAPACITY 8
#define EMBER_HT_MAX_LOAD_NUM 9   /* resize when count * 10 >= capacity * 9 */
#define EMBER_HT_MAX_LOAD_DEN 10

uint64_t ember_hash_bytes(const void *data, size_t len) {
    /* FNV-1a: simple, fast, and good enough avalanche behavior for a hash
     * table (we are not defending against adversarial hash-flooding here;
     * that would call for SipHash with a random per-process seed). */
    const unsigned char *bytes = data;
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= bytes[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static size_t round_up_pow2(size_t n) {
    size_t p = EMBER_HT_MIN_CAPACITY;
    while (p < n) p <<= 1;
    return p;
}

static void init_entries(ember_ht_entry *entries, size_t capacity) {
    for (size_t i = 0; i < capacity; i++) {
        entries[i].dib = -1;
    }
}

ember_hashtable *ember_hashtable_create(size_t initial_capacity) {
    ember_hashtable *ht = malloc(sizeof(ember_hashtable));
    if (!ht) return NULL;

    size_t capacity = round_up_pow2(initial_capacity ? initial_capacity : EMBER_HT_MIN_CAPACITY);
    ht->entries = malloc(sizeof(ember_ht_entry) * capacity);
    if (!ht->entries) {
        free(ht);
        return NULL;
    }
    init_entries(ht->entries, capacity);
    ht->capacity = capacity;
    ht->count = 0;
    return ht;
}

void ember_hashtable_destroy(ember_hashtable *ht, void (*free_value)(void *)) {
    if (!ht) return;
    for (size_t i = 0; i < ht->capacity; i++) {
        if (ht->entries[i].dib != -1) {
            free(ht->entries[i].key);
            if (free_value) free_value(ht->entries[i].value);
        }
    }
    free(ht->entries);
    free(ht);
}

/* Classic Robin Hood insertion: walk the probe sequence, and whenever the
 * slot in hand is "richer" (smaller dib) than the entry being carried,
 * swap them so the richer entry stays put and the poorer one keeps
 * looking. This is what bounds worst-case probe length. */
static void robin_hood_insert(ember_hashtable *ht, ember_ht_entry incoming) {
    size_t mask = ht->capacity - 1;
    size_t idx = incoming.hash & mask;
    incoming.dib = 0;

    for (;;) {
        ember_ht_entry *slot = &ht->entries[idx];
        if (slot->dib == -1) {
            *slot = incoming;
            return;
        }
        if (slot->dib < incoming.dib) {
            ember_ht_entry tmp = *slot;
            *slot = incoming;
            incoming = tmp;
        }
        idx = (idx + 1) & mask;
        incoming.dib++;
    }
}

static void ember_hashtable_grow(ember_hashtable *ht) {
    size_t old_capacity = ht->capacity;
    ember_ht_entry *old_entries = ht->entries;

    size_t new_capacity = old_capacity * 2;
    ember_ht_entry *new_entries = malloc(sizeof(ember_ht_entry) * new_capacity);
    if (!new_entries) return; /* keep operating at the old (higher) load factor */
    init_entries(new_entries, new_capacity);

    ht->entries = new_entries;
    ht->capacity = new_capacity;

    for (size_t i = 0; i < old_capacity; i++) {
        if (old_entries[i].dib != -1) {
            robin_hood_insert(ht, old_entries[i]);
        }
    }
    free(old_entries);
}

static bool needs_grow(const ember_hashtable *ht) {
    return (ht->count + 1) * EMBER_HT_MAX_LOAD_DEN >= ht->capacity * EMBER_HT_MAX_LOAD_NUM;
}

/* Shared lookup used by get/set/delete/contains. Robin Hood hashing lets
 * lookups stop early: along a probe sequence, dib values never "dip" below
 * the number of probes taken so far unless the key is absent, so seeing a
 * slot with dib < the current probe count proves the key can't be further
 * ahead. */
static bool find_slot(const ember_hashtable *ht, const char *key, size_t keylen,
                       uint64_t hash, size_t *out_index) {
    size_t mask = ht->capacity - 1;
    size_t idx = hash & mask;
    int32_t dib = 0;

    while (ht->entries[idx].dib != -1 && ht->entries[idx].dib >= dib) {
        const ember_ht_entry *e = &ht->entries[idx];
        if (e->hash == hash && e->keylen == keylen && memcmp(e->key, key, keylen) == 0) {
            *out_index = idx;
            return true;
        }
        idx = (idx + 1) & mask;
        dib++;
    }
    return false;
}

bool ember_hashtable_set(ember_hashtable *ht, const char *key, size_t keylen,
                          void *value, void **old_value) {
    uint64_t hash = ember_hash_bytes(key, keylen);

    size_t idx;
    if (find_slot(ht, key, keylen, hash, &idx)) {
        if (old_value) *old_value = ht->entries[idx].value;
        ht->entries[idx].value = value;
        return true;
    }

    if (needs_grow(ht)) {
        ember_hashtable_grow(ht);
    }

    char *key_copy = malloc(keylen + 1);
    if (!key_copy) return false;
    memcpy(key_copy, key, keylen);
    key_copy[keylen] = '\0';

    ember_ht_entry incoming = {
        .hash = hash,
        .key = key_copy,
        .keylen = keylen,
        .value = value,
        .dib = 0,
    };
    robin_hood_insert(ht, incoming);
    ht->count++;
    if (old_value) *old_value = NULL;
    return true;
}

bool ember_hashtable_get(const ember_hashtable *ht, const char *key, size_t keylen,
                          void **out_value) {
    uint64_t hash = ember_hash_bytes(key, keylen);
    size_t idx;
    if (!find_slot(ht, key, keylen, hash, &idx)) return false;
    if (out_value) *out_value = ht->entries[idx].value;
    return true;
}

bool ember_hashtable_contains(const ember_hashtable *ht, const char *key, size_t keylen) {
    return ember_hashtable_get(ht, key, keylen, NULL);
}

bool ember_hashtable_delete(ember_hashtable *ht, const char *key, size_t keylen,
                             void **old_value) {
    uint64_t hash = ember_hash_bytes(key, keylen);
    size_t idx;
    if (!find_slot(ht, key, keylen, hash, &idx)) return false;

    if (old_value) *old_value = ht->entries[idx].value;
    free(ht->entries[idx].key);

    size_t mask = ht->capacity - 1;
    size_t i = idx;
    for (;;) {
        size_t next = (i + 1) & mask;
        if (ht->entries[next].dib <= 0) {
            /* Next slot is empty, or already at its ideal bucket: nothing
             * left to shift back, so this is where the hole ends. */
            ht->entries[i].dib = -1;
            break;
        }
        ht->entries[i] = ht->entries[next];
        ht->entries[i].dib -= 1;
        i = next;
    }

    ht->count--;
    return true;
}

size_t ember_hashtable_size(const ember_hashtable *ht) {
    return ht->count;
}

void ember_hashtable_clear(ember_hashtable *ht, void (*free_value)(void *)) {
    for (size_t i = 0; i < ht->capacity; i++) {
        if (ht->entries[i].dib != -1) {
            free(ht->entries[i].key);
            if (free_value) free_value(ht->entries[i].value);
            ht->entries[i].dib = -1;
        }
    }
    ht->count = 0;
}

ember_ht_iter ember_hashtable_iter(const ember_hashtable *ht) {
    ember_ht_iter it = { .ht = ht, .index = 0 };
    return it;
}

bool ember_ht_iter_next(ember_ht_iter *it, const char **key, size_t *keylen, void **value) {
    while (it->index < it->ht->capacity) {
        const ember_ht_entry *e = &it->ht->entries[it->index];
        it->index++;
        if (e->dib != -1) {
            if (key) *key = e->key;
            if (keylen) *keylen = e->keylen;
            if (value) *value = e->value;
            return true;
        }
    }
    return false;
}
