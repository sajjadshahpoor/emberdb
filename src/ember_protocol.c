#include "ember_protocol.h"
#include "ember_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Finds the next "\r\n" at or after `start`. Returns its index, or -1 if
 * not present in buf[0..buflen). */
static long find_crlf(const char *buf, size_t buflen, size_t start) {
    for (size_t i = start; i + 1 < buflen; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n') return (long)i;
    }
    return -1;
}

static bool parse_int_strict(const char *s, size_t len, int64_t *out) {
    if (len == 0) return false;
    size_t i = 0;
    bool negative = false;
    if (s[0] == '-') {
        negative = true;
        i = 1;
        if (len == 1) return false;
    }
    int64_t value = 0;
    for (; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        value = value * 10 + (s[i] - '0');
    }
    *out = negative ? -value : value;
    return true;
}

static void free_argv(char **argv, size_t *argvlen, int built) {
    for (int i = 0; i < built; i++) free(argv[i]);
    free(argv);
    free(argvlen);
}

static int parse_multibulk(const char *buf, size_t buflen, ember_command *out,
                            size_t *consumed) {
    long crlf = find_crlf(buf, buflen, 0);
    if (crlf < 0) return EMBER_ERR_AGAIN;

    int64_t num_args;
    if (!parse_int_strict(buf + 1, (size_t)crlf - 1, &num_args) || num_args < 0 || num_args > 1024 * 1024) {
        return EMBER_ERR_PROTOCOL;
    }

    size_t pos = (size_t)crlf + 2;

    if (num_args == 0) {
        out->argv = NULL;
        out->argvlen = NULL;
        out->argc = 0;
        *consumed = pos;
        return EMBER_OK;
    }

    char **argv = malloc(sizeof(char *) * (size_t)num_args);
    size_t *argvlen = malloc(sizeof(size_t) * (size_t)num_args);
    if (!argv || !argvlen) {
        free(argv);
        free(argvlen);
        return EMBER_ERR_OOM;
    }

    int built = 0;
    for (int64_t i = 0; i < num_args; i++) {
        if (pos >= buflen || buf[pos] != '$') {
            if (pos >= buflen) {
                free_argv(argv, argvlen, built);
                return EMBER_ERR_AGAIN;
            }
            free_argv(argv, argvlen, built);
            return EMBER_ERR_PROTOCOL;
        }

        long len_crlf = find_crlf(buf, buflen, pos);
        if (len_crlf < 0) {
            free_argv(argv, argvlen, built);
            return EMBER_ERR_AGAIN;
        }

        int64_t len;
        if (!parse_int_strict(buf + pos + 1, (size_t)len_crlf - pos - 1, &len) || len < 0 || len > 512 * 1024 * 1024) {
            free_argv(argv, argvlen, built);
            return EMBER_ERR_PROTOCOL;
        }

        size_t data_start = (size_t)len_crlf + 2;
        size_t data_end = data_start + (size_t)len;
        if (data_end + 2 > buflen) {
            free_argv(argv, argvlen, built);
            return EMBER_ERR_AGAIN;
        }
        if (buf[data_end] != '\r' || buf[data_end + 1] != '\n') {
            free_argv(argv, argvlen, built);
            return EMBER_ERR_PROTOCOL;
        }

        char *arg = malloc((size_t)len + 1);
        if (!arg) {
            free_argv(argv, argvlen, built);
            return EMBER_ERR_OOM;
        }
        memcpy(arg, buf + data_start, (size_t)len);
        arg[len] = '\0';

        argv[i] = arg;
        argvlen[i] = (size_t)len;
        built++;

        pos = data_end + 2;
    }

    out->argv = argv;
    out->argvlen = argvlen;
    out->argc = (int)num_args;
    *consumed = pos;
    return EMBER_OK;
}

