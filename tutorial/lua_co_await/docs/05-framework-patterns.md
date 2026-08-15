# 05 — Lua/C++ Interaction Framework Patterns

Real iris-based projects add a small framework layer on top of the iris
primitives. This doc catalogs the recurring patterns, where to see them
running (minimal: `tutorial/lua_event_framework`; full-scale:
paintsnownext), and why each one exists. The patterns are described in the
form "problem → pattern → reference".

## 1. Type-alias layer

**Problem:** application code should never spell out long iris template names
(`iris::iris_coroutine_t<iris::iris_lua_t::optional_result_t<int>>`).

**Pattern:** one header at the top of the framework maps iris types into short
project names: `Coroutine<T>`, `Result<T>`, `ResultError`, `Ref`, `RefPtr<T>`,
`SharedRef<T>`, `Warp`, `AsyncWorker`, `LuaState`, ...

**References:** `lua_event_framework/src/common.h` (minimal);
paintsnownext `src/PaintsNow.h` (full).

## 2. Object hierarchy + method guard

**Problem:** Lua-exposed C++ objects need lifetime semantics and protection
against **reentrant calls** (a callback re-entering a method of the object
whose method is still on the stack).

**Pattern:**

- A base `Object` with flags (`OBJECT_RUNNING`, ...) and
  `BeginMethod`/`EndMethod` **reentrancy guard**.
- The iris binding layer calls `lua_method_begin`/`lua_method_end` hooks
  (defined on the Lua traits type) around every bound method invocation, so
  the guard is automatic. For coroutine methods the guard is held for the
  whole async lifetime and cleared in the completion callback.
- Two ownership styles on top: **placement** (object lives inside the Lua
  userdata, Lua owns it) and **view** (userdata holds a pointer to a
  C++-owned object); `shared_object_t` adds reference counting for shared
  lifetime.

**Why:** prevents a whole class of same-object reentrancy bugs without manual
locking; cross-object concurrency stays with the warp model.

**References:** paintsnownext `src/Object.h` (`IObject` → `Object` →
`AwaitObject`/`ModuleObject`); iris README "Object Holding: Placement vs
View".

## 3. Kernel + ScriptWarp

**Problem:** the Lua state is not thread-safe; every script invocation must be
serialized, while async completions arrive from worker threads.

**Pattern:**

- The **Kernel** owns the thread pool (`async_worker`), the memory quota
  queue, and the root **ScriptWarp**.
- The ScriptWarp binds the Lua VM: script callbacks are dispatched through the
  warp's mutual exclusion, and async completions are routed back to it and
  resumed on the Lua thread by the poll loop (`main_warp->poll()` inside
  `step()`).
- A `preempt_guard` keeps the warp's tasks from being stolen by workers while
  the Lua thread is polling.

**Reference:** `lua_event_framework/src/event_framework.{h,cpp}` (the
`async_worker` + `main_warp` + `step()` combination); paintsnownext
`src/Kernel.h` + `src/Warp.h`.

## 4. Plugin / module registration

**Problem:** a project is split into modules; each module contributes types to
the Lua registry without cross-module compile-time dependencies.

**Pattern:**

- Each plugin exposes `luaopen_<name>` (iris: `iris_lua_t::forward(L, ...)`
  + `make_type<ModuleClass>()`), so `require("plugin")` works naturally.
- The module class keeps a **type registry** (`Types()` returns a table of its
  registered sub-types, e.g. `device.object`, `graphic.object`).
- Monolithic builds generate a `plugins.inl` that registers every enabled
  plugin at startup (`luaL_requiref` per plugin).

**Reference:** `lua_event_framework/src/main.cpp` (luaopen entry);
paintsnownext `src/PaintsNowModule.h` + `doc/overview.md` §4.

## 5. Result-based error handling

**Problem:** errors must cross the C++/Lua boundary without breaking RAII or
crashing the event loop.

**Pattern:**

- Methods return `optional_result_t<T>`; failures return `result_error_t`.
- The binding layer **raises** a failed result as a Lua error; Lua wraps
  calls in `pcall` (see [03-architecture.md](03-architecture.md)).
- The C++ dispatch site (`lua.call`) catches handler errors and reports them,
  so one bad command can never break the loop.

**Reference:** `lua_event_framework/src/async_job.{h,cpp}` (`fail`),
`event_framework.cpp` (`process_command` error report).

