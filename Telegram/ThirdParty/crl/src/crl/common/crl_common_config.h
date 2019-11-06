/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#if defined _MSC_VER
#define CRL_USE_WINAPI_TIME
#elif defined __APPLE__ // _MSC_VER
#define CRL_USE_MAC_TIME
#else // __APPLE__
#define CRL_USE_LINUX_TIME
#endif // !_MSC_VER && !__APPLE__

#if defined _MSC_VER && !defined CRL_FORCE_QT

#if defined _WIN64
#define CRL_USE_WINAPI
#define CRL_WINAPI_X64
#elif defined _M_IX86 // _WIN64
#define CRL_USE_WINAPI
#define CRL_WINAPI_X86
#else // _M_IX86
#error "Configuration is not supported."
#endif // !_WIN64 && !_M_IX86

#ifdef CRL_FORCE_STD_LIST
#define CRL_USE_COMMON_LIST
#else // CRL_FORCE_STD_LIST
#define CRL_USE_WINAPI_LIST
#endif // !CRL_FORCE_STD_LIST

#elif defined __APPLE__ && !defined CRL_FORCE_QT // _MSC_VER && !CRL_FORCE_QT

#define CRL_USE_DISPATCH

#ifdef CRL_USE_COMMON_QUEUE
#define CRL_USE_COMMON_LIST
#endif // CRL_USE_COMMON_QUEUE

#elif __has_include(<QtCore/QThreadPool>) // __APPLE__ && !CRL_FORCE_QT

#define CRL_USE_QT
#define CRL_USE_COMMON_LIST

#else // Qt
#error "Configuration is not supported."
#endif // !_MSC_VER && !__APPLE__ && !Qt

#if __has_include(<rpl/producer.h>)
#define CRL_ENABLE_RPL_INTEGRATION
#endif // __has_include(<rpl/producer.h>)
