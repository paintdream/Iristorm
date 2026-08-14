# 02 — Design Principles

Four principles distilled from paintsnownext. They are the *why* behind most
of the conventions in these docs and behind the framework patterns in
[05-framework-patterns.md](05-framework-patterns.md).

## 1. C++/Lua co-development

> Prefer expressing cross-module composition/configuration in **Lua** over
> adding C++ cross-module references. Keep C++ modules small and decoupled.
> Delegate object lifetime, error handling and async orchestration to the Lua
> layer; do not re-implement lifecycle bookkeeping in C++.

Rationale in practice:

- C++ exposes **primitives** (types, methods, coroutines); Lua composes them
  into features and pipelines.
- The script layer is where modules meet: a Lua script wires plugin A to
  plugin B without either knowing about the other.
- Lifecycle (who owns what, when to tear down) and async state machines live
  in Lua scripts, where they are easy to inspect and change.

Reference: `tutorial/lua_event_framework/scripts/Main.lua` — task bookkeeping
(`tasks` table, `begin_task`/`end_task` hooks, result routing) is script-side;
the C++ side only provides the primitives (`input`, `step`, `push_result`).

## 2. Concurrency is designed top-down

> Design concurrency top-down with the **warp/dispatcher model**: declare
> which tasks may run in parallel and which must be serialized; rely on the
> scheduler for mutual exclusion and ordering. Do not use local blocking
> primitives (blocking waits, hand-rolled mutexes, condition-variable loops)
> as the primary coordination mechanism. Every shared container reachable from
> multiple threads must have a **stated ownership + synchronization protocol**.

Rules of thumb:

- Decide the **warp layout first**: one warp per object/stage that must be
  serialized; different warps may run in parallel.
- Once a task is queued to a warp, the scheduler guarantees mutual exclusion —
  no locks, no atomics for that data.
- A shared container is allowed only with an explicit protocol written next to
  it: "single owner", "locked", or "ordered by warp X". (Example in
  `lua_event_framework/src/event_framework.h`: the command queue is
  mutex-guarded because it is cross-thread; results and handlers are
  Lua-thread-only and lock-free.)
- `queue_routine_parallel` is the explicit escape hatch for in-warp parallel
  read-mostly work; it behaves like read locks vs write locks. Use it
  deliberately, not casually.

## 3. Self-closing program loop

> No mutable global/static state in library code; no reference cycles; teardown
> must be deterministic; exit must report **zero unfreed allocations**.

Concrete rules:

- Library code: prefer explicitly owned instances over globals/statics.
- Break every potential `SharedRef` cycle with a **raw/non-owning** link so
  destruction cascades deterministically (managers outlive managed objects).
- Release shared resources in dependency order; teardown must be deterministic
  and repeatable.
- After any lifetime or shutdown change, re-run the offline / clean-exit
  checks: the process must exit with no leaks and no assertions.

This is also why scripts must exit through the host's cleanup path (see
`lua_event_framework` docs, "Exit discipline"): a bypassed `lua_close()` leaks
the whole Lua state and trips the block-allocator clean-exit assertion.

## 4. Script callback model

> Embedded callbacks (UI/render/message handlers invoked through `lua.call`)
> must **never yield**; they act as **event senders** — fire the work, mark a
> pending state, return immediately. Async results are collected later by
> polling (the two-step pattern, see
> [03-architecture.md](03-architecture.md)).

Why: while a callback runs, the engine is synchronously waiting for its return
value. A yield mid-callback tears the execution flow into pieces the engine
cannot reassemble (it surfaces as "attempt to yield from outside a coroutine"
and corrupts the Lua stack). Yieldability is **composed by the script**: the
callback creates a Lua coroutine whose first C++ coroutine call suspends it.

This is a best-practice choice, not a framework rule: the decision procedure
is "does the current context allow blocking?" — see the "Async pattern choice"
section of the iris `AGENTS.md`.
