/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <crl/winapi/crl_winapi_async.h>

#ifdef CRL_USE_WINAPI

#include <concrt.h>

namespace crl::details {

void async_plain(void (*callable)(void*), void *argument) {
	Concurrency::CurrentScheduler::ScheduleTask(callable, argument);
}

} // namespace crl::details

#endif // CRL_USE_WINAPI
