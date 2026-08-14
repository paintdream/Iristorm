// tutorial_warp.h
// PaintDream (paintdream@paintdream.com)
// 2023-06-01
//
// Warp mutual-exclusion tutorial: warp_variable is protected by the stage
// warp (always consistent), free_variable is deliberately accessed from
// multiple threads WITHOUT a warp to demonstrate the race. free_variable is
// std::atomic so the demo race stays defined behavior (results are simply
// nondeterministic) -- a plain int would be a C++ data race (UB).

#pragma once
#include "common.h"

#include <atomic>

namespace iris {
	class tutorial_warp_t : enable_read_write_fence_t<> {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		tutorial_warp_t(iris_async_worker_t<>& async_worker);
		~tutorial_warp_t() noexcept;

		iris_coroutine_t<void> pipeline();
		int get_free_variable() const noexcept;

	protected:
		iris_warp_t<iris_async_worker_t<>> stage_warp;
		int warp_variable = 0;
		std::atomic<int> free_variable{ 0 };
	};
}