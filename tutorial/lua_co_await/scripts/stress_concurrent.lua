-- stress_concurrent.lua
-- Stress the multi-threaded tutorials with many mixed rounds.
--   fast round (every round):  event, barrier, dispatcher, engine, system, luabridge
--   medium round (every 10):   + callback (300 ms async)
--   slow round (every 100):    + warp, quota, readwrite (full concurrency)
local co = require("lua_co_await").new()
local rounds = 3000
co:start(4)

local failures = 0
local function guarded(fn)
    local ok, err = pcall(fn)
    if not ok then
        failures = failures + 1
        print("FAIL: " .. tostring(err))
    end
end

-- run a tutorial that internally yields (needs the poll loop to drive it)
local function run_async(fn)
    local running = coroutine.running()
    local done = false
    coroutine.wrap(function()
        fn()
        done = true
        coroutine.resume(running)
    end)()
    while not done do
        if not co:poll(1) then
            -- nothing pending on the main warp: busy-wait (brief)
        end
    end
end

for i = 1, rounds do
    guarded(function() run_async(function() co:tutorial_event().new():run() end) end)
    guarded(function() run_async(function() co:tutorial_barrier(4).new():run() end) end)
    guarded(function() co:tutorial_dispatcher().new():run() end)
    guarded(function() co:tutorial_engine().new():run() end)
    guarded(function() co:tutorial_system().new():run() end)
    guarded(function() co:tutorial_luabridge().new():run() end)

    if i % 10 == 0 then
        guarded(function() run_async(function() co:tutorial_callback().new():run() end) end)
    end

    if i % 100 == 0 then
        guarded(function() run_async(function() co:tutorial_warp().new():run() end) end)
        guarded(function() run_async(function() co:tutorial_quota(100).new():run() end) end)
        guarded(function() run_async(function() co:tutorial_readwrite().new():run() end) end)
        guarded(function() run_async(function() co:tutorial_async().new():run() end) end)
    end

    -- collect every round so tutorial objects (each holding worker/thread
    -- resources) do not accumulate
    collectgarbage()
    if i % 100 == 0 then
        print("round " .. i .. "/" .. rounds .. " (failures: " .. failures .. ")")
    end
end

co:terminate()
collectgarbage()
if failures == 0 then
    print("concurrent stress complete: " .. rounds .. " rounds, no failures")
    return 0
else
    print("concurrent stress FAILED: " .. failures .. " failures")
    return 1
end
