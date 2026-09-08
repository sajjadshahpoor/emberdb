/*
 * ember_protocol.h - RESP2 (REdis Serialization Protocol) parser and
 * reply serializer.
 *
 * EmberDB speaks the same wire protocol as Redis, which means the
 * standard `redis-cli` (and any RESP client library) can talk to it
 * without modification. RESP requests are sent as an array of bulk
 * strings, e.g. `SET foo bar` on the wire is:
 *
 *   *3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
 *
 * A plain-text "inline command" form (`SET foo bar\r\n`, no framing) is
 * also accepted, which is what lets you talk to the server with a bare
 * `nc`/telnet for quick manual testing.
 *
 * Because TCP is a byte stream, a single read() can hand back half of a
 * command (or several commands back to back). ember_protocol_parse takes
 * whatever has been buffered so far and either:
 *   - returns EMBER_OK having parsed exactly one command and told the
 *     caller how many bytes it consumed, or
 *   - returns EMBER_ERR_AGAIN, meaning "not enough bytes yet, buffer more
 *     and call again" (nothing is consumed in this case), or
 *   - returns EMBER_ERR_PROTOCOL for malformed input, at which point the
 *     connection should be dropped.
 *
 * The parser re-scans from the start of the buffered data on every call
 * rather than resuming a suspended state machine. That is simpler to get
 * right and fast enough for a single in-flight command per connection,
 * at the cost of doing a little redundant work if a command straddles
 * many small reads.
 */
#ifndef EMBER_PROTOCOL_H
#define EMBER_PROTOCOL_H

#include "ember_sds.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char **argv;
    size_t *argvlen;
    int argc;
} ember_command;

/* Attempts to parse exactly one command from buf[0..buflen). On success
 * (EMBER_OK), *out is populated (caller must ember_command_free it) and
 * *consumed is how many bytes of buf that command occupied. */
int ember_protocol_parse(const char *buf, size_t buflen, ember_command *out,
                          size_t *consumed);

void ember_command_free(ember_command *cmd);

/* Reply serialization: each function appends to `buf` (an sds owned by the
 * caller) and returns the (possibly reallocated) sds, following the same
 * ownership convention as ember_sds's sds_cat_* functions. Returns NULL on
 * allocation failure, leaving the original `buf` untouched. */
sds ember_reply_simple_string(sds buf, const char *s);
sds ember_reply_error(sds buf, const char *s);
sds ember_reply_integer(sds buf, int64_t value);
sds ember_reply_bulk_string(sds buf, const char *data, size_t len);
sds ember_reply_null_bulk(sds buf);
sds ember_reply_array_header(sds buf, int64_t count);

#endif /* EMBER_PROTOCOL_H */
