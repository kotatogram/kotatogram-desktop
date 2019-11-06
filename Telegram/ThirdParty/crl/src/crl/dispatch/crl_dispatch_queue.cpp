/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <crl/dispatch/crl_dispatch_queue.h>

#if defined CRL_USE_DISPATCH && !defined CRL_USE_COMMON_QUEUE

#include <dispatch/dispatch.h>
#include <exception>

namespace crl {
namespace {

dispatch_queue_t Unwrap(void *value) {
	return static_cast<dispatch_queue_t>(value);
}

} // namespace

auto queue::implementation::create() -> pointer {
	auto result = dispatch_queue_create(nullptr, DISPATCH_QUEUE_SERIAL);
	if (!result) {
		std::terminate();
	}
	return result;
}

void queue::implementation::operator()(pointer value) {
	if (value) {
		dispatch_release(Unwrap(value));
	}
};

queue::queue() : _handle(implementation::create()) {
}

void queue::async_plain(void (*callable)(void*), void *argument) {
	dispatch_async_f(
		Unwrap(_handle.get()),
		argument,
		callable);
}

void queue::sync_plain(void (*callable)(void*), void *argument) {
	dispatch_sync_f(
		Unwrap(_handle.get()),
		argument,
		callable);
}

} // namespace crl

#endif // CRL_USE_DISPATCH && !CRL_USE_COMMON_QUEUE
