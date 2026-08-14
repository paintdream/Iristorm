// tutorial_readwrite.h
// PaintDream (paintdream@paintdream.com)
// 2023-06-01
//
// Read/write fence tutorial. IMPORTANT (VERIFIED PITFALL, 2026): the fences
// are DETECTORS, not synchronizers -- write_fence asserts that no reader
// holds the monitor (exchange(~0) must see 0). A "serialized write stage"
// alone does NOT guarantee that all readers have finished: another coroutine
// may still be inside its read block. Every phase switch must therefore be
// synchronized with a barrier (all readers done -> write stage -> all
// writers done -> next read stage).

#pragma once
#include "common.h"

namespace iris {
	class tutorial_readwrite_t : enable_read_write_fence_t<> {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		tutorial_readwrite_t(iris_async_worker_t<>& async_worker);
		~tutorial_readwrite_t() noexcept;

		iris_coroutine_t<void> pipeline();

	protected:
		iris_warp_t<iris_async_worker_t<>> stage_warp;
		// phase gate: N participants must all finish a phase before the next
		// one starts (N = loop_count from the run script)
		iris_barrier_t<warp_t, bool> phase_barrier;
	};
}