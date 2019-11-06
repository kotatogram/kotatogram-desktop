/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <crl/common/crl_common_config.h>

#if defined CRL_USE_DISPATCH && !defined CRL_USE_COMMON_QUEUE

#include <crl/dispatch/crl_dispatch_async.h>
#include <crl/common/crl_common_utils.h>

namespace crl {
namespace details {

extern main_queue_wrapper _main_wrapper;

struct MainQueueWrapper {
	static inline void Invoke(void (*callable)(void*), void *argument) {
		_main_wrapper(callable, argument);
	}
};

} // namespace details

inline void init_main_queue(main_queue_processor processor) {
}

inline void wrap_main_queue(main_queue_wrapper wrapper) {
	details::_main_wrapper = wrapper;
}

template <typename Callable>
inline void on_main(Callable &&callable) {
	return details::on_queue_invoke<details::MainQueueWrapper>(
		details::main_queue_dispatch(),
		details::on_queue_async,
		std::forward<Callable>(callable));
}

template <typename Callable>
inline void on_main_sync(Callable &&callable) {
	return details::on_queue_invoke<details::MainQueueWrapper>(
		details::main_queue_dispatch(),
		details::on_queue_sync,
		std::forward<Callable>(callable));
}

} // namespace crl

#endif // CRL_USE_DISPATCH && !CRL_USE_COMMON_QUEUE
