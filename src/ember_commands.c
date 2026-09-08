#include "ember_commands.h"
#include "ember_common.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */

typedef bool (*ember_cmd_fn)(ember_server *server, const ember_command *cmd, sds *reply);

typedef struct {
    const char *name;
    int min_argc; /* including the command name itself */
    bool is_write;
    ember_cmd_fn handler;
} ember_cmd_spec;

static bool cmd_ping(ember_server *server, const ember_command *cmd, sds *reply) {
    EMBER_UNUSED(server);
    if (!reply) return false;
    if (cmd->argc >= 2) {
        *reply = ember_reply_bulk_string(*reply, cmd->argv[1], cmd->argvlen[1]);
    } else {
        *reply = ember_reply_simple_string(*reply, "PONG");
    }
    return false;
}

static bool cmd_echo(ember_server *server, const ember_command *cmd, sds *reply) {
    EMBER_UNUSED(server);
    if (reply) *reply = ember_reply_bulk_string(*reply, cmd->argv[1], cmd->argvlen[1]);
    return false;
}

static bool cmd_set(ember_server *server, const ember_command *cmd, sds *reply) {
    int64_t ttl_ms = 0;

    if (cmd->argc == 5) {
        if (strcasecmp(cmd->argv[3], "EX") == 0) {
            ttl_ms = atoll(cmd->argv[4]) * 1000;
        } else if (strcasecmp(cmd->argv[3], "PX") == 0) {
            ttl_ms = atoll(cmd->argv[4]);
        } else {
            if (reply) *reply = ember_reply_error(*reply, "ERR syntax error");
            return false;
        }
    } else if (cmd->argc != 3) {
        if (reply) *reply = ember_reply_error(*reply, "ERR syntax error");
        return false;
    }

    int status = ember_db_set(server->db, cmd->argv[1], cmd->argvlen[1],
                               cmd->argv[2], cmd->argvlen[2], ttl_ms);
    if (status != EMBER_OK) {
        if (reply) *reply = ember_reply_error(*reply, "ERR out of memory");
        return false;
    }
    if (reply) *reply = ember_reply_simple_string(*reply, "OK");
    return true;
}

static bool cmd_get(ember_server *server, const ember_command *cmd, sds *reply) {
    const char *val;
    size_t len;
    int status = ember_db_get(server->db, cmd->argv[1], cmd->argvlen[1], &val, &len);
    if (!reply) return false;
    if (status == EMBER_OK) *reply = ember_reply_bulk_string(*reply, val, len);
    else *reply = ember_reply_null_bulk(*reply);
    return false;
}

static bool cmd_del(ember_server *server, const ember_command *cmd, sds *reply) {
    int64_t deleted = 0;
    for (int i = 1; i < cmd->argc; i++) {
        if (ember_db_del(server->db, cmd->argv[i], cmd->argvlen[i])) deleted++;
    }
    if (reply) *reply = ember_reply_integer(*reply, deleted);
    return true;
}

static bool cmd_exists(ember_server *server, const ember_command *cmd, sds *reply) {
    int64_t count = 0;
    for (int i = 1; i < cmd->argc; i++) {
        if (ember_db_exists(server->db, cmd->argv[i], cmd->argvlen[i])) count++;
    }
    if (reply) *reply = ember_reply_integer(*reply, count);
    return false;
}

static bool cmd_expire(ember_server *server, const ember_command *cmd, sds *reply) {
    int64_t seconds = atoll(cmd->argv[2]);
    bool ok = ember_db_expire(server->db, cmd->argv[1], cmd->argvlen[1], seconds * 1000);
    if (reply) *reply = ember_reply_integer(*reply, ok ? 1 : 0);
    return ok;
}

static bool cmd_pexpire(ember_server *server, const ember_command *cmd, sds *reply) {
    int64_t ms = atoll(cmd->argv[2]);
    bool ok = ember_db_expire(server->db, cmd->argv[1], cmd->argvlen[1], ms);
    if (reply) *reply = ember_reply_integer(*reply, ok ? 1 : 0);
    return ok;
}

