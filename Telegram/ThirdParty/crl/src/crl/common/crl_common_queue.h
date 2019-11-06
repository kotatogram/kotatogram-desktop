/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <crl/common/crl_common_config.h>

#if defined CRL_USE_COMMON_QUEUE || !defined CRL_USE_DISPATCH

#include <crl/common/crl_common_list.h>
#include <crl/common/crl_common_utils.h>
#include <atomic>

namespace crl {
namespace details {
class main_queue_pointer;
} // namespace details

class queue {
public:
	queue();
	queue(const queue &other) = delete;
	queue &operator=(const queue &other) = delete;

	template <typename Callable>
	void async(Callable &&callable) {
		if (_list.push_is_first(std::forward<Callable>(callable))) {
			wake_async();
		}
	}

	template <typename Callable>
	void sync(Callable &&callable) {
		semaphore waiter;
		async([&] {
			const auto guard = details::finally([&] { waiter.release(); });
			callable();
		});
		waiter.acquire();
	}

private:
	friend class details::main_queue_pointer;

	static void ProcessCallback(void *that);

	queue(main_queue_processor processor);

	void wake_async();
	void process();

	main_queue_processor _main_processor = nullptr;
	details::list _list;
	std::atomic<bool> _queued = false;

};

} // namespace crl

#endif // CRL_USE_COMMON_QUEUE || !CRL_USE_DISPATCH
