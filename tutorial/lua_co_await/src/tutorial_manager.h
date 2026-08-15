// tutorial_manager.h
// PaintDream (paintdream@paintdream.com)
// 2026
//
// Host/unit lifetime pattern (see docs/05-framework-patterns.md, pattern 7;
// full-scale: paintsnownext). A Manager class manages objects of a Unit
// class, and the Manager instance must ALWAYS outlive every Unit instance.
// The pattern is purely structural:
//   1. the Manager type (metatable) is registered FIRST;
//   2. the Unit type is registered SECOND, and its metatable carries
//      __host -> the Manager instance;
//   3. every Unit userdata strongly references its metatable, so while any
//      Unit lives, the Manager lives: userdata -> metatable -> __host;
//   4. when everything dies together (a reference cycle, or lua_close),
//      Lua's GC finalizes objects in the REVERSE order of becoming
//      finalizable (metatable-set order, verified on Lua 5.5.1), so units
//      (finalizable later) are destroyed BEFORE the host (finalizable
//      first). The host's lua_finalize asserts this: every registered unit
//      must already be destroyed when the host is finalized.
//
// The __host pattern itself is strictly ONE-WAY (unit -> manager); the
// manager -> units edge is an OWNERSHIP decision (see
// docs/05-framework-patterns.md, pattern 7): holding a unit keeps it alive,
// so a bidirectional manager needs an explicit add/remove protocol. This
// tutorial implements the hold with a GC-visible edge -- a units table in
// the manager userdata's uservalue slot (lua_uservalue_count; Lua 5.4+, the
// built-in Lua 5.5 satisfies this; on 5.1-5.3 the equivalent is the
// userdata environment table via lua_setfenv) -- so that a
// mid-run collectgarbage can collect the whole cycle. A registry-reference
// hold (refptr_t / luaL_ref) would be a GC root instead: the cycle then
// only dies at lua_close, and the destruction order stays correct either
// way (units are always finalized before the host).

#pragma once

#include "common.h"

#include <atomic>

namespace iris {
	class tutorial_manager_t;

	// a managed object; its metatable carries __host -> the manager instance
	class tutorial_unit_t {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		// lifecycle hooks (called by the binding layer when the userdata is
		// created / garbage collected)
		static void lua_initialize(iris_lua_t lua, int index, tutorial_unit_t* p);
		static void lua_finalize(iris_lua_t lua, int index, tutorial_unit_t* p) noexcept;

		tutorial_unit_t();
		~tutorial_unit_t() noexcept;

		// reach the manager through the metatable __host (see the .cpp)
		iris_lua_t::refptr_t<tutorial_manager_t> get_host(iris_lua_t&& lua);
		int get_id() const noexcept;

		// called by the manager when the unit is registered (add_unit)
		void attach_host(tutorial_manager_t* host) noexcept;

		// how many unit userdata are currently alive
		static int get_alive_count() noexcept;

	protected:
		int id = 0;
		// the manager this unit is registered to (nullptr until then); lets
		// the unit report its destruction back to the host (lua_finalize)
		tutorial_manager_t* host = nullptr;
		static std::atomic<int> serial;
		static std::atomic<int> alive_count;
	};

	// the manager ("host"): owns a pool of units and must outlive all of them
	class tutorial_manager_t {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		static void lua_initialize(iris_lua_t lua, int index, tutorial_manager_t* p);
		static void lua_finalize(iris_lua_t lua, int index, tutorial_manager_t* p) noexcept;

		tutorial_manager_t();
		~tutorial_manager_t() noexcept;

		// one uservalue slot: a Lua table holding the registered units.
		// The table is part of the userdata's GC graph (not a registry
		// root), so the manager -> units edge participates in collection.
		static int lua_uservalue_count() noexcept {
			return 1;
		}

		// register a unit: appended to the units table (uservalue #1), the
		// manager -> units edge that completes the lifetime cycle
		bool add_unit(iris_lua_t&& lua, iris_lua_t::refptr_t<tutorial_unit_t>&& unit);
		int get_unit_count(iris_lua_t&& lua) const noexcept;

		// drop the type registry entries so the whole cycle can be collected
		// (the run script calls this before the final collectgarbage)
		bool clear_types(iris_lua_t&& lua);

		// lifecycle counters for the run script
		static int get_alive_host_count() noexcept;
		static int get_alive_unit_count() noexcept;

	protected:
		// the units report their destruction back through lua_finalize
		friend class tutorial_unit_t;

	protected:
		// Lua-thread-only: add_unit / clear_types / lua_finalize all run on
		// the Lua thread
		int registered_units_total = 0; // for the final report
		// units registered to THIS host that are still alive; decremented by
		// the units themselves in lua_finalize -- valid only because units
		// are destroyed before the host (the property this tutorial verifies)
		std::atomic<int> live_registered_units{ 0 };

		static std::atomic<int> host_alive_count;
	};
}