static bool cmd_ttl(ember_server *server, const ember_command *cmd, sds *reply) {
    int64_t ttl_ms = ember_db_ttl_ms(server->db, cmd->argv[1], cmd->argvlen[1]);
    if (!reply) return false;
    if (ttl_ms == EMBER_ERR_NOTFOUND) *reply = ember_reply_integer(*reply, -2);
    else if (ttl_ms == 0) *reply = ember_reply_integer(*reply, -1);
    else *reply = ember_reply_integer(*reply, (ttl_ms + 999) / 1000);
    return false;
}

static bool cmd_pttl(ember_server *server, const ember_command *cmd, sds *reply) {
    int64_t ttl_ms = ember_db_ttl_ms(server->db, cmd->argv[1], cmd->argvlen[1]);
    if (!reply) return false;
    if (ttl_ms == EMBER_ERR_NOTFOUND) *reply = ember_reply_integer(*reply, -2);
    else if (ttl_ms == 0) *reply = ember_reply_integer(*reply, -1);
    else *reply = ember_reply_integer(*reply, ttl_ms);
    return false;
}

static bool cmd_persist(ember_server *server, const ember_command *cmd, sds *reply) {
    bool ok = ember_db_persist(server->db, cmd->argv[1], cmd->argvlen[1]);
    if (reply) *reply = ember_reply_integer(*reply, ok ? 1 : 0);
    return ok;
}

static bool do_incrby(ember_server *server, const ember_command *cmd, sds *reply, int64_t delta) {
    int64_t result;
    int status = ember_db_incrby(server->db, cmd->argv[1], cmd->argvlen[1], delta, &result);
    if (status == EMBER_ERR_TYPE) {
        if (reply) *reply = ember_reply_error(*reply, "ERR value is not an integer or out of range");
        return false;
    }
    if (status != EMBER_OK) {
        if (reply) *reply = ember_reply_error(*reply, "ERR out of memory");
        return false;
    }
    if (reply) *reply = ember_reply_integer(*reply, result);
    return true;
}

static bool cmd_incr(ember_server *server, const ember_command *cmd, sds *reply) {
    return do_incrby(server, cmd, reply, 1);
}

static bool cmd_decr(ember_server *server, const ember_command *cmd, sds *reply) {
    return do_incrby(server, cmd, reply, -1);
}

static bool cmd_incrby(ember_server *server, const ember_command *cmd, sds *reply) {
    return do_incrby(server, cmd, reply, atoll(cmd->argv[2]));
}

static bool cmd_decrby(ember_server *server, const ember_command *cmd, sds *reply) {
    return do_incrby(server, cmd, reply, -atoll(cmd->argv[2]));
}

static bool cmd_append(ember_server *server, const ember_command *cmd, sds *reply) {
    size_t newlen;
    int status = ember_db_append(server->db, cmd->argv[1], cmd->argvlen[1],
                                  cmd->argv[2], cmd->argvlen[2], &newlen);
    if (status != EMBER_OK) {
        if (reply) *reply = ember_reply_error(*reply, "ERR out of memory");
        return false;
    }
    if (reply) *reply = ember_reply_integer(*reply, (int64_t)newlen);
    return true;
}

static bool cmd_strlen(ember_server *server, const ember_command *cmd, sds *reply) {
    size_t len;
    ember_db_strlen(server->db, cmd->argv[1], cmd->argvlen[1], &len);
    if (reply) *reply = ember_reply_integer(*reply, (int64_t)len);
    return false;
}

/* Real glob-pattern matching (as Redis's KEYS supports) is not implemented
 * here - only the two forms needed for interactive testing: "*" (list
 * everything) and an exact key name (existence check). */
