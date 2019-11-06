/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <crl/dispatch/crl_dispatch_async.h>

#ifdef CRL_USE_DISPATCH

#include <dispatch/dispatch.h>

namespace crl::details {

void empty_main_wrapper(void (*callable)(void*), void *argument) {
	callable(argument);
}

main_queue_wrapper _main_wrapper = &empty_main_wrapper;

void *background_queue_dispatch() {
	return dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
}

void *main_queue_dispatch() {
	return dispatch_get_main_queue();
}

void on_queue_async(void *queue, void (*callable)(void*), void *argument) {
	dispatch_async_f(
		static_cast<dispatch_queue_t>(queue),
		argument,
		callable);
}

void on_queue_sync(void *queue, void (*callable)(void*), void *argument) {
	dispatch_sync_f(
		static_cast<dispatch_queue_t>(queue),
		argument,
		callable);
}

} // namespace crl::details

#endif // CRL_USE_DISPATCH
