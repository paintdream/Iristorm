// tutorial_object.cpp
// PaintDream (paintdream@paintdream.com)
// 2026
//

#include "tutorial_object.h"

#include <cstdio>

namespace iris {
	std::atomic<int> tutorial_object_t::alive_count{ 0 };

	void tutorial_object_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		lua.set_current_new<&iris_lua_t::place_new_object<tutorial_object_t>>("new");
		lua.set_current<&tutorial_object_t::touch>("touch");
		lua.set_current<&tutorial_object_t::reenter>("reenter");
		lua.set_current<&tutorial_object_t::get_alive_count>("get_alive_count");
		lua.set_current("run", lua.load("local self = ...\n\
print('[tutorial_object] ')\n\
print('\\talive after new = ' .. tostring(self:get_alive_count()))\n\
print('\\ttouch #1 -> ' .. tostring(self:touch()))\n\
print('\\ttouch #2 -> ' .. tostring(self:touch()))\n\
-- reentrancy: reenter() calls a Lua callback; the callback calls touch()\n\
-- on the SAME object while reenter() is still on the stack -> the method\n\
-- guard rejects it (the binding layer raises a Lua error, caught here)\n\
local ok, err = pcall(self.reenter, self, function() return self:touch() end)\n\
print('\\treenter -> ok=' .. tostring(ok) .. ' err=' .. tostring(err))\n\
-- the guard was cleared when reenter() returned: normal calls work again\n\
print('\\ttouch #3 -> ' .. tostring(self:touch()))\n\
-- drop the last reference: the GC runs lua_finalize (see C++ output)\n\
self = nil\n\
collectgarbage()\n\
print('[tutorial_object] complete!')\n"));
	}

	void tutorial_object_t::lua_initialize(iris_lua_t lua, int index, tutorial_object_t* p) {
		alive_count.fetch_add(1, std::memory_order_relaxed);
		printf("[tutorial_object] lua_initialize, alive = %d\n", alive_count.load(std::memory_order_relaxed));
	}

	void tutorial_object_t::lua_finalize(iris_lua_t lua, int index, tutorial_object_t* p) noexcept {
		alive_count.fetch_sub(1, std::memory_order_relaxed);
		printf("[tutorial_object] lua_finalize, alive = %d\n", alive_count.load(std::memory_order_relaxed));
	}

	tutorial_object_t::tutorial_object_t() {}
	tutorial_object_t::~tutorial_object_t() noexcept {}

	void tutorial_object_t::begin_method(iris_lua_t lua, bool is_coroutine) {
		if (flags & OBJECT_RUNNING) {
			// reentrant call on the same object: reject
			lua.syserror("error.race", "tutorial_object_t::begin_method() -> Cannot call a method before previous one completed.");
		}

		flags |= OBJECT_RUNNING;
	}

	void tutorial_object_t::end_method(iris_lua_t lua, bool is_coroutine) noexcept {
		flags &= ~OBJECT_RUNNING;
	}

	int tutorial_object_t::touch() noexcept {
		return 1;
	}

	void tutorial_object_t::reenter(iris_lua_t&& lua, lua_ref_t&& callback) {
		// this method holds OBJECT_RUNNING; the callback below tries to call
		// touch() on the same object -> begin_method raises
		auto result = lua.call<int>(callback);
		if (!result) {
			printf("[tutorial_object] reenter caught on the C++ side: %s\n", std::string(result.message).c_str());
		}

		lua.deref(std::move(callback));
	}

	int tutorial_object_t::get_alive_count() noexcept {
		return alive_count.load(std::memory_order_relaxed);
	}
}
