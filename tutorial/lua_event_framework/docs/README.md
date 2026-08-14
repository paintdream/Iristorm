# lua_event_framework — Event-Driven Lua + C++ Framework Tutorial

A minimal **event-driven framework** built on top of the iris header-only
library. It is the distilled, runnable form of the "lua/cpp interaction
framework" patterns used by real iris-based projects (see
[paintsnownext](https://github.com/paintdream/paintsnownext)):

> **receive event → forward to worker → poll result in the event loop**

with two hard rules:

1. **No synchronous blocking flow** — the event loop never waits for any
   task. Heavy work runs on the worker pool; results come back asynchronously.
2. **Async flows can yield** — a Lua coroutine calling a C++ coroutine method
   suspends at the first suspension point; the event loop keeps running and
   other commands/coroutines interleave freely.

Zero external dependencies: only the iris headers, the standard library, the
built-in Lua 5.5 and the CMake template from `tutorial/lua_co_await`.

---

## 1. How input works (the core idea)

This project has **no input thread and no stdin**. The script *is* the user:
`framework:input("do work-a 4")` is exactly what typing a line at a console
would do. `input()` only parses the line, enqueues it and returns immediately —
**typing never blocks**, and it never waits for previous commands to finish.

```
scripts/Main.lua (the "console user")
   │  framework:input("do work-a 4")      ── enqueue, return immediately
   │  framework:input("ping")             ── typed while work-a is still running
   │  framework:input("running")          ── inspect in-flight tasks
   │  framework:input("poll")             ── collect already-finished results
   │  framework:input("quit")             ── graceful shutdown
   ▼
event_framework_t (src/event_framework.{h,cpp})
   ├─ command_queue   cross-thread, mutex-guarded (ownership + sync protocol)
   ├─ step()          process ONE command (embedded callback) + poll main warp
   ├─ main_warp       async completions are routed here and resumed on the
   │                  Lua thread during step()
   └─ finished_results  two-step result queue, drained by get_finished()
   ▼
async_job_t (src/async_job.{h,cpp})   multi-step coroutine pipeline
   ├─ co_await iris_switch(nullptr)   → worker pool: heavy step (sleep 150 ms)
   ├─ co_await iris_switch(current)   → main warp: publish progress
   └─ ... loops, then co_return result_t<int> → Lua coroutine resumes
```

### The message loop lives in Lua

`step()` is a single-step primitive: process at most one pending command and
poll the main warp once. The loop itself is a Lua `while`:

```lua
while not framework:is_quit() do
    local busy = framework:step()
    if not busy then framework:sleep(1) end   -- 1 ms pacing, never task-waiting
    -- ... the script may input() more commands at any time ...
end
```

Because the loop is script-side, "typing while work is in flight" is
demonstrated directly: `Main.lua` keeps calling `framework:input(...)` inside
the loop (`mid_inputs`), exactly like a user who keeps typing.

## 2. Execution contexts (same model as paintsnownext)

| Context | Semantics | In this project |
|---|---|---|
| **Embedded callback** (handler) | Invoked through `lua.call` from `step()`; the engine synchronously waits for the return value; **must never yield** | `register_handler("do", function(args) ... end)` |
| **Event sender** | A handler that needs async work creates + resumes a Lua coroutine and returns immediately | the `do` handler: `coroutine.wrap(...)()` |
| **Worker task** | Heavy step on the thread pool, never touches the Lua state | inside `async_job_t::process` after `iris_switch(nullptr)` |
| **Lua coroutine** | Created by the script; calling a C++ coroutine method suspends it at the first suspension point; resumed by the framework when the C++ coroutine completes | `local ok, result = job:process(count)` |

Corollary: the C++ coroutine must `co_await iris_switch(current)` back to the
main warp before it finishes, so that the binding layer resumes the Lua
coroutine on the Lua thread (inside `step()`'s `main_warp->poll()`).

### The two-step async pattern

There is **no "wait for task" API**. Results are produced into
`finished_results` and collected later:

1. **Send**: the handler fires the coroutine, returns instantly.
2. **Poll**: the `poll` command calls `framework:get_finished()` — it may be
   empty ("no finished results yet (non-blocking poll)") and that is fine.
3. **Observe**: the `running` command reads live per-job progress.

### Error convention (Result)

C++ methods return `result_t<T>` (`optional_result_t<T>`): a success value or
a `result_error_t`. **The binding layer RAISES a failed `optional_result_t`
as a Lua error** — for both synchronous and coroutine methods — with the
message `"C-function execution error: <message>"` (see `iris_lua.h`
`function_proxy_dispatch` / `function_coroutine_proxy_dispatch`). Therefore
Lua code must wrap Result-returning calls in `pcall`:

```lua
local ok, err = pcall(job.fail, job, "simulated failure")  -- ok=false, err=message
local ok, value = pcall(job.process, job, 4)               -- ok=true, value=400
```

This mirrors paintsnownext's "wrap callbacks in pcall" convention, and it is
the reason functions that own RAII objects / hold references must return
`result_error_t` instead of calling `lua.syserror` (a raised error unwinds
through frames that may hold live references).

### Exit discipline (VERIFIED PITFALL)

**Never call `os.exit()` from the script.** `os.exit()` terminates the
process and bypasses the host's `lua_close()`: the Lua state is never
collected, coroutine frames are never released, and the iris block allocator
trips `IRIS_ASSERT(blocks.empty())` during static destruction (Debug build;
observed live during the development of this tutorial). Instead, **return a
numeric exit code from the script**: the host (`src/main.cpp`) closes the Lua
state first and then exits with the script's code:

```lua
-- Main.lua
local ok = run_auto()
return ok and 0 or 1   -- never os.exit(...)
```

The same discipline applies to real projects: teardown must be deterministic
and exit must report zero unfreed allocations (paintsnownext's IRISLEAK /
clean-exit checks).

## 3. Commands

| Command | Behavior | Demonstrates |
|---|---|---|
| `do <name> <steps>` | dispatch an async multi-step job | event sender + yield + parallelism |
| `fail` | job returns `result_error_t`, caught via `pcall` | error convention (no `lua.syserror`) |
| `wait <ms>` | async sleep | yielding without blocking the loop |
| `ping` | synchronous reply | embedded callback, sync path |
| `running` | list in-flight jobs + progress | observing async state |
| `poll` | drain finished results | two-step result collection |
| `quit` | graceful shutdown | deterministic teardown |

## 4. Running

```
# offline/regression path (fully deterministic, no interaction):
cd build64/Debug
.\lua.exe ..\..\scripts\Main.lua --auto
echo %ERRORLEVEL%        # 0 = PASS, 1 = FAIL

# expected auto output (abridged):
#   [do] work-a dispatched (4 steps), running = 1
#   [ping] pong! (synchronous embedded callback)
#   [do] work-b dispatched (6 steps), running = 2
#   [running] work-a: step 1/4
#   [fail] caught result_error_t: C-function execution error: ...
#   [poll] no finished results yet (non-blocking poll)
#   (user types: do work-c 2)          <- typed while work-a/b still run
#   (user types: running)
#   (user types: quit)
#   result work-a = 400 / work-b = 600 / work-c = 200
#   === auto demo PASS ===

# interactive use from any Lua host (e.g. the lua.exe console):
local f = require("lua_event_framework").new()
f:start(4)
f:input("do work-a 4")          -- never blocks
f:input("ping")                 -- typed while work-a runs
f:input("running")
f:input("poll")
while not f:is_quit() and (f:has_pending() or f:has_running()) do
    f:step()
    f:sleep(1)
end
f:terminate()
```

## 5. Lua API

| Method | Description |
|---|---|
| `new()` | create the framework |
| `start(thread_count)` | start the worker pool (registers the Lua thread as an external worker) |
| `terminate()` | stop workers, join, drain pending warp tasks |
| `input(line)` | enqueue a command line `"<name> <arg1> ..."`; returns immediately |
| `has_pending()` | is there a queued command? |
| `step()` | process one command + poll the main warp; true if something was done |
| `sleep(ms)` | synchronous short sleep (loop pacing only) |
| `is_quit()` / `quit()` | graceful shutdown flag |
| `get_running_count()` / `begin_task()` / `end_task()` | in-flight task counter (Lua-side bookkeeping hooks) |
| `push_result(name, value)` / `get_finished()` | two-step result queue (`{name1, value1, ...}`) |
| `register_handler(name, fn)` | register an embedded callback (args arrive as an array table) |
| `async_job()` | returns the `async_job` type (`async_job().new()`) |

### async_job

| Method | Description |
|---|---|
| `process(steps)` | multi-step pipeline, yields the calling Lua coroutine; returns `ok, result` (or `nil, error` on failure) |
| `fail(message)` | returns `result_error_t` (raised as a Lua error; catch with `pcall(job.fail, job, msg)` → `ok=false, err=message`) |
| `wait(ms)` | async sleep (yields) |
| `get_name()` / `get_progress()` / `get_total_steps()` | live state for the `running` command |

## 6. Map to paintsnownext

| lua_event_framework | paintsnownext |
|---|---|
| `event_framework_t` + `main_warp` | `PaintsNowModule` + `Kernel` + `ScriptWarp` |
| `input()` (script-side console) | glfw callbacks → window message loop |
| Lua-side message loop over `step()` | `PaintsNow::Join` / `Graphic::Create` window loop |
| handlers (embedded callbacks) | `SetUICallback` / `MessageHandler` |
| `do` handler = event sender | UI callback two-step pattern (`RebuildBody`/`PollRebuild`/`ApplyRebuildSwap`) |
| `job:process()` multi-step coroutine | `BakeSky` / `ResolvePipeline` coroutines |
| `push_result`/`get_finished` | async state + poll-and-apply |
| `result_t<>` / `result_error_t` | `optional_result_t<>` / `result_error_t` |
| `--auto` offline run | `run_regression.ps1` offline regression |
| graceful drain + clean exit stats | IRISLEAK / clean-exit discipline |
| `common.h` alias layer | `src/PaintsNow.h` type-alias layer |
| mutex-guarded command queue | stated ownership + sync protocol per shared container |

## 7. Extending this framework

- **New commands**: register a handler in `Main.lua` — no C++ change needed.
- **New async capabilities**: add methods returning `coroutine_t<result_t<T>>`
  to `async_job_t` (or a new type) following the switch-out / switch-back
  pattern; never touch the Lua state while detached.
- **Real event sources**: replace `input()` with whatever produces events in
  your host (window callbacks, network messages, ...). The protocol stays:
  enqueue → `step()` on the Lua thread → poll results.
