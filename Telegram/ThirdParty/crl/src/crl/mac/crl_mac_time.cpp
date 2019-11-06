/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <crl/crl_time.h>

#ifdef CRL_USE_MAC_TIME

#include <mach/mach_time.h>

namespace crl::details {
namespace {

double Frequency/* = 0.*/;
double ProfileFrequency/* = 0.*/;

} // namespace

void init() {
	mach_timebase_info_data_t tb = { 0, 0 };
	mach_timebase_info(&tb);
	Frequency = (double(tb.numer) / tb.denom) / 1000000.;
	ProfileFrequency = (double(tb.numer) / tb.denom) / 1000.;
}

inner_time_type current_value() {
	return mach_absolute_time();
}

time convert(inner_time_type value) {
	return time(value * Frequency);
}

inner_profile_type current_profile_value() {
	return mach_absolute_time();
}

profile_time convert_profile(inner_profile_type value) {
	return profile_time(value * ProfileFrequency);
}

} // namespace crl::details

#endif // CRL_USE_MAC_TIME
