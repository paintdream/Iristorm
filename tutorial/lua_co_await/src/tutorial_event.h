// tutorial_event.h
// PaintDream (paintdream@paintdream.com)
// 2026
//
// iris_event_t: event signaling for coroutines. N coroutines co_await the
// event; a single notify() releases all of them (and stays signaled until
// reset()).

#pragma once

#include "common.h"

namespace iris {
	class tutorial_event_t {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		tutorial_event_t(iris_async_worker_t<>& async_worker);
		~tutorial_event_t() noexcept;

		// suspends the calling Lua coroutine until notify()
		coroutine_t<void> wait_event();
		// release all waiting coroutines (idempotent until reset())
		void notify();
		void reset();

	protected:
		iris_event_t<warp_t> event;
	};
}
