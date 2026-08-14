-- stress_lifecycle.lua
-- Stress the worker-pool lifecycle: repeated start()/terminate() cycles.
-- This exercises append()/make_current() accumulation and clean teardown.
local co = require("lua_co_await").new()

local rounds = 5000
for i = 1, rounds do
    if not co:start(4) then
        print("FAIL: start rejected at round " .. i)
        return 1
    end
    if not co:terminate() then
        print("FAIL: terminate rejected at round " .. i)
        return 1
    end
    if i % 500 == 0 then
        print("lifecycle round " .. i .. "/" .. rounds)
    end
end

collectgarbage()
print("lifecycle stress complete: " .. rounds .. " rounds")
return 0
