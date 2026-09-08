#include "ember_db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ember_db *ember_db_create(void) {
    ember_db *db = malloc(sizeof(ember_db));
    if (!db) return NULL;
    db->dict = ember_hashtable_create(0);
    if (!db->dict) {
        free(db);
        return NULL;
    }
    db->expire_cursor = 0;
    return db;
}

void ember_db_destroy(ember_db *db) {
    if (!db) return;
    ember_hashtable_destroy(db->dict, ember_object_free);
    free(db);
}

/* Looks up `key`, lazily evicting it first if it has expired. Returns NULL
 * if the key is absent (or was just evicted for being expired). */
static ember_object *lookup_live(ember_db *db, const char *key, size_t keylen) {
    void *raw = NULL;
    if (!ember_hashtable_get(db->dict, key, keylen, &raw)) return NULL;

    ember_object *obj = raw;
    if (ember_object_is_expired(obj, ember_now_ms())) {
        void *old = NULL;
        ember_hashtable_delete(db->dict, key, keylen, &old);
        ember_object_free(old);
        return NULL;
    }
    return obj;
}

int ember_db_set(ember_db *db, const char *key, size_t keylen,
                  const void *data, size_t len, int64_t ttl_ms) {
    ember_object *obj = ember_object_create(data, len);
    if (!obj) return EMBER_ERR_OOM;
    if (ttl_ms > 0) obj->expire_at_ms = ember_now_ms() + ttl_ms;

    void *old = NULL;
    if (!ember_hashtable_set(db->dict, key, keylen, obj, &old)) {
        ember_object_free(obj);
        return EMBER_ERR_OOM;
    }
    if (old) ember_object_free(old);
    return EMBER_OK;
}

int ember_db_get(ember_db *db, const char *key, size_t keylen,
                  const char **out_value, size_t *out_len) {
    ember_object *obj = lookup_live(db, key, keylen);
    if (!obj) return EMBER_ERR_NOTFOUND;
    *out_value = obj->value;
    *out_len = sds_len(obj->value);
    return EMBER_OK;
}

bool ember_db_exists(ember_db *db, const char *key, size_t keylen) {
    return lookup_live(db, key, keylen) != NULL;
}

bool ember_db_del(ember_db *db, const char *key, size_t keylen) {
    /* lookup_live evicts if expired and returns NULL in that case, which
     * correctly makes DEL on an already-expired key report "not found". */
    if (!lookup_live(db, key, keylen)) return false;

    void *old = NULL;
    ember_hashtable_delete(db->dict, key, keylen, &old);
    ember_object_free(old);
    return true;
}

bool ember_db_expire(ember_db *db, const char *key, size_t keylen, int64_t ttl_ms) {
    ember_object *obj = lookup_live(db, key, keylen);
    if (!obj) return false;
    obj->expire_at_ms = ttl_ms > 0 ? ember_now_ms() + ttl_ms : 0;
    return true;
}

bool ember_db_persist(ember_db *db, const char *key, size_t keylen) {
    ember_object *obj = lookup_live(db, key, keylen);
    if (!obj) return false;
    obj->expire_at_ms = 0;
    return true;
}

int64_t ember_db_ttl_ms(ember_db *db, const char *key, size_t keylen) {
    ember_object *obj = lookup_live(db, key, keylen);
    if (!obj) return EMBER_ERR_NOTFOUND;
    if (obj->expire_at_ms == 0) return 0;
    int64_t remaining = obj->expire_at_ms - ember_now_ms();
    return remaining > 0 ? remaining : 0;
}

