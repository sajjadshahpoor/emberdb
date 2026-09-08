#include "ember_eventloop.h"
#include "ember_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#define EMBER_EVENTLOOP_MAX_EVENTS 64

typedef struct {
    ember_fd_callback on_readable;
    ember_fd_callback on_writable;
    void *user_data;
    bool in_use;
} ember_fd_entry;

typedef struct {
    ember_timer_callback cb;
    void *user_data;
} ember_timer_ctx;

struct ember_eventloop {
    int epoll_fd;
    ember_fd_entry *fd_table;
    size_t fd_table_cap;
    bool running;
};

ember_eventloop *ember_eventloop_create(void) {
    ember_eventloop *loop = malloc(sizeof(ember_eventloop));
    if (!loop) return NULL;

    loop->epoll_fd = epoll_create1(0);
    if (loop->epoll_fd < 0) {
        free(loop);
        return NULL;
    }
    loop->fd_table = NULL;
    loop->fd_table_cap = 0;
    loop->running = false;
    return loop;
}

void ember_eventloop_destroy(ember_eventloop *loop) {
    if (!loop) return;
    close(loop->epoll_fd);
    free(loop->fd_table);
    free(loop);
}

static bool ensure_capacity(ember_eventloop *loop, int fd) {
    if ((size_t)fd < loop->fd_table_cap) return true;

    size_t new_cap = loop->fd_table_cap == 0 ? 16 : loop->fd_table_cap;
    while (new_cap <= (size_t)fd) new_cap *= 2;

    ember_fd_entry *new_table = realloc(loop->fd_table, sizeof(ember_fd_entry) * new_cap);
    if (!new_table) return false;

    for (size_t i = loop->fd_table_cap; i < new_cap; i++) {
        memset(&new_table[i], 0, sizeof(ember_fd_entry));
    }
    loop->fd_table = new_table;
    loop->fd_table_cap = new_cap;
    return true;
}

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int ember_eventloop_add_fd(ember_eventloop *loop, int fd,
                            ember_fd_callback on_readable,
                            ember_fd_callback on_writable, void *user_data) {
    if (fd < 0) return EMBER_ERR_INVALID;
    if (!ensure_capacity(loop, fd)) return EMBER_ERR_OOM;

    set_nonblocking(fd);

    loop->fd_table[fd].on_readable = on_readable;
    loop->fd_table[fd].on_writable = on_writable;
    loop->fd_table[fd].user_data = user_data;
    loop->fd_table[fd].in_use = true;

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.fd = fd;
    if (on_readable) ev.events |= EPOLLIN;
    if (on_writable) ev.events |= EPOLLOUT;

    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        loop->fd_table[fd].in_use = false;
        return EMBER_ERR_IO;
    }
    return EMBER_OK;
}

int ember_eventloop_mod_fd(ember_eventloop *loop, int fd,
                            ember_fd_callback on_readable,
                            ember_fd_callback on_writable, void *user_data) {
    if (fd < 0 || (size_t)fd >= loop->fd_table_cap || !loop->fd_table[fd].in_use) {
        return EMBER_ERR_NOTFOUND;
    }

    loop->fd_table[fd].on_readable = on_readable;
    loop->fd_table[fd].on_writable = on_writable;
    loop->fd_table[fd].user_data = user_data;

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.fd = fd;
    if (on_readable) ev.events |= EPOLLIN;
    if (on_writable) ev.events |= EPOLLOUT;

    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        return EMBER_ERR_IO;
    }
    return EMBER_OK;
}

int ember_eventloop_remove_fd(ember_eventloop *loop, int fd) {
    if (fd < 0 || (size_t)fd >= loop->fd_table_cap || !loop->fd_table[fd].in_use) {
        return EMBER_ERR_NOTFOUND;
    }
    epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    memset(&loop->fd_table[fd], 0, sizeof(ember_fd_entry));
    return EMBER_OK;
}

static void timer_readable(ember_eventloop *loop, int fd, void *user_data) {
    uint64_t expirations;
    ssize_t n = read(fd, &expirations, sizeof(expirations));
    EMBER_UNUSED(n); /* EAGAIN or a short read here is harmless: we only
                         care that the timer fired at least once. */

    ember_timer_ctx *ctx = user_data;
    ctx->cb(loop, ctx->user_data);
}

int ember_eventloop_add_timer(ember_eventloop *loop, int64_t interval_ms,
                               ember_timer_callback cb, void *user_data) {
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (tfd < 0) return -1;

    struct itimerspec spec;
    spec.it_value.tv_sec = interval_ms / 1000;
    spec.it_value.tv_nsec = (interval_ms % 1000) * 1000000L;
    spec.it_interval = spec.it_value;

    if (timerfd_settime(tfd, 0, &spec, NULL) < 0) {
        close(tfd);
        return -1;
    }

    ember_timer_ctx *ctx = malloc(sizeof(ember_timer_ctx));
    if (!ctx) {
        close(tfd);
        return -1;
    }
    ctx->cb = cb;
    ctx->user_data = user_data;

    if (ember_eventloop_add_fd(loop, tfd, timer_readable, NULL, ctx) != EMBER_OK) {
        close(tfd);
        free(ctx);
        return -1;
    }
    return tfd;
}

void ember_eventloop_remove_timer(ember_eventloop *loop, int timer_handle) {
    if (timer_handle < 0 || (size_t)timer_handle >= loop->fd_table_cap) return;
    if (!loop->fd_table[timer_handle].in_use) return;

    ember_timer_ctx *ctx = loop->fd_table[timer_handle].user_data;
    ember_eventloop_remove_fd(loop, timer_handle);
    free(ctx);
    close(timer_handle);
}

void ember_eventloop_run(ember_eventloop *loop) {
    struct epoll_event events[EMBER_EVENTLOOP_MAX_EVENTS];
    loop->running = true;

    while (loop->running) {
        int n = epoll_wait(loop->epoll_fd, events, EMBER_EVENTLOOP_MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            uint32_t flags = events[i].events;

            if ((size_t)fd >= loop->fd_table_cap || !loop->fd_table[fd].in_use) continue;

            /* Re-fetch the callback/user_data pointers immediately before
             * each invocation (rather than caching them across the two
             * checks below): the readable callback might remove fd, or
             * even trigger a table growth that reallocates fd_table, so
             * anything read before the call could be stale afterward. */
            if ((flags & (EPOLLIN | EPOLLHUP | EPOLLERR)) && loop->fd_table[fd].on_readable) {
                ember_fd_callback cb = loop->fd_table[fd].on_readable;
                void *ud = loop->fd_table[fd].user_data;
                cb(loop, fd, ud);
            }

            if ((size_t)fd < loop->fd_table_cap && loop->fd_table[fd].in_use &&
                (flags & EPOLLOUT) && loop->fd_table[fd].on_writable) {
                ember_fd_callback cb = loop->fd_table[fd].on_writable;
                void *ud = loop->fd_table[fd].user_data;
                cb(loop, fd, ud);
            }
        }
    }
}

void ember_eventloop_stop(ember_eventloop *loop) {
    loop->running = false;
}
