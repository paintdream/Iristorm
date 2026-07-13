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

#pragma once

//
// iris_execution.h  --  P2300-style Sender / Receiver execution model
//                        backed by iris_warp_t as the concrete scheduler.
//
// Each execution unit (sender chain) can explicitly choose which warp it
// dispatches on via schedule(warp) and transfer(warp).
//
// Vocabulary:
//   schedule(warp)           - sender that completes on a warp
//   transfer(warp)           - adaptor that switches execution to another warp
//   then(callable)           - adaptor that chains a transformation
//   just(values...)          - sender that immediately yields values
//   sender | adaptor         - pipe syntax
//   submit(sender, w, cb)    - connect + start, polls warp until completion
//
// All execution ultimately flows through iris_warp_t::queue_routine_post(),
// preserving the M:N scheduling, priority, suspend/resume, and preemption
// semantics of the underlying warp.
//

#include "iris_dispatcher.h"

#include <type_traits>
#include <utility>
#include <exception>
#include <tuple>
#include <atomic>

#if defined(IRIS_ENABLE_STDEXEC)
#include <stdexec/execution.hpp>
#endif

namespace iris {

// ===================================================================
// Receiver protocol
// ===================================================================

template <typename receiver_t, typename... values_t>
void set_value(receiver_t&& r, values_t&&... vals)
	noexcept(noexcept(std::forward<receiver_t>(r).set_value(std::forward<values_t>(vals)...))) {
	std::forward<receiver_t>(r).set_value(std::forward<values_t>(vals)...);
}

template <typename receiver_t>
void set_error(receiver_t&& r, std::exception_ptr ep)
	noexcept(noexcept(std::forward<receiver_t>(r).set_error(std::move(ep)))) {
	std::forward<receiver_t>(r).set_error(std::move(ep));
}

namespace exec_impl {

template <typename R, typename Tup, size_t... I>
void apply_set_value(R&& r, Tup&& tup, std::index_sequence<I...>)
	noexcept(noexcept(iris::set_value(std::forward<R>(r), std::get<I>(std::forward<Tup>(tup))...))) {
	iris::set_value(std::forward<R>(r), std::get<I>(std::forward<Tup>(tup))...);
}

template <typename f_t, typename... values_t>
using invoke_result_t = decltype(std::declval<f_t>()(std::declval<values_t>()...));

template <typename f_t, typename... values_t>
struct is_void_invoke : std::is_void<invoke_result_t<f_t, values_t...>> {};

} // namespace exec_impl

// ===================================================================
// just(values...)  --  immediate-value sender
// ===================================================================

template <typename... values_t>
struct just_sender_t {
	std::tuple<values_t...> values;

	explicit just_sender_t(values_t... vals) noexcept : values(std::move(vals)...) {}
	just_sender_t(just_sender_t&&) noexcept = default;
	just_sender_t& operator=(just_sender_t&&) noexcept = default;

	template <typename receiver_t>
	struct op_state_t {
		std::tuple<values_t...> values;
		receiver_t receiver;

		void start() & noexcept {
			try {
				exec_impl::apply_set_value(std::move(receiver), std::move(values),
					std::index_sequence_for<values_t...>{});
			} catch (...) {
				iris::set_error(std::move(receiver), std::current_exception());
			}
		}
	};

	template <typename receiver_t>
	op_state_t<typename std::decay<receiver_t>::type> connect(receiver_t&& receiver) && {
		return { std::move(values), std::forward<receiver_t>(receiver) };
	}

	template <typename receiver_t>
	op_state_t<typename std::decay<receiver_t>::type> connect(receiver_t&& receiver) & {
		return { values, std::forward<receiver_t>(receiver) };
	}
};

template <typename... values_t>
just_sender_t<typename std::decay<values_t>::type...> just(values_t&&... values) noexcept {
	return just_sender_t<typename std::decay<values_t>::type...>(std::forward<values_t>(values)...);
}

inline just_sender_t<> just() noexcept {
	return just_sender_t<>{};
}

// ===================================================================
// schedule(warp)  --  sender that completes on a specific warp
// ===================================================================

template <typename warp_t>
struct schedule_sender_t {
	warp_t* warp;

	explicit schedule_sender_t(warp_t& w) noexcept : warp(&w) {}

	template <typename receiver_t>
	struct op_state_t {
		warp_t* warp;
		receiver_t receiver;

		void start() & {
			warp->queue_routine_post([this]() {
				try {
					iris::set_value(std::move(receiver));
				} catch (...) {
					iris::set_error(std::move(receiver), std::current_exception());
				}
			});
		}
	};

