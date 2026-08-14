// tutorial_engine.cpp
// PaintDream (paintdream@paintdream.com)
// 2026
//

#include "tutorial_engine.h"

#include <cstdio>
#include <thread>

namespace iris {
	void tutorial_engine_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		lua.set_current_new<&iris_lua_t::place_new_object<tutorial_engine_t>>("new");
		lua.set_current<&tutorial_engine_t::start>("start");
		lua.set_current<&tutorial_engine_t::tick>("tick");
		lua.set_current<&tutorial_engine_t::sleep>("sleep");
		lua.set_current<&tutorial_engine_t::terminate>("terminate");
		lua.set_current<&tutorial_engine_t::is_running>("is_running");
		lua.set_current<&tutorial_engine_t::get_frame_count>("get_frame_count");
		lua.set_current<&tutorial_engine_t::get_pipe_sum>("get_pipe_sum");
		lua.set_current("run", lua.load("local self = ...\n\
print('[tutorial_engine] ')\n\
print('\\tstart: ' .. tostring(self:start(4)))\n\
-- drive 8 frames: each tick() opens the frame gate (complete callback);\n\
-- the pipeline coroutine runs one frame (audio -> script -> render). Note\n\
-- that tick() does NOT wait for the frame to finish (same as the test);\n\
-- the last frame completes asynchronously after tick 8 returns.\n\
for i = 1, 8 do\n\
	self:tick()\n\
end\n\
-- terminate() stops the pipeline (set_value(false) + final dispatch) and\n\
-- waits for the coroutine to exit, so all 8 frames are complete afterwards\n\
self:terminate()\n\
print('\\tframes = ' .. tostring(self:get_frame_count()) .. ' (expected 8)')\n\
print('\\tpipe sum = ' .. tostring(self:get_pipe_sum()) .. ' (expected 21 = 0+1+...+6)')\n\
print('\\tterminated, running = ' .. tostring(self:is_running()))\n\
print('[tutorial_engine] complete!')\n"));
	}

	tutorial_engine_t::tutorial_engine_t() : async_worker(std::make_shared<iris_async_worker_t<>>()),
		audio_warp(*async_worker), script_warp(*async_worker), render_warp(*async_worker),
		frame(*async_worker, 2, true), pipe(*async_worker) {}

	tutorial_engine_t::~tutorial_engine_t() noexcept {
		// force teardown on destruction
		terminate();
	}

	bool tutorial_engine_t::is_running() const noexcept {
		return started;
	}

	bool tutorial_engine_t::start(size_t thread_count) {
		if (started) {
			return false;
		}

		async_worker->resize(thread_count);
		async_worker->start();

		// launch the pipeline coroutine: it runs to its first suspension
		// point (co_await frame) and waits for tick() to open the gate
		frame_pipeline().run();
		started = true;
		return true;
	}

	void tutorial_engine_t::tick() {
		if (!started) {
			return;
		}

		// dispatch() is a FULL participant of the frame barrier: coroutine +
		// dispatch = 2 arrivals -> the gate opens for exactly one frame.
		// The complete callback fires when the gate opens; tick() waits for
		// the CALLBACK, not for the frame to finish (same as the test's
		// cv.wait on the dispatch callback). dispatch is called WITHOUT the
		// mutex: the callback takes the mutex itself, so there is no lock
		// ordering issue whether the callback runs on this thread or on a
		// worker.
		{
			std::lock_guard<std::mutex> lock(frame_mutex);
			frame_opened = false;
		}

		frame.dispatch([this](auto&) {
			std::lock_guard<std::mutex> lock(frame_mutex);
			frame_opened = true;
			frame_cv.notify_one();
		});

		std::unique_lock<std::mutex> guard(frame_mutex);
		frame_cv.wait(guard, [this] { return frame_opened; });
	}

	void tutorial_engine_t::sleep(size_t milliseconds) const noexcept {
		std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
	}

	void tutorial_engine_t::stop() {
		// let the resident coroutine exit: clear the gate value and open the
		// gate once more; the coroutine observes value == false, leaves the
		// loop and signals pipeline_done
		frame.set_value(false);
		frame.dispatch([](auto&) {});
	}

	void tutorial_engine_t::terminate() noexcept {
		if (started) {
			started = false;

			// stop the pipeline and wait for it to finish its current frame
			// and exit, then tear down the worker pool (deterministic teardown)
			stop();
			while (!pipeline_done.load(std::memory_order_relaxed)) {
				std::this_thread::sleep_for(std::chrono::microseconds(100));
			}

			async_worker->terminate();
			async_worker->join();

			// drain any remaining warp tasks
			while (warp_t::poll({ std::ref(audio_warp), std::ref(script_warp), std::ref(render_warp) })) {
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		}
	}

	int tutorial_engine_t::get_frame_count() const noexcept {
		return frame_count.load(std::memory_order_relaxed);
	}

	int tutorial_engine_t::get_pipe_sum() const noexcept {
		return pipe_sum.load(std::memory_order_relaxed);
	}

	iris_coroutine_t<void> tutorial_engine_t::frame_pipeline() {
		int frame_index = 0;
		while (co_await frame) {
			// frame N consumes the value produced at the end of frame N-1
			// (the pipe is empty during frame 0)
			if (frame_index > 0) {
				int value = co_await pipe;
				pipe_sum.fetch_add(value, std::memory_order_relaxed);
			}

			// serialized stage pipeline: each stage runs on its own warp
			co_await iris_switch(&audio_warp);
			co_await iris_switch(&script_warp);
			co_await iris_switch(&render_warp);

			// produce this frame's value for the next frame
			pipe.emplace(frame_index);
			frame_index++;
			frame_count.store(frame_index, std::memory_order_relaxed);
		}

		pipeline_done.store(true, std::memory_order_relaxed);
	}
}
