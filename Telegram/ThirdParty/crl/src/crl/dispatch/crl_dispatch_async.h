/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <crl/common/crl_common_config.h>

#ifdef CRL_USE_DISPATCH

#include <crl/common/crl_common_utils.h>
#include <type_traits>

namespace crl::details {

void *background_queue_dispatch();
void *main_queue_dispatch();

void on_queue_async(void *queue, void (*callable)(void*), void *argument);
void on_queue_sync(void *queue, void (*callable)(void*), void *argument);

template <
	typename Wrapper,
	typename Invoker,
	typename Callable,
	typename Return = decltype(std::declval<Callable>()())>
inline void on_queue_invoke(
		void *queue,
		Invoker invoker,
		Callable &&callable) {
	using Function = std::decay_t<Callable>;

	if constexpr (details::is_plain_function_v<Function, Return>) {
		using Plain = Return(*)();
		static_assert(sizeof(Plain) <= sizeof(void*));
		const auto copy = static_cast<Plain>(callable);
		invoker(queue, [](void *passed) {
			Wrapper::Invoke([](void *passed) {
				const auto callable = reinterpret_cast<Plain>(passed);
				(*callable)();
			}, passed);
		}, reinterpret_cast<void*>(copy));
	} else {
		const auto copy = new Function(std::forward<Callable>(callable));
		invoker(queue, [](void *passed) {
			Wrapper::Invoke([](void *passed) {
				const auto callable = static_cast<Function*>(passed);
				const auto guard = details::finally([=] { delete callable; });
				(*callable)();
			}, passed);
		}, static_cast<void*>(copy));
	}
}

struct EmptyWrapper {
	template <typename Callable>
	static inline void Invoke(Callable &&callable, void *argument) {
		callable(argument);
	}
};

inline void async_plain(void (*callable)(void*), void *argument) {
	return on_queue_async(
		background_queue_dispatch(),
		callable,
		argument);
}

} // namespace crl::details

namespace crl {

template <typename Callable>
inline void async(Callable &&callable) {
	return details::on_queue_invoke<details::EmptyWrapper>(
		details::background_queue_dispatch(),
		details::on_queue_async,
		std::forward<Callable>(callable));
}

template <typename Callable>
inline void sync(Callable &&callable) {
	return details::on_queue_invoke<details::EmptyWrapper>(
		details::background_queue_dispatch(),
		details::on_queue_sync,
		std::forward<Callable>(callable));
}

} // namespace crl

#endif // CRL_USE_DISPATCH
