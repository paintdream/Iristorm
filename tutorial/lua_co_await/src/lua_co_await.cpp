#include "lua_co_await.h"

// register tutorial types, remove it freely
// to run tutorial, just launch the lua console, type:
// require("lua_co_await").new():run_tutorials()
//

#include "tutorial_binding.h"
#include "tutorial_async.h"
#include "tutorial_quota.h"
#include "tutorial_warp.h"
#include "tutorial_readwrite.h"
#include "tutorial_result.h"
#include "tutorial_callback.h"
#include "tutorial_module.h"
#include "tutorial_object.h"
#include "tutorial_event.h"
#include "tutorial_barrier.h"
#include "tutorial_luabridge.h"
#include "tutorial_system.h"
#include "tutorial_engine.h"
#include "tutorial_dispatcher.h"
#include "tutorial_manager.h"

namespace iris {
	void lua_co_await_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		lua.set_current_new<&iris_lua_t::place_new_object<lua_co_await_t>>("new");
		lua.set_current<&lua_co_await_t::get_version>("get_version");
		lua.set_current<&lua_co_await_t::start>("start");
		lua.set_current<&lua_co_await_t::terminate>("terminate");
		lua.set_current<&lua_co_await_t::poll>("poll");
		lua.set_current<&lua_co_await_t::tutorial_binding>("tutorial_binding");
		lua.set_current<&lua_co_await_t::tutorial_async>("tutorial_async");
		lua.set_current<&lua_co_await_t::tutorial_warp>("tutorial_warp");
		lua.set_current<&lua_co_await_t::tutorial_quota>("tutorial_quota");
		lua.set_current<&lua_co_await_t::tutorial_readwrite>("tutorial_readwrite");
		lua.set_current<&lua_co_await_t::tutorial_result>("tutorial_result");
		lua.set_current<&lua_co_await_t::tutorial_callback>("tutorial_callback");
		lua.set_current<&lua_co_await_t::tutorial_module>("tutorial_module");
		lua.set_current<&lua_co_await_t::tutorial_object>("tutorial_object");
		lua.set_current<&lua_co_await_t::tutorial_event>("tutorial_event");
		lua.set_current<&lua_co_await_t::tutorial_barrier>("tutorial_barrier");
		lua.set_current<&lua_co_await_t::tutorial_luabridge>("tutorial_luabridge");
		lua.set_current<&lua_co_await_t::tutorial_system>("tutorial_system");
		lua.set_current<&lua_co_await_t::tutorial_engine>("tutorial_engine");
		lua.set_current<&lua_co_await_t::tutorial_dispatcher>("tutorial_dispatcher");
		lua.set_current<&lua_co_await_t::tutorial_manager>("tutorial_manager");
		lua.set_current<&lua_co_await_t::run_tutorials>("run_tutorials");

