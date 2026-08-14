-- Main.lua — lua_event_framework demo driver
--
-- Usage:
--   lua.exe Main.lua --auto     run the automated input simulation, then exit
--                               cleanly (this is the offline/regression path)
--   lua.exe Main.lua            print usage
--
-- What this script demonstrates:
--   * The script plays the "console user": every framework:input() call below
--     is exactly what a user typing at a console would do.
--   * Typing NEVER blocks: `do work-a 4` enqueues a command and returns
--     immediately. The commands that follow it (ping/running/do work-b/poll)
--     are processed while work-a is still running — the event loop keeps
--     flowing (upstream input is not blocked by downstream async work).
--   * `poll` collects already-finished results (may be empty: the poll is
--     non-blocking), `running` inspects in-flight tasks with live progress.
--   * `quit` is graceful: it stops accepting new commands, the loop keeps
--     driving until every in-flight task finishes, then we drain results,
--     terminate the worker pool and verify a clean exit.

local framework = require("lua_event_framework").new()
local async_job_type = framework:async_job()

-- ---------------------------------------------------------------
-- Lua-side orchestration: task bookkeeping (keep orchestration in
-- Lua, not in C++ — the framework only provides the primitives).
-- ---------------------------------------------------------------
local tasks = {}          -- name -> job (in-flight tasks)
local mid_inputs = { "do work-c 2", "running", "ping" }  -- typed mid-flight
local mid_index = 1

-- ---------------------------------------------------------------
-- Command handlers.
--
-- These are EMBEDDED CALLBACKS: invoked through lua.call from
-- step(), the engine is synchronously waiting for the return value,
-- so they MUST NEVER YIELD. A command that needs async work acts as
-- an EVENT SENDER: create+resume a Lua coroutine (which will yield
-- inside the C++ coroutine method) and return immediately.
-- ---------------------------------------------------------------

framework:register_handler("do", function(args)
    local name, count = args[1], tonumber(args[2])
    local job = async_job_type.new()
    tasks[name] = job
    framework:begin_task()
    coroutine.wrap(function()
        -- A failed optional_result_t is RAISED as a Lua error by the binding
        -- layer (both sync and coroutine methods), so error paths go through
        -- pcall -- never lua.syserror / raw errors from C++.
        local ok, result = pcall(job.process, job, count)   -- yields the Lua coroutine
        framework:end_task()
        tasks[name] = nil
        if ok then
            framework:push_result(name, result)
        else
            framework:push_result(name, "ERROR: " .. tostring(result))
        end
    end)()
    print(string.format("[do] %s dispatched (%d steps), running = %d",
        name, count, framework:get_running_count()))
end)

framework:register_handler("fail", function(args)
    local name = args[1] or "fail-job"
    local job = async_job_type.new()
    -- synchronous error path: pcall captures the raised result_error_t
    local ok, err = pcall(job.fail, job, "simulated failure for " .. name)
    if ok then
        print("[fail] UNEXPECTED SUCCESS: " .. tostring(err))
    else
        print("[fail] caught result_error_t: " .. tostring(err))
    end
end)

framework:register_handler("wait", function(args)
    local ms = tonumber(args[1]) or 500
    framework:begin_task()
    coroutine.wrap(function()
        local job = async_job_type.new()
        pcall(job.wait, job, ms)                -- async sleep, yields
        framework:end_task()
        print(string.format("[wait] %d ms elapsed", ms))
    end)()
    print("[wait] dispatched")
end)

framework:register_handler("ping", function()
    print("[ping] pong! (synchronous embedded callback)")
end)

framework:register_handler("running", function()
    if next(tasks) == nil then
        print("[running] no tasks in flight")
    else
        for name, job in pairs(tasks) do
            print(string.format("[running] %s: step %d/%d",
                name, job:get_progress(), job:get_total_steps()))
        end
    end
end)

framework:register_handler("poll", function()
    local results = framework:get_finished()
    if #results == 0 then
        print("[poll] no finished results yet (non-blocking poll)")
    else
        for i = 1, #results, 2 do
            print(string.format("[poll] %s = %s",
                tostring(results[i]), tostring(results[i + 1])))
        end
    end
end)

framework:register_handler("quit", function()
    print("[quit] requested: stop accepting commands, drain in-flight tasks")
    framework:quit()
end)

framework:register_handler("help", function()
    print("commands: do <name> <steps> | fail | wait <ms> | ping | running | poll | quit")
end)

