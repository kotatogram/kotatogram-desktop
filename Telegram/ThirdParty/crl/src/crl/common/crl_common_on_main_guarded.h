/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <crl/common/crl_common_config.h>

#include <memory>
#include <type_traits>

namespace crl::details {

template <typename T>
constexpr std::size_t dependent_zero = 0;

} // namespace crl::details

namespace crl {

template <typename T, typename Enable = void>
struct guard_traits;

template <typename Callable>
struct deduce_call_type_traits {
	using type = decltype(&Callable::operator());
};

template <typename Callable>
using deduced_call_type = typename deduce_call_type_traits<
	std::decay_t<Callable>>::type;

template <typename Guard, typename Callable>
class guarded_wrap {
public:
	using ClearCallable = std::decay_t<Callable>;
	using GuardTraits = guard_traits<std::decay_t<Guard>>;
	using GuardType = decltype(GuardTraits::create(std::declval<Guard>()));

	guarded_wrap(Guard &&object, Callable &&callable)
	: _guard(GuardTraits::create(std::forward<Guard>(object)))
	, _callable(std::forward<Callable>(callable)) {
	}

	template <
		typename ...OtherArgs,
		typename Return = decltype(
			std::declval<ClearCallable>()(std::declval<OtherArgs>()...))>
	Return operator()(OtherArgs &&...args) {
		return GuardTraits::check(_guard)
			? _callable(std::forward<OtherArgs>(args)...)
			: Return();
	}

	template <
		typename ...OtherArgs,
		typename Return = decltype(
			std::declval<ClearCallable>()(std::declval<OtherArgs>()...))>
	Return operator()(OtherArgs &&...args) const {
		return GuardTraits::check(_guard)
			? _callable(std::forward<OtherArgs>(args)...)
			: Return();
	}

private:
	GuardType _guard;
	ClearCallable _callable;

};

template <typename Guard, typename Callable>
struct deduce_call_type_traits<guarded_wrap<Guard, Callable>> {
	using type = deduced_call_type<Callable>;
};

template <
	typename Guard,
	typename Callable,
	typename GuardTraits = guard_traits<std::decay_t<Guard>>,
	typename = std::enable_if_t<
		sizeof(GuardTraits) != details::dependent_zero<GuardTraits>>>
inline auto guard(Guard &&object, Callable &&callable)
-> guarded_wrap<Guard, Callable> {
	return {
		std::forward<Guard>(object),
		std::forward<Callable>(callable)
	};
}

template <
	typename Guard,
	typename Callable,
	typename GuardTraits = guard_traits<std::decay_t<Guard>>,
	typename = std::enable_if_t<
		sizeof(GuardTraits) != details::dependent_zero<GuardTraits>>>
inline void on_main(Guard &&object, Callable &&callable) {
	return on_main(guard(
		std::forward<Guard>(object),
		std::forward<Callable>(callable)));
}

template <
	typename Guard,
	typename Callable,
	typename GuardTraits = guard_traits<std::decay_t<Guard>>,
	typename = std::enable_if_t<
		sizeof(GuardTraits) != details::dependent_zero<GuardTraits>>>
inline void on_main_sync(Guard &&object, Callable &&callable) {
	return on_main_sync(guard(
		std::forward<Guard>(object),
		std::forward<Callable>(callable)));
}

} // namespace crl
