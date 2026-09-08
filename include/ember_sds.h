/*
 * ember_sds.h - Simple Dynamic String.
 *
 * A heap-allocated, length-prefixed byte buffer. Unlike a plain
 * NUL-terminated char*, an sds tracks its length and capacity explicitly,
 * which makes appends amortized O(1) and lets values contain embedded NUL
 * bytes safely (needed because our wire protocol is binary-safe, just like
 * the values a KV store has to hold).
 *
 * The handle returned to callers is a plain `char *` pointing just past a
 * hidden header, so it can still be passed to things like memcpy/strlen
 * when the caller knows the content has no embedded NULs. This mirrors the
 * classic "sds" trick used by real production C string libraries.
 */
#ifndef EMBER_SDS_H
#define EMBER_SDS_H

#include <stddef.h>

typedef char *sds;

typedef struct {
    size_t len;    /* bytes of actual content, excluding the NUL terminator */
    size_t cap;    /* bytes allocated for `buf`, excluding the header/NUL */
    char buf[];
} sds_header;

/* Creates a new sds copying `init_len` bytes from `init` (may be NULL to
 * create an empty buffer of that capacity). Always NUL-terminates for
 * convenience with C string APIs, even though len is tracked separately. */
sds sds_new_len(const void *init, size_t init_len);
sds sds_new(const char *init_cstr);
sds sds_empty(void);

void sds_free(sds s);

size_t sds_len(const sds s);
size_t sds_avail(const sds s);

/* Appends `len` bytes to `s`, growing the underlying buffer as needed.
 * Returns the (possibly reallocated) sds; the old handle must not be used
 * afterward. Returns NULL on allocation failure (original `s` is left
 * untouched in that case). */
sds sds_cat_len(sds s, const void *data, size_t len);
sds sds_cat(sds s, const char *cstr);
sds sds_cat_sds(sds s, const sds other);

/* Replaces the contents of s with a formatted string, like sprintf but
 * growing the buffer as needed. */
sds sds_cat_printf(sds s, const char *fmt, ...);

sds sds_dup(const sds s);
void sds_clear(sds s);

/* Removes the first `n` bytes in place, shifting the remainder left. Used
 * by the connection read loop to drop a command's bytes off the front of
 * the input buffer once the protocol parser has consumed them. */
void sds_advance(sds s, size_t n);

int sds_cmp(const sds a, const sds b);

#endif /* EMBER_SDS_H */
