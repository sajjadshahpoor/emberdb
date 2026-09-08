# EmberDB Architecture

EmberDB is a single-threaded-reactor, multi-threaded-background key-value
store, modeled closely on Redis's internal design. This document explains
*why* each subsystem is built the way it is, not just what it does — the
tradeoffs here are the interesting part.

```
                        ┌─────────────────────────────┐
                        │        main() / startup      │
                        │  load snapshot OR replay AOF │
                        └──────────────┬───────────────┘
                                       │
                     ┌─────────────────▼─────────────────┐
                     │      ember_eventloop (epoll)       │
                     │  one thread, non-blocking sockets  │
                     │                                     │
   client sockets ──▶│  accept → read → parse → dispatch  │
                     │           ▲                    │    │
                     │           │ RESP2       write ─┘    │
                     │  ┌────────┴─────────┐               │
                     │  │  timerfd timers  │               │
                     │  │ - active expire  │               │
                     │  │ - AOF fsync tick │               │
                     │  └────────┬─────────┘               │
                     └───────────┼─────────────────────────┘
                                 │ submit()
                     ┌───────────▼─────────────┐
                     │   ember_threadpool       │
                     │ (fsync, BGSAVE, expiry)  │
                     └──────────────────────────┘

              ember_db (Robin Hood hash table + TTLs)
                     │                      │
              ember_persist.c (AOF)   ember_persist.c (snapshot)
```

## Memory: the arena allocator (`ember_arena`)

Most allocators optimize for the general case: any size, any lifetime,
freed in any order. EmberDB has a narrower, more common need: a batch of
allocations that all die together (every temporary buffer built while
replaying the AOF at startup, for instance). For that shape, a bump
allocator wins on every axis that matters — allocation is a pointer
increment, and teardown is O(1) instead of O(n) individual `free()` calls.
The tradeoff is you give up freeing individual objects; that's the right
trade when the objects' lifetimes are already tied together.

## The hash table: Robin Hood hashing (`ember_hashtable`)

The keyspace itself needs a general-purpose map that stays fast under
sustained insert/delete churn — a workload arenas are the wrong fit for.
Naive linear-probing open addressing degrades under load because a bucket
that collided many times sits right next to one that never collided,
so probe lengths become wildly uneven. Robin Hood hashing tracks each
slot's distance from its ideal bucket (DIB) and, on insert, always lets
the entry that has probed further "win" the slot from one that hasn't -
"steal from the rich, give to the poor." That equalizes probe length
across the table, which is what you actually want for predictable p99
lookup latency.

Deletion uses backward-shift deletion instead of tombstones: neighboring
entries shift back to fill the hole immediately. Tombstone-based schemes
look simple but degrade monotonically under a delete-heavy workload,
since probe sequences have to walk past every tombstone ever created
until the table is rehashed; backward-shift deletion never accumulates
that debt.

## Values and TTLs (`ember_object`, `ember_db`)

Every value is a small struct — the actual bytes (an `sds`) plus an
optional absolute expiry timestamp — rather than a bare string. That
mirrors Redis's `robj`: it means TTL state lives right next to the value
it belongs to, with no second hash table to keep in sync.

Expiration is deliberately two-pronged, also copying Redis:

- **Lazy**: every read checks the target key's expiry first, so a client
  can never observe a value that should already be gone, no matter how
  far behind the background sweep is.
- **Active**: `ember_db_active_expire_cycle` walks a bounded number of
  buckets per call from a persisted cursor, rather than scanning the
  whole table at once. That reclaims memory for keys nobody ever reads
  again without ever introducing a latency spike from a full-table scan.

## The wire protocol (`ember_protocol`)

EmberDB speaks RESP2, the same protocol Redis uses - not a custom format.
That's a deliberate choice: it means the standard `redis-cli`, or any
existing RESP client library in any language, can talk to EmberDB with
zero modification, and it forces the implementation to deal with the
real complexity of a length-prefixed binary protocol arriving over a
byte stream in arbitrary-sized chunks.

The parser re-scans buffered input from the start on every call rather
than resuming a suspended state machine mid-command. That is simpler to
prove correct - there's exactly one code path, and "not enough data yet"
is just "return EMBER_ERR_AGAIN, consumed nothing" - at the cost of
rescanning bytes already looked at if a command straddles many small
reads. For a KV store's usual command sizes, that cost is negligible;
it would be worth revisiting for a protocol carrying multi-megabyte
payloads.

## The event loop (`ember_eventloop`)

A single reactor thread multiplexes every connection through one
`epoll` instance instead of a thread per connection. Threads cost real
memory (stack space) and scheduler overhead; with thousands of mostly
idle connections, `epoll_wait` scales with the number of *ready* file
descriptors, not the number of open ones. The cost of this design is
that nothing on the reactor thread may block - which is exactly why
`ember_threadpool` exists as its counterpart.

Timers are implemented with Linux's `timerfd` rather than a separate
"compute the next timeout" code path: a timer is just another
readable file descriptor, so the exact same `epoll_wait`/dispatch loop
drives both socket I/O and the background timers (active expiry, AOF
fsync) with no special-casing.

## Background work (`ember_threadpool`)

A fixed pool of worker threads pulls jobs off a mutex-and-condvar-guarded
queue. It exists specifically so that anything with unpredictable latency
- `fsync()`, walking the keyspace for a snapshot - never runs on the
reactor thread. Shutdown is deliberately graceful: once
`ember_threadpool_destroy` is called, no new work is accepted, but
everything already queued is allowed to finish, so a server shutting
down never drops an in-flight AOF fsync or an in-progress BGSAVE.

## Durability (`ember_persist`)

The AOF logs every successful write command re-encoded in the exact
RESP format a client would have sent it in - replay is just feeding the
file back through the same parser the server already has. Durability is
a dial: fsync-per-write guarantees no data loss but makes every write
wait on disk, while never fsyncing risks losing everything since the
last flush. EmberDB fsyncs on a 1-second timer from a thread-pool worker
(mirroring Redis's `appendfsync everysec`), bounding potential data loss
to about one second without ever blocking a client on disk I/O.

**Known simplification**: the AOF and the RDB-style snapshot
(`SAVE`/`BGSAVE`) are treated as *alternative* sources of truth on
startup, not merged. Replaying the complete AOF on top of a snapshot
that already reflects part of that same history would double-apply
commands like `INCR`. A production store solves this with AOF
rewrite/compaction immediately after a snapshot; EmberDB keeps the two
paths independent instead (AOF replay is preferred when the AOF is
enabled) as a conscious scope cut, called out here rather than hidden.

## Command dispatch (`ember_commands`)

Commands are a static table of `{name, min_argc, is_write, handler}`.
Keeping "is this command a write" as static per-command metadata (rather
than inferring it from what the handler actually did) means the AOF
logging decision in `ember_command_dispatch` is a simple table lookup,
and it's what lets `ember_command_apply_for_replay` reuse the exact same
handler functions during AOF replay - with no `ember_server`, no AOF, no
reply buffer - just the keyspace being mutated. There is exactly one
implementation of `SET`, used both by live clients and by startup replay.