		// shared-library crossing
		lua.set_current<&lua_co_await_t::__async_worker__>("__async_worker__");
	}

	lua_co_await_t::lua_co_await_t() : async_worker(std::make_shared<iris_async_worker_t<>>()) {
		reset();

		async_worker->set_priority_task_handler([this](iris_async_worker_t<>::task_base_t* task, size_t& priority) {
			main_warp->queue_routine([this, task]() {
				async_worker->execute_task(task);
			});

			return true;
		}, 0);
	}

	lua_co_await_t::~lua_co_await_t() noexcept {
		// force terminate on destructing
		terminate();
	}

	void* lua_co_await_t::__async_worker__(void* new_async_worker_ptr) {
		if (new_async_worker_ptr != nullptr && set_async_worker(*reinterpret_cast<std::shared_ptr<iris_async_worker_t<>>*>(new_async_worker_ptr))) {
			return new_async_worker_ptr;
		} else {
			return reinterpret_cast<void*>(&async_worker);
		}
	}

	bool lua_co_await_t::set_async_worker(std::shared_ptr<iris_async_worker_t<>> worker) {
		if (is_running())
			return false;

		std::swap(async_worker, worker);
		reset();
		return true;
	}

	void lua_co_await_t::reset() {
		if (main_warp_guard) {
			main_warp_guard.reset();
		}

		main_warp = std::make_unique<iris_warp_t<iris_async_worker_t<>>>(*async_worker);
		main_warp_guard = std::make_unique<iris_warp_t<iris_async_worker_t<>>::preempt_guard_t>(*main_warp, 0);
	}

	std::string_view lua_co_await_t::get_version() const noexcept {
		return "lua_co_await 0.0.0";
	}

	bool lua_co_await_t::is_running() const noexcept {
		return async_worker->get_thread_count() != 0;
	}

	bool lua_co_await_t::start(size_t thread_count) {
		if (!is_running()) {
			async_worker->resize(thread_count);
			// add current thread as an external worker
			size_t thread_index = async_worker->append(std::thread());
			async_worker->make_current(thread_index);
			async_worker->start();
			reset();

			return true;
		} else {
			return false;
		}
	}

	bool lua_co_await_t::terminate() noexcept {
		if (is_running()) {
			async_worker->terminate();
			async_worker->join();

			// manually polling events
			while (main_warp->poll()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}

			reset();
			return true;
		} else {
			return false;
		}
	}

	bool lua_co_await_t::poll(size_t delay_in_milliseconds) {
		auto guard = write_fence();
		// try to poll tasks of main_warp, also poll other tasks in given time if there is no task in main_warp.
		if (async_worker != nullptr && main_warp != nullptr) {
			// try poll
			if (main_warp->poll()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(delay_in_milliseconds));
				return true;
			} else {
				return false;
			}
		} else {
			return false;
		}
	}

	// tutorials
	iris_lua_t::ref_t lua_co_await_t::tutorial_binding(iris_lua_t&& lua) {
		return lua.make_type<tutorial_binding_t>();
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_async(iris_lua_t&& lua) {
		return lua.make_type<tutorial_async_t>();
	}
	
	iris_lua_t::ref_t lua_co_await_t::tutorial_warp(iris_lua_t&& lua) {
		assert(async_worker != nullptr);
		return lua.make_type<tutorial_warp_t>().with(lua, [&](iris_lua_t lua) {
			lua.set_current_new<&iris_lua_t::place_new_object<tutorial_warp_t, std::reference_wrapper<iris_async_worker_t<>>>>("new", std::ref(*async_worker));
		});
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_quota(iris_lua_t&& lua, size_t capacity) {
		assert(async_worker != nullptr);
		return lua.make_type<tutorial_quota_t>().with(lua, [&](iris_lua_t lua) {
			lua.set_current_new<&iris_lua_t::place_new_object<tutorial_quota_t, std::reference_wrapper<iris_async_worker_t<>>, size_t>>("new", std::ref(*async_worker), capacity);
		});
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_readwrite(iris_lua_t&& lua) {
		assert(async_worker != nullptr);
		return lua.make_type<tutorial_readwrite_t>().with(lua, [&](iris_lua_t lua) {
			lua.set_current_new<&iris_lua_t::place_new_object<tutorial_readwrite_t, std::reference_wrapper<iris_async_worker_t<>>>>("new", std::ref(*async_worker));
		});
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_result(iris_lua_t&& lua) {
		return lua.make_type<tutorial_result_t>();
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_callback(iris_lua_t&& lua) {
		assert(async_worker != nullptr);
		return lua.make_type<tutorial_callback_t>().with(lua, [&](iris_lua_t lua) {
			lua.set_current_new<&iris_lua_t::place_new_object<tutorial_callback_t, std::reference_wrapper<iris_async_worker_t<>>>>("new", std::ref(*async_worker));
		});
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_module(iris_lua_t&& lua) {
		return lua.make_type<tutorial_module_t>();
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_object(iris_lua_t&& lua) {
		return lua.make_type<tutorial_object_t>();
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_event(iris_lua_t&& lua) {
		assert(async_worker != nullptr);
		return lua.make_type<tutorial_event_t>().with(lua, [&](iris_lua_t lua) {
			lua.set_current_new<&iris_lua_t::place_new_object<tutorial_event_t, std::reference_wrapper<iris_async_worker_t<>>>>("new", std::ref(*async_worker));
		});
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_barrier(iris_lua_t&& lua, size_t count) {
		assert(async_worker != nullptr);
		return lua.make_type<tutorial_barrier_t>().with(lua, [&](iris_lua_t lua) {
			lua.set_current_new<&iris_lua_t::place_new_object<tutorial_barrier_t, std::reference_wrapper<iris_async_worker_t<>>, size_t>>("new", std::ref(*async_worker), count);
		});
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_luabridge(iris_lua_t&& lua) {
		return lua.make_type<tutorial_luabridge_t>();
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_system(iris_lua_t&& lua) {
		return lua.make_type<tutorial_system_t>();
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_engine(iris_lua_t&& lua) {
		// the engine owns its own worker pool; no shared-worker injection
		return lua.make_type<tutorial_engine_t>();
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_dispatcher(iris_lua_t&& lua) {
		assert(async_worker != nullptr);
		return lua.make_type<tutorial_dispatcher_t>().with(lua, [&](iris_lua_t lua) {
			lua.set_current_new<&iris_lua_t::place_new_object<tutorial_dispatcher_t, std::reference_wrapper<iris_async_worker_t<>>>>("new", std::ref(*async_worker));
		});
	}

	iris_lua_t::ref_t lua_co_await_t::tutorial_manager(iris_lua_t&& lua) {
		// host/unit lifetime pattern (see tutorial_manager.h and
		// docs/05-framework-patterns.md, pattern 7): the MANAGER (host)
		// metatable is registered FIRST, then the host instance is created,
		// and the UNIT metatable is registered SECOND with __host -> the
		// host instance. Lua's GC then guarantees the host outlives every
		// unit (reverse finalization order, verified on Lua 5.5.1).
		auto manager_type = lua.make_registry_type<tutorial_manager_t>();
		auto host = lua.make_registry_object<tutorial_manager_t>();
		auto unit_type = lua.make_registry_type<tutorial_unit_t>().with(lua, [&](iris_lua_t lua) {
			lua.set_current("__host", host);
		});

		// the bundle handed to the script: the two types, the host instance
		// (the one embedded in the unit metatable) and the run script
		return lua.make_table([&](iris_lua_t lua) {
			lua.set_current("manager", std::move(manager_type));
			lua.set_current("unit", std::move(unit_type));
			lua.set_current("host", std::move(host));
			lua.set_current("run", lua.load("local self = ...\n\
print('[tutorial_manager] ')\n\
-- bundle: self.manager (host type), self.unit (unit type), self.host (the\n\
-- host INSTANCE embedded in the unit metatable as __host)\n\
local unit_type = self.unit\n\
local host = self.host\n\
-- 1. the structure: unit userdata -> metatable (unit type) -> __host -> host\n\
local unit = unit_type.new()\n\
print('\\tunit #' .. tostring(unit:get_id()))\n\
print('\\tgetmetatable(unit).__host == host: ' .. tostring(getmetatable(unit).__host == host))\n\
print('\\tunit:get_host() == host: ' .. tostring(unit:get_host() == host))\n\
-- 2. strong reference: drop the direct host reference; the unit still keeps\n\
--    the host alive through metatable -> __host\n\
self.host = nil\n\
host = nil\n\
collectgarbage()\n\
local host2 = unit:get_host()\n\
print('\\thost alive after collectgarbage (via unit metatable): ' .. tostring(host2 ~= nil))\n\
-- 3. the manager -> units edge: registering units completes the cycle\n\
--    (manager -> units -> unit metatable -> __host -> manager)\n\
host2:add_unit(unit)\n\
host2:add_unit(unit_type.new())\n\
unit = nil\n\
collectgarbage()\n\
print('\\tunits alive via the manager: ' .. tostring(host2:get_unit_count() == 2))\n\
print('\\talive: hosts=' .. tostring(self.manager.get_alive_host_count()) .. ' units=' .. tostring(self.manager.get_alive_unit_count()))\n\
-- 4. teardown: clear the registry types and drop every reference. NOTE:\n\
--    the final collectgarbage must run AFTER this function returns -- the\n\
--    function's vararg tuple still references the bundle (self) while the\n\
--    function is running, so an in-function collect cannot collect the\n\
--    cycle. The caller collects and verifies via the returned counters.\n\
local alive_hosts = self.manager.get_alive_host_count\n\
local alive_units = self.manager.get_alive_unit_count\n\
host2:clear_types()\n\
self, host2, unit_type = nil\n\
return alive_hosts, alive_units\n"));
		});
	}

	void lua_co_await_t::run_tutorials(iris_lua_t::refptr_t<lua_co_await_t>&& self, iris_lua_t&& lua) {
		lua.call<void>(lua.load("local co_await = ... \n\
co_await:start(4) \n\
co_await:tutorial_binding().new():run() \n\
local complete_count = 0 \n\
coroutine.wrap(function () \n\
	co_await:tutorial_async().new():run() \n\
	print('complete async') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	co_await:tutorial_warp().new():run() \n\
	print('complete warp') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	co_await:tutorial_quota(100).new():run() \n\
	print('complete quota') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	co_await:tutorial_readwrite().new():run() \n\
	print('complete readwrite') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	co_await:tutorial_result().new():run() \n\
	print('complete result') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	co_await:tutorial_callback().new():run() \n\
	print('complete callback') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	co_await:tutorial_module().new():run() \n\
	print('complete module') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	co_await:tutorial_object().new():run() \n\
	print('complete object') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	co_await:tutorial_event().new():run() \n\
	print('complete event') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	co_await:tutorial_barrier(4).new():run() \n\
	print('complete barrier') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	co_await:tutorial_luabridge().new():run() \n\
	print('complete luabridge') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	co_await:tutorial_system().new():run() \n\
	print('complete system') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	co_await:tutorial_engine().new():run() \n\
	print('complete engine') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	co_await:tutorial_dispatcher().new():run() \n\
	print('complete dispatcher') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
coroutine.wrap(function () \n\
	local alive_hosts, alive_units = co_await:tutorial_manager():run() \n\
	collectgarbage() \n\
	collectgarbage() \n\
	print('complete manager (collected: hosts=' .. tostring(alive_hosts()) .. ' units=' .. tostring(alive_units()) .. ')') \n\
	complete_count = complete_count + 1 \n\
end)() \n\
while complete_count < 15 do \n\
	co_await:poll(1000) \n\
end \n\
co_await:terminate() \n\
collectgarbage() \n\
print('all completed')\n\
"), std::move(self));
	}
}