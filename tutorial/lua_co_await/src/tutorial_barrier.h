// tutorial_barrier.h
// PaintDream (paintdream@paintdream.com)
// 2026
//
// iris_barrier_t: N coroutines co_await the barrier; all of them are released
// together once the N-th participant arrives (rendezvous).

#pragma once

#include "common.h"

namespace iris {
	class tutorial_barrier_t {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		tutorial_barrier_t(iris_async_worker_t<>& async_worker, size_t count);
		~tutorial_barrier_t() noexcept;

		// suspends the calling Lua coroutine until every participant arrived
		coroutine_t<void> hit();

	protected:
		iris_barrier_t<warp_t, bool> barrier;
	};
}
