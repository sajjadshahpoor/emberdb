/*
 * ember_persist.h - crash-safe durability: an append-only command log
 * (AOF) plus point-in-time binary snapshots.
 *
 * The AOF is simply every successful write command, re-encoded in the
 * exact same RESP format clients use to send commands in the first
 * place. That means replaying it at startup is just feeding the file's
 * bytes through the ordinary protocol parser and applying whatever comes
 * out - no separate log format to design or maintain.
 *
 * Durability vs. throughput is a dial, not a switch: fsync()ing after
 * every single write would make every command wait on a disk round trip,
 * but never fsyncing risks losing everything since the last flush on a
 * crash. EmberDB takes the same middle ground Redis's `appendfsync
 * everysec` does - writes are buffered immediately, and a background
 * thread-pool job fsyncs at most once per second, so a crash can lose at
 * most ~1 second of writes instead of either "everything" or "nothing".
 */
#ifndef EMBER_PERSIST_H
#define EMBER_PERSIST_H

#include "ember_db.h"
#include "ember_protocol.h"

typedef struct ember_aof ember_aof;

/* Opens (creating if necessary) the AOF file at `path` for appending. */
ember_aof *ember_aof_open(const char *path);
void ember_aof_close(ember_aof *aof);

/* Appends one already-applied command to the log. Does not fsync -
 * durability is handled by ember_aof_fsync_job running periodically on a
 * thread-pool worker instead, so the caller (the reactor thread) never
 * blocks on disk I/O here. */
int ember_aof_append_command(ember_aof *aof, const ember_command *cmd);

/* Job entry point: pass an ember_aof* as the argument when scheduling this
 * on the thread pool. fsyncs only if there have been writes since the
 * last call (cheap to call on every timer tick). */
void ember_aof_fsync_job(void *arg);

/* Replays every command previously logged at `path` against `db`. A
 * missing file is not an error - it just means there's nothing to
 * replay yet. */
int ember_aof_load(const char *path, ember_db *db);

/* Point-in-time snapshot of the whole keyspace ("RDB-lite"): a simple
 * length-prefixed binary format, written to a temp file and atomically
 * renamed into place so a crash mid-write can never corrupt the previous
 * snapshot. */
int ember_snapshot_save(ember_db *db, const char *path);
int ember_snapshot_load(const char *path, ember_db *db);

#endif /* EMBER_PERSIST_H */
