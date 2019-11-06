/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <crl/winapi/crl_winapi_list.h>

#ifdef CRL_USE_WINAPI_LIST

#include <crl/winapi/crl_winapi_dll.h>
#include <crl/winapi/crl_winapi_windows_h.h>

namespace crl::details {
namespace {

PSLIST_HEADER UnwrapList(void *wrapped) {
	return static_cast<PSLIST_HEADER>(wrapped);
}

PSLIST_ENTRY UnwrapEntry(void *wrapped) {
	return static_cast<PSLIST_ENTRY>(wrapped);
}

SLIST_ENTRY *ReverseList(SLIST_ENTRY *entry, SLIST_ENTRY *next) {
	entry->Next = nullptr;
	do {
		auto third = next->Next;
		next->Next = entry;
		entry = next;
		next = third;
	} while (next);
	return entry;
}

PSLIST_ENTRY (NTAPI *RtlFirstEntrySList)(const SLIST_HEADER *ListHead) = nullptr;

} // namespace

list::list()
: _impl(std::make_unique<lock_free_list>())
, _alive(new bool(true)) {
	static auto initialize = [] {
		const auto library = details::dll(
			L"ntdll.dll",
			details::dll::own_policy::load_and_leak);
		library.load(RtlFirstEntrySList, "RtlFirstEntrySList");
		return true;
	}(); // TODO crl::once?..

	static_assert(alignof(lock_free_list) == MEMORY_ALLOCATION_ALIGNMENT);
	static_assert(alignof(lock_free_list) >= alignof(SLIST_HEADER));
	static_assert(sizeof(lock_free_list) == sizeof(SLIST_HEADER));
	InitializeSListHead(UnwrapList(_impl.get()));
}

bool list::push_entry(BasicEntry *entry) {
	return (InterlockedPushEntrySList(
		UnwrapList(_impl.get()),
		UnwrapEntry(&entry->plain)) == nullptr);
}

bool list::empty() const {
	return RtlFirstEntrySList(UnwrapList(_impl.get())) == nullptr;
}

bool list::process() {
	if (auto entry = InterlockedFlushSList(UnwrapList(_impl.get()))) {
		const auto alive = _alive;
		if (const auto next = entry->Next) {
			entry = ReverseList(entry, next);
		}
		do {
			const auto basic = reinterpret_cast<BasicEntry*>(entry);
			entry = entry->Next;
			basic->process(basic);
			if (!*alive) {
				delete alive;
				return false;
			}
		} while (entry);
	}
	return true;
}

list::~list() {
	*_alive = false;
}

} // namespace crl::details

#endif // CRL_USE_WINAPI_LIST
