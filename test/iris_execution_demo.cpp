/*
The Iristorm Concurrency Framework

This software is a C++ 17 Header-Only reimplementation of core part from project PaintsNow.

The MIT License (MIT)

Copyright (c) 2014-2026 PaintDream

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include "../src/iris_execution.h"
#include "../src/iris_common.inl"

#ifndef IRIS_ENABLE_STDEXEC
#error "iris_execution_demo.cpp requires IRIS_ENABLE_STDEXEC"
#endif

#include <cstdio>
#include <string>

namespace ex = stdexec;
using namespace iris;

template <typename element_t>
using worker_allocator_t = iris_object_allocator_t<element_t>;

template <bool strand>
using worker_t = iris_async_worker_t<std::thread, std::function<void()>, worker_allocator_t>;

template <bool strand>
using warp_t = iris_warp_t<worker_t<strand>, strand>;

template <bool strand>
static void test_schedule_then();
template <bool strand>
static void test_starts_on_values();
template <bool strand>
static void test_continues_on();
template <bool strand>
static void test_parallel_schedule();
template <bool strand>
static void test_worker_schedule_then();
template <bool strand>
static void test_worker_starts_on();

template <bool strand>
static void run_tests() {
	test_schedule_then<strand>();
	test_starts_on_values<strand>();
	test_continues_on<strand>();
	test_parallel_schedule<strand>();
	test_worker_schedule_then<strand>();
	test_worker_starts_on<strand>();
}

int main(void) {
	std::printf("=== iris stdexec demo (strand=false) ===\n");
	run_tests<false>();
	std::printf("=== iris stdexec demo (strand=true)  ===\n");
	run_tests<true>();
	std::printf("=== stdexec scheduler tests passed! ===\n");
	return 0;
}

template <bool strand>
static void test_schedule_then() {
	IRIS_PROFILE_SCOPE(__FUNCTION__);

	worker_t<strand> worker(4);
	worker.start();

	warp_t<strand> warp(worker);
	auto sender = ex::schedule(iris_warp_scheduler_t<warp_t<strand>>{ warp })
		| ex::then([&]() noexcept {
			IRIS_ASSERT(warp_t<strand>::get_current() == &warp);
			return 21;
		})
		| ex::then([&](int value) noexcept {
			IRIS_ASSERT(warp_t<strand>::get_current() == &warp);
			return value * 2;
		});

	auto result = ex::sync_wait(std::move(sender));
	IRIS_ASSERT(result.has_value());
	auto [value] = std::move(result).value();
	IRIS_ASSERT(value == 42);
	std::printf("  [schedule] value=%d\n", value);

	worker.terminate();
	worker.join();
}

template <bool strand>
static void test_starts_on_values() {
	IRIS_PROFILE_SCOPE(__FUNCTION__);

	worker_t<strand> worker(4);
	worker.start();

	warp_t<strand> warp(worker);
	auto sender = ex::starts_on(
		iris_warp_scheduler_t<warp_t<strand>>{ warp },
		ex::just(std::string("iris"), 26))
		| ex::then([&](std::string prefix, int suffix) noexcept {
			IRIS_ASSERT(warp_t<strand>::get_current() == &warp);
			return prefix + "-exec-" + std::to_string(suffix);
		});

	auto result = ex::sync_wait(std::move(sender));
	IRIS_ASSERT(result.has_value());
	auto [text] = std::move(result).value();
	IRIS_ASSERT(text == "iris-exec-26");
	std::printf("  [starts_on] text=%s\n", text.c_str());

	worker.terminate();
	worker.join();
}

template <bool strand>
static void test_continues_on() {
	IRIS_PROFILE_SCOPE(__FUNCTION__);

	worker_t<strand> worker(4);
	worker.start();

	warp_t<strand> warp_a(worker);
	warp_t<strand> warp_b(worker);
	std::atomic<int> phase(0);

	auto sender = ex::starts_on(iris_warp_scheduler_t<warp_t<strand>>{ warp_a }, ex::just(5))
		| ex::then([&](int value) noexcept {
			IRIS_ASSERT(warp_t<strand>::get_current() == &warp_a);
			phase.store(1, std::memory_order_release);
			return value + 1;
		})
		| ex::continues_on(iris_warp_scheduler_t<warp_t<strand>>{ warp_b })
		| ex::then([&](int value) noexcept {
			IRIS_ASSERT(warp_t<strand>::get_current() == &warp_b);
			phase.store(2, std::memory_order_release);
			return value * 2;
		});

	auto result = ex::sync_wait(std::move(sender));
	IRIS_ASSERT(result.has_value());
	auto [value] = std::move(result).value();
	IRIS_ASSERT(value == 12);
	IRIS_ASSERT(phase.load(std::memory_order_acquire) == 2);
	std::printf("  [continues_on] value=%d\n", value);

	worker.terminate();
	worker.join();
}

template <bool strand>
static void test_parallel_schedule() {
	IRIS_PROFILE_SCOPE(__FUNCTION__);

	worker_t<strand> worker(4);
	worker.start();

	warp_t<strand> warp(worker);
	auto sender = ex::schedule(iris_warp_parallel_scheduler_t<warp_t<strand>>{ warp })
		| ex::then([&]() noexcept {
			return 17;
		})
		| ex::then([&](int value) noexcept {
			return value * 3;
		});

	auto result = ex::sync_wait(std::move(sender));
	IRIS_ASSERT(result.has_value());
	auto [value] = std::move(result).value();
	IRIS_ASSERT(value == 51);
	std::printf("  [parallel] value=%d\n", value);

	worker.terminate();
	worker.join();
}

// ===================================================================
// Worker scheduler tests  --  schedule directly on worker_t
//                               (NO warp context)
// ===================================================================
// The iris_worker_scheduler_t posts work directly to the underlying
// async_worker thread pool without going through a warp. Callbacks
// execute on any available worker thread and do NOT have a warp
// context (iris_warp_t::get_current() returns nullptr).

template <bool strand>
static void test_worker_schedule_then() {
	IRIS_PROFILE_SCOPE(__FUNCTION__);

	worker_t<strand> worker(4);
	worker.start();

	auto sender = ex::schedule(iris_worker_scheduler_t<worker_t<strand>>{ worker })
		| ex::then([]() noexcept {
			// NOT in any warp context — get_current() would return nullptr
			return 21;
		})
		| ex::then([](int value) noexcept {
			return value * 2;
		});

	auto result = ex::sync_wait(std::move(sender));
	IRIS_ASSERT(result.has_value());
	auto [value] = std::move(result).value();
	IRIS_ASSERT(value == 42);
	std::printf("  [worker-schedule] value=%d\n", value);

	worker.terminate();
	worker.join();
}

template <bool strand>
static void test_worker_starts_on() {
	IRIS_PROFILE_SCOPE(__FUNCTION__);

	worker_t<strand> worker(4);
	worker.start();

	auto sender = ex::starts_on(
		iris_worker_scheduler_t<worker_t<strand>>{ worker },
		ex::just(std::string("worker"), 7))
		| ex::then([](std::string prefix, int suffix) noexcept {
			// NOT in any warp context
			return prefix + "-direct-" + std::to_string(suffix);
		});

	auto result = ex::sync_wait(std::move(sender));
	IRIS_ASSERT(result.has_value());
	auto [text] = std::move(result).value();
	IRIS_ASSERT(text == "worker-direct-7");
	std::printf("  [worker-starts_on] text=%s\n", text.c_str());

	worker.terminate();
	worker.join();
}