	template <typename receiver_t>
	op_state_t<typename std::decay<receiver_t>::type> connect(receiver_t&& receiver) const {
		return { warp, std::forward<receiver_t>(receiver) };
	}
};

template <typename warp_t>
schedule_sender_t<warp_t> schedule(warp_t& warp) noexcept {
	return schedule_sender_t<warp_t>(warp);
}

// ===================================================================
// then(callable)  --  chain a transformation
// ===================================================================

template <typename callable_t>
struct then_adaptor_t {
	callable_t callable;

	explicit then_adaptor_t(callable_t c) noexcept : callable(std::move(c)) {}

	template <typename upstream_sender_t>
	auto apply(upstream_sender_t&& upstream) &&;

	template <typename upstream_sender_t>
	auto apply(upstream_sender_t&& upstream) &;
};

template <typename callable_t, typename downstream_receiver_t>
struct then_receiver_t {
	callable_t callable;
	downstream_receiver_t downstream;

	template <typename... values_t>
	void set_value(values_t&&... values) && noexcept {
		try {
			if constexpr (exec_impl::is_void_invoke<callable_t, values_t...>::value) {
				std::move(callable)(std::forward<values_t>(values)...);
				iris::set_value(std::move(downstream));
			} else {
				iris::set_value(std::move(downstream),
					std::move(callable)(std::forward<values_t>(values)...));
			}
		} catch (...) {
			iris::set_error(std::move(downstream), std::current_exception());
		}
	}

	void set_error(std::exception_ptr ep) && noexcept {
		iris::set_error(std::move(downstream), std::move(ep));
	}
};

template <typename upstream_sender_t, typename func_callable_t>
struct then_sender_impl_t {
	using upstream_t = typename std::decay<upstream_sender_t>::type;
	using callable_t = typename std::decay<func_callable_t>::type;

	upstream_t upstream;
	callable_t callable;

	template <typename us_t, typename c_t>
	then_sender_impl_t(us_t&& us, c_t&& c) noexcept
		: upstream(std::forward<us_t>(us)), callable(std::forward<c_t>(c)) {}

	template <typename receiver_t>
	auto connect(receiver_t&& receiver) && {
		using stored_receiver_t = typename std::decay<receiver_t>::type;
		then_receiver_t<callable_t, stored_receiver_t> intermediate{
			std::move(callable), std::forward<receiver_t>(receiver)
		};
		return std::move(upstream).connect(std::move(intermediate));
	}

	template <typename receiver_t>
	auto connect(receiver_t&& receiver) & {
		using stored_receiver_t = typename std::decay<receiver_t>::type;
		then_receiver_t<callable_t, stored_receiver_t> intermediate{
			callable, std::forward<receiver_t>(receiver)
		};
		return upstream.connect(std::move(intermediate));
	}
};

template <typename callable_t>
template <typename upstream_sender_t>
auto then_adaptor_t<callable_t>::apply(upstream_sender_t&& upstream) && {
	return then_sender_impl_t<upstream_sender_t, callable_t>(
		std::forward<upstream_sender_t>(upstream), std::move(callable));
}

template <typename callable_t>
template <typename upstream_sender_t>
auto then_adaptor_t<callable_t>::apply(upstream_sender_t&& upstream) & {
	return then_sender_impl_t<upstream_sender_t, callable_t&>(
		std::forward<upstream_sender_t>(upstream), callable);
}

template <typename callable_t>
then_adaptor_t<typename std::decay<callable_t>::type> then(callable_t&& callable) noexcept {
	return then_adaptor_t<typename std::decay<callable_t>::type>(std::forward<callable_t>(callable));
}

// ===================================================================
// transfer(warp)  --  switch execution context to a different warp
// ===================================================================

template <typename warp_t>
struct transfer_adaptor_t {
	warp_t* warp;

	explicit transfer_adaptor_t(warp_t& w) noexcept : warp(&w) {}

	template <typename upstream_sender_t>
	auto apply(upstream_sender_t&& upstream) &&;

	template <typename upstream_sender_t>
	auto apply(upstream_sender_t&& upstream) &;
};

template <typename warp_t, typename downstream_receiver_t>
struct transfer_receiver_t {
	warp_t* warp;
	downstream_receiver_t downstream;

	template <typename... values_t>
	void set_value(values_t&&... values) && noexcept {
		auto captured = std::make_tuple(std::forward<values_t>(values)...);

		warp->queue_routine_post(
			[downstream = std::move(downstream),
			 vals = std::move(captured)]() mutable {
				try {
					exec_impl::apply_set_value(std::move(downstream), std::move(vals),
						std::index_sequence_for<values_t...>{});
				} catch (...) {
					iris::set_error(std::move(downstream), std::current_exception());
				}
			});
	}

