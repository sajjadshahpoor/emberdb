/*
 * ember_object.h - the value stored for every key.
 *
 * Modeled after Redis's robj: a small wrapper around the actual payload
 * that carries metadata the store needs regardless of what the payload
 * is. Today the only metadata is an optional absolute expiry timestamp,
 * which is what lets TTL/EXPIRE/PERSIST live at the object level instead
 * of needing a second parallel hash table keyed by the same string.
 */
#ifndef EMBER_OBJECT_H
#define EMBER_OBJECT_H

#include "ember_sds.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct ember_object {
    sds value;
    int64_t expire_at_ms; /* 0 means "no expiry" */
} ember_object;

ember_object *ember_object_create(const void *data, size_t len);
void ember_object_free(void *obj); /* void* signature so it plugs directly
                                       into ember_hashtable's free_value */

bool ember_object_is_expired(const ember_object *obj, int64_t now_ms);

int64_t ember_now_ms(void);

#endif /* EMBER_OBJECT_H */
