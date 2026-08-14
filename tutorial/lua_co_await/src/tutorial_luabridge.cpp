// tutorial_luabridge.cpp
// PaintDream (paintdream@paintdream.com)
// 2026
//

#include "tutorial_luabridge.h"

#include <cstdio>

namespace iris {
	void tutorial_shared_object_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		lua.set_current_new<&iris_lua_t::place_new_object<tutorial_shared_object_t>>("new");
		lua.set_current<&tutorial_shared_object_t::value>("value");
		lua.set_current<&tutorial_shared_object_t::add>("add");
	}

	int tutorial_shared_object_t::add(int delta) noexcept {
		value += delta;
		return value;
	}

	void tutorial_luabridge_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		lua.set_current_new<&iris_lua_t::place_new_object<tutorial_luabridge_t>>("new");
		lua.set_current<&tutorial_luabridge_t::create_bridge>("create_bridge");
		lua.set_current<&tutorial_luabridge_t::is_bridge_alive>("is_bridge_alive");
		lua.set_current<&tutorial_luabridge_t::transfer_simple_values>("transfer_simple_values");
		lua.set_current<&tutorial_luabridge_t::transfer_functions>("transfer_functions");
		lua.set_current<&tutorial_luabridge_t::transfer_objects>("transfer_objects");
		lua.set_current<&tutorial_luabridge_t::transfer_cycle_tables>("transfer_cycle_tables");
		lua.set_current<&tutorial_luabridge_t::close_bridge>("close_bridge");
		lua.set_current("run", lua.load("local self = ...\n\
print('[tutorial_luabridge] ')\n\
print('\\tcreate bridge vm = ' .. tostring(self:create_bridge()))\n\
self:transfer_simple_values()\n\
self:transfer_functions()\n\
self:transfer_objects()\n\
self:transfer_cycle_tables()\n\
self:close_bridge()\n\
print('\\talive after close = ' .. tostring(self:is_bridge_alive()))\n\
print('[tutorial_luabridge] complete!')\n"));
	}

	tutorial_luabridge_t::tutorial_luabridge_t() {}
	tutorial_luabridge_t::~tutorial_luabridge_t() noexcept {
		// force teardown on destruction
		close_bridge();
	}

	bool tutorial_luabridge_t::create_bridge() {
		if (bridge_state != nullptr) {
			return false;
		}

		bridge_state = luaL_newstate();
		luaL_openlibs(bridge_state);
		return bridge_state != nullptr;
	}

	bool tutorial_luabridge_t::is_bridge_alive() const noexcept {
		return bridge_state != nullptr;
	}

	void tutorial_luabridge_t::close_bridge() {
		if (bridge_state != nullptr) {
			// release the type registered on the bridge by transfer_objects
			// (teardown in the reverse order of creation)
			iris_lua_t bridge(bridge_state);
			bridge.clear_registry_type<tutorial_shared_object_t>();

			lua_close(bridge_state);
			bridge_state = nullptr;
		}
	}

	// ------------------------------------------------------------------
	// 2. simple values
	// ------------------------------------------------------------------

	void tutorial_luabridge_t::transfer_simple_values(iris_lua_t&& lua) {
		printf("[tutorial_luabridge] simple values:\n");
		iris_lua_t bridge(bridge_state);

		// numbers and strings are copied onto the bridge stack
		lua.native_push_variable(1234);
		lua.native_push_variable(std::string_view("hello from host"));
		lua.native_cross_transfer_variable<false>(bridge, -2);
		lua.native_cross_transfer_variable<false>(bridge, -1);
		lua.native_pop_variable(2);

		// tables are deep-copied element-wise (use <true> so nested values
		// and self references are handled by the recursive transfer)
		lua.native_push_variable(lua.make_table([](iris_lua_t lua) {
			lua.set_current("name", "host table");
			lua.set_current("count", 7);
		}));
		lua.native_cross_transfer_variable<true>(bridge, -1);
		lua.native_pop_variable(1);

		// verify inside the bridge: the three values are on its stack
		auto check = bridge.load("local a, b, c = ...\n\
print('\\tbridge sees a = ' .. tostring(a) .. ', b = ' .. tostring(b) .. ', c.name = ' .. tostring(c.name) .. ', c.count = ' .. tostring(c.count))\n\
").value();
		auto result = bridge.native_call(std::move(check), 3);
		if (!result) {
			printf("\tbridge check failed: %s\n", std::string(result.message).c_str());
		}
	}

	// ------------------------------------------------------------------
	// 3. functions
	// ------------------------------------------------------------------

	void tutorial_luabridge_t::transfer_functions(iris_lua_t&& lua) {
		printf("[tutorial_luabridge] functions:\n");
		iris_lua_t bridge(bridge_state);

		// C closure: the C function pointer is reused, upvalues are copied
		lua.native_push_variable([](const char* text) noexcept {
			printf("\tbridge called C closure: %s\n", text);
			return "no metatable";
		});
		lua.native_cross_transfer_variable<true>(bridge, -1);
		lua.native_pop_variable(1);
		auto c_func = bridge.native_get_variable<iris_lua_t::ref_t>(-1);
		bridge.native_pop_variable(1);
		bridge.call<void>(std::move(c_func), "transferred!");

		// Lua chunk: bytecode is dumped and reloaded in the bridge, _ENV is
		// remapped to the bridge's global table (so `print` resolves there)
		lua.native_push_variable(lua.load("print('bridge called Lua chunk: transferred!')"));
		lua.native_cross_transfer_variable<true>(bridge, -1);
		lua.native_pop_variable(1);
		auto lua_func = bridge.native_get_variable<iris_lua_t::ref_t>(-1);
		bridge.native_pop_variable(1);
		bridge.call<void>(std::move(lua_func));
	}

	// ------------------------------------------------------------------
	// 4. objects
	// ------------------------------------------------------------------

	void tutorial_luabridge_t::transfer_objects(iris_lua_t&& lua) {
		printf("[tutorial_luabridge] objects:\n");
		iris_lua_t bridge(bridge_state);

		// ONE type table shared by all object scenarios, registered on the
		// host. Transferring the same table twice is deduplicated by the
		// cross-VM transfer; using two different tables for the same C++ type
		// (make_type vs make_registry_type) trips the Debug assertion in the
		// second metatable transfer.
		auto host_type = lua.make_registry_type<tutorial_shared_object_t>();

		// (a) type registries are per-VM: register the type on the bridge,
		//     then create a VIEW there -- both VMs share the same C++ memory
		auto bridge_type = bridge.make_registry_type<tutorial_shared_object_t>();
		tutorial_shared_object_t shared;
		shared.value = 100;
		auto bridge_view = bridge.make_object_view<tutorial_shared_object_t>(bridge_type, &shared);
		bridge.call<void>(bridge.load("local o = ...\n\
print('\\tbridge mutates its view: ' .. tostring(o:add(5)))\n\
"), std::move(bridge_view));
		printf("\thost sees shared.value = %d (views share memory)\n", shared.value);
		// ref_t owns a registry reference: deref before it goes out of scope
		bridge.deref(std::move(bridge_type));

		// (b) a HOST view transferred to the bridge (its metatable/type is
		//     carried along automatically); the bridge sees the same object
		tutorial_shared_object_t host_obj;
		host_obj.value = 200;
		lua.native_push_variable(lua.make_object_view<tutorial_shared_object_t>(host_type, &host_obj));
		lua.native_cross_transfer_variable<false>(bridge, -1);
		lua.native_pop_variable(1);
		auto host_view = bridge.native_get_variable<iris_lua_t::ref_t>(-1);
		bridge.native_pop_variable(1);
		bridge.call<void>(bridge.load("local o = ...\n\
print('\\tbridge mutates transferred view: ' .. tostring(o:add(10)))\n\
"), std::move(host_view));
		printf("\thost sees host_obj.value = %d (transferred view still shared)\n", host_obj.value);

		// (c) a PLACEMENT object (owned by the host userdata) is
		//     move-constructed into the bridge with <true>: the bridge gets
		//     its own instance.
		lua.native_push_variable(lua.make_registry_object<tutorial_shared_object_t>());
		lua.native_cross_transfer_variable<true>(bridge, -1);
		lua.native_pop_variable(1);

		// the transferred object is now on the bridge stack; call a chunk
		// with native_call using the stack value as the argument
		auto check = bridge.load("local o = ...\n\
print('\\tbridge got placement object, type = ' .. type(o) .. ', add(1) = ' .. tostring(o:add(1)))\n\
").value();
		auto result = bridge.native_call(std::move(check), 1);
		if (!result) {
			printf("\tbridge check failed: %s\n", std::string(result.message).c_str());
		}

		lua.deref(std::move(host_type));
	}

	// ------------------------------------------------------------------
	// 5. cycle tables
	// ------------------------------------------------------------------

	void tutorial_luabridge_t::transfer_cycle_tables(iris_lua_t&& lua) {
		printf("[tutorial_luabridge] cycle tables:\n");
		iris_lua_t bridge(bridge_state);

		// build a self-referencing table inside the bridge, transfer it back
		auto make = bridge.load("local a = { b = {}, text = 'cross text' }\n\
a.b.self = a\n\
return a\n\
").value();
		auto table = bridge.call<iris_lua_t::ref_t>(std::move(make)).value();
		bridge.native_push_variable(std::move(table));
		bridge.native_cross_transfer_variable<true>(lua, -1);
		bridge.native_pop_variable(1);

		// verify on the host: the self reference survived the transfer
		auto host_ref = lua.native_get_variable<iris_lua_t::ref_t>(-1);
		lua.native_pop_variable(1);
		auto check = lua.call<void>(lua.load("local a = ...\n\
print('\\thost: a.text = ' .. tostring(a.text) .. ', self reference intact = ' .. tostring(a.b.self == a))\n\
"), std::move(host_ref));
		if (!check) {
			printf("\thost check failed: %s\n", std::string(check.message).c_str());
		}
	}
}
