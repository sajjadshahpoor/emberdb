#include "ember_persist.h"
#include "ember_commands.h"
#include "ember_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct ember_aof {
    int fd;
    atomic_bool dirty;
};

ember_aof *ember_aof_open(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return NULL;

    ember_aof *aof = malloc(sizeof(ember_aof));
    if (!aof) {
        close(fd);
        return NULL;
    }
    aof->fd = fd;
    atomic_init(&aof->dirty, false);
    return aof;
}

void ember_aof_close(ember_aof *aof) {
    if (!aof) return;
    close(aof->fd);
    free(aof);
}

int ember_aof_append_command(ember_aof *aof, const ember_command *cmd) {
    sds encoded = ember_encode_multibulk(cmd->argv, cmd->argvlen, cmd->argc);
    if (!encoded) return EMBER_ERR_OOM;

    size_t len = sds_len(encoded);
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(aof->fd, encoded + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            sds_free(encoded);
            return EMBER_ERR_IO;
        }
        written += (size_t)n;
    }
    sds_free(encoded);
    atomic_store(&aof->dirty, true);
    return EMBER_OK;
}

void ember_aof_fsync_job(void *arg) {
    ember_aof *aof = arg;
    if (atomic_exchange(&aof->dirty, false)) {
        fsync(aof->fd);
    }
}

int ember_aof_load(const char *path, ember_db *db) {
    FILE *f = fopen(path, "rb");
    if (!f) return EMBER_OK; /* nothing logged yet */

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return EMBER_OK;
    }

    char *data = malloc((size_t)size);
    if (!data) {
        fclose(f);
        return EMBER_ERR_OOM;
    }
    size_t total_read = fread(data, 1, (size_t)size, f);
    fclose(f);

    size_t offset = 0;
    int replayed = 0;
    while (offset < total_read) {
        ember_command cmd;
        size_t consumed;
        int status = ember_protocol_parse(data + offset, total_read - offset, &cmd, &consumed);
        if (status == EMBER_ERR_AGAIN) {
            log_warn("AOF: truncated command at offset %zu, stopping replay (likely a partial "
                     "write during a crash)", offset);
            break;
        }
        if (status != EMBER_OK) {
            log_warn("AOF: corrupt entry at offset %zu, stopping replay", offset);
            break;
        }

        if (cmd.argc > 0) {
            ember_command_apply_for_replay(db, &cmd);
            replayed++;
        }
        ember_command_free(&cmd);
        offset += consumed;
    }

    free(data);
    log_info("AOF: replayed %d command(s) from %s", replayed, path);
    return EMBER_OK;
}

#define EMBER_SNAPSHOT_MAGIC "EMBERDB1"
#define EMBER_SNAPSHOT_EOF_MARK 0xFFFFFFFFu

int ember_snapshot_save(ember_db *db, const char *path) {
    char tmp_path[4096];
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(tmp_path)) return EMBER_ERR_INVALID;

    FILE *f = fopen(tmp_path, "wb");
    if (!f) return EMBER_ERR_IO;

    fwrite(EMBER_SNAPSHOT_MAGIC, 1, 8, f);

    ember_db_iter it = ember_db_iter_start(db);
    const char *key;
    size_t keylen;
    size_t count = 0;

    while (ember_db_iter_next(&it, &key, &keylen)) {
        const char *val;
        size_t vallen;
        if (ember_db_get(db, key, keylen, &val, &vallen) != EMBER_OK) continue;

        int64_t ttl_remaining_ms = ember_db_ttl_ms(db, key, keylen);
        if (ttl_remaining_ms == EMBER_ERR_NOTFOUND) continue; /* raced an expiry mid-scan */

        uint32_t klen32 = (uint32_t)keylen;
        uint32_t vlen32 = (uint32_t)vallen;
        fwrite(&klen32, sizeof(klen32), 1, f);
        fwrite(key, 1, keylen, f);
        fwrite(&vlen32, sizeof(vlen32), 1, f);
        fwrite(val, 1, vallen, f);
        fwrite(&ttl_remaining_ms, sizeof(ttl_remaining_ms), 1, f);
        count++;
    }

    uint32_t eof_mark = EMBER_SNAPSHOT_EOF_MARK;
    fwrite(&eof_mark, sizeof(eof_mark), 1, f);

    if (ferror(f)) {
        fclose(f);
        remove(tmp_path);
        return EMBER_ERR_IO;
    }
    fclose(f);

    if (rename(tmp_path, path) != 0) return EMBER_ERR_IO;

    log_info("SAVE: wrote %zu key(s) to %s", count, path);
    return EMBER_OK;
}

int ember_snapshot_load(const char *path, ember_db *db) {
    FILE *f = fopen(path, "rb");
    if (!f) return EMBER_OK; /* no snapshot yet */

    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, EMBER_SNAPSHOT_MAGIC, 8) != 0) {
        fclose(f);
        log_warn("SNAPSHOT: %s has an unrecognized header, ignoring it", path);
        return EMBER_ERR_INVALID;
    }

    size_t count = 0;
    for (;;) {
        uint32_t klen;
        if (fread(&klen, sizeof(klen), 1, f) != 1) break;
        if (klen == EMBER_SNAPSHOT_EOF_MARK) break;

        char *key = malloc(klen > 0 ? klen : 1);
        if (!key || fread(key, 1, klen, f) != klen) {
            free(key);
            break;
        }

        uint32_t vlen;
        if (fread(&vlen, sizeof(vlen), 1, f) != 1) {
            free(key);
            break;
        }
        char *val = malloc(vlen > 0 ? vlen : 1);
        if (!val || fread(val, 1, vlen, f) != vlen) {
            free(key);
            free(val);
            break;
        }

        int64_t ttl_ms;
        if (fread(&ttl_ms, sizeof(ttl_ms), 1, f) != 1) {
            free(key);
            free(val);
            break;
        }

        ember_db_set(db, key, klen, val, vlen, ttl_ms);
        free(key);
        free(val);
        count++;
    }

    fclose(f);
    log_info("SNAPSHOT: loaded %zu key(s) from %s", count, path);
    return EMBER_OK;
}