	void set_error(std::exception_ptr ep) && noexcept {
		warp->queue_routine_post(
			[downstream = std::move(downstream), ep = std::move(ep)]() mutable {
				iris::set_error(std::move(downstream), std::move(ep));
			});
	}
};

template <typename upstream_sender_t, typename warp_t>
struct transfer_sender_impl_t {
	using upstream_t = typename std::decay<upstream_sender_t>::type;

	upstream_t upstream;
	warp_t* warp;

	template <typename us_t>
	transfer_sender_impl_t(us_t&& us, warp_t& w) noexcept
		: upstream(std::forward<us_t>(us)), warp(&w) {}

	template <typename receiver_t>
	auto connect(receiver_t&& receiver) && {
		using stored_receiver_t = typename std::decay<receiver_t>::type;
		transfer_receiver_t<warp_t, stored_receiver_t> intermediate{
			warp, std::forward<receiver_t>(receiver)
		};
		return std::move(upstream).connect(std::move(intermediate));
	}

	template <typename receiver_t>
	auto connect(receiver_t&& receiver) & {
		using stored_receiver_t = typename std::decay<receiver_t>::type;
		transfer_receiver_t<warp_t, stored_receiver_t> intermediate{
			warp, std::forward<receiver_t>(receiver)
		};
		return upstream.connect(std::move(intermediate));
	}
};

template <typename warp_t>
template <typename upstream_sender_t>
auto transfer_adaptor_t<warp_t>::apply(upstream_sender_t&& upstream) && {
	return transfer_sender_impl_t<upstream_sender_t, warp_t>(
		std::forward<upstream_sender_t>(upstream), *warp);
}

template <typename warp_t>
template <typename upstream_sender_t>
auto transfer_adaptor_t<warp_t>::apply(upstream_sender_t&& upstream) & {
	return transfer_sender_impl_t<upstream_sender_t, warp_t>(
		std::forward<upstream_sender_t>(upstream), *warp);
}

template <typename warp_t>
transfer_adaptor_t<warp_t> transfer(warp_t& warp) noexcept {
	return transfer_adaptor_t<warp_t>(warp);
}

// ===================================================================
// Operator |  (pipe syntax)
// ===================================================================

template <typename sender_t, typename adaptor_t>
auto operator| (sender_t&& sender, adaptor_t&& adaptor)
	noexcept(noexcept(std::forward<adaptor_t>(adaptor).apply(std::forward<sender_t>(sender))))
	-> decltype(std::forward<adaptor_t>(adaptor).apply(std::forward<sender_t>(sender))) {
	return std::forward<adaptor_t>(adaptor).apply(std::forward<sender_t>(sender));
}

// ===================================================================
// submit(sender, warp, callback)  --  connect + start, polls warp
// ===================================================================
//
// Connects the sender to a terminal receiver, calls start(), and
// then polls the given warp (and its async_worker) until the
// terminal signal fires. This is a BLOCKING operation.
//
// Use this for tests, synchronous-style code, or any scenario where
// the caller drives the warp's event loop.
//
//   callback()           - for void-valued senders
//   callback(values...)  - for value-valued senders

namespace exec_impl {

template <typename callback_t>
struct blocking_terminal_t {
	callback_t callback;
	std::atomic<bool>* finished;

	template <typename... values_t>
	void set_value(values_t&&... values) && noexcept {
		std::move(callback)(std::forward<values_t>(values)...);
		finished->store(true, std::memory_order_release);
	}

	void set_error(std::exception_ptr) && noexcept {
		finished->store(true, std::memory_order_release);
	}
};

} // namespace exec_impl

template <typename sender_t, typename warp_t, typename callback_t>
void submit(sender_t&& sender, warp_t& warp, callback_t&& callback) {
	using decayed_sender_t = typename std::decay<sender_t>::type;
	using decayed_callback_t = typename std::decay<callback_t>::type;
	using terminal_t = exec_impl::blocking_terminal_t<decayed_callback_t>;

	std::atomic<bool> finished(false);
	terminal_t term{ std::forward<callback_t>(callback), &finished };

	// connect + start -- op_state lives on the stack,
	// kept alive by the polling loop below
	auto op = std::forward<decayed_sender_t>(sender).connect(std::move(term));
	op.start();

	// Drive the warp until the terminal receiver fires
	while (!finished.load(std::memory_order_acquire)) {
		warp.poll();
	}
}

#if defined(IRIS_ENABLE_STDEXEC)

template <typename warp_t>
struct iris_warp_scheduler_t {
	using scheduler_concept = STDEXEC::scheduler_tag;

	warp_t* warp = nullptr;

	iris_warp_scheduler_t() = default;
	/* implicit */ iris_warp_scheduler_t(warp_t& w) noexcept : warp(&w) {}

	struct sender {
		using sender_concept = STDEXEC::sender_tag;
		using completion_signatures =
			STDEXEC::completion_signatures<STDEXEC::set_value_t(), STDEXEC::set_stopped_t()>;

