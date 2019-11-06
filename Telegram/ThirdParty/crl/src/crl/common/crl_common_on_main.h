/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <crl/common/crl_common_config.h>

#if defined CRL_USE_COMMON_QUEUE || !defined CRL_USE_DISPATCH

#include <crl/common/crl_common_queue.h>
#include <atomic>

namespace crl::details {

extern queue *MainQueue;
extern std::atomic<int> MainQueueCounter;

class main_queue_pointer {
public:
	main_queue_pointer() {
		grab();
	}

	void create(main_queue_processor processor);

	explicit operator bool() const {
		return _pointer != nullptr;
	}

	queue *operator->() const {
		return _pointer;
	}

	~main_queue_pointer() {
		ungrab();
	}

private:
	void grab();
	void ungrab();

	queue *_pointer = nullptr;

};

} // namespace crl::details

namespace crl {

void init_main_queue(main_queue_processor processor);

inline void wrap_main_queue(main_queue_wrapper wrapper) {
	// If wrapping is needed here, it can be done inside processor.
}

template <typename Callable>
inline void on_main(Callable &&callable) {
	if (const auto main = details::main_queue_pointer()) {
		main->async(std::forward<Callable>(callable));
	}
}

template <typename Callable>
inline void on_main_sync(Callable &&callable) {
	if (const auto main = details::main_queue_pointer()) {
		main->sync(std::forward<Callable>(callable));
	}
}

} // namespace crl

#endif // CRL_USE_COMMON_QUEUE || !CRL_USE_DISPATCH
