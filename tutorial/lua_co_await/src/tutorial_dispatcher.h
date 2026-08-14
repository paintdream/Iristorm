// tutorial_dispatcher.h
// PaintDream (paintdream@paintdream.com)
// 2026
//
// DAG-based task dispatcher tutorial (distilled from
// test/iris_dispatcher_demo.cpp graph_dispatch). Demonstrates:
//   - iris_dispatcher_t: allocate tasks (optionally bound to a warp),
//     declare partial order with order(a, b), then dispatch
//   - execution respects the dependencies: a task runs only after every
//     task ordered before it has finished
//   - the CRTP completion hooks (dispatcher_complete fires when all
//     dispatched tasks are done)
//   - cross-warp shared state needs an explicit protocol: the execution
//     log is mutex-guarded because tasks run in parallel on different warps

#pragma once

#include "common.h"

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace iris {
	class tutorial_dispatcher_t {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		tutorial_dispatcher_t(iris_async_worker_t<>& async_worker);
		~tutorial_dispatcher_t() noexcept;

		// tasks
		int add_task(std::string_view name, int warp_index);
		void order(int first, int second);
		void dispatch_all();
		bool is_done() const noexcept;
		void sleep(size_t milliseconds) const noexcept;
		// results
		lua_ref_t get_execution_log(iris_lua_t&& lua);
		int get_log_count() const noexcept;

	protected:
		struct dispatcher_impl : iris_dispatcher_t<warp_t, dispatcher_impl> {
			using base = iris_dispatcher_t<warp_t, dispatcher_impl>;
			dispatcher_impl(iris_async_worker_t<>& worker) : base(worker) {}

			// CRTP hooks (optional; default no-ops)
			void dispatcher_complete() {
				if (on_complete) {
					on_complete();
				}
			}
			void dispatcher_enter_execute(const routine_handle_t&) {}
			void dispatcher_leave_execute(const routine_handle_t&) {}

			std::function<void()> on_complete;
		};

		using routine_handle_t = dispatcher_impl::routine_handle_t;

	protected:
		dispatcher_impl dispatcher;
		std::vector<warp_t> warps;
		std::deque<routine_handle_t> handles;
		// cross-warp shared state: tasks run in parallel on different warps,
		// so the log is guarded by a mutex (ownership + sync protocol)
		std::vector<std::string> execution_log;
		mutable std::mutex log_mutex;
		std::atomic<bool> done{ false };
	};
}
