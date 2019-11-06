/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <crl/common/crl_common_list.h>

#if defined CRL_USE_COMMON_LIST

namespace crl::details {

list::list() : _alive(new bool(true)) {
}

auto list::ReverseList(BasicEntry *entry, BasicEntry *next) -> BasicEntry* {
	entry->next = nullptr;
	do {
		auto third = next->next;
		next->next = entry;
		entry = next;
		next = third;
	} while (next);
	return entry;
}

bool list::push_entry(BasicEntry *entry) {
	auto head = (BasicEntry*)nullptr;
	while (true) {
		if (_head.compare_exchange_weak(head, entry)) {
			return (head == nullptr);
		}
		entry->next = head;
	}
}

bool list::empty() const {
	return (_head == nullptr);
}

bool list::process() {
	if (auto entry = _head.exchange(nullptr)) {
		const auto alive = _alive;
		if (const auto next = entry->next) {
			entry = ReverseList(entry, next);
		}
		do {
			const auto basic = entry;
			entry = entry->next;
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

#endif // CRL_USE_COMMON_LIST
