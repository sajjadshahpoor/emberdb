#include "ember_commands.h"
#include "ember_server.h"
#include "ember_common.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static ember_server *g_server = NULL;

static void handle_shutdown_signal(int sig) {
    EMBER_UNUSED(sig);
    if (g_server) ember_server_stop(g_server);
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [--port N] [--aof PATH] [--rdb PATH] [--no-aof] "
            "[--loglevel debug|info|warn|error]\n"
            "\n"
            "  --port N       TCP port to listen on (default 6380)\n"
            "  --aof PATH     AOF file to log writes to and replay at startup "
            "(default emberdb.aof)\n"
            "  --rdb PATH     Snapshot file for SAVE/BGSAVE (default emberdb.rdb)\n"
            "  --no-aof       Disable the AOF; the keyspace only survives restarts "
            "via SAVE/BGSAVE\n",
            prog);
}

int main(int argc, char **argv) {
    int port = 6380;
    const char *aof_path = "emberdb.aof";
    const char *rdb_path = "emberdb.rdb";
    bool aof_enabled = true;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--aof") == 0 && i + 1 < argc) {
            aof_path = argv[++i];
        } else if (strcmp(argv[i], "--rdb") == 0 && i + 1 < argc) {
            rdb_path = argv[++i];
        } else if (strcmp(argv[i], "--no-aof") == 0) {
            aof_enabled = false;
        } else if (strcmp(argv[i], "--loglevel") == 0 && i + 1 < argc) {
            const char *lvl = argv[++i];
            if (strcmp(lvl, "debug") == 0) ember_log_set_level(EMBER_LOG_DEBUG);
            else if (strcmp(lvl, "warn") == 0) ember_log_set_level(EMBER_LOG_WARN);
            else if (strcmp(lvl, "error") == 0) ember_log_set_level(EMBER_LOG_ERROR);
            else ember_log_set_level(EMBER_LOG_INFO);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    ember_server server;
    memset(&server, 0, sizeof(server));
    server.db = ember_db_create();
    server.snapshot_path = strdup(rdb_path);
    server.pool = ember_threadpool_create(4);
    server.loop = ember_eventloop_create();

    /* AOF and snapshot are treated as alternative sources of truth rather
     * than merged, since replaying the full AOF on top of a snapshot that
     * already reflects part of that same history would double-apply
     * commands like INCR. A production implementation would perform AOF
     * rewrite/compaction right after a snapshot to keep the two in sync;
     * that's called out in docs/ARCHITECTURE.md as a known simplification. */
    if (aof_enabled) {
        ember_aof_load(aof_path, server.db);
        server.aof = ember_aof_open(aof_path);
        if (!server.aof) {
            log_error("Could not open AOF file '%s'", aof_path);
            return 1;
        }
    } else {
        ember_snapshot_load(rdb_path, server.db);
    }

    if (ember_server_listen(&server, port) != EMBER_OK) {
        log_error("Could not bind to port %d", port);
        return 1;
    }

    g_server = &server;
    signal(SIGINT, handle_shutdown_signal);
    signal(SIGTERM, handle_shutdown_signal);
    signal(SIGPIPE, SIG_IGN); /* a client disconnecting mid-write must not kill us */

    ember_server_run(&server); /* blocks until SIGINT/SIGTERM */

    log_info("Shutting down");
    close(server.listen_fd);
    ember_threadpool_destroy(server.pool);
    ember_eventloop_destroy(server.loop);
    if (server.aof) ember_aof_close(server.aof);
    ember_db_destroy(server.db);
    free(server.snapshot_path);

    return 0;
}
