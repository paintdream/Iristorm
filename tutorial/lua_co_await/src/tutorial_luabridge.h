// tutorial_luabridge.h
// PaintDream (paintdream@paintdream.com)
// 2026
//
// Cross-Lua-VM tutorial: how to create an independent Lua VM ("bridge") and
// move values between VMs (see docs/05-framework-patterns.md §6; full-scale:
// paintsnownext plugin/luabridge). Steps:
//   1. create_bridge        — a second, fully independent lua_State
//   2. transfer_simple_values — numbers/strings copied, tables deep-copied
//   3. transfer_functions   — C closures (upvalues copied) and Lua chunks
//      (bytecode dumped + reloaded, _ENV remapped to the target globals)
//   4. transfer_objects     — types are per-VM; views share C++ memory,
//      placement objects are move/copy-constructed into the target
//   5. transfer_cycle_tables — self-referencing tables survive (cycle map)
//   6. close_bridge         — teardown in the reverse order

#pragma once

#include "common.h"

namespace iris {
	// a small object used to demonstrate cross-VM object transfer
	class tutorial_shared_object_t {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		int value = 0;
		int add(int delta) noexcept;
	};

	class tutorial_luabridge_t {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		tutorial_luabridge_t();
		~tutorial_luabridge_t() noexcept;

		// 1. create an independent bridge VM
		bool create_bridge();
		bool is_bridge_alive() const noexcept;
		// 2. simple values and deep-copied tables
		void transfer_simple_values(iris_lua_t&& lua);
		// 3. C closures and Lua chunks, called inside the bridge
		void transfer_functions(iris_lua_t&& lua);
		// 4. type registration per VM + view/placement object transfer
		void transfer_objects(iris_lua_t&& lua);
		// 5. self-referencing tables survive the transfer
		void transfer_cycle_tables(iris_lua_t&& lua);
		// 6. close the bridge VM
		void close_bridge();

	protected:
		lua_State* bridge_state = nullptr;
	};
}
