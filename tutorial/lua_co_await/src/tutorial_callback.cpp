// tutorial_callback.cpp
// PaintDream (paintdream@paintdream.com)
// 2026
//

#include "tutorial_callback.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace iris {
	void tutorial_callback_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		// "new" is registered by lua_co_await_t::tutorial_callback() with the
		// worker reference (same pattern as tutorial_warp_t)
		lua.set_current<&tutorial_callback_t::register_callback>("register_callback");
		lua.set_current<&tutorial_callback_t::emit>("emit");
		lua.set_current<&tutorial_callback_t::async_work>("async_work");
		lua.set_current<&tutorial_callback_t::submit_result>("submit_result");
		lua.set_current<&tutorial_callback_t::poll_results>("poll_results");
		lua.set_current<&tutorial_callback_t::get_pending_count>("get_pending_count");
		lua.set_current("run", lua.load("local self = ...\n\
print('[tutorial_callback] ')\n\
local running = coroutine.running()\n\
local waiting, complete\n\
-- 1. register embedded callbacks (Lua handlers invoked through lua.call)\n\
self:register_callback('sync', function(value) return value * 2 end)\n\
self:register_callback('async', function(value)\n\
	print('\\tasync event received: ' .. tostring(value))\n\
	-- EVENT SENDER: never yield here; fire a coroutine and return\n\
	coroutine.wrap(function()\n\
		local ok, result = pcall(self.async_work, self, 3)\n\
		self:submit_result('async', ok and result or ('ERROR: ' .. tostring(result)))\n\
		complete = true\n\
		if waiting then waiting = false; coroutine.resume(running) end\n\
	end)()\n\
end)\n\
-- 2. sync path: emit synchronously waits for the handler's return value\n\
local ret = self:emit('sync', 21)\n\
print('\\tsync emit -> ' .. tostring(ret))\n\
-- 3. failing handler: the engine catches it (lua.call), Lua catches too\n\
self:register_callback('bad', function() error('handler exploded') end)\n\
local ok2, err2 = pcall(self.emit, self, 'bad', 1)\n\
print('\\tbad handler -> ok=' .. tostring(ok2) .. ' err=' .. tostring(err2))\n\
-- 4. async path: emit returns immediately (handler only fired the event)\n\
self:emit('async', 7)\n\
print('\\tasynchronous emit returned immediately')\n\
-- 5. two-step: poll (may legitimately be empty, non-blocking)\n\
local results = self:poll_results()\n\
print('\\tpoll #1 -> ' .. tostring(#results) .. ' result(s) (non-blocking)')\n\
-- 6. wait for the async completion (driven by the outer poll loop)\n\
if not complete then\n\
	waiting = true\n\
	coroutine.yield()\n\
end\n\
-- 7. poll again: the result is ready now\n\
results = self:poll_results()\n\
for i = 1, #results, 2 do\n\
	print('\\tpoll #2 -> ' .. tostring(results[i]) .. ' = ' .. tostring(results[i + 1]))\n\
end\n\
print('[tutorial_callback] complete!')\n"));
	}

	void tutorial_callback_t::lua_finalize(iris_lua_t lua, int index, tutorial_callback_t* p) {
		for (auto& entry : p->callbacks) {
			lua.deref(std::move(entry.second));
		}
		p->callbacks.clear();

		for (auto& entry : p->results) {
			lua.deref(std::move(entry.second));
		}
		p->results.clear();
	}

	tutorial_callback_t::tutorial_callback_t(iris_async_worker_t<>& async_worker) : stage_warp(async_worker) {}
	tutorial_callback_t::~tutorial_callback_t() noexcept {}

	bool tutorial_callback_t::register_callback(std::string_view name, lua_ref_t&& handler) {
		callbacks[std::string(name)] = std::move(handler);
		return true;
	}

	result_t<int> tutorial_callback_t::emit(iris_lua_t&& lua, std::string_view name, int value) {
		auto it = callbacks.find(std::string(name));
		if (it == callbacks.end()) {
			return result_t<int>(result_error_t("unknown callback: " + std::string(name)));
		}

		// embedded callback: synchronously wait for the return value
		return lua.call<int>(it->second, value);
	}

	coroutine_t<result_t<int>> tutorial_callback_t::async_work(size_t steps) {
		// detach to the worker pool; the calling Lua coroutine yields here
		warp_t* current = co_await iris_switch(static_cast<warp_t*>(nullptr));

		for (size_t i = 0; i < steps; i++) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		// back to the original warp; the binding layer resumes the Lua coroutine
		co_await iris_switch(current);
		co_return result_t<int>(static_cast<int>(steps * 100));
	}

	void tutorial_callback_t::submit_result(std::string_view name, lua_ref_t&& value) {
		results.emplace_back(std::string(name), std::move(value));
		pending.fetch_sub(1, std::memory_order_relaxed);
	}

	lua_ref_t tutorial_callback_t::poll_results(iris_lua_t&& lua) {
		return lua.make_table([this](iris_lua_t lua) {
			size_t index = 1;
			for (auto& entry : results) {
				lua.set_current(static_cast<int>(index), entry.first);
				lua.set_current(static_cast<int>(index + 1), entry.second);
				// the table now holds the values; release the registry refs
				lua.deref(std::move(entry.second));
				index += 2;
			}

			results.clear();
		});
	}

	int tutorial_callback_t::get_pending_count() const noexcept {
		return pending.load();
	}
}
