// async_job.cpp
// PaintDream (paintdream@paintdream.com)
// 2026
//

#include "async_job.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace iris {
	std::atomic<size_t> async_job_t::serial{ 0 };

	void async_job_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		lua.set_current_new<&iris_lua_t::place_new_object<async_job_t>>("new");
		lua.set_current<&async_job_t::process>("process");
		lua.set_current<&async_job_t::fail>("fail");
		lua.set_current<&async_job_t::wait>("wait");
		lua.set_current<&async_job_t::get_name>("get_name");
		lua.set_current<&async_job_t::get_progress>("get_progress");
		lua.set_current<&async_job_t::get_total_steps>("get_total_steps");
	}

	async_job_t::async_job_t() {
		size_t id = serial.fetch_add(1, std::memory_order_relaxed);
		name = "job-" + std::to_string(id);
	}

	async_job_t::~async_job_t() noexcept {}

	std::string_view async_job_t::get_name() const noexcept {
		return name;
	}

	int async_job_t::get_progress() const noexcept {
		return progress.load(std::memory_order_relaxed);
	}

	size_t async_job_t::get_total_steps() const noexcept {
		return total_steps;
	}

	coroutine_t<result_t<int>> async_job_t::process(size_t steps) {
		total_steps = steps;
		progress.store(0, std::memory_order_relaxed);

		// detach to the worker pool: heavy work must never run on the Lua thread.
		// This is the first suspension point -- the calling Lua coroutine yields
		// here and the event loop keeps running.
		warp_t* current = co_await iris_switch(static_cast<warp_t*>(nullptr));

		for (size_t i = 0; i < steps; i++) {
			// heavy step, executed on a worker thread
			std::this_thread::sleep_for(std::chrono::milliseconds(150));

			// back to the main warp: publish progress here (shared-state updates
			// happen inside the warp context, not on a detached worker)
			co_await iris_switch(current);
			progress.store(static_cast<int>(i + 1), std::memory_order_relaxed);

			// detach again for the next step
			co_await iris_switch(static_cast<warp_t*>(nullptr));
		}

		// back to the main warp; the binding layer resumes the Lua coroutine
		// with the result value
		co_await iris_switch(current);
		co_return result_t<int>(static_cast<int>(steps * 100));
	}

	result_t<int> async_job_t::fail(std::string_view message) {
		// error convention: return result_error_t instead of lua.syserror.
		// Synchronous methods surface this as (nil, message) in Lua.
		return result_t<int>(result_error_t(message));
	}

	coroutine_t<void> async_job_t::wait(size_t milliseconds) {
		// async sleep: detach to a worker, sleep there, come back. The event
		// loop is never blocked and other commands keep flowing.
		warp_t* current = co_await iris_switch(static_cast<warp_t*>(nullptr));
		std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
		co_await iris_switch(current);
	}
}
