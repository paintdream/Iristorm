// tutorial_callback.h
// PaintDream (paintdream@paintdream.com)
// 2026
//
// The script execution model (see docs/03-architecture.md):
//   - register_callback stores a Lua handler (embedded callback).
//   - emit() simulates an engine event: it invokes the handler through
//     lua.call and synchronously waits for its return value. The handler
//     must NEVER yield; if it needs async work it acts as an EVENT SENDER:
//     create+resume a Lua coroutine (which yields inside async_work) and
//     return immediately.
//   - Results are collected later with poll_results() (the two-step pattern:
//     send -> poll -> apply).

#pragma once

#include "common.h"

#include <atomic>
#include <deque>
#include <string>
#include <unordered_map>

namespace iris {
	class tutorial_callback_t {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);
		// release every registry reference held by ref_t members before the
		// member destructors run (ref_t must be LUA_REFNIL on destruction)
		static void lua_finalize(iris_lua_t lua, int index, tutorial_callback_t* p);

		tutorial_callback_t(iris_async_worker_t<>& async_worker);
		~tutorial_callback_t() noexcept;

		// embedded callback registry
		bool register_callback(std::string_view name, lua_ref_t&& handler);
		// simulate an engine event: synchronously invoke the handler via
		// lua.call and return its result (a failing handler surfaces as a
		// failed result here, never breaking the caller)
		result_t<int> emit(iris_lua_t&& lua, std::string_view name, int value);

		// async work for the event-sender pattern: yields the calling Lua
		// coroutine and runs on the worker pool
		coroutine_t<result_t<int>> async_work(size_t steps);

		// two-step result collection (called from the resumed coroutine /
		// the polling site; both on the Lua thread)
		void submit_result(std::string_view name, lua_ref_t&& value);
		lua_ref_t poll_results(iris_lua_t&& lua);
		int get_pending_count() const noexcept;

	protected:
		iris_warp_t<iris_async_worker_t<>> stage_warp;

		// Lua-thread-only state
		std::unordered_map<std::string, lua_ref_t> callbacks;
		std::deque<std::pair<std::string, lua_ref_t>> results;
		std::atomic<int> pending{ 0 };
	};
}
