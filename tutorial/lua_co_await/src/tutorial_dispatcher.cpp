// tutorial_dispatcher.cpp
// PaintDream (paintdream@paintdream.com)
// 2026
//

#include "tutorial_dispatcher.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace iris {
	void tutorial_dispatcher_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		// "new" is registered by lua_co_await_t::tutorial_dispatcher() with
		// the worker reference (same pattern as tutorial_warp_t)
		lua.set_current<&tutorial_dispatcher_t::add_task>("add_task");
		lua.set_current<&tutorial_dispatcher_t::order>("order");
		lua.set_current<&tutorial_dispatcher_t::dispatch_all>("dispatch_all");
		lua.set_current<&tutorial_dispatcher_t::is_done>("is_done");
		lua.set_current<&tutorial_dispatcher_t::sleep>("sleep");
		lua.set_current<&tutorial_dispatcher_t::get_execution_log>("get_execution_log");
		lua.set_current<&tutorial_dispatcher_t::get_log_count>("get_log_count");
		lua.set_current("run", lua.load("local self = ...\n\
print('[tutorial_dispatcher] ')\n\
-- build a small DAG: A -> B -> {C, D} (order(a, b) means a runs before b)\n\
local a = self:add_task('A', 0)\n\
local b = self:add_task('B', 1)\n\
local c = self:add_task('C', 2)\n\
local d = self:add_task('D', 1)\n\
self:order(a, b)\n\
self:order(b, c)\n\
self:order(b, d)\n\
print('\\tDAG: A -> B -> {C, D}, dispatched')\n\
self:dispatch_all()\n\
-- tasks run on the worker pool; the dispatcher completes when all are done\n\
while not self:is_done() do\n\
	self:sleep(1)\n\
end\n\
-- verify the partial order: A before B, B before C and D\n\
local log = self:get_execution_log()\n\
local pos = {}\n\
for i = 1, #log do\n\
	pos[log[i]] = i\n\
end\n\
print('\\texecution order: ' .. table.concat(log, ' '))\n\
local ok = pos['A'] and pos['B'] and pos['C'] and pos['D']\n\
	and pos['A'] < pos['B'] and pos['B'] < pos['C'] and pos['B'] < pos['D']\n\
print('\\tpartial order verified: ' .. tostring(ok))\n\
print('\\tlog count = ' .. tostring(self:get_log_count()))\n\
print('[tutorial_dispatcher] complete!')\n"));
	}

	tutorial_dispatcher_t::tutorial_dispatcher_t(iris_async_worker_t<>& async_worker) : dispatcher(async_worker) {
		// three warps for the demo tasks; tasks on the same warp serialize,
		// tasks on different warps may run in parallel
		warps.reserve(3);
		warps.emplace_back(async_worker);
		warps.emplace_back(async_worker);
		warps.emplace_back(async_worker);

		dispatcher.on_complete = [this]() {
			done.store(true, std::memory_order_release);
		};
	}

	tutorial_dispatcher_t::~tutorial_dispatcher_t() noexcept {}

	int tutorial_dispatcher_t::add_task(std::string_view name, int warp_index) {
		warp_t* warp = (warp_index >= 0 && warp_index < static_cast<int>(warps.size())) ? &warps[warp_index] : nullptr;
		auto handle = dispatcher.allocate(warp, [this, name](const auto&) {
			// tasks on different warps may run concurrently: guard the log
			std::lock_guard<std::mutex> guard(log_mutex);
			execution_log.emplace_back(name);
		});

		handles.emplace_back(std::move(handle));
		return static_cast<int>(handles.size()) - 1;
	}

	void tutorial_dispatcher_t::order(int first, int second) {
		dispatcher.order(handles[first], handles[second]);
	}

	void tutorial_dispatcher_t::dispatch_all() {
		for (auto& handle : handles) {
			dispatcher.dispatch(std::move(handle));
		}
	}

	bool tutorial_dispatcher_t::is_done() const noexcept {
		return done.load(std::memory_order_acquire);
	}

	void tutorial_dispatcher_t::sleep(size_t milliseconds) const noexcept {
		std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
	}

	lua_ref_t tutorial_dispatcher_t::get_execution_log(iris_lua_t&& lua) {
		return lua.make_table([this](iris_lua_t lua) {
			std::lock_guard<std::mutex> guard(log_mutex);
			size_t index = 1;
			for (auto& name : execution_log) {
				lua.set_current(static_cast<int>(index), name);
				index++;
			}
		});
	}

	int tutorial_dispatcher_t::get_log_count() const noexcept {
		std::lock_guard<std::mutex> guard(log_mutex);
		return static_cast<int>(execution_log.size());
	}
}
