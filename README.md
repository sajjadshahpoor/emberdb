# EmberDB

[![CI](https://github.com/sajjadshahpoor/emberdb/actions/workflows/ci.yml/badge.svg)](https://github.com/sajjadshahpoor/emberdb/actions/workflows/ci.yml)

EmberDB is an in-memory key-value store server written from scratch in C,
speaking the same wire protocol as Redis (RESP2) — so the standard
`redis-cli`, or any RESP client library, can talk to it directly. It's a
systems-programming project built to demonstrate the fundamentals in
working, tested code rather than just describe them:

- **Manual memory management** — a bump-pointer arena allocator with
  chained growable blocks
- **A hash table built from scratch** — open addressing with Robin Hood
  hashing and tombstone-free backward-shift deletion
- **A hand-rolled binary protocol parser** — RESP2, incremental over a
  byte stream, binary-safe
- **An epoll-based event loop** — single-threaded reactor, non-blocking
  sockets, `timerfd`-backed timers
- **A pthread thread pool** — background fsync/snapshot/expiry work kept
  off the reactor thread
- **Crash-safe persistence** — an append-only command log (AOF) plus
  binary point-in-time snapshots
- **Lazy + active TTL expiration**, the same two-pronged strategy Redis uses

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the design
rationale behind each of these — the tradeoffs are the interesting part.

## Building

Requires a C compiler and `make` on Linux (or WSL on Windows — this uses
`epoll` and `timerfd`, so it targets Linux/POSIX specifically).

```sh
make            # builds bin/emberdb-server and bin/emberdb-cli
make test       # builds and runs every test suite
make debug      # rebuilds and re-tests under AddressSanitizer + UBSan
```

## Running it

```sh
./bin/emberdb-server --port 6380
```

```
Usage: ./bin/emberdb-server [--port N] [--aof PATH] [--rdb PATH] [--no-aof] [--loglevel debug|info|warn|error]

  --port N       TCP port to listen on (default 6380)
  --aof PATH     AOF file to log writes to and replay at startup (default emberdb.aof)
  --rdb PATH     Snapshot file for SAVE/BGSAVE (default emberdb.rdb)
  --no-aof       Disable the AOF; the keyspace only survives restarts via SAVE/BGSAVE
```

Talk to it with the bundled CLI, real `redis-cli`, or `nc`:

```
$ ./bin/emberdb-cli --port 6380
EmberDB CLI connected to 127.0.0.1:6380. Type a command, or QUIT to exit.
emberdb> SET foo bar
OK
emberdb> GET foo
"bar"
emberdb> EXPIRE foo 60
(integer) 1
emberdb> TTL foo
(integer) 60
emberdb> INCR counter
(integer) 1
emberdb> INCR counter
(integer) 2
emberdb> KEYS *
(array of 2)
  1) "foo"
  2) "counter"
emberdb> INFO
"# Server
emberdb_version:0.1.0
# Stats
connections_total:1
commands_processed:7
keys:2
"
emberdb> QUIT
OK
```

## Supported commands

`PING`, `ECHO`, `SET` (with `EX`/`PX`), `GET`, `DEL`, `EXISTS`, `EXPIRE`,
`PEXPIRE`, `TTL`, `PTTL`, `PERSIST`, `INCR`, `DECR`, `INCRBY`, `DECRBY`,
`APPEND`, `STRLEN`, `KEYS` (`*` or an exact key), `TYPE`, `DBSIZE`,
`FLUSHALL`, `SAVE`, `BGSAVE`, `INFO`, `QUIT`.

## Testing

Every module has a standalone unit test suite in `tests/` (hand-rolled
assertion macros, no external test framework), plus an end-to-end smoke
test in CI that boots the real server and drives it through the real CLI
over a real socket. All of it — unit tests included — also runs under
AddressSanitizer and UndefinedBehaviorSanitizer on every push.

## Project layout

```
include/    public headers, one per module
src/        implementations, plus main.c (server) and ember_cli_main.c (CLI)
tests/      one standalone test suite per module
docs/       architecture notes
```

## License

MIT — see [LICENSE](LICENSE).
