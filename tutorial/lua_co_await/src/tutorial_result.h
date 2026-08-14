// tutorial_result.h
// PaintDream (paintdream@paintdream.com)
// 2026
//
// Error convention: return optional_result_t<T> / result_error_t instead of
// calling lua.syserror (unsafe in functions that own RAII objects / hold
// references). The binding layer RAISES a failed optional_result_t as a Lua
// error ("C-function execution error: <message>"), so Lua code wraps
// Result-returning calls in pcall. See docs/03-architecture.md.

#pragma once

#include "common.h"

namespace iris {
	class tutorial_result_t {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		tutorial_result_t();
		~tutorial_result_t() noexcept;

		// success: returns the value (Lua: single return value)
		result_t<int> work(int value);
		// failure: returns result_error_t (Lua: raises, catch with pcall)
		result_t<int> fail(std::string_view message);
	};
}
