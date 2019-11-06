/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <crl/common/crl_common_queue.h>

#if defined CRL_USE_COMMON_QUEUE || !defined CRL_USE_DISPATCH

#include <crl/crl_async.h>

namespace crl {

queue::queue() = default;

queue::queue(main_queue_processor processor) : _main_processor(processor) {
}

void queue::wake_async() {
	auto expected = false;
	if (_queued.compare_exchange_strong(expected, true)) {
		(_main_processor ? _main_processor : details::async_plain)(
			ProcessCallback,
			static_cast<void*>(this));
	}
}

void queue::process() {
	if (!_list.process()) {
		return;
	}
	_queued.store(false);

	if (!_list.empty()) {
		wake_async();
	}
}

void queue::ProcessCallback(void *that) {
	static_cast<queue*>(that)->process();
}

} // namespace crl

#endif // CRL_USE_COMMON_QUEUE || !CRL_USE_DISPATCH
