/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <crl/crl_semaphore.h>

namespace crl {

template <typename Callable>
inline void sync(Callable &&callable) {
	semaphore waiter;
	async([&] {
		const auto guard = details::finally([&] { waiter.release(); });
		callable();
	});
	waiter.acquire();
}

} // namespace crl
