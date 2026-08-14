// tutorial_barrier.cpp
// PaintDream (paintdream@paintdream.com)
// 2026
//

#include "tutorial_barrier.h"

namespace iris {
	void tutorial_barrier_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		// "new" is registered by lua_co_await_t::tutorial_barrier() with the
		// worker reference and the participant count (same pattern as
		// tutorial_quota_t)
		lua.set_current<&tutorial_barrier_t::hit>("hit");
		lua.set_current("run", lua.load("local self = ...\n\
print('[tutorial_barrier] ')\n\
local running = coroutine.running()\n\
local complete_count = 0\n\
local waiting = false\n\
local loop_count = 4\n\
for i = 1, loop_count do\n\
	coroutine.wrap(function()\n\
		print('\\tparticipant ' .. tostring(i) .. ' arrived')\n\
		self:hit()\n\
		print('\\tparticipant ' .. tostring(i) .. ' released')\n\
		complete_count = complete_count + 1\n\
		if complete_count == loop_count and waiting then\n\
			waiting = false\n\
			coroutine.resume(running)\n\
		end\n\
	end)()\n\
end\n\
-- every participant suspends on the barrier; the 4th arrival releases all\n\
if complete_count ~= loop_count then\n\
	waiting = true\n\
	coroutine.yield()\n\
end\n\
print('[tutorial_barrier] complete!')\n"));
	}

	tutorial_barrier_t::tutorial_barrier_t(iris_async_worker_t<>& async_worker, size_t count) : barrier(async_worker, count) {}
	tutorial_barrier_t::~tutorial_barrier_t() noexcept {}

	coroutine_t<void> tutorial_barrier_t::hit() {
		co_await barrier;
	}
}