static int parse_inline(const char *buf, size_t buflen, ember_command *out,
                         size_t *consumed) {
    size_t nl = 0;
    bool found = false;
    for (; nl < buflen; nl++) {
        if (buf[nl] == '\n') {
            found = true;
            break;
        }
    }
    if (!found) return EMBER_ERR_AGAIN;

    size_t linelen = nl;
    if (linelen > 0 && buf[linelen - 1] == '\r') linelen--;

    /* Count whitespace-separated tokens first so we can size argv exactly. */
    int argc = 0;
    size_t i = 0;
    while (i < linelen) {
        while (i < linelen && buf[i] == ' ') i++;
        if (i >= linelen) break;
        argc++;
        while (i < linelen && buf[i] != ' ') i++;
    }

    if (argc == 0) {
        out->argv = NULL;
        out->argvlen = NULL;
        out->argc = 0;
        *consumed = nl + 1;
        return EMBER_OK;
    }

    char **argv = malloc(sizeof(char *) * (size_t)argc);
    size_t *argvlen = malloc(sizeof(size_t) * (size_t)argc);
    if (!argv || !argvlen) {
        free(argv);
        free(argvlen);
        return EMBER_ERR_OOM;
    }

    int built = 0;
    i = 0;
    while (i < linelen) {
        while (i < linelen && buf[i] == ' ') i++;
        if (i >= linelen) break;
        size_t start = i;
        while (i < linelen && buf[i] != ' ') i++;
        size_t len = i - start;

        char *arg = malloc(len + 1);
        if (!arg) {
            free_argv(argv, argvlen, built);
            return EMBER_ERR_OOM;
        }
        memcpy(arg, buf + start, len);
        arg[len] = '\0';

        argv[built] = arg;
        argvlen[built] = len;
        built++;
    }

    out->argv = argv;
    out->argvlen = argvlen;
    out->argc = argc;
    *consumed = nl + 1;
    return EMBER_OK;
}

int ember_protocol_parse(const char *buf, size_t buflen, ember_command *out,
                          size_t *consumed) {
    *consumed = 0;
    if (buflen == 0) return EMBER_ERR_AGAIN;

    if (buf[0] == '*') {
        return parse_multibulk(buf, buflen, out, consumed);
    }
    return parse_inline(buf, buflen, out, consumed);
}

void ember_command_free(ember_command *cmd) {
    if (!cmd) return;
    for (int i = 0; i < cmd->argc; i++) free(cmd->argv[i]);
    free(cmd->argv);
    free(cmd->argvlen);
    cmd->argv = NULL;
    cmd->argvlen = NULL;
    cmd->argc = 0;
}

sds ember_reply_simple_string(sds buf, const char *s) {
    buf = sds_cat(buf, "+");
    if (!buf) return NULL;
    buf = sds_cat(buf, s);
    if (!buf) return NULL;
    return sds_cat(buf, "\r\n");
}

sds ember_reply_error(sds buf, const char *s) {
    buf = sds_cat(buf, "-");
    if (!buf) return NULL;
    buf = sds_cat(buf, s);
    if (!buf) return NULL;
    return sds_cat(buf, "\r\n");
}

sds ember_reply_integer(sds buf, int64_t value) {
    return sds_cat_printf(buf, ":%lld\r\n", (long long)value);
}

sds ember_reply_bulk_string(sds buf, const char *data, size_t len) {
    buf = sds_cat_printf(buf, "$%zu\r\n", len);
    if (!buf) return NULL;
    buf = sds_cat_len(buf, data, len);
    if (!buf) return NULL;
    return sds_cat(buf, "\r\n");
}

sds ember_reply_null_bulk(sds buf) {
    return sds_cat(buf, "$-1\r\n");
}

sds ember_reply_array_header(sds buf, int64_t count) {
    return sds_cat_printf(buf, "*%lld\r\n", (long long)count);
}

sds ember_encode_multibulk(char *const *argv, const size_t *argvlen, int argc) {
    sds buf = ember_reply_array_header(sds_empty(), argc);
    if (!buf) return NULL;

    for (int i = 0; i < argc; i++) {
        buf = ember_reply_bulk_string(buf, argv[i], argvlen[i]);
        if (!buf) return NULL;
    }
    return buf;
}
