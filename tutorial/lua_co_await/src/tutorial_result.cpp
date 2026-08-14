// tutorial_result.cpp
// PaintDream (paintdream@paintdream.com)
// 2026
//

#include "tutorial_result.h"

namespace iris {
	void tutorial_result_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		lua.set_current_new<&iris_lua_t::place_new_object<tutorial_result_t>>("new");
		lua.set_current<&tutorial_result_t::work>("work");
		lua.set_current<&tutorial_result_t::fail>("fail");
		lua.set_current("run", lua.load("local self = ...\n\
print('[tutorial_result] ')\n\
local value = self:work(42)\n\
print('\\twork(42) -> ' .. tostring(value))\n\
local ok, err = pcall(self.fail, self, 'simulated failure')\n\
print('\\tfail() -> ok=' .. tostring(ok) .. ' err=' .. tostring(err))\n\
print('[tutorial_result] complete!')\n"));
	}

	tutorial_result_t::tutorial_result_t() {}
	tutorial_result_t::~tutorial_result_t() noexcept {}

	result_t<int> tutorial_result_t::work(int value) {
		return result_t<int>(value);
	}

	result_t<int> tutorial_result_t::fail(std::string_view message) {
		// never lua.syserror here: return the error instead
		return result_t<int>(result_error_t(message));
	}
}
