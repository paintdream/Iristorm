// async_job.h
// PaintDream (paintdream@paintdream.com)
// 2026
//
// A demo of a multi-step asynchronous pipeline driven from Lua.
//
// process(steps) is a C++20 coroutine bound to Lua: calling it from a Lua
// coroutine yields the Lua coroutine immediately (the binding layer suspends
// it at the first suspension point of the C++ coroutine). Each step:
//
//   co_await iris_switch(nullptr)  -> detach to the worker pool, do heavy work
//   co_await iris_switch(current)  -> return to the main warp, publish progress
//
// While the pipeline is suspended the event loop keeps running: other commands
// are processed and other jobs make progress concurrently. progress is updated
// on the main warp and read from Lua ("running" command); std::atomic keeps the
// contract explicit even though the current access pattern is single-threaded.
//
// fail(message) demonstrates the error convention: return result_error_t
// instead of raising lua.syserror (which is unsafe in functions that own RAII
// objects / hold references). It is intentionally SYNCHRONOUS. NOTE: the
// binding layer RAISES a failed optional_result_t as a Lua error -- for both
// sync and coroutine methods ("C-function execution error: <message>", see
// iris_lua.h function_proxy_dispatch / function_coroutine_proxy_dispatch) --
// so Lua code must wrap Result-returning calls in pcall and treat
// (ok, value-or-error). This mirrors paintsnownext's "wrap callbacks in
// pcall" convention.

#pragma once

#include "common.h"

#include <atomic>
#include <string>

namespace iris {
	class async_job_t {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		async_job_t();
		~async_job_t() noexcept;

		// multi-step pipeline; yields the calling Lua coroutine
		coroutine_t<result_t<int>> process(size_t steps);
		// simulated failure: returns result_error_t synchronously
		result_t<int> fail(std::string_view message);
		// async sleep (never blocks the event loop)
		coroutine_t<void> wait(size_t milliseconds);

		std::string_view get_name() const noexcept;
		int get_progress() const noexcept;
		size_t get_total_steps() const noexcept;

	protected:
		std::string name;
		size_t total_steps = 0;
		std::atomic<int> progress{ 0 };

		static std::atomic<size_t> serial;
	};
}
