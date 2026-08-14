/*
lua_event_framework

The MIT License (MIT)

Copyright (c) 2023-2026 PaintDream

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

#pragma once

#include <iris_common.h>
#include <iris_coroutine.h>
#include <iris_dispatcher.h>
#include <iris_lua.h>

namespace iris {
	// Minimal type-alias layer.
	//
	// Real iris-based projects put a small alias layer at the top of their
	// framework (see paintsnownext's src/PaintsNow.h) so that application code
	// never has to spell out the full iris template names. This tutorial keeps
	// the same shape with the smallest useful set.
	//
	//   async_worker_t   M:N thread pool (worker threads)
	//   warp_t           logical mutually-exclusive execution context
	//   coroutine_t<T>   C++20 coroutine return type (yields the calling Lua coroutine)
	//   result_t<T>      optional_result_t: success value or result_error_t message
	//   result_error_t   error value for result_t<T> (never lua.syserror with RAII)

	using async_worker_t = iris_async_worker_t<>;
	using warp_t = iris_warp_t<iris_async_worker_t<>>;

	template <typename T>
	using coroutine_t = iris_coroutine_t<T>;

	template <typename T>
	using result_t = iris_lua_t::optional_result_t<T>;
	using result_error_t = iris_lua_t::result_error_t;

	using lua_ref_t = iris_lua_t::ref_t;
}
