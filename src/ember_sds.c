#include "ember_sds.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDS_HDR(s) ((sds_header *)((s) - offsetof(sds_header, buf)))

static sds sds_from_header(sds_header *hdr) {
    return hdr->buf;
}

sds sds_new_len(const void *init, size_t init_len) {
    sds_header *hdr = malloc(sizeof(sds_header) + init_len + 1);
    if (!hdr) return NULL;

    hdr->len = init_len;
    hdr->cap = init_len;

    if (init_len > 0) {
        if (init) {
            memcpy(hdr->buf, init, init_len);
        } else {
            memset(hdr->buf, 0, init_len);
        }
    }
    hdr->buf[init_len] = '\0';
    return sds_from_header(hdr);
}

sds sds_new(const char *init_cstr) {
    return sds_new_len(init_cstr, init_cstr ? strlen(init_cstr) : 0);
}

sds sds_empty(void) {
    return sds_new_len(NULL, 0);
}

void sds_free(sds s) {
    if (!s) return;
    free(SDS_HDR(s));
}

size_t sds_len(const sds s) {
    return SDS_HDR(s)->len;
}

size_t sds_avail(const sds s) {
    sds_header *hdr = SDS_HDR(s);
    return hdr->cap - hdr->len;
}

/* Grows `s` so it has room for at least `addlen` more bytes, over-allocating
 * to amortize future growth (doubling below 1MiB, linear +1MiB above that,
 * the same growth policy used by most production sds implementations). */
static sds sds_make_room_for(sds s, size_t addlen) {
    sds_header *hdr = SDS_HDR(s);
    if (hdr->cap - hdr->len >= addlen) return s;

    size_t needed = hdr->len + addlen;
    size_t new_cap;
    if (needed < 1024 * 1024) {
        new_cap = hdr->cap == 0 ? needed : hdr->cap * 2;
        if (new_cap < needed) new_cap = needed;
    } else {
        new_cap = needed + 1024 * 1024;
    }

    sds_header *new_hdr = realloc(hdr, sizeof(sds_header) + new_cap + 1);
    if (!new_hdr) return NULL;

    new_hdr->cap = new_cap;
    return sds_from_header(new_hdr);
}

sds sds_cat_len(sds s, const void *data, size_t len) {
    if (len == 0) return s;

    sds new_s = sds_make_room_for(s, len);
    if (!new_s) return NULL;

    sds_header *hdr = SDS_HDR(new_s);
    memcpy(hdr->buf + hdr->len, data, len);
    hdr->len += len;
    hdr->buf[hdr->len] = '\0';
    return new_s;
}

sds sds_cat(sds s, const char *cstr) {
    return sds_cat_len(s, cstr, strlen(cstr));
}

sds sds_cat_sds(sds s, const sds other) {
    return sds_cat_len(s, other, sds_len(other));
}

sds sds_cat_printf(sds s, const char *fmt, ...) {
    va_list args, args_copy;
    va_start(args, fmt);
    va_copy(args_copy, args);

    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) {
        va_end(args_copy);
        return NULL;
    }

    sds new_s = sds_make_room_for(s, (size_t)needed);
    if (!new_s) {
        va_end(args_copy);
        return NULL;
    }

    sds_header *hdr = SDS_HDR(new_s);
    vsnprintf(hdr->buf + hdr->len, (size_t)needed + 1, fmt, args_copy);
    va_end(args_copy);

    hdr->len += (size_t)needed;
    return new_s;
}

sds sds_dup(const sds s) {
    return sds_new_len(s, sds_len(s));
}

void sds_clear(sds s) {
    sds_header *hdr = SDS_HDR(s);
    hdr->len = 0;
    hdr->buf[0] = '\0';
}

int sds_cmp(const sds a, const sds b) {
    size_t la = sds_len(a), lb = sds_len(b);
    size_t minlen = la < lb ? la : lb;
    int cmp = memcmp(a, b, minlen);
    if (cmp != 0) return cmp;
    if (la < lb) return -1;
    if (la > lb) return 1;
    return 0;
}
