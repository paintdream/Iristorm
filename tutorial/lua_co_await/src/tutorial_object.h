// tutorial_object.h
// PaintDream (paintdream@paintdream.com)
// 2026
//
// Minimal Object with a method reentrancy guard (see
// docs/05-framework-patterns.md section 2; full-scale: paintsnownext src/Object.h).
//
// The binding layer detects the static lua_method_begin/lua_method_end
// templates on the class (has_lua_method_begin trait) and wraps EVERY bound
// method invocation with them, so the guard is automatic. A method that is
// still on the stack rejects re-entrant calls on the same object.

#pragma once

#include "common.h"

#include <atomic>

namespace iris {
	class tutorial_object_t {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		// lifecycle hooks (called by the binding layer when the userdata is
		// created / garbage collected)
		static void lua_initialize(iris_lua_t lua, int index, tutorial_object_t* p);
		static void lua_finalize(iris_lua_t lua, int index, tutorial_object_t* p) noexcept;

		// method guard hooks: detected by the binding layer automatically
		template <typename T>
		static void lua_method_begin(iris_lua_t lua, T* object, bool is_coroutine) {
			object->begin_method(lua, is_coroutine);
		}

		template <typename T>
		static void lua_method_end(iris_lua_t lua, T* object, bool is_coroutine) noexcept {
			object->end_method(lua, is_coroutine);
		}

		tutorial_object_t();
		~tutorial_object_t() noexcept;

		// a normal bound method
		int touch() noexcept;
		// calls a Lua callback; the callback re-enters touch() on the same
		// object, which the guard rejects (surfaces as a Lua error)
		void reenter(iris_lua_t&& lua, lua_ref_t&& callback);
		static int get_alive_count() noexcept;

	protected:
		void begin_method(iris_lua_t lua, bool is_coroutine);
		void end_method(iris_lua_t lua, bool is_coroutine) noexcept;

	protected:
		enum : uint32_t {
			OBJECT_RUNNING = 1 << 0,
		};

		uint32_t flags = 0;
		static std::atomic<int> alive_count;
	};
}
