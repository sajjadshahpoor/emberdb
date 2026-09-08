/*
 * ember_db.h - the keyspace: hash table + TTL semantics + expiration.
 *
 * Expiration follows the same two-pronged strategy Redis uses:
 *  - Lazy: every read/write checks the target key's expiry and evicts it
 *    on the spot if it has passed, so a client never observes a stale
 *    value even if the background sweep hasn't gotten to it yet.
 *  - Active: ember_db_active_expire_cycle() is called periodically (from
 *    the event loop's timer, off the back of the thread pool) and walks a
 *    bounded number of buckets per call using a persisted cursor, so
 *    memory used by expired keys that are never touched again is still
 *    reclaimed without ever pausing to scan the whole table at once.
 */
#ifndef EMBER_DB_H
#define EMBER_DB_H

#include "ember_common.h"
#include "ember_hashtable.h"
#include "ember_object.h"

#include <stdint.h>

typedef struct ember_db {
    ember_hashtable *dict;
    size_t expire_cursor; /* bucket index for the active expire cycle */
} ember_db;

ember_db *ember_db_create(void);
void ember_db_destroy(ember_db *db);

/* Sets key to a copy of data[0..len). If ttl_ms > 0 the key expires that
 * many milliseconds from now; ttl_ms == 0 means no expiry. */
int ember_db_set(ember_db *db, const char *key, size_t keylen,
                  const void *data, size_t len, int64_t ttl_ms);

/* On success writes out_value/out_len describing the value's bytes; the
 * pointer is owned by the store and only valid until the next mutation of
 * this key. Returns EMBER_ERR_NOTFOUND if absent or expired. */
int ember_db_get(ember_db *db, const char *key, size_t keylen,
                  const char **out_value, size_t *out_len);

bool ember_db_exists(ember_db *db, const char *key, size_t keylen);
bool ember_db_del(ember_db *db, const char *key, size_t keylen);

/* Attaches a TTL to an existing key. Returns false if the key doesn't exist. */
bool ember_db_expire(ember_db *db, const char *key, size_t keylen, int64_t ttl_ms);
bool ember_db_persist(ember_db *db, const char *key, size_t keylen);

/* Returns remaining TTL in milliseconds, 0 if no TTL, or EMBER_ERR_NOTFOUND. */
int64_t ember_db_ttl_ms(ember_db *db, const char *key, size_t keylen);

/* Parses the current value as a base-10 int64, adds `delta`, stores the
 * result back as a string, and returns it via *out_result.
 * EMBER_ERR_TYPE if the existing value isn't a valid integer. A missing
 * key is treated as 0 before applying the delta, like Redis's INCR. */
int ember_db_incrby(ember_db *db, const char *key, size_t keylen, int64_t delta,
                     int64_t *out_result);

int ember_db_append(ember_db *db, const char *key, size_t keylen,
                     const void *data, size_t len, size_t *out_newlen);

int ember_db_strlen(ember_db *db, const char *key, size_t keylen, size_t *out_len);

void ember_db_flushall(ember_db *db);
size_t ember_db_size(ember_db *db);

/* Bounded active-expiration pass: examines up to `sample_size` buckets
 * starting at the persisted cursor, evicting any expired key found, and
 * returns how many keys it actually evicted. */
size_t ember_db_active_expire_cycle(ember_db *db, size_t sample_size);

/* Iteration helper for commands like KEYS: skips expired entries lazily
 * (evicting them) as it walks. Do not mutate the db while iterating. */
typedef struct {
    ember_ht_iter ht_iter;
    ember_db *db;
} ember_db_iter;

ember_db_iter ember_db_iter_start(ember_db *db);
bool ember_db_iter_next(ember_db_iter *it, const char **key, size_t *keylen);

#endif /* EMBER_DB_H */
