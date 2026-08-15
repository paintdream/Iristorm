// tutorial_manager.cpp
// PaintDream (paintdream@paintdream.com)
// 2026
//
// See tutorial_manager.h for the pattern this module demonstrates.

#include "tutorial_manager.h"

#include <cstdio>

namespace iris {
	std::atomic<int> tutorial_unit_t::serial{ 0 };
	std::atomic<int> tutorial_unit_t::alive_count{ 0 };
	std::atomic<int> tutorial_manager_t::host_alive_count{ 0 };

	void tutorial_unit_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		lua.set_current_new<&iris_lua_t::place_new_object<tutorial_unit_t>>("new");
		lua.set_current<&tutorial_unit_t::get_host>("get_host");
		lua.set_current<&tutorial_unit_t::get_id>("get_id");
	}

	void tutorial_unit_t::lua_initialize(iris_lua_t lua, int index, tutorial_unit_t* p) {
		p->id = serial.fetch_add(1);
		alive_count.fetch_add(1);
	}

	void tutorial_unit_t::lua_finalize(iris_lua_t lua, int index, tutorial_unit_t* p) noexcept {
		// report the destruction back to the host. This is only valid because
		// units are finalized BEFORE the host (see the header): the host's
		// C++ object is still alive at this point.
		if (p->host != nullptr) {
			p->host->live_registered_units.fetch_sub(1);
		}

		alive_count.fetch_sub(1);
		printf("[tutorial_manager] unit #%d destroyed\n", p->id);
	}

	tutorial_unit_t::tutorial_unit_t() {}
	tutorial_unit_t::~tutorial_unit_t() noexcept {}

	iris_lua_t::refptr_t<tutorial_manager_t> tutorial_unit_t::get_host(iris_lua_t&& lua) {
		lua_State* L = lua.get_state();
		iris_lua_t::stack_guard_t guard(L);

		// self (this unit's userdata) is at index 1; its metatable is the
		// unit type table, which carries __host -> the manager instance
		if (!lua_getmetatable(L, 1)) {
			return iris_lua_t::refptr_t<tutorial_manager_t>();
		}

		lua_getfield(L, -1, "__host");
		if (lua_type(L, -1) != LUA_TUSERDATA) {
			lua_pop(L, 2); // metatable + field
			return iris_lua_t::refptr_t<tutorial_manager_t>();
		}

		// luaL_ref pops the __host userdata; the metatable is still on the
		// stack (popped at the end). ref_t::as performs the __typeid check
		// (same conversion as a bound method argument) and duplicates the
		// registry reference; the temporary holder is then released
		// (ref_t must be LUA_REFNIL on destruction).
		iris_lua_t::ref_t holder(luaL_ref(L, LUA_REGISTRYINDEX));
		auto host = holder.as<iris_lua_t::refptr_t<tutorial_manager_t>>(lua);
		holder.deref(lua);

		lua_pop(L, 1); // pop the metatable
		return host;
	}

	int tutorial_unit_t::get_id() const noexcept {
		return id;
	}

	void tutorial_unit_t::attach_host(tutorial_manager_t* host) noexcept {
		this->host = host;
	}

	int tutorial_unit_t::get_alive_count() noexcept {
		return alive_count.load();
	}

	void tutorial_manager_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		// NOTE: no "new" here -- the host instance is created once by
		// lua_co_await_t::tutorial_manager() (a single host per type pair,
		// like paintsnownext's Kernel/ModuleObject)
		lua.set_current<&tutorial_manager_t::add_unit>("add_unit");
		lua.set_current<&tutorial_manager_t::get_unit_count>("get_unit_count");
		lua.set_current<&tutorial_manager_t::clear_types>("clear_types");
		lua.set_current<&tutorial_manager_t::get_alive_host_count>("get_alive_host_count");
		lua.set_current<&tutorial_manager_t::get_alive_unit_count>("get_alive_unit_count");
	}

	void tutorial_manager_t::lua_initialize(iris_lua_t lua, int index, tutorial_manager_t* p) {
		// uservalue #1: the units table. It is part of the userdata's GC
		// graph (not a registry root), so the manager -> units edge can be
		// collected together with the cycle.
		lua_State* L = lua.get_state();
		lua_newtable(L);
		lua_setiuservalue(L, index, 1); // pops the table

		host_alive_count.fetch_add(1);
	}

	void tutorial_manager_t::lua_finalize(iris_lua_t lua, int index, tutorial_manager_t* p) noexcept {
		// THE guarantee this tutorial demonstrates: every unit registered to
		// this host must already be destroyed. If Lua's finalization order
		// were different, this assert fires (units would still be alive).
		IRIS_ASSERT(p->live_registered_units.load() == 0);

		// NOTE: no release needed for the units table -- it is a uservalue
		// and is collected together with this userdata
		host_alive_count.fetch_sub(1);

		printf("[tutorial_manager] host destroyed (%d registered units, all destroyed first)\n", p->registered_units_total);
	}

	tutorial_manager_t::tutorial_manager_t() {}
	tutorial_manager_t::~tutorial_manager_t() noexcept {}

	bool tutorial_manager_t::add_unit(iris_lua_t&& lua, iris_lua_t::refptr_t<tutorial_unit_t>&& unit) {
		lua_State* L = lua.get_state();
		iris_lua_t::stack_guard_t guard(L);

		unit->attach_host(this);
		live_registered_units.fetch_add(1);
		registered_units_total++;

		// append the unit to the units table (uservalue #1 of self, which
		// is at index 1). The table stores a strong, GC-visible reference;
		// the temporary registry reference from the argument is released
		// afterwards (refptr_t must be LUA_REFNIL on destruction).
		lua_getiuservalue(L, 1, 1);
		IRIS_ASSERT(lua_istable(L, -1));
		lua_rawgeti(L, LUA_REGISTRYINDEX, unit.get_ref_index());
		lua_rawseti(L, -2, static_cast<int>(lua_rawlen(L, -2)) + 1);
		lua_pop(L, 1); // pop the units table

		unit.deref(lua);
		return true;
	}

	int tutorial_manager_t::get_unit_count(iris_lua_t&& lua) const noexcept {
		lua_State* L = lua.get_state();
		iris_lua_t::stack_guard_t guard(L);

		// self (this manager's userdata) is at index 1; count the entries
		// of the units table (uservalue #1)
		lua_getiuservalue(L, 1, 1);
		IRIS_ASSERT(lua_istable(L, -1));
		int count = static_cast<int>(lua_rawlen(L, -1));
		lua_pop(L, 1); // pop the units table
		return count;
	}

	bool tutorial_manager_t::clear_types(iris_lua_t&& lua) {
		// drop the registry entries created by tutorial_manager(); the type
		// tables then live only through the remaining script/cycle
		// references and can be collected together with the objects
		lua.clear_registry_type<tutorial_manager_t>();
		lua.clear_registry_type<tutorial_unit_t>();
		return true;
	}

	int tutorial_manager_t::get_alive_host_count() noexcept {
		return host_alive_count.load();
	}

	int tutorial_manager_t::get_alive_unit_count() noexcept {
		return tutorial_unit_t::get_alive_count();
	}
}
