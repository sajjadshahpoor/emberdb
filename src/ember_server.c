/*
 * ember_server.c - the TCP front end: accepts connections, drives each
 * one through non-blocking read/parse/dispatch/write on the reactor
 * thread, and schedules the two recurring background timers (active
 * expiration and AOF fsync).
 */
#include "ember_server.h"
#include "ember_commands.h"
#include "ember_common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define EMBER_READ_CHUNK 16384
#define EMBER_ACTIVE_EXPIRE_INTERVAL_MS 100
#define EMBER_ACTIVE_EXPIRE_SAMPLE_SIZE 20
#define EMBER_AOF_FSYNC_INTERVAL_MS 1000

typedef struct {
    int fd;
    sds inbuf;         /* bytes read but not yet parsed into a command */
    sds outbuf;        /* RESP replies queued to write back */
    size_t outbuf_sent; /* how much of outbuf has already been written */
    ember_server *server;
} ember_conn;

static void on_readable_client(ember_eventloop *loop, int fd, void *user_data);
static void on_writable_client(ember_eventloop *loop, int fd, void *user_data);

static void close_conn(ember_eventloop *loop, ember_conn *conn) {
    ember_eventloop_remove_fd(loop, conn->fd);
    close(conn->fd);
    sds_free(conn->inbuf);
    sds_free(conn->outbuf);
    free(conn);
}

/* Writes as much of conn->outbuf as the socket will currently accept.
 * Returns false if the connection was closed (on a write error) - in that
 * case `conn` must not be touched again by the caller. */
static bool try_flush_output(ember_eventloop *loop, ember_conn *conn) {
    size_t total = sds_len(conn->outbuf);

    while (conn->outbuf_sent < total) {
        ssize_t n = write(conn->fd, conn->outbuf + conn->outbuf_sent, total - conn->outbuf_sent);
        if (n > 0) {
            conn->outbuf_sent += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /* Socket send buffer is full: keep the unsent tail and ask the
             * reactor to let us know as soon as we can write more. */
            ember_eventloop_mod_fd(loop, conn->fd, on_readable_client, on_writable_client, conn);
            return true;
        }
        close_conn(loop, conn);
        return false;
    }

    sds_clear(conn->outbuf);
    conn->outbuf_sent = 0;
    ember_eventloop_mod_fd(loop, conn->fd, on_readable_client, NULL, conn);
    return true;
}

static void on_writable_client(ember_eventloop *loop, int fd, void *user_data) {
    EMBER_UNUSED(fd);
    try_flush_output(loop, user_data);
}

static void on_readable_client(ember_eventloop *loop, int fd, void *user_data) {
    ember_conn *conn = user_data;
    char buf[EMBER_READ_CHUNK];
    bool peer_closed = false;

    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            sds grown = sds_cat_len(conn->inbuf, buf, (size_t)n);
            if (!grown) {
                close_conn(loop, conn);
                return;
            }
            conn->inbuf = grown;
            if ((size_t)n < sizeof(buf)) break; /* likely drained the socket */
            continue;
        }
        if (n == 0) {
            peer_closed = true;
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        peer_closed = true;
        break;
    }

    bool keep_open = true;
    for (;;) {
        ember_command cmd;
        size_t consumed;
        int status = ember_protocol_parse(conn->inbuf, sds_len(conn->inbuf), &cmd, &consumed);
        if (status == EMBER_ERR_AGAIN) break;
        if (status != EMBER_OK) {
            conn->outbuf = ember_reply_error(conn->outbuf, "ERR Protocol error");
            sds_clear(conn->inbuf);
            keep_open = false;
            break;
        }

        sds_advance(conn->inbuf, consumed);
        keep_open = ember_command_dispatch(conn->server, &cmd, &conn->outbuf);
        ember_command_free(&cmd);
        if (!keep_open) break;
    }

    if (!try_flush_output(loop, conn)) return; /* conn was closed on write error */

    if (peer_closed || !keep_open) {
        close_conn(loop, conn);
    }
}

static void on_accept(ember_eventloop *loop, int fd, void *user_data) {
    ember_server *server = user_data;

    for (;;) {
        int client_fd = accept(fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            log_warn("accept() failed: %s", strerror(errno));
            break;
        }

        int one = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        ember_conn *conn = malloc(sizeof(ember_conn));
        conn->fd = client_fd;
        conn->inbuf = sds_empty();
        conn->outbuf = sds_empty();
        conn->outbuf_sent = 0;
        conn->server = server;
        server->stat_connections_total++;

        if (ember_eventloop_add_fd(loop, client_fd, on_readable_client, NULL, conn) != EMBER_OK) {
            close(client_fd);
            sds_free(conn->inbuf);
            sds_free(conn->outbuf);
            free(conn);
        }
    }
}

static void active_expire_timer(ember_eventloop *loop, void *user_data) {
    EMBER_UNUSED(loop);
    ember_server *server = user_data;
    ember_db_active_expire_cycle(server->db, EMBER_ACTIVE_EXPIRE_SAMPLE_SIZE);
}

static void aof_fsync_timer(ember_eventloop *loop, void *user_data) {
    EMBER_UNUSED(loop);
    ember_server *server = user_data;
    if (server->aof) {
        ember_threadpool_submit(server->pool, ember_aof_fsync_job, server->aof);
    }
}

int ember_server_listen(ember_server *server, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return EMBER_ERR_IO;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return EMBER_ERR_IO;
    }
    if (listen(fd, 511) < 0) {
        close(fd);
        return EMBER_ERR_IO;
    }

    server->listen_fd = fd;
    server->port = port;
    return EMBER_OK;
}

void ember_server_run(ember_server *server) {
    ember_eventloop_add_fd(server->loop, server->listen_fd, on_accept, NULL, server);
    ember_eventloop_add_timer(server->loop, EMBER_ACTIVE_EXPIRE_INTERVAL_MS, active_expire_timer, server);
    if (server->aof) {
        ember_eventloop_add_timer(server->loop, EMBER_AOF_FSYNC_INTERVAL_MS, aof_fsync_timer, server);
    }

    log_info("EmberDB listening on port %d", server->port);
    ember_eventloop_run(server->loop);
}

void ember_server_stop(ember_server *server) {
    ember_eventloop_stop(server->loop);
}
