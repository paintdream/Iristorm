// tutorial_module.cpp
// PaintDream (paintdream@paintdream.com)
// 2026
//

#include "tutorial_module.h"

namespace iris {
	void tutorial_item_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		lua.set_current_new<&iris_lua_t::place_new_object<tutorial_item_t>>("new");
		lua.set_current<&tutorial_item_t::get_name>("get_name");
		lua.set_current<&tutorial_item_t::value>("value");
	}

	tutorial_item_t::tutorial_item_t() {}
	tutorial_item_t::~tutorial_item_t() noexcept {}

	std::string_view tutorial_item_t::get_name() const noexcept {
		return "tutorial_item";
	}

	void tutorial_module_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		lua.set_current_new<&iris_lua_t::place_new_object<tutorial_module_t>>("new");
		lua.set_current<&tutorial_module_t::types>("types");
		lua.set_current("run", lua.load("local self = ...\n\
print('[tutorial_module] ')\n\
-- the module exposes a type registry, like ModuleObject::Types()\n\
local types = self:types()\n\
print('\\tregistry: item = ' .. tostring(types.item))\n\
local item = types.item.new()\n\
item.value = 42\n\
print('\\titem: name=' .. item:get_name() .. ' value=' .. tostring(item.value))\n\
print('[tutorial_module] complete!')\n"));
	}

	tutorial_module_t::tutorial_module_t() {}
	tutorial_module_t::~tutorial_module_t() noexcept {}

	lua_ref_t tutorial_module_t::types(iris_lua_t&& lua) {
		return lua.make_table([&](iris_lua_t lua) {
			lua.set_current("item", lua.make_type<tutorial_item_t>());
		});
	}
}
