# AGENTS.md — Iristorm (iris)

Header-only C++ async framework (M:N warp scheduler, C++20 coroutines, Lua 5.5 binding).

## Layout

- `src/iris_*.h` — the library: header-only, pure standard library, zero external dependencies.
- `test/*_demo.cpp` — standalone demos.
- `tutorial/lua_co_await` — fundamental Lua + coroutine tutorial.
- `tutorial/lua_event_framework` — event-driven framework tutorial (reference pattern for iris-based projects).

## Rules

1. Library code must not introduce external dependencies; keep it header-only.
2. Cross-module composition/configuration lives in Lua; keep C++ modules small and decoupled; object lifetime, error handling and async orchestration belong to the Lua layer — never re-implement lifecycle bookkeeping in C++.
3. Concurrency is designed top-down with the warp/dispatcher model: declare parallel vs serial boundaries and rely on the scheduler for mutual exclusion; do not use local blocking primitives (blocking waits, hand-rolled mutexes, condition-variable loops) as the primary coordination mechanism; every shared container reachable from multiple threads must declare an ownership + synchronization protocol.
4. Unless necessary, do not add global variables or rely on global initialization/termination logic; the program must always exit cleanly from its entry point without memory or resource leaks.
5. Lua binding: coroutine methods must switch back to the original warp before finishing; failures are returned as `result_error_t`, never `lua.syserror`; `ref_t` must be deref'ed before destruction.

## Async pattern choice (best practice, not enforced)

First decide whether the current context may block:

- **Blocking is allowed** (async flow on a worker) → yield directly, use a normal coroutine.
- **Blocking is not allowed** (embedded callback, e.g. a handler invoked through `lua.call`) → never yield: dispatch the work to the thread pool and let the event loop poll the result (two-step: send → poll → apply).

The latter is common; `tutorial/lua_event_framework` provides a reference implementation.

## Commit messages

Start every commit message with one of `[ADD]` `[MOD]` `[FIX]` `[DOC]` `[DEL]` followed by a space, then a short English summary. Example: `[DOC] Add AGENTS.md with project rules.`
