// event_framework.cpp
// PaintDream (paintdream@paintdream.com)
// 2026
//

#include "event_framework.h"
#include "async_job.h"

#include <cctype>
#include <cstdio>

namespace iris {
	void event_framework_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		lua.set_current_new<&iris_lua_t::place_new_object<event_framework_t>>("new");
		lua.set_current<&event_framework_t::get_version>("get_version");
		lua.set_current<&event_framework_t::start>("start");
		lua.set_current<&event_framework_t::terminate>("terminate");

		lua.set_current<&event_framework_t::input>("input");
		lua.set_current<&event_framework_t::has_pending>("has_pending");
		lua.set_current<&event_framework_t::get_total_commands>("get_total_commands");

		lua.set_current<&event_framework_t::step>("step");
		lua.set_current<&event_framework_t::sleep>("sleep");

		lua.set_current<&event_framework_t::is_quit>("is_quit");
		lua.set_current<&event_framework_t::quit>("quit");
		lua.set_current<&event_framework_t::get_running_count>("get_running_count");
		lua.set_current<&event_framework_t::has_running>("has_running");
		lua.set_current<&event_framework_t::begin_task>("begin_task");
		lua.set_current<&event_framework_t::end_task>("end_task");

		lua.set_current<&event_framework_t::push_result>("push_result");
		lua.set_current<&event_framework_t::get_finished>("get_finished");

