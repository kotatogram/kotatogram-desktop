/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <crl/common/crl_common_config.h>

#ifdef CRL_USE_WINAPI

#include <exception>
#include <crl/winapi/crl_winapi_windows_h.h>

namespace crl::details {

class dll {
public:
	enum class own_policy {
		owner,
		load_and_leak,
		use_existing,
	};
	dll(LPCWSTR library, own_policy policy)
	: _handle((policy == own_policy::use_existing)
		? GetModuleHandle(library)
		: LoadLibrary(library))
	, _policy(policy) {
	}

	template <typename Function>
	bool try_load(Function &function, const char *name) const {
		if (!_handle) {
			return false;
		}
		function = reinterpret_cast<Function>(GetProcAddress(_handle, name));
		return (function != nullptr);
	}

	template <typename Function>
	void load(Function &function, const char *name) const {
		if (!try_load(function, name)) {
			Failed();
		}
	}

	~dll() {
		if (_handle && _policy == own_policy::owner) {
			FreeLibrary(_handle);
		}
	}

private:
	[[noreturn]] static void Failed() {
		std::terminate();
	}

	HMODULE _handle = nullptr;
	own_policy _policy = own_policy::use_existing;

};

} // namespace crl::details

#endif // CRL_USE_WINAPI
