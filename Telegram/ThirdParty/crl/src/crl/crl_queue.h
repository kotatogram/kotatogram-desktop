/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <crl/common/crl_common_config.h>

#if defined CRL_USE_WINAPI || defined CRL_USE_COMMON_QUEUE
#include <crl/common/crl_common_queue.h>
#elif defined CRL_USE_DISPATCH // CRL_USE_WINAPI
#include <crl/dispatch/crl_dispatch_queue.h>
#elif defined CRL_USE_QT // CRL_USE_DISPATCH
#include <crl/common/crl_common_queue.h>
#else // CRL_USE_QT
#error "Configuration is not supported."
#endif // !CRL_USE_WINAPI && !CRL_USE_DISPATCH && !CRL_USE_QT