int ember_db_incrby(ember_db *db, const char *key, size_t keylen, int64_t delta,
                     int64_t *out_result) {
    ember_object *obj = lookup_live(db, key, keylen);
    int64_t current = 0;

    if (obj) {
        const char *s = obj->value;
        size_t len = sds_len(obj->value);
        if (len == 0) return EMBER_ERR_TYPE;

        char *end;
        long long parsed = strtoll(s, &end, 10);
        if (end != s + len) return EMBER_ERR_TYPE;
        current = parsed;
    }

    int64_t result = current + delta;

    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%lld", (long long)result);

    if (obj) {
        sds new_val = sds_new_len(buf, (size_t)n);
        if (!new_val) return EMBER_ERR_OOM;
        sds_free(obj->value);
        obj->value = new_val;
    } else {
        int status = ember_db_set(db, key, keylen, buf, (size_t)n, 0);
        if (status != EMBER_OK) return status;
    }

    *out_result = result;
    return EMBER_OK;
}

int ember_db_append(ember_db *db, const char *key, size_t keylen,
                     const void *data, size_t len, size_t *out_newlen) {
    ember_object *obj = lookup_live(db, key, keylen);
    if (!obj) {
        int status = ember_db_set(db, key, keylen, data, len, 0);
        if (status != EMBER_OK) return status;
        *out_newlen = len;
        return EMBER_OK;
    }

    sds new_val = sds_cat_len(obj->value, data, len);
    if (!new_val) return EMBER_ERR_OOM;
    obj->value = new_val;
    *out_newlen = sds_len(obj->value);
    return EMBER_OK;
}

int ember_db_strlen(ember_db *db, const char *key, size_t keylen, size_t *out_len) {
    ember_object *obj = lookup_live(db, key, keylen);
    if (!obj) {
        *out_len = 0;
        return EMBER_OK;
    }
    *out_len = sds_len(obj->value);
    return EMBER_OK;
}

void ember_db_flushall(ember_db *db) {
    ember_hashtable_clear(db->dict, ember_object_free);
    db->expire_cursor = 0;
}

size_t ember_db_size(ember_db *db) {
    return ember_hashtable_size(db->dict);
}

size_t ember_db_active_expire_cycle(ember_db *db, size_t sample_size) {
    size_t capacity = db->dict->capacity;
    if (capacity == 0) return 0;

    size_t evicted = 0;
    size_t examined = 0;
    int64_t now = ember_now_ms();

    while (examined < sample_size && examined < capacity) {
        if (db->expire_cursor >= capacity) db->expire_cursor = 0;

        ember_ht_entry *entry = &db->dict->entries[db->expire_cursor];
        if (entry->dib != -1) {
            ember_object *obj = entry->value;
            if (ember_object_is_expired(obj, now)) {
                /* Copy the key out before deleting: backward-shift deletion
                 * may move a different entry into this slot, but we only
                 * need the bytes to look the key up once more. */
                char key_copy[256];
                size_t klen = entry->keylen < sizeof(key_copy) ? entry->keylen : sizeof(key_copy);
                memcpy(key_copy, entry->key, klen);

                void *old = NULL;
                ember_hashtable_delete(db->dict, key_copy, klen, &old);
                ember_object_free(old);
                evicted++;
                /* Don't advance the cursor: another entry may now occupy
                 * this slot after the shift, so re-examine it next time. */
            } else {
                db->expire_cursor++;
            }
        } else {
            db->expire_cursor++;
        }
        examined++;
    }

    return evicted;
}

ember_db_iter ember_db_iter_start(ember_db *db) {
    ember_db_iter it = { .ht_iter = ember_hashtable_iter(db->dict), .db = db };
    return it;
}

bool ember_db_iter_next(ember_db_iter *it, const char **key, size_t *keylen) {
    const char *k;
    size_t klen;
    void *v;

    while (ember_ht_iter_next(&it->ht_iter, &k, &klen, &v)) {
        ember_object *obj = v;
        if (!ember_object_is_expired(obj, ember_now_ms())) {
            *key = k;
            *keylen = klen;
            return true;
        }
        /* Expired: skip it for this iteration. We deliberately don't evict
         * here to avoid mutating the table mid-scan; the active expire
         * cycle will reclaim it shortly. */
    }
    return false;
}
