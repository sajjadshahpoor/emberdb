/*
 * ember_commands.h - the command table: parses a request's argv into a
 * concrete operation against the keyspace and produces a RESP reply.
 */
#ifndef EMBER_COMMANDS_H
#define EMBER_COMMANDS_H

#include "ember_protocol.h"
#include "ember_sds.h"
#include "ember_server.h"

#include <stdbool.h>

/* Executes `cmd` for a live client connection: applies it to server->db,
 * appends the RESP-encoded reply to *reply_buf, and - if it was a
 * successful write and server->aof is non-NULL - appends it to the AOF.
 * Returns false if the connection should be closed once the reply has
 * been flushed (true otherwise). */
bool ember_command_dispatch(ember_server *server, const ember_command *cmd, sds *reply_buf);

/* Applies a write command directly to `db` with no reply built and no AOF
 * append. This is the only entry point ember_aof_load uses, which is what
 * makes replay safe to call on a server whose AOF is already open (it
 * cannot recursively re-log what it's replaying). */
void ember_command_apply_for_replay(ember_db *db, const ember_command *cmd);

#endif /* EMBER_COMMANDS_H */
