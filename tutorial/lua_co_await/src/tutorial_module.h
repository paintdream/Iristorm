// tutorial_module.h
// PaintDream (paintdream@paintdream.com)
// 2026
//
// Module/type-registry pattern (see docs/05-framework-patterns.md §4):
// a module class exposes a Types() table of its registered sub-types, so
// scripts construct plugin types without compile-time C++ dependencies
// (paintsnownext: ModuleObject::Types()).

#pragma once

#include "common.h"

namespace iris {
	class tutorial_item_t {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		tutorial_item_t();
		~tutorial_item_t() noexcept;

		std::string_view get_name() const noexcept;
		int value = 0; // read/write property
	};

	class tutorial_module_t {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		tutorial_module_t();
		~tutorial_module_t() noexcept;

		// returns the type registry: { item = <tutorial_item_t type> }
		lua_ref_t types(iris_lua_t&& lua);
	};
}
