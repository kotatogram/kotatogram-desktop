/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <crl/common/crl_common_on_main.h>

#if defined CRL_USE_COMMON_QUEUE || !defined CRL_USE_DISPATCH

#include <exception>

namespace {

crl::queue *Queue/* = nullptr*/;
std::atomic<int> Counter/* = 0*/;
crl::details::main_queue_pointer Lifetime;

} // namespace

namespace crl::details {

void main_queue_pointer::grab() {
	auto counter = Counter.load(std::memory_order_acquire);
	while (true) {
		if (!counter) {
			return;
		} else if (Counter.compare_exchange_weak(counter, counter + 1)) {
			_pointer = Queue;
			return;
		}
	}
}

void main_queue_pointer::ungrab() {
	if (_pointer) {
		if (--Counter == 0) {
			delete _pointer;
		}
		_pointer = nullptr;
	}
}

void main_queue_pointer::create(main_queue_processor processor) {
	if (Counter.load(std::memory_order_acquire) != 0) {
		std::terminate();
	}
	Queue = new queue(processor);
	Counter.store(1, std::memory_order_release);
	_pointer = Queue;
}

} // namespace crl::details

namespace crl {

void init_main_queue(main_queue_processor processor) {
	Lifetime.create(processor);
}

} // namespace crl

#endif // CRL_USE_COMMON_QUEUE || !CRL_USE_DISPATCH
