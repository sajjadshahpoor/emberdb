/*
 * ember_server.h - the pieces every connection handler and command needs
 * a handle to: the keyspace, the background thread pool, the reactor, and
 * (optionally) the AOF.
 */
#ifndef EMBER_SERVER_H
#define EMBER_SERVER_H

#include "ember_db.h"
#include "ember_eventloop.h"
#include "ember_persist.h"
#include "ember_threadpool.h"

#include <stdint.h>

typedef struct ember_server {
    ember_db *db;
    ember_threadpool *pool;
    ember_eventloop *loop;
    ember_aof *aof;        /* NULL if persistence is disabled */
    char *snapshot_path;   /* used by SAVE/BGSAVE and startup load */

    int listen_fd;
    int port;

    uint64_t stat_connections_total;
    uint64_t stat_commands_processed;
} ember_server;

#endif /* EMBER_SERVER_H */
