/*
 * ember_eventloop.h - epoll-based reactor.
 *
 * A single thread multiplexes every client socket through one epoll
 * instance instead of spawning a thread (or process) per connection.
 * That's the standard trade a high-connection-count server makes: threads
 * are expensive (stack memory, context-switch cost, scheduler pressure)
 * once you have thousands of mostly-idle sockets, whereas epoll_wait lets
 * the kernel tell us in O(ready fds) which sockets actually have work,
 * regardless of how many are merely open. The cost is that every callback
 * run on this thread must be non-blocking - anything that could stall
 * (disk I/O, a slow computation) has to go through ember_threadpool
 * instead, which is exactly why that module exists alongside this one.
 *
 * Timers are implemented with Linux's timerfd: a timer is just another
 * fd that becomes readable when it fires, which means the reactor doesn't
 * need a separate "compute next timeout" code path at all - timers and
 * socket I/O are handled by the exact same epoll_wait/dispatch loop.
 */
#ifndef EMBER_EVENTLOOP_H
#define EMBER_EVENTLOOP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct ember_eventloop ember_eventloop;

typedef void (*ember_fd_callback)(ember_eventloop *loop, int fd, void *user_data);
typedef void (*ember_timer_callback)(ember_eventloop *loop, void *user_data);

ember_eventloop *ember_eventloop_create(void);
void ember_eventloop_destroy(ember_eventloop *loop);

/* Registers fd for the events implied by which callbacks are non-NULL, and
 * puts fd into non-blocking mode. Returns EMBER_OK or an EMBER_ERR_*. */
int ember_eventloop_add_fd(ember_eventloop *loop, int fd,
                            ember_fd_callback on_readable,
                            ember_fd_callback on_writable, void *user_data);

/* Changes which callbacks/events fd is watched for (e.g. to start/stop
 * watching for writability once a socket's outbound buffer drains). */
int ember_eventloop_mod_fd(ember_eventloop *loop, int fd,
                            ember_fd_callback on_readable,
                            ember_fd_callback on_writable, void *user_data);

/* Stops watching fd. Does not close it - the caller owns fd's lifetime. */
int ember_eventloop_remove_fd(ember_eventloop *loop, int fd);

/* Registers a repeating timer firing every interval_ms. Returns a timer
 * handle (>= 0) to pass to ember_eventloop_remove_timer, or -1 on failure. */
int ember_eventloop_add_timer(ember_eventloop *loop, int64_t interval_ms,
                               ember_timer_callback cb, void *user_data);
void ember_eventloop_remove_timer(ember_eventloop *loop, int timer_handle);

/* Blocks, dispatching callbacks, until ember_eventloop_stop is called. */
void ember_eventloop_run(ember_eventloop *loop);
void ember_eventloop_stop(ember_eventloop *loop);

#endif /* EMBER_EVENTLOOP_H */
