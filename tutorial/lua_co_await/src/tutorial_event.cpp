// tutorial_event.cpp
// PaintDream (paintdream@paintdream.com)
// 2026
//

#include "tutorial_event.h"

namespace iris {
	void tutorial_event_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		// "new" is registered by lua_co_await_t::tutorial_event() with the
		// worker reference (same pattern as tutorial_warp_t)
		lua.set_current<&tutorial_event_t::wait_event>("wait_event");
		lua.set_current<&tutorial_event_t::notify>("notify");
		lua.set_current<&tutorial_event_t::reset>("reset");
		lua.set_current("run", lua.load("local self = ...\n\
print('[tutorial_event] ')\n\
local running = coroutine.running()\n\
local complete_count = 0\n\
local waiting = false\n\
local loop_count = 3\n\
for i = 1, loop_count do\n\
	coroutine.wrap(function()\n\
		print('\\twaiter ' .. tostring(i) .. ' waiting')\n\
		self:wait_event()\n\
		print('\\twaiter ' .. tostring(i) .. ' released')\n\
		complete_count = complete_count + 1\n\
		if complete_count == loop_count and waiting then\n\
			waiting = false\n\
			coroutine.resume(running)\n\
		end\n\
	end)()\n\
end\n\
-- all waiters are now suspended on the event; a single notify() releases all\n\
self:notify()\n\
if complete_count ~= loop_count then\n\
	waiting = true\n\
	coroutine.yield()\n\
end\n\
self:reset()\n\
print('[tutorial_event] complete!')\n"));
	}

	tutorial_event_t::tutorial_event_t(iris_async_worker_t<>& async_worker) : event(async_worker) {}
	tutorial_event_t::~tutorial_event_t() noexcept {}

	coroutine_t<void> tutorial_event_t::wait_event() {
		co_await event;
	}

	void tutorial_event_t::notify() {
		event.notify();
	}

	void tutorial_event_t::reset() {
		event.reset();
	}
}