-- ---------------------------------------------------------------
-- Message loop (Lua side).
--
-- Each iteration processes at most one command and polls the main
-- warp for async completions. It never waits for any task; when
-- there is nothing to do it sleeps 1 ms and tries again. While the
-- loop runs, the script keeps "typing" commands (mid_inputs),
-- exactly like a user who keeps typing while work is in flight.
-- ---------------------------------------------------------------
local function message_loop()
    local iterations = 0
    while not framework:is_quit() do
        local busy = framework:step()
        iterations = iterations + 1

        if not busy then
            if not framework:has_pending() and not framework:has_running() then
                -- all commands processed and every task finished:
                -- the user finally types `quit`
                print("(user types: quit)")
                framework:input("quit")
                framework:step()
                break
            end
            framework:sleep(1)
        end

        -- simulate the user typing more commands WHILE work is in flight:
        -- the command queue is empty but tasks are still running
        if not framework:has_pending() and framework:has_running()
            and mid_index <= #mid_inputs then
            local line = mid_inputs[mid_index]
            mid_index = mid_index + 1
            print("(user types: " .. line .. ")")
            framework:input(line)
        end
    end

    -- graceful drain: quit() stops new commands; keep driving the loop until
    -- every in-flight task has completed (deterministic teardown)
    while framework:has_running() do
        framework:step()
        framework:sleep(1)
    end
end

-- ---------------------------------------------------------------
-- Auto mode: the offline/regression path.
-- ---------------------------------------------------------------
local function run_auto()
    print("=== lua_event_framework auto demo ===")
    print("version: " .. framework:get_version())

    if not framework:start(4) then
        print("FAILED to start worker pool")
        os.exit(1)
    end
    print("started with 4 worker threads")

    -- the user's first batch of console commands. Note that `do work-a 4`
    -- takes 4 * 150 ms = 600 ms of worker time, while all commands below
    -- are processed within microseconds -- upstream input is never blocked.
    framework:input("do work-a 4")
    framework:input("ping")
    framework:input("do work-b 6")
    framework:input("running")
    framework:input("fail")
    framework:input("poll")       -- likely empty: non-blocking poll
    -- `quit` is NOT pre-typed here: the message loop types it once all
    -- commands are processed and every task has finished.

    message_loop()

    -- after the loop: collect everything (results are drained, not lost)
    print("=== summary ===")
    print(string.format("commands processed : %d", framework:get_total_commands()))
    print(string.format("running tasks left : %d", framework:get_running_count()))
    print(string.format("in-flight tasks    : %d", next(tasks) ~= nil and 1 or 0))

    local results = framework:get_finished()
    local result_count = #results / 2
    if result_count == 0 then
        print("results: none")
    else
        for i = 1, #results, 2 do
            print(string.format("result %-12s = %s", tostring(results[i]), tostring(results[i + 1])))
        end
    end

    framework:terminate()
    print("terminated cleanly")
    collectgarbage()

    -- pass/fail: every dispatched job must have produced a result,
    -- nothing may remain in flight, no pending commands left
    local ok = framework:get_running_count() == 0
        and next(tasks) == nil
        and not framework:has_pending()
        and result_count >= 3
    print(ok and "=== auto demo PASS ===" or "=== auto demo FAIL ===")
    return ok
end

-- ---------------------------------------------------------------
-- Entry
--
-- IMPORTANT exit discipline: never call os.exit() from the script.
-- os.exit() terminates the process and bypasses the host's lua_close(),
-- leaking the Lua state and tripping the iris block-allocator clean-exit
-- assertion. Instead, return a numeric exit code: the host (src/main.cpp)
-- closes the Lua state first and then exits with the script's code.
-- ---------------------------------------------------------------
local args = { ... }
if args[1] == "--auto" then
    local ok = run_auto()
    return ok and 0 or 1
else
    print("lua_event_framework demo driver")
    print("usage: lua.exe Main.lua --auto")
    print()
    print("commands: do <name> <steps> | fail | wait <ms> | ping | running | poll | quit")
    print()
    print("interactive example (from any Lua host):")
    print("  local f = require('lua_event_framework').new()")
    print("  f:start(4)")
    print("  f:input('do work-a 4')   -- never blocks")
    print("  f:input('ping')          -- typed while work-a runs")
    print("  f:input('running')")
    print("  f:input('poll')")
    print("  while not f:is_quit() and (f:has_pending() or f:has_running()) do")
    print("      f:step()")
    print("      f:sleep(1)")
    print("  end")
    print("  f:terminate()")
    return 0
end
