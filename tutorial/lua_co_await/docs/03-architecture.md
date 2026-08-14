# 03 — Architecture: Script Execution Model

The engine runs Lua scripts in cooperating execution contexts with **opposite
rules about yielding**. Understanding this model is the single most important
thing about iris-based projects.

## The three execution contexts

| Context | Created by | May yield? | Notes |
|---|---|---|---|
| **Embedded callback** | C++ invokes a Lua function through `lua.call` (UI callback, render callback, message handler) | **Never** | The engine synchronously waits for the return value |
| **Posted task** | `PaintsNow::Post()` (iris: `worker.queue(...)` / warp `queue_routine`) | No (it is not itself a yieldable context) | The task body still runs on the Lua main state; use it to defer work, not to suspend |
| **Lua coroutine** | The script: `coroutine.create` + `resume` | Yes | Calling a C++ coroutine method (`iris_coroutine_t<T>`) suspends the Lua coroutine at the method's first suspension point and resumes it when the C++ coroutine completes |

**Key composition rule:** yieldability is not a primitive of the engine — it
is **composed by the script**. Inside a callback (or a posted task) the script
manually creates a Lua coroutine and resumes it; the resume advances the
coroutine to the first suspension point and returns immediately, so the
callback never blocks. This is the **two-step composition** (post a task +
create/resume a coroutine). The coroutine lives entirely on the Lua side and
is managed by the GC, so C++ carries no bookkeeping burden.

Reference implementation: `tutorial/lua_event_framework` — `input()` enqueues
a command, `step()` invokes the registered handler via `lua.call` (embedded
callback), the handler fires `coroutine.wrap(...)()` (event sender), and the
C++ `async_job_t::process` yields between worker steps.

## The two-step async pattern

For async operations triggered from an embedded callback (the classic UI
case):

1. **Send** — the callback sets a pending flag, creates/resumes the coroutine
   (and/or posts the task), and **returns without waiting**.
2. **Poll** — a later loop poll checks the async state (e.g. `poll` command →
   `get_finished()`); results may legitimately not be ready yet.
3. **Apply** — once complete/failed, the poll site applies the result (swap
   in the new state, refresh widgets).

While the operation is in flight the UI stays in a pending state: grey out the
affected controls and tell the user the operation is still running — instead
of freezing or acting like a bug.

This is a **best practice**, not an enforced rule: first decide whether the
current context may block (see iris `AGENTS.md`, "Async pattern choice"). On a
worker where blocking is allowed, just `co_await` normally.

## Exceptions and safe handoff

- **`*Sync` variants are the accepted exception** for short synchronous work
  with a known bounded cost (e.g. `BakeSkySync`, `ResolvePipelineSync`,
  per-frame render tick). Long tasks must go through the two-step pattern, not
  through more `Sync` wrappers.
- **Handoff between an async task and a consuming loop must happen at a safe
  point** — e.g. at the top of the render callback, via an atomic swap of the
  produced state. Never have a callback replace objects the loop is currently
  using.

## VERIFIED PITFALL: warp re-entrancy deadlock

If a loop holds the script warp for the whole frame (or whole iteration), a
coroutine suspended from that context can only be resumed by re-entering the
warp — which the loop itself holds, so the two wait on each other forever
(observed as a `worker tasks=N` stall log). The fix is structural: the loop
must enter/leave the script warp **per callback**, so that between callbacks
the warp is free and worker threads can preempt it, and long async operations
must be driven by the two-step pattern (send → poll → apply) instead of being
awaited inside the held warp.

## Lua-side contract of `optional_result_t` (from the binding layer)

- A method returning `optional_result_t<T>` that **fails is raised as a Lua
  error** — for both synchronous and coroutine methods — with the message
  `C-function execution error: <message>`.
- Lua code therefore wraps Result-returning calls in `pcall`:
  `local ok, err = pcall(job.fail, job, msg)`.
- C++ code must return `result_error_t` instead of calling `lua.syserror`
  where RAII objects / references are held (a raised error unwinds through
  those frames).
