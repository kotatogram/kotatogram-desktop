/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <crl/crl_time.h>

#include <atomic>
#include <ctime>
#include <limits>

namespace crl {
namespace details {
namespace {

time LastAdjustmentTime/* = 0*/;
std::time_t LastAdjustmentUnixtime/* = 0*/;

using seconds_type = std::uint32_t;
std::atomic<seconds_type> AdjustSeconds/* = 0*/;

inner_time_type StartValue/* = 0*/;
inner_profile_type StartProfileValue/* = 0*/;

struct StaticInit {
	StaticInit();
};

StaticInit::StaticInit() {
	StartValue = current_value();
	StartProfileValue = current_profile_value();

	init();

	LastAdjustmentUnixtime = ::time(nullptr);
}

StaticInit StaticInitObject;

bool adjust_time() {
	const auto now = crl::now();
	const auto delta = (now - LastAdjustmentTime);
	const auto unixtime = ::time(nullptr);
	const auto real = (unixtime - LastAdjustmentUnixtime);
	const auto seconds = (time(real) * 1000 - delta) / 1000;

	LastAdjustmentUnixtime = unixtime;
	LastAdjustmentTime = now;

	if (seconds <= 0) {
		return false;
	}
	auto current = seconds_type(0);
	static constexpr auto max = std::numeric_limits<seconds_type>::max();
	while (true) {
		if (time(current) + seconds > time(max)) {
			return false;
		}
		const auto next = current + seconds_type(seconds);
		if (AdjustSeconds.compare_exchange_weak(current, next)) {
			return true;
		}
	}
	return false;
}

time compute_adjustment() {
	return time(AdjustSeconds.load()) * 1000;
}

profile_time compute_profile_adjustment() {
	return compute_adjustment() * 1000;
}

} // namespace
} // namespace details

time now() {
	const auto elapsed = details::current_value() - details::StartValue;
	return details::convert(elapsed) + details::compute_adjustment();
}

profile_time profile() {
	const auto elapsed = details::current_profile_value()
		- details::StartProfileValue;
	return details::convert_profile(elapsed)
		+ details::compute_profile_adjustment();
}

bool adjust_time() {
	return details::adjust_time();
}

} // namespace crl