		lua.set_current<&event_framework_t::register_handler>("register_handler");
		lua.set_current<&event_framework_t::async_job>("async_job");
	}

	void event_framework_t::lua_finalize(iris_lua_t lua, int index, event_framework_t* p) {
		// release every registry reference held by ref_t members; afterwards
		// the member destructors see only LUA_REFNIL
		for (auto& entry : p->handlers) {
			lua.deref(std::move(entry.second));
		}
		p->handlers.clear();

		for (auto& entry : p->finished_results) {
			lua.deref(std::move(entry.second));
		}
		p->finished_results.clear();
	}

	event_framework_t::event_framework_t() : async_worker(std::make_shared<async_worker_t>()) {
		reset();

		// Route every worker-pool task back through the main warp: tasks that
		// touch the Lua state (coroutine completion callbacks) must run on the
		// Lua thread, inside step()'s main_warp->poll().
		async_worker->set_priority_task_handler([this](async_worker_t::task_base_t* task, size_t& priority) {
			main_warp->queue_routine([this, task]() {
				async_worker->execute_task(task);
			});

			return true;
		}, 0);
	}

	event_framework_t::~event_framework_t() noexcept {
		// force terminate on destruction
		terminate();
	}

	bool event_framework_t::is_running() const noexcept {
		return async_worker->get_thread_count() != 0;
	}

	bool event_framework_t::start(size_t thread_count) {
		if (!is_running()) {
			async_worker->resize(thread_count);
			// register the calling thread (the Lua thread) as an external worker
			size_t thread_index = async_worker->append(std::thread());
			async_worker->make_current(thread_index);
			async_worker->start();
			reset();

			return true;
		} else {
			return false;
		}
	}

	void event_framework_t::terminate() noexcept {
		if (is_running()) {
			async_worker->terminate();
			async_worker->join();

			// manually polling events
			while (main_warp->poll()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}

			reset();
		}
	}

	void event_framework_t::reset() {
		if (main_warp_guard) {
			main_warp_guard.reset();
		}

		main_warp = std::make_unique<warp_t>(*async_worker);
		main_warp_guard = std::make_unique<warp_t::preempt_guard_t>(*main_warp, 0);
	}

	std::string_view event_framework_t::get_version() const noexcept {
		return "lua_event_framework 0.0.0";
	}

	// ------------------------------------------------------------------
	// input
	// ------------------------------------------------------------------

	bool event_framework_t::input(std::string_view command_line) {
		if (quit_flag.load()) {
			return false;
		}

		std::lock_guard<std::mutex> guard(command_mutex);
		command_queue.emplace_back(command_line);
		return true;
	}

	bool event_framework_t::has_pending() const noexcept {
		std::lock_guard<std::mutex> guard(command_mutex);
		return !command_queue.empty();
	}

	size_t event_framework_t::get_total_commands() const noexcept {
		return total_commands;
	}

	// ------------------------------------------------------------------
	// event loop primitives
	// ------------------------------------------------------------------

	bool event_framework_t::step(iris_lua_t&& lua) {
		auto guard = write_fence();
		bool busy = false;

		// 1) process at most ONE pending command on the Lua thread.
		//    The handler is an embedded callback: it may not yield, so the
		//    command queue keeps flowing (upstream input is never blocked).
		std::string line;
		{
			std::lock_guard<std::mutex> lock(command_mutex);
			if (!command_queue.empty()) {
				line = std::move(command_queue.front());
				command_queue.pop_front();
			}
		}

		if (!line.empty()) {
			process_command(std::move(lua), std::move(line));
			busy = true;
		}

		// 2) poll the main warp: drive async completions (coroutine resumes,
		//    Lua-side wrappers) on this thread.
		if (main_warp->poll()) {
			busy = true;
		}

		return busy;
	}

	void event_framework_t::sleep(size_t milliseconds) const noexcept {
		std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
	}

	// ------------------------------------------------------------------
	// command dispatch
	// ------------------------------------------------------------------

	void event_framework_t::process_command(iris_lua_t&& lua, std::string line) {
		// tokenize: the first token is the command name, the rest are arguments
		std::vector<std::string> tokens;
		size_t pos = 0;
		while (pos < line.size()) {
			while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
				pos++;
			}
			if (pos >= line.size()) {
				break;
			}

			size_t start = pos;
			while (pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos]))) {
				pos++;
			}
			tokens.emplace_back(line.substr(start, pos - start));
		}

		if (tokens.empty()) {
			return;
		}

		total_commands++;

		std::string name = std::move(tokens[0]);
		auto it = handlers.find(name);
		if (it == handlers.end()) {
			printf("[event] unknown command: %s\n", name.c_str());
			return;
		}

		// pack the arguments into an array table { [1] = arg1, [2] = arg2, ... }
		lua_ref_t args = lua.make_table([&tokens](iris_lua_t lua) {
			for (size_t i = 1; i < tokens.size(); i++) {
				lua.set_current(static_cast<int>(i), tokens[i]);
			}
		});

		// embedded callback: lua.call returns the handler's results directly.
		// A failing handler (Lua error) is caught here and reported, so one
		// bad command can never break the event loop.
		auto result = lua.call<void>(it->second, std::move(args));
		if (!result) {
			printf("[event] handler '%s' failed: %s\n", name.c_str(), std::string(result.message).c_str());
		}
	}

	// ------------------------------------------------------------------
	// status
	// ------------------------------------------------------------------

	bool event_framework_t::is_quit() const noexcept {
		return quit_flag.load();
	}

	void event_framework_t::quit() {
		quit_flag.store(true);
	}

	size_t event_framework_t::get_running_count() const noexcept {
		return running_count.load();
	}

	bool event_framework_t::has_running() const noexcept {
		return running_count.load() != 0;
	}

	void event_framework_t::begin_task() {
		running_count.fetch_add(1, std::memory_order_relaxed);
	}

	void event_framework_t::end_task() {
		running_count.fetch_sub(1, std::memory_order_relaxed);
	}

	// ------------------------------------------------------------------
	// two-step results
	// ------------------------------------------------------------------

	void event_framework_t::push_result(std::string_view name, lua_ref_t&& value) {
		finished_results.emplace_back(std::string(name), std::move(value));
	}

	lua_ref_t event_framework_t::get_finished(iris_lua_t&& lua) {
		return lua.make_table([this](iris_lua_t lua) {
			size_t index = 1;
			for (auto& entry : finished_results) {
				lua.set_current(static_cast<int>(index), entry.first);
				lua.set_current(static_cast<int>(index + 1), entry.second);
				// the table now holds the values; release the registry refs
				// (ref_t must be LUA_REFNIL before destruction)
				lua.deref(std::move(entry.second));
				index += 2;
			}

			finished_results.clear();
		});
	}

	// ------------------------------------------------------------------
	// handlers & types
	// ------------------------------------------------------------------

	bool event_framework_t::register_handler(std::string_view command, lua_ref_t&& handler) {
		handlers[std::string(command)] = std::move(handler);
		return true;
	}

	lua_ref_t event_framework_t::async_job(iris_lua_t&& lua) {
		return lua.make_type<async_job_t>();
	}
}
