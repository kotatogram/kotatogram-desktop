/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <crl/common/crl_common_config.h>
#include <utility>

namespace crl {

using main_queue_processor = void(*)(void (*callable)(void*), void *argument);
using main_queue_wrapper = void(*)(void (*callable)(void*), void *argument);

} // namespace crl

namespace crl::details {

using true_t = char;
struct false_t {
	char data[2];
};

template <typename Return, typename ...Args>
struct check_plain_function {
	static false_t check(...);
	static true_t check(Return(*)(Args...));
};

template <typename Callable, typename Return, typename ...Args>
constexpr bool is_plain_function_v = sizeof(
	check_plain_function<Return, Args...>::check(
		std::declval<Callable>())) == sizeof(true_t);

template <typename Callable>
class finalizer {
public:
	explicit finalizer(Callable &&callable)
	: _callable(std::move(callable)) {
	}
	finalizer(const finalizer &other) = delete;
	finalizer &operator=(const finalizer &other) = delete;
	finalizer(finalizer &&other)
	: _callable(std::move(other._callable))
	, _disabled(std::exchange(other._disabled, true)) {
	}
	finalizer &operator=(finalizer &&other) {
		_callable = std::move(other._callable);
		_disabled = std::exchange(other._disabled, true);
	}
	~finalizer() {
		if (!_disabled) {
			_callable();
		}
	}

private:
	Callable _callable;
	bool _disabled = false;

};

template <
	typename Callable,
	typename = std::enable_if_t<!std::is_reference_v<Callable>>>
finalizer<Callable> finally(Callable &&callable) {
	return finalizer<Callable>{ std::move(callable) };
}

} // namespace crl::details
