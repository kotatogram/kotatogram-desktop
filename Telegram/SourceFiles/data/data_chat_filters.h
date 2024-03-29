/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"

class History;

namespace Dialogs {
class MainList;
class Key;
} // namespace Dialogs

namespace Data {

class Session;
struct LocalFolder;

class ChatFilter final {
public:
	enum class Flag : ushort {
		Contacts    = 0x01,
		NonContacts = 0x02,
		Groups      = 0x04,
		Channels    = 0x08,
		Bots        = 0x10,
		NoMuted     = 0x20,
		NoRead      = 0x40,
		NoArchived  = 0x80,

		// Local flags
		Owned       = 0x0100,
		Admin       = 0x0200,
		NotOwned    = 0x0400,
		NotAdmin    = 0x0800,
		Recent      = 0x1000,
		NoFilter    = 0x2000,
	};
	friend constexpr inline bool is_flag_type(Flag) { return true; };
	using Flags = base::flags<Flag>;

	static constexpr int kPinnedLimit = 100;

	ChatFilter() = default;
	ChatFilter(FilterId id, bool isLocal = false);
	ChatFilter(
		FilterId id,
		const QString &title,
		const QString &iconEmoji,
		Flags flags,
		base::flat_set<not_null<History*>> always,
		std::vector<not_null<History*>> pinned,
		base::flat_set<not_null<History*>> never,
		bool isDefault = false,
		bool isLocal = false,
		int localCloudOrder = 0);

	[[nodiscard]] static ChatFilter local(
		const LocalFolder &data,
		not_null<Session*> owner);

	[[nodiscard]] static ChatFilter FromTL(
		const MTPDialogFilter &data,
		not_null<Session*> owner,
		bool isLocal = false);
	[[nodiscard]] MTPDialogFilter tl(FilterId replaceId = 0) const;
	[[nodiscard]] LocalFolder toLocal(FilterId replaceId = 0) const;

	[[nodiscard]] FilterId id() const;
	[[nodiscard]] QString title() const;
	[[nodiscard]] bool isDefault() const;
	[[nodiscard]] QString iconEmoji() const;
	[[nodiscard]] Flags flags() const;
	[[nodiscard]] const base::flat_set<not_null<History*>> &always() const;
	[[nodiscard]] const std::vector<not_null<History*>> &pinned() const;
	[[nodiscard]] const base::flat_set<not_null<History*>> &never() const;

	[[nodiscard]] bool contains(not_null<History*> history) const;

	[[nodiscard]] bool isLocal() const;

	void setLocalCloudOrder(int order) {
		_cloudLocalOrder = order;
	}

private:
	FilterId _id = 0;
	QString _title;
	QString _iconEmoji;
	base::flat_set<not_null<History*>> _always;
	std::vector<not_null<History*>> _pinned;
	base::flat_set<not_null<History*>> _never;
	Flags _flags;
	bool _isDefault = false;
	bool _isLocal = false;
	int _cloudLocalOrder = 0;

};

inline bool operator==(const ChatFilter &a, const ChatFilter &b) {
	return (a.title() == b.title())
		&& (a.iconEmoji() == b.iconEmoji())
		&& (a.flags() == b.flags())
		&& (a.always() == b.always())
		&& (a.never() == b.never());
}

inline bool operator!=(const ChatFilter &a, const ChatFilter &b) {
	return !(a == b);
}

struct SuggestedFilter {
	ChatFilter filter;
	QString description;
};

class ChatFilters final {
public:
	explicit ChatFilters(not_null<Session*> owner);
	~ChatFilters();

	void setPreloaded(const QVector<MTPDialogFilter> &result);

	void load();
	void apply(const MTPUpdate &update);
	void set(ChatFilter filter);
	void remove(FilterId id);
	[[nodiscard]] const std::vector<ChatFilter> &list() const;
	[[nodiscard]] rpl::producer<> changed() const;

	bool loadNextExceptions(bool chatsListLoaded);

	void refreshHistory(not_null<History*> history);

	[[nodiscard]] not_null<Dialogs::MainList*> chatsList(FilterId filterId);

	const ChatFilter &applyUpdatedPinned(
		FilterId id,
		const std::vector<Dialogs::Key> &dialogs);
	void saveOrder(
		const std::vector<FilterId> &order,
		mtpRequestId after = 0);

	[[nodiscard]] bool archiveNeeded() const;

	void requestSuggested();
	[[nodiscard]] bool suggestedLoaded() const;
	[[nodiscard]] auto suggestedFilters() const
		-> const std::vector<SuggestedFilter> &;
	[[nodiscard]] rpl::producer<> suggestedUpdated() const;

	void saveLocal();

private:
	void load(bool force);
	void received(const QVector<MTPDialogFilter> &list);
	bool applyOrder(const QVector<MTPint> &order);
	bool applyChange(ChatFilter &filter, ChatFilter &&updated);
	void applyInsert(ChatFilter filter, int position);
	void applyRemove(int position);

	const not_null<Session*> _owner;

	std::vector<ChatFilter> _list;
	base::flat_map<FilterId, std::unique_ptr<Dialogs::MainList>> _chatsLists;
	rpl::event_stream<> _listChanged;
	mtpRequestId _loadRequestId = 0;
	mtpRequestId _saveOrderRequestId = 0;
	mtpRequestId _saveOrderAfterId = 0;
	bool _loaded = false;

	mtpRequestId _suggestedRequestId = 0;
	std::vector<SuggestedFilter> _suggested;
	rpl::event_stream<> _suggestedUpdated;
	crl::time _suggestedLastReceived = 0;

	std::deque<FilterId> _exceptionsToLoad;
	mtpRequestId _exceptionsLoadRequestId = 0;

};

struct LocalFolder {
	QJsonObject toJson();

	int id = 0;
	int cloudOrder = 0;
	QString name;
	QString emoticon;
	std::vector<uint64> always;
	std::vector<uint64> never;
	std::vector<uint64> pinned;
	ChatFilter::Flags flags = Data::ChatFilter::Flags(0);
};

LocalFolder MakeLocalFolder(const QJsonObject &obj);

} // namespace Data
