// tutorial_engine.h
// PaintDream (paintdream@paintdream.com)
// 2026
//
// Frame-loop engine tutorial (distilled from test/iris_engine_demo.cpp).
// Demonstrates composing the primitives into a tiny engine:
//   - iris_barrier_t as the FRAME GATE: a pipeline coroutine awaits the
//     barrier each frame; tick() dispatches the barrier to release it
//     (dispatch() is a full participant, reusable across frames)
//   - iris_event_t for FRAME COMPLETION: tick() waits until the coroutine
//     signals the frame is done, so every dispatch is strictly paired with
//     one frame (no orphan dispatches, deterministic teardown)
//   - iris_pipe_t for FRAME-TO-FRAME data: the coroutine consumes the
//     previous frame's value at the top of each frame and produces a new
//     one at the bottom
//   - iris_switch between dedicated warps (audio -> script -> render) as a
//     serialized stage pipeline
// The engine owns its own worker pool (it must not touch the shared one),
// and runs entirely on the C++ side; Lua only drives frames (tick).

#pragma once

#include "common.h"

#include <atomic>

namespace iris {
	class tutorial_engine_t {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		tutorial_engine_t();
		~tutorial_engine_t() noexcept;

		bool start(size_t thread_count);
		void tick();
		void sleep(size_t milliseconds) const noexcept;
		void terminate() noexcept;

		bool is_running() const noexcept;
		int get_frame_count() const noexcept;
		int get_pipe_sum() const noexcept;

	protected:
		iris_coroutine_t<void> frame_pipeline();

	protected:
		static constexpr size_t total_frames = 8;

		std::shared_ptr<iris_async_worker_t<>> async_worker;
		warp_t audio_warp;
		warp_t script_warp;
		warp_t render_warp;
		// warp_t = void: continuations are dispatched straight to the worker
		// pool instead of being queued through a warp (the frame gate itself
		// needs no warp affinity)
		iris_barrier_t<void, bool, iris_async_worker_t<>> frame;
		iris_pipe_t<int, warp_t> pipe;

		std::atomic<int> frame_count{ 0 };
		std::atomic<int> pipe_sum{ 0 };
		bool started = false;
	};
}
