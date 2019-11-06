/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <crl/dispatch/crl_dispatch_semaphore.h>

#ifdef CRL_USE_DISPATCH

#include <dispatch/dispatch.h>
#include <exception>

namespace crl {
namespace {

dispatch_semaphore_t Unwrap(void *value) {
	return static_cast<dispatch_semaphore_t>(value);
}

} // namespace

auto semaphore::implementation::create() -> pointer {
	auto result = dispatch_semaphore_create(0);
	if (!result) {
		std::terminate();
	}
	return result;
}

void semaphore::implementation::operator()(pointer value) {
	if (value) {
		dispatch_release(Unwrap(value));
	}
};

void semaphore::acquire() {
	dispatch_semaphore_wait(Unwrap(_handle.get()), DISPATCH_TIME_FOREVER);
}

void semaphore::release() {
	dispatch_semaphore_signal(Unwrap(_handle.get()));
}

} // namespace crl

#endif // CRL_USE_DISPATCH
