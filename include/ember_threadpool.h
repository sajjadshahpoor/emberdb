/*
 * ember_threadpool.h - fixed-size worker pool for background jobs.
 *
 * The event loop thread must never block: a stall there stalls every
 * connected client. Anything that can take unpredictable time - fsync()ing
 * the AOF, writing a point-in-time snapshot, walking the keyspace for
 * active expiration - is handed off to this pool instead of being done
 * inline on the reactor thread.
 *
 * Jobs are a simple (function pointer, argument) pair queued on a singly
 * linked list guarded by a mutex, with a condition variable to wake idle
 * workers. Shutdown is graceful: once ember_threadpool_destroy is called,
 * no new jobs are accepted, but everything already queued is allowed to
 * finish before the worker threads exit.
 */
#ifndef EMBER_THREADPOOL_H
#define EMBER_THREADPOOL_H

#include <stdbool.h>
#include <stddef.h>

typedef struct ember_threadpool ember_threadpool;

ember_threadpool *ember_threadpool_create(size_t num_threads);

/* Queues fn(arg) to run on a worker thread. Returns false if the pool is
 * shutting down or allocation failed. */
bool ember_threadpool_submit(ember_threadpool *pool, void (*fn)(void *), void *arg);

/* Number of jobs currently queued (not yet picked up by a worker). */
size_t ember_threadpool_pending(ember_threadpool *pool);

/* Stops accepting new jobs, waits for all queued jobs to complete, joins
 * every worker thread, then frees the pool. */
void ember_threadpool_destroy(ember_threadpool *pool);

#endif /* EMBER_THREADPOOL_H */
