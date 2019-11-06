/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <crl/common/crl_common_config.h>

#ifdef CRL_USE_WINAPI

#include <crl/common/crl_common_utils.h>
#include <crl/common/crl_common_sync.h>
#include <type_traits>

namespace crl::details {

void async_plain(void (*callable)(void*), void *argument);

} // namespace crl::details

namespace crl {

template <
	typename Callable,
	typename Return = decltype(std::declval<Callable>()())>
inline void async(Callable &&callable) {
	using Function = std::decay_t<Callable>;

	if constexpr (details::is_plain_function_v<Function, Return>) {
		using Plain = Return(*)();
		const auto copy = static_cast<Plain>(callable);
		details::async_plain([](void *passed) {
			const auto callable = reinterpret_cast<Plain>(passed);
			(*callable)();
		}, reinterpret_cast<void*>(copy));
	} else {
		const auto copy = new Function(std::forward<Callable>(callable));
		details::async_plain([](void *passed) {
			const auto callable = static_cast<Function*>(passed);
			const auto guard = details::finally([=] { delete callable; });
			(*callable)();
		}, static_cast<void*>(copy));
	}
}

} // namespace crl

#endif // CRL_USE_WINAPI
