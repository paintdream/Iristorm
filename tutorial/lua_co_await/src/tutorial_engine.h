// tutorial_engine.h
// PaintDream (paintdream@paintdream.com)
// 2026
//
// Frame-loop engine tutorial (distilled from test/iris_engine_demo.cpp).
// The frame sync follows the test EXACTLY: there is no "wait for the frame
// to finish" step. tick() only waits for the barrier's complete callback
// (the frame gate opening); the pipeline coroutine is resident and advances
// one frame per gate opening:
//   - iris_barrier_t as the FRAME GATE (warp_t = void, participants:
//     the coroutine's co_await + tick()'s dispatch)
//   - iris_pipe_t for FRAME-TO-FRAME data: the coroutine consumes the
//     previous frame's value at the top of each frame and produces a new
//     one at the bottom
//   - iris_switch between dedicated warps (audio -> script -> render) as a
//     serialized stage pipeline
//   - stopping: set_value(false) + one final dispatch lets the coroutine
//     observe value == false and exit (the test's set_value(false) pattern)
// The engine owns its own worker pool and runs entirely on the C++ side;
// Lua only drives frames (tick) and teardown (terminate).

#pragma once

#include "common.h"

#include <atomic>
#include <condition_variable>
#include <mutex>

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
		void stop();

	protected:
		std::shared_ptr<iris_async_worker_t<>> async_worker;
		warp_t audio_warp;
		warp_t script_warp;
		warp_t render_warp;
		// warp_t = void: continuations are dispatched straight to the worker
		// pool instead of being queued through a warp (the frame gate itself
		// needs no warp affinity)
		iris_barrier_t<void, bool, iris_async_worker_t<>> frame;
		iris_pipe_t<int, warp_t> pipe;

		// frame-gate synchronization (same pattern as test/iris_engine_demo):
		// the complete callback sets frame_opened and notifies; tick() waits
		// on it. dispatch() is called WITHOUT holding the mutex (the callback
		// takes the mutex itself), mirroring the test's lock discipline.
		mutable std::mutex frame_mutex;
		std::condition_variable frame_cv;
		bool frame_opened = false;

		std::atomic<int> frame_count{ 0 };
		std::atomic<int> pipe_sum{ 0 };
		std::atomic<bool> pipeline_done{ false };
		bool started = false;
	};
}