static bool cmd_keys(ember_server *server, const ember_command *cmd, sds *reply) {
    if (!reply) return false;
    bool match_all = (cmd->argvlen[1] == 1 && cmd->argv[1][0] == '*');

    if (!match_all) {
        bool exists = ember_db_exists(server->db, cmd->argv[1], cmd->argvlen[1]);
        *reply = ember_reply_array_header(*reply, exists ? 1 : 0);
        if (exists) *reply = ember_reply_bulk_string(*reply, cmd->argv[1], cmd->argvlen[1]);
        return false;
    }

    /* RESP arrays are length-prefixed, so every key has to be collected
     * before the header (with its count) can be written. */
    size_t cap = 16, count = 0;
    char **keys = malloc(sizeof(char *) * cap);
    size_t *lens = malloc(sizeof(size_t) * cap);

    ember_db_iter it = ember_db_iter_start(server->db);
    const char *k;
    size_t klen;
    while (ember_db_iter_next(&it, &k, &klen)) {
        if (count == cap) {
            cap *= 2;
            keys = realloc(keys, sizeof(char *) * cap);
            lens = realloc(lens, sizeof(size_t) * cap);
        }
        keys[count] = (char *)k;
        lens[count] = klen;
        count++;
    }

    *reply = ember_reply_array_header(*reply, (int64_t)count);
    for (size_t i = 0; i < count; i++) {
        *reply = ember_reply_bulk_string(*reply, keys[i], lens[i]);
    }
    free(keys);
    free(lens);
    return false;
}

static bool cmd_type(ember_server *server, const ember_command *cmd, sds *reply) {
    bool exists = ember_db_exists(server->db, cmd->argv[1], cmd->argvlen[1]);
    if (reply) *reply = ember_reply_simple_string(*reply, exists ? "string" : "none");
    return false;
}

static bool cmd_dbsize(ember_server *server, const ember_command *cmd, sds *reply) {
    EMBER_UNUSED(cmd);
    if (reply) *reply = ember_reply_integer(*reply, (int64_t)ember_db_size(server->db));
    return false;
}

static bool cmd_flushall(ember_server *server, const ember_command *cmd, sds *reply) {
    EMBER_UNUSED(cmd);
    ember_db_flushall(server->db);
    if (reply) *reply = ember_reply_simple_string(*reply, "OK");
    return true;
}

static bool cmd_save(ember_server *server, const ember_command *cmd, sds *reply) {
    EMBER_UNUSED(cmd);
    const char *path = server->snapshot_path ? server->snapshot_path : "emberdb.rdb";
    int status = ember_snapshot_save(server->db, path);
    if (reply) {
        *reply = (status == EMBER_OK) ? ember_reply_simple_string(*reply, "OK")
                                       : ember_reply_error(*reply, "ERR save failed");
    }
    return false;
}

typedef struct {
    ember_db *db;
    char *path;
} ember_bgsave_arg;

static void bgsave_job(void *arg_ptr) {
    ember_bgsave_arg *arg = arg_ptr;
    ember_snapshot_save(arg->db, arg->path);
    free(arg->path);
    free(arg);
}

static bool cmd_bgsave(ember_server *server, const ember_command *cmd, sds *reply) {
    EMBER_UNUSED(cmd);
    const char *path = server->snapshot_path ? server->snapshot_path : "emberdb.rdb";

    if (!server->pool) {
        /* No pool available (e.g. a bare ember_db used from a unit test):
         * fall back to a synchronous save rather than failing outright. */
        ember_snapshot_save(server->db, path);
        if (reply) *reply = ember_reply_simple_string(*reply, "OK");
        return false;
    }

    ember_bgsave_arg *arg = malloc(sizeof(ember_bgsave_arg));
    arg->db = server->db;
    arg->path = strdup(path);
    if (!ember_threadpool_submit(server->pool, bgsave_job, arg)) {
        free(arg->path);
        free(arg);
        if (reply) *reply = ember_reply_error(*reply, "ERR could not schedule background save");
        return false;
    }
    if (reply) *reply = ember_reply_simple_string(*reply, "Background saving started");
    return false;
}

