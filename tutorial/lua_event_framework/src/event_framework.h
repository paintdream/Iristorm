// event_framework.h
// PaintDream (paintdream@paintdream.com)
// 2026
//
// A minimal event-driven framework built on iris primitives. It demonstrates
// the "receive event -> forward to worker -> poll result in the event loop"
// pattern used by real iris-based projects (see paintsnownext):
//
//   - INPUT: commands are injected from Lua with input(), exactly like a user
//     typing at a console. input() only enqueues and returns immediately, so
//     typing NEVER blocks and never waits for previous work to finish.
//   - HANDLERS: registered Lua functions invoked through lua.call from step().
//     They are EMBEDDED CALLBACKS and must NEVER yield. If a command needs
//     asynchronous work, the handler acts as an EVENT SENDER: it creates and
//     resumes a Lua coroutine (which yields inside a C++ coroutine method)
//     and returns immediately.
//   - WORKERS: heavy steps of a coroutine run on the worker thread pool after
//     co_await iris_switch(nullptr); completion is routed back to the main
//     warp so the Lua coroutine resumes on the Lua thread during step().
//   - EVENT LOOP: driven from Lua with step() (process one command + poll the
//     main warp). The loop never blocks on any task; results are collected
//     with get_finished() (the "poll" command) and in-flight tasks are
//     observed with get_running_count()/Lua-side bookkeeping (the "running"
//     command).
//
// Threading protocol (mirrors the "ownership + synchronization" principle):
//   - command_queue: cross-thread, guarded by command_mutex (single writer,
//     single reader today; the lock documents the protocol for growth).
//   - finished_results / handlers: only touched on the Lua thread (inside
//     step() and coroutine resumes), no lock needed.
//   - running_count / quit_flag: std::atomic, written and read by the Lua
//     thread and observed from the Lua loop.

#pragma once

#include "common.h"

#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace iris {
	class async_job_t;

	class event_framework_t : enable_read_write_fence_t<> {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);
		// Called by the binding layer when the Lua userdata is garbage
		// collected (the Lua state is still alive here). ref_t holds a raw
		// registry reference and must be deref'ed before destruction; this is
		// where handlers/results are released.
		static void lua_finalize(iris_lua_t lua, int index, event_framework_t* p);

		event_framework_t();
		~event_framework_t() noexcept;

		// ---- lifecycle -------------------------------------------------
		std::string_view get_version() const noexcept;
		bool is_running() const noexcept;
		bool start(size_t thread_count);
		void terminate() noexcept;

		// ---- input (simulated console; never blocks) -------------------
		// Enqueue one command line ("<name> <arg1> <arg2> ..."). Returns
		// immediately; the command is processed by a later step() call.
		bool input(std::string_view command_line);
		bool has_pending() const noexcept;
		size_t get_total_commands() const noexcept;

		// ---- event loop primitives (driven from Lua) -------------------
		// Process at most one pending command and poll the main warp for
		// async completions. Returns true when something was done.
		bool step(iris_lua_t&& lua);
		// Synchronous short sleep for the Lua-side loop pacing (1 ms).
		void sleep(size_t milliseconds) const noexcept;

		// ---- status ----------------------------------------------------
		bool is_quit() const noexcept;
		void quit();
		size_t get_running_count() const noexcept;
		bool has_running() const noexcept;
		// Lua-side task bookkeeping hooks (called by the coroutine wrapper).
		void begin_task();
		void end_task();

		// ---- two-step results ------------------------------------------
		// push_result: called by the Lua coroutine wrapper when a task
		// finishes (on the Lua thread). get_finished: drains the queue into
		// a Lua table {name1, value1, name2, value2, ...} (the "poll" step).
		void push_result(std::string_view name, lua_ref_t&& value);
		lua_ref_t get_finished(iris_lua_t&& lua);

		// ---- command handlers (embedded callbacks) ---------------------
		bool register_handler(std::string_view command, lua_ref_t&& handler);

		// ---- types -----------------------------------------------------
		lua_ref_t async_job(iris_lua_t&& lua);

	protected:
		void process_command(iris_lua_t&& lua, std::string line);
		void reset();

	protected:
		std::shared_ptr<async_worker_t> async_worker;
		std::unique_ptr<warp_t> main_warp;
		std::unique_ptr<warp_t::preempt_guard_t> main_warp_guard;

		// cross-thread command queue: guarded by command_mutex
		mutable std::mutex command_mutex;
		std::deque<std::string> command_queue;

		// Lua-thread-only state
		std::unordered_map<std::string, lua_ref_t> handlers;
		std::deque<std::pair<std::string, lua_ref_t>> finished_results;
		size_t total_commands = 0;

		// atomics observed from the Lua-side loop
		std::atomic<size_t> running_count{ 0 };
		std::atomic<bool> quit_flag{ false };
	};
}