		template <STDEXEC::receiver receiver_t>
		struct operation_state {
			using operation_state_concept = STDEXEC::operation_state_tag;

			void start() & noexcept {
				warp->queue_routine_post([this]() noexcept {
					auto stop_token = STDEXEC::get_stop_token(STDEXEC::get_env(receiver));
					if constexpr (STDEXEC::unstoppable_token<decltype(stop_token)>) {
						STDEXEC::set_value(std::move(receiver));
					} else if (stop_token.stop_requested()) {
						STDEXEC::set_stopped(std::move(receiver));
					} else {
						STDEXEC::set_value(std::move(receiver));
					}
				});
			}

			warp_t* warp;
			receiver_t receiver;
		};

		struct attrs {
			template <typename cpo_t>
			iris_warp_scheduler_t query(STDEXEC::get_completion_scheduler_t<cpo_t>) const noexcept {
				return iris_warp_scheduler_t{ *warp };
			}

			warp_t* warp = nullptr;
		};

		template <STDEXEC::receiver receiver_t>
		auto connect(receiver_t receiver) const -> operation_state<receiver_t> {
			return operation_state<receiver_t>{ warp, std::move(receiver) };
		}

		auto get_env() const noexcept -> attrs {
			return attrs{ warp };
		}

		warp_t* warp = nullptr;

		sender() = default;
		/* implicit */ sender(warp_t& w) noexcept : warp(&w) {}
	};

	auto schedule() const noexcept -> sender {
		return sender{ *warp };
	}

	template <typename cpo_t>
	auto query(STDEXEC::get_completion_scheduler_t<cpo_t>) const noexcept -> iris_warp_scheduler_t {
		return *this;
	}

	auto query(STDEXEC::get_forward_progress_guarantee_t) const noexcept
		-> STDEXEC::forward_progress_guarantee {
		return STDEXEC::forward_progress_guarantee::parallel;
	}

	bool operator == (const iris_warp_scheduler_t& rhs) const noexcept {
		return warp == rhs.warp;
	}
};

template <typename warp_t>
struct iris_warp_parallel_scheduler_t {
	using scheduler_concept = STDEXEC::scheduler_tag;

	warp_t* warp = nullptr;

	iris_warp_parallel_scheduler_t() = default;
	/* implicit */ iris_warp_parallel_scheduler_t(warp_t& w) noexcept : warp(&w) {}

	struct sender {
		using sender_concept = STDEXEC::sender_tag;
		using completion_signatures =
			STDEXEC::completion_signatures<STDEXEC::set_value_t(), STDEXEC::set_stopped_t()>;

		template <STDEXEC::receiver receiver_t>
		struct operation_state {
			using operation_state_concept = STDEXEC::operation_state_tag;

			void start() & noexcept {
				warp->queue_routine_parallel([this]() noexcept {
					auto stop_token = STDEXEC::get_stop_token(STDEXEC::get_env(receiver));
					if constexpr (STDEXEC::unstoppable_token<decltype(stop_token)>) {
						STDEXEC::set_value(std::move(receiver));
					} else if (stop_token.stop_requested()) {
						STDEXEC::set_stopped(std::move(receiver));
					} else {
						STDEXEC::set_value(std::move(receiver));
					}
				});
			}

			warp_t* warp;
			receiver_t receiver;
		};

		struct attrs {
			template <typename cpo_t>
			iris_warp_parallel_scheduler_t query(STDEXEC::get_completion_scheduler_t<cpo_t>) const noexcept {
				return iris_warp_parallel_scheduler_t{ *warp };
			}

			warp_t* warp = nullptr;
		};

		template <STDEXEC::receiver receiver_t>
		auto connect(receiver_t receiver) const -> operation_state<receiver_t> {
			return operation_state<receiver_t>{ warp, std::move(receiver) };
		}

		auto get_env() const noexcept -> attrs {
			return attrs{ warp };
		}

		warp_t* warp = nullptr;

		sender() = default;
		/* implicit */ sender(warp_t& w) noexcept : warp(&w) {}
	};

	auto schedule() const noexcept -> sender {
		return sender{ *warp };
	}

	template <typename cpo_t>
	auto query(STDEXEC::get_completion_scheduler_t<cpo_t>) const noexcept -> iris_warp_parallel_scheduler_t {
		return *this;
	}

	auto query(STDEXEC::get_forward_progress_guarantee_t) const noexcept
		-> STDEXEC::forward_progress_guarantee {
		return STDEXEC::forward_progress_guarantee::parallel;
	}

	bool operator == (const iris_warp_parallel_scheduler_t& rhs) const noexcept {
		return warp == rhs.warp;
	}
};

#endif

} // namespace iris