static bool cmd_info(ember_server *server, const ember_command *cmd, sds *reply) {
    EMBER_UNUSED(cmd);
    if (!reply) return false;

    sds info = sds_new("# Server\r\nemberdb_version:" EMBER_VERSION "\r\n# Stats\r\n");
    info = sds_cat_printf(info, "connections_total:%llu\r\n",
                           (unsigned long long)server->stat_connections_total);
    info = sds_cat_printf(info, "commands_processed:%llu\r\n",
                           (unsigned long long)server->stat_commands_processed);
    info = sds_cat_printf(info, "keys:%zu\r\n", ember_db_size(server->db));

    *reply = ember_reply_bulk_string(*reply, info, sds_len(info));
    sds_free(info);
    return false;
}

static bool cmd_quit(ember_server *server, const ember_command *cmd, sds *reply) {
    EMBER_UNUSED(server);
    EMBER_UNUSED(cmd);
    if (reply) *reply = ember_reply_simple_string(*reply, "OK");
    return false;
}

static const ember_cmd_spec COMMAND_TABLE[] = {
    { "PING", 1, false, cmd_ping },
    { "ECHO", 2, false, cmd_echo },
    { "SET", 3, true, cmd_set },
    { "GET", 2, false, cmd_get },
    { "DEL", 2, true, cmd_del },
    { "EXISTS", 2, false, cmd_exists },
    { "EXPIRE", 3, true, cmd_expire },
    { "PEXPIRE", 3, true, cmd_pexpire },
    { "TTL", 2, false, cmd_ttl },
    { "PTTL", 2, false, cmd_pttl },
    { "PERSIST", 2, true, cmd_persist },
    { "INCR", 2, true, cmd_incr },
    { "DECR", 2, true, cmd_decr },
    { "INCRBY", 3, true, cmd_incrby },
    { "DECRBY", 3, true, cmd_decrby },
    { "APPEND", 3, true, cmd_append },
    { "STRLEN", 2, false, cmd_strlen },
    { "KEYS", 2, false, cmd_keys },
    { "TYPE", 2, false, cmd_type },
    { "DBSIZE", 1, false, cmd_dbsize },
    { "FLUSHALL", 1, true, cmd_flushall },
    { "SAVE", 1, false, cmd_save },
    { "BGSAVE", 1, false, cmd_bgsave },
    { "INFO", 1, false, cmd_info },
    { "QUIT", 1, false, cmd_quit },
};

static const size_t COMMAND_TABLE_LEN = sizeof(COMMAND_TABLE) / sizeof(COMMAND_TABLE[0]);

static const ember_cmd_spec *find_spec(const char *name) {
    for (size_t i = 0; i < COMMAND_TABLE_LEN; i++) {
        if (strcasecmp(COMMAND_TABLE[i].name, name) == 0) return &COMMAND_TABLE[i];
    }
    return NULL;
}

bool ember_command_dispatch(ember_server *server, const ember_command *cmd, sds *reply_buf) {
    server->stat_commands_processed++;
    if (cmd->argc == 0) return true; /* blank inline command: no-op */

    const ember_cmd_spec *spec = find_spec(cmd->argv[0]);
    if (!spec) {
        *reply_buf = ember_reply_error(*reply_buf, "ERR unknown command");
        return true;
    }
    if (cmd->argc < spec->min_argc) {
        *reply_buf = ember_reply_error(*reply_buf, "ERR wrong number of arguments");
        return true;
    }

    bool mutated = spec->handler(server, cmd, reply_buf);

    /* Only commands that both mutate the keyspace and actually did so get
     * logged - a no-op EXPIRE on a missing key, for instance, has nothing
     * useful to replay. */
    if (spec->is_write && mutated && server->aof) {
        ember_aof_append_command(server->aof, cmd);
    }

    if (strcasecmp(cmd->argv[0], "QUIT") == 0) return false;
    return true;
}

void ember_command_apply_for_replay(ember_db *db, const ember_command *cmd) {
    if (cmd->argc == 0) return;

    const ember_cmd_spec *spec = find_spec(cmd->argv[0]);
    if (!spec || !spec->is_write) return;

    ember_server stub;
    memset(&stub, 0, sizeof(stub));
    stub.db = db;

    spec->handler(&stub, cmd, NULL);
}
