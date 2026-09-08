# EmberDB

EmberDB is an in-memory key-value store server written from scratch in C,
inspired by Redis. It's a systems-programming project focused on the
fundamentals: manual memory management, a custom hash table, a hand-rolled
binary wire protocol, an epoll-based event loop, a thread pool, and
crash-safe persistence.

This project is under active development. See `docs/ARCHITECTURE.md` (coming
soon) for a deeper dive into the design once the core modules land.

## Status

Early scaffolding — memory management primitives are being built first,
followed by the data store, networking layer, and persistence.

## License

MIT — see [LICENSE](LICENSE).