## 6. Zero-copy stack exchange (advanced)

**Pattern:** a `StackIndex` type carrying `{lua_State* dataStack, int index}`
allows moving values between Lua states / stacks with `lua_xmove` instead of
serializing through tables — used for data exchange between VMs.

**Reference:** paintsnownext `src/PaintsNow.h` (`StackIndex`),
`plugin/luabridge` (multi-VM exchange).

## 7. Hosted objects: `__host` lifetime anchor (Manager/Unit)

**Problem:** a Manager class manages objects of a Unit class, and the Manager
instance must **always outlive** every Unit instance. Hand-enforcing "destroy
units before the manager" is error-prone at `lua_close` or when reference
cycles exist.

**Pattern:** make the ordering structural, entirely on the Lua side:

1. Register the **Manager metatable FIRST** (its type table), then create the
   Manager instance.
2. Register the **Unit metatable SECOND**, with a `__host` field in the unit
   type table pointing at the Manager instance.
3. Every Unit userdata strongly references its metatable, so while any Unit
   (or the Unit type) lives, the Manager lives:
   `unit userdata → metatable → __host → manager instance`. Dropping the
   script's direct Manager reference changes nothing.
4. When everything dies together (a reference cycle, or `lua_close`), Lua's
   GC finalizes objects in the **reverse order of becoming finalizable**
   (the order their metatables were set; verified on Lua 5.5.1): the Unit
   userdata (metatable set later) is destroyed **before** the Manager
   (metatable set first).

**Scope — one-way only:** the `__host` chain is a **one-way** reference
(unit → manager). In pure Lua this distinction is invisible: the GC
collects bidirectional cycles naturally, so nobody thinks twice. In a
Lua/C++ project it is a trap: the C++-side hold is not explicit in the
script, and a `refptr_t` / `luaL_ref` reference is a GC **root** — a
"silent" bidirectional binding (the manager holds its units, nothing ever
removes them) keeps every unit alive until `lua_close`. If the manager must
hold its units, make it an explicit **add/remove** protocol (see below).

The Manager's `lua_finalize` can therefore assert "every registered unit is
already destroyed" — if the order were ever wrong, the assert fires. This
order guarantee is independent of how the manager references its units:
even a registry-anchored cycle (units held through `luaL_ref` GC roots) dies
in the right order at `lua_close`.

The technique only covers the unit → manager direction. The reverse
direction is an ownership decision with its own rules:

- **One-way binding (recommended default):** the manager does not hold its
  units. No cycle exists; units are owned by their natural owners and the
  `__host` chain alone guarantees the order.
- **Bidirectional binding:** holding a unit is an explicit "keep alive"
  decision, so the system needs an explicit **add/remove** protocol — a
  unit stays alive until it is removed from the manager or the manager dies.
- **When the hold can be collected** depends on its implementation: registry
  references are GC roots (the cycle survives until `lua_close`; the order
  at close is still correct), while a GC-visible edge (e.g. a units table in
  a userdata **uservalue slot**, `lua_uservalue_count()`, Lua 5.4+ — on
  5.1-5.3 the equivalent is the userdata environment table via
  `lua_setfenv`) allows a mid-run `collectgarbage()` to collect the whole
  cycle — units first, host last.
- **Bidirectional without registry roots (per-instance types):** give each
  manager instance its own unit type (per-instance types, not global
  registry types) and keep the units ref table in the unit metatable; C++
  addresses units by an integer index into that table, looked up on demand
  through the metatable — no persistent Lua reference is held in C++, so no
  new reference type is needed.

Script-level note for self-verifying teardowns: the final
`collectgarbage()` must run **outside** the function that received the
bundle — while a Lua function runs, its `...` vararg tuple still references
the call arguments, so an in-function collect cannot collect the objects the
function itself is holding.

**Reference:** `tutorial/lua_co_await/src/tutorial_manager.{h,cpp}` (runnable
module `tutorial_manager`); paintsnownext (host/module instances embedded in
object metatables).

## Putting it together

The minimal end-to-end reference is `tutorial/lua_event_framework`: alias
layer (1) → event loop with script warp (3) → module entry (4) → result
handling (5), with the two-step async pattern from
[03-architecture.md](03-architecture.md) driving the demo. paintsnownext is
the full-scale instance of every pattern above.
