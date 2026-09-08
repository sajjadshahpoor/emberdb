/*
 * ember_cli_main.c - a minimal standalone client, in the same spirit as
 * redis-cli: connect over TCP, send whatever the user types as a RESP
 * command, and render the reply.
 *
 * This intentionally does not reuse ember_protocol_parse: that function
 * parses *requests* (multibulk arrays sent by a client), while a reply
 * uses RESP's other four types (+, -, :, $) as well as $/* for bulk
 * strings and arrays of them. A small dedicated reader is simpler here
 * than teaching one parser both directions of the protocol.
 */
#include "ember_common.h"
#include "ember_protocol.h"
#include "ember_sds.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    int fd;
    sds buf; /* bytes read from the socket but not yet consumed */
} ember_client_conn;

static bool fill_buffer(ember_client_conn *c) {
    char tmp[4096];
    ssize_t n = read(c->fd, tmp, sizeof(tmp));
    if (n <= 0) return false;
    sds grown = sds_cat_len(c->buf, tmp, (size_t)n);
    if (!grown) return false;
    c->buf = grown;
    return true;
}

static bool read_line(ember_client_conn *c, char **out_line) {
    for (;;) {
        size_t len = sds_len(c->buf);
        for (size_t i = 0; i + 1 < len; i++) {
            if (c->buf[i] == '\r' && c->buf[i + 1] == '\n') {
                *out_line = malloc(i + 1);
                memcpy(*out_line, c->buf, i);
                (*out_line)[i] = '\0';
                sds_advance(c->buf, i + 2);
                return true;
            }
        }
        if (!fill_buffer(c)) return false;
    }
}

static bool read_exact(ember_client_conn *c, char *dst, size_t n) {
    while (sds_len(c->buf) < n) {
        if (!fill_buffer(c)) return false;
    }
    memcpy(dst, c->buf, n);
    sds_advance(c->buf, n);
    return true;
}

/* Reads and prints exactly one RESP value, recursing for arrays. Returns
 * false if the connection dropped mid-reply. */
static bool print_reply(ember_client_conn *c) {
    char *line;
    if (!read_line(c, &line)) return false;

    char type = line[0];
    const char *rest = line + 1;

    switch (type) {
        case '+':
            printf("%s\n", rest);
            break;
        case '-':
            printf("(error) %s\n", rest);
            break;
        case ':':
            printf("(integer) %s\n", rest);
            break;
        case '$': {
            long len = atol(rest);
            if (len < 0) {
                printf("(nil)\n");
                break;
            }
            char *data = malloc((size_t)len + 2); /* payload + trailing \r\n */
            if (!read_exact(c, data, (size_t)len + 2)) {
                free(data);
                free(line);
                return false;
            }
            data[len] = '\0';
            printf("\"%s\"\n", data);
            free(data);
            break;
        }
        case '*': {
            long count = atol(rest);
            if (count < 0) {
                printf("(nil)\n");
                break;
            }
            printf("(array of %ld)\n", count);
            for (long i = 0; i < count; i++) {
                printf("  %ld) ", i + 1);
                if (!print_reply(c)) {
                    free(line);
                    return false;
                }
            }
            break;
        }
        default:
            printf("%s\n", line);
    }

    free(line);
    return true;
}

static bool send_command(int fd, char **argv, size_t *argvlen, int argc) {
    sds encoded = ember_encode_multibulk(argv, argvlen, argc);
    if (!encoded) return false;

    size_t sent = 0, total = sds_len(encoded);
    while (sent < total) {
        ssize_t n = write(fd, encoded + sent, total - sent);
        if (n <= 0) {
            sds_free(encoded);
            return false;
        }
        sent += (size_t)n;
    }
    sds_free(encoded);
    return true;
}

static int connect_to(const char *host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "invalid host: %s\n", host);
        exit(1);
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        exit(1);
    }
    return fd;
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    int port = 6380;
    int i = 1;

    while (i < argc) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[++i];
            i++;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
            i++;
        } else {
            break;
        }
    }

    int fd = connect_to(host, port);
    ember_client_conn conn = { .fd = fd, .buf = sds_empty() };

    if (i < argc) {
        /* Single-shot mode: everything left on the command line is one
         * command, like `emberdb-cli SET foo bar`. */
        int cmd_argc = argc - i;
        char **cmd_argv = &argv[i];
        size_t *lens = malloc(sizeof(size_t) * (size_t)cmd_argc);
        for (int j = 0; j < cmd_argc; j++) lens[j] = strlen(cmd_argv[j]);

        bool ok = send_command(fd, cmd_argv, lens, cmd_argc) && print_reply(&conn);
        free(lens);
        close(fd);
        sds_free(conn.buf);
        return ok ? 0 : 1;
    }

    printf("EmberDB CLI connected to %s:%d. Type a command, or QUIT to exit.\n", host, port);
    char line[4096];
    for (;;) {
        printf("emberdb> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        char *tokens[64];
        int ntok = 0;
        char *saveptr;
        char *tok = strtok_r(line, " \t\r\n", &saveptr);
        while (tok && ntok < 64) {
            tokens[ntok++] = tok;
            tok = strtok_r(NULL, " \t\r\n", &saveptr);
        }
        if (ntok == 0) continue;

        size_t lens[64];
        for (int j = 0; j < ntok; j++) lens[j] = strlen(tokens[j]);

        if (!send_command(fd, tokens, lens, ntok)) {
            printf("connection lost\n");
            break;
        }
        if (!print_reply(&conn)) {
            printf("connection closed by server\n");
            break;
        }
        if (strcasecmp(tokens[0], "QUIT") == 0) break;
    }

    close(fd);
    sds_free(conn.buf);
    return 0;
}
