/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <crl/common/crl_common_config.h>

#if defined CRL_USE_WINAPI_LIST

#include <crl/winapi/crl_winapi_list.h>

#elif defined CRL_USE_COMMON_LIST // CRL_USE_WINAPI_LIST

#include <crl/common/crl_common_utils.h>
#include <crl/crl_semaphore.h>
#include <atomic>

namespace crl::details {

class list {
public:
	list();

	template <typename Callable>
	bool push_is_first(Callable &&callable) {
		return push_entry(AllocateEntry(std::forward<Callable>(callable)));
	}
	bool process();
	bool empty() const;

	~list();

private:
	struct BasicEntry;
	using ProcessEntryMethod = void(*)(BasicEntry *entry);

	struct BasicEntry {
		BasicEntry(ProcessEntryMethod method) : process(method) {
		}

		BasicEntry *next = nullptr;
		ProcessEntryMethod process = nullptr;
	};

	template <typename Function>
	struct Entry : BasicEntry {
		Entry(Function &&function)
		: BasicEntry(Entry::Process)
		, function(std::move(function)) {
		}
		Entry(const Function &function)
		: BasicEntry(Entry::Process)
		, function(function) {
		}
		Function function;

		static void Process(BasicEntry *entry) {
			auto full = static_cast<Entry*>(entry);
			auto guard = details::finally([=] { delete full; });
			full->function();
		}

	};

	template <typename Callable>
	static Entry<std::decay_t<Callable>> *AllocateEntry(
			Callable &&callable) {
		using Function = std::decay_t<Callable>;
		using Type = Entry<Function>;

		return new Type(std::forward<Callable>(callable));
	}

	static BasicEntry *ReverseList(BasicEntry *entry, BasicEntry *next);

	bool push_entry(BasicEntry *entry);

	std::atomic<BasicEntry*> _head = nullptr;
	bool *_alive = nullptr;

};

} // namespace crl::details

#elif !defined CRL_USE_DISPATCH // CRL_USE_COMMON_LIST
#error "Configuration is not supported."
#endif // !CRL_USE_WINAPI_LIST && !CRL_USE_COMMON_LIST
