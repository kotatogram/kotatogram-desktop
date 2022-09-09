/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_chat_filters.h"

#include "kotato/kotato_settings.h"
#include "boxes/premium_limits_box.h"
#include "history/history.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_histories.h"
#include "data/data_premium_limits.h"
#include "dialogs/dialogs_main_list.h"
#include "history/history.h"
#include "history/history_unread_things.h"
#include "ui/ui_utility.h"
#include "ui/chat/more_chats_bar.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "mainwidget.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kRefreshSuggestedTimeout = 7200 * crl::time(1000);
constexpr auto kLoadExceptionsAfter = 100;
constexpr auto kLoadExceptionsPerRequest = 100;

[[nodiscard]] crl::time RequestUpdatesEach(not_null<Session*> owner) {
	const auto appConfig = &owner->session().account().appConfig();
	return appConfig->get<int>(u"chatlist_update_period"_q, 3600)
		* crl::time(1000);
}

const std::map<ChatFilter::Flag, QString> LocalFolderSettingsFlags {
	{ ChatFilter::Flag::Contacts,    qsl("include_contacts") },
	{ ChatFilter::Flag::NonContacts, qsl("include_non_contacts") },
	{ ChatFilter::Flag::Groups,      qsl("include_groups") },
	{ ChatFilter::Flag::Channels,    qsl("include_channels") },
	{ ChatFilter::Flag::Bots,        qsl("include_bots") },
	{ ChatFilter::Flag::NoMuted,     qsl("exclude_muted") },
	{ ChatFilter::Flag::NoRead,      qsl("exclude_read") },
	{ ChatFilter::Flag::NoArchived,  qsl("exclude_archived") },
	{ ChatFilter::Flag::Owned,       qsl("exclude_not_owned") },
	{ ChatFilter::Flag::Admin,       qsl("exclude_not_admin") },
	{ ChatFilter::Flag::NotOwned,    qsl("exclude_owned") },
	{ ChatFilter::Flag::NotAdmin,    qsl("exclude_admin") },
	{ ChatFilter::Flag::Recent,      qsl("exclude_non_recent") },
	{ ChatFilter::Flag::NoFilter,    qsl("exclude_filtered") },
};

bool ReadOption(QJsonObject obj, QString key, std::function<void(QJsonValue)> callback) {
	const auto it = obj.constFind(key);
	if (it == obj.constEnd()) {
		return false;
	}
	callback(*it);
	return true;
}

bool ReadStringOption(QJsonObject obj, QString key, std::function<void(QString)> callback) {
	auto readResult = false;
	auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
		if (v.isString()) {
			callback(v.toString());
			readResult = true;
		}
	});
	return (readValueResult && readResult);
}

bool ReadIntOption(QJsonObject obj, QString key, std::function<void(int)> callback) {
	auto readResult = false;
	auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
		if (v.isDouble()) {
			callback(v.toInt());
			readResult = true;
		}
	});
	return (readValueResult && readResult);
}

bool ReadArrayOption(QJsonObject obj, QString key, std::function<void(QJsonArray)> callback) {
	auto readResult = false;
	auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
		if (v.isArray()) {
			callback(v.toArray());
			readResult = true;
		}
	});
	return (readValueResult && readResult);
}

} // namespace

QJsonObject LocalFolder::toJson() {
	auto folderObject = QJsonObject();

	folderObject.insert(qsl("id"), id);
	folderObject.insert(qsl("order"), cloudOrder);
	folderObject.insert(qsl("name"), name);
	folderObject.insert(qsl("emoticon"), emoticon);

	for (const auto &[flag, option] : LocalFolderSettingsFlags) {
		if (flags & flag) {
			folderObject.insert(option, true);
		}
	}

	const auto peerToStr = [](uint64 peer) {
		auto peerId = PeerId(peer);
		return (peerIsChannel(peerId))
			? qsl("channel")
			: (peerIsChat(peerId))
			? qsl("chat")
			: qsl("user");
	};

	const auto peerToLocalBare = [](uint64 peer) {
		auto peerId = PeerId(peer);
		return QString::number((peerIsChannel(peerId))
			? peerToChannel(peerId).bare
			: (peerIsChat(peerId))
			? peerToChat(peerId).bare
			: peerToUser(peerId).bare);
	};

	const auto fillChatsArray = [peerToStr, peerToLocalBare] (const std::vector<uint64> &chats) -> QJsonArray {
		auto result = QJsonArray();
		for (auto peer : chats) {
			auto peerObj = QJsonObject();
			peerObj.insert(qsl("type"), peerToStr(peer));
			peerObj.insert(qsl("id"), peerToLocalBare(peer));
			result << peerObj;
		}
		return result;
	};

	folderObject.insert(qsl("never"), fillChatsArray(never));
	folderObject.insert(qsl("pinned"), fillChatsArray(pinned));
	folderObject.insert(qsl("always"), fillChatsArray(always));

	return folderObject;
}

LocalFolder MakeLocalFolder(const QJsonObject &obj) {
	auto result = LocalFolder();

	ReadIntOption(obj, "id", [&](auto v) {
		result.id = v;
	});

	ReadIntOption(obj, "order", [&](auto v) {
		result.cloudOrder = v;
	});

	ReadStringOption(obj, "name", [&](auto v) {
		result.name = v;
	});

	ReadStringOption(obj, "emoticon", [&](auto v) {
		result.emoticon = v;
	});

	for (const auto &[flag, option] : LocalFolderSettingsFlags) {
		const auto it = obj.constFind(option);
		if (it != obj.constEnd()) {
			const auto v = *it;
			if (v.isBool() && v.toBool()) {
				result.flags |= flag;
			}
		}
	}

	const auto readChatsArray = [obj] (const QString &key, std::vector<uint64> &chats) {
		ReadArrayOption(obj, key, [&](auto a) {
			for (auto i = a.constBegin(), e = a.constEnd(); i != e; ++i) {
				if (!(*i).isObject()) {
					continue;
				}

				auto peer = (*i).toObject();
				BareId peerId = 0;

				auto isPeerIdRead = ReadIntOption(peer, "id", [&](auto v) {
					peerId = v;
				});

				if (!isPeerIdRead) {
					isPeerIdRead = ReadStringOption(peer, "id", [&](auto v) {
						peerId = static_cast<BareId>(v.toLongLong());
					});
				}

				if (peerId == 0 || !isPeerIdRead) {
					continue;
				}

				auto isPeerTypeRead = ReadStringOption(peer, "type", [&](auto v) {
					peerId = (QString::compare(v.toLower(), "channel") == 0)
						? peerFromChannel(ChannelId(peerId)).value
						: (QString::compare(v.toLower(), "chat") == 0)
						? peerFromChat(ChatId(peerId)).value
						: peerFromUser(UserId(peerId)).value;
				});

				if (!isPeerTypeRead) {
					peerId = peerFromUser(UserId(peerId)).value;
				}

				chats.push_back(peerId);
			}
		});
	};

	readChatsArray(qsl("never"), result.never);
	readChatsArray(qsl("pinned"), result.pinned);
	readChatsArray(qsl("always"), result.always);

	return result;
}

ChatFilter::ChatFilter(FilterId id, bool isLocal)
: _id(id)
, _isLocal(isLocal) {
}

ChatFilter::ChatFilter(
	FilterId id,
	const QString &title,
	const QString &iconEmoji,
	Flags flags,
	base::flat_set<not_null<History*>> always,
	std::vector<not_null<History*>> pinned,
	base::flat_set<not_null<History*>> never,
	bool isDefault,
	bool isLocal,
	int cloudLocalOrder)
: _id(id)
, _title(title)
, _iconEmoji(iconEmoji)
, _always(std::move(always))
, _pinned(std::move(pinned))
, _never(std::move(never))
, _flags(flags)
, _isDefault(isDefault)
, _isLocal(isLocal)
, _cloudLocalOrder(cloudLocalOrder) {
}

ChatFilter ChatFilter::local(
		const LocalFolder &data,
		not_null<Session*> owner) {
	auto &&to_histories = ranges::views::transform([&](
			const uint64 &filterPeer) {
		PeerData *peer = nullptr;
		auto peerId = PeerId(filterPeer);

		if (peerIsUser(peerId)) {
			const auto user = owner->user(peerToUser(peerId).bare);
			peer = (PeerData *)user;
		} else if (peerIsChat(peerId)) {
			const auto chat = owner->chat(peerToChat(peerId).bare);
			peer = (PeerData *)chat;
		} else if (peerIsChannel(peerId)) {
			const auto channel = owner->channel(peerToChannel(peerId).bare);
			peer = (PeerData *)channel;
		}
		return peer ? owner->history(peer).get() : nullptr;
	}) | ranges::views::filter([](History *history) {
		return history != nullptr;
	}) | ranges::views::transform([](History *history) {
		return not_null<History*>(history);
	});
	auto &&always = ranges::views::concat(
		data.always
	) | to_histories;
	auto pinned = ranges::views::all(
		data.pinned
	) | to_histories | ranges::to_vector;
	auto &&never = ranges::views::all(
		data.never
	) | to_histories;
	auto &&all = ranges::views::concat(always, pinned);
	auto list = base::flat_set<not_null<History*>>{
		all.begin(),
		all.end()
	};
	const auto defaultFilterId = owner->session().account().defaultFilterId();
	return ChatFilter(
		data.id,
		data.name,
		data.emoticon,
		data.flags,
		std::move(list),
		std::move(pinned),
		{ never.begin(), never.end() },
		(data.id == defaultFilterId),
		true,
		data.cloudOrder);
}

ChatFilter ChatFilter::FromTL(
		const MTPDialogFilter &data,
		not_null<Session*> owner,
		bool isLocal) {
	return data.match([&](const MTPDdialogFilter &data) {
		const auto flags = (data.is_contacts() ? Flag::Contacts : Flag(0))
			| (data.is_non_contacts() ? Flag::NonContacts : Flag(0))
			| (data.is_groups() ? Flag::Groups : Flag(0))
			| (data.is_broadcasts() ? Flag::Channels : Flag(0))
			| (data.is_bots() ? Flag::Bots : Flag(0))
			| (data.is_exclude_muted() ? Flag::NoMuted : Flag(0))
			| (data.is_exclude_read() ? Flag::NoRead : Flag(0))
			| (data.is_exclude_archived() ? Flag::NoArchived : Flag(0));
		auto &&to_histories = ranges::views::transform([&](
				const MTPInputPeer &input) {
			const auto peer = Data::PeerFromInputMTP(owner, input);
			return peer ? owner->history(peer).get() : nullptr;
		}) | ranges::views::filter([](History *history) {
			return history != nullptr;
		}) | ranges::views::transform([](History *history) {
			return not_null<History*>(history);
		});
		auto &&always = ranges::views::concat(
			data.vinclude_peers().v
		) | to_histories;
		auto pinned = ranges::views::all(
			data.vpinned_peers().v
		) | to_histories | ranges::to_vector;
		auto &&never = ranges::views::all(
			data.vexclude_peers().v
		) | to_histories;
		auto &&all = ranges::views::concat(always, pinned);
		auto list = base::flat_set<not_null<History*>>{
			all.begin(),
			all.end()
		};
		const auto defaultFilterId = owner->session().account().defaultFilterId();
		return ChatFilter(
			data.vid().v,
			qs(data.vtitle()),
			qs(data.vemoticon().value_or_empty()),
			flags,
			std::move(list),
			std::move(pinned),
			{ never.begin(), never.end() },
			(data.vid().v == defaultFilterId),
			isLocal);
	}, [](const MTPDdialogFilterDefault &d) {
		return ChatFilter();
	}, [&](const MTPDdialogFilterChatlist &data) {
		auto &&to_histories = ranges::views::transform([&](
				const MTPInputPeer &data) {
			const auto peer = data.match([&](const MTPDinputPeerUser &data) {
				const auto user = owner->user(data.vuser_id().v);
				user->setAccessHash(data.vaccess_hash().v);
				return (PeerData*)user;
			}, [&](const MTPDinputPeerChat &data) {
				return (PeerData*)owner->chat(data.vchat_id().v);
			}, [&](const MTPDinputPeerChannel &data) {
				const auto channel = owner->channel(data.vchannel_id().v);
				channel->setAccessHash(data.vaccess_hash().v);
				return (PeerData*)channel;
			}, [&](const MTPDinputPeerSelf &data) {
				return (PeerData*)owner->session().user();
			}, [&](const auto &data) {
				return (PeerData*)nullptr;
			});
			return peer ? owner->history(peer).get() : nullptr;
		}) | ranges::views::filter([](History *history) {
			return history != nullptr;
		}) | ranges::views::transform([](History *history) {
			return not_null<History*>(history);
		});
		auto &&always = ranges::views::concat(
			data.vinclude_peers().v
		) | to_histories;
		auto pinned = ranges::views::all(
			data.vpinned_peers().v
		) | to_histories | ranges::to_vector;
		auto &&all = ranges::views::concat(always, pinned);
		auto list = base::flat_set<not_null<History*>>{
			all.begin(),
			all.end()
		};
		return ChatFilter(
			data.vid().v,
			qs(data.vtitle()),
			qs(data.vemoticon().value_or_empty()),
			(Flag::Chatlist
				| (data.is_has_my_invites() ? Flag::HasMyLinks : Flag())),
			std::move(list),
			std::move(pinned),
			{});
	});
}

ChatFilter ChatFilter::withId(FilterId id) const {
	auto result = *this;
	result._id = id;
	return result;
}

ChatFilter ChatFilter::withTitle(const QString &title) const {
	auto result = *this;
	result._title = title;
	return result;
}

ChatFilter ChatFilter::withChatlist(bool chatlist, bool hasMyLinks) const {
	auto result = *this;
	if (chatlist) {
		result._flags |= Flag::Chatlist;
		if (hasMyLinks) {
			result._flags |= Flag::HasMyLinks;
		} else {
			result._flags &= ~Flag::HasMyLinks;
		}
	} else {
		result._flags &= ~(Flag::Chatlist | Flag::HasMyLinks);
	}
	return result;
}

MTPDialogFilter ChatFilter::tl(FilterId replaceId) const {
	auto always = _always;
	auto pinned = QVector<MTPInputPeer>();
	pinned.reserve(_pinned.size());
	for (const auto &history : _pinned) {
		pinned.push_back(history->peer->input);
		always.remove(history);
	}
	auto include = QVector<MTPInputPeer>();
	include.reserve(always.size());
	for (const auto &history : always) {
		include.push_back(history->peer->input);
	}
	if (_flags & Flag::Chatlist) {
		using TLFlag = MTPDdialogFilterChatlist::Flag;
		const auto flags = TLFlag::f_emoticon;
		return MTP_dialogFilterChatlist(
			MTP_flags(flags),
			MTP_int(replaceId ? replaceId : _id),
			MTP_string(_title),
			MTP_string(_iconEmoji),
			MTP_vector<MTPInputPeer>(pinned),
			MTP_vector<MTPInputPeer>(include));
	}
	using TLFlag = MTPDdialogFilter::Flag;
	const auto flags = TLFlag::f_emoticon
		| ((_flags & Flag::Contacts) ? TLFlag::f_contacts : TLFlag(0))
		| ((_flags & Flag::NonContacts) ? TLFlag::f_non_contacts : TLFlag(0))
		| ((_flags & Flag::Groups) ? TLFlag::f_groups : TLFlag(0))
		| ((_flags & Flag::Channels) ? TLFlag::f_broadcasts : TLFlag(0))
		| ((_flags & Flag::Bots) ? TLFlag::f_bots : TLFlag(0))
		| ((_flags & Flag::NoMuted) ? TLFlag::f_exclude_muted : TLFlag(0))
		| ((_flags & Flag::NoRead) ? TLFlag::f_exclude_read : TLFlag(0))
		| ((_flags & Flag::NoArchived)
			? TLFlag::f_exclude_archived
			: TLFlag(0));
	auto never = QVector<MTPInputPeer>();
	never.reserve(_never.size());
	for (const auto &history : _never) {
		never.push_back(history->peer->input);
	}
	return MTP_dialogFilter(
		MTP_flags(flags),
		MTP_int(replaceId ? replaceId : _id),
		MTP_string(_title),
		MTP_string(_iconEmoji),
		MTP_vector<MTPInputPeer>(pinned),
		MTP_vector<MTPInputPeer>(include),
		MTP_vector<MTPInputPeer>(never));
}

LocalFolder ChatFilter::toLocal(FilterId replaceId) const {
	auto always = _always;
	auto pinned = std::vector<uint64>();
	pinned.reserve(_pinned.size());
	for (const auto &history : _pinned) {
		const auto &peer = history->peer;
		pinned.push_back(peer->id.value);
		always.remove(history);
	}
	auto include = std::vector<uint64>();
	include.reserve(always.size());
	for (const auto &history : always) {
		const auto &peer = history->peer;
		include.push_back(peer->id.value);
	}
	auto never = std::vector<uint64>();
	never.reserve(_never.size());
	for (const auto &history : _never) {
		const auto &peer = history->peer;
		never.push_back(peer->id.value);
	}
	return {
		.id = replaceId ? replaceId : _id,
		.cloudOrder = _cloudLocalOrder,
		.name = _title,
		.emoticon = _iconEmoji,
		.always = include,
		.never = never,
		.pinned = pinned,
		.flags = _flags
	};
}

FilterId ChatFilter::id() const {
	return _id;
}

QString ChatFilter::title() const {
	return _title;
}

bool ChatFilter::isDefault() const {
	return _isDefault;
}

QString ChatFilter::iconEmoji() const {
	return _iconEmoji;
}

ChatFilter::Flags ChatFilter::flags() const {
	return _flags;
}

bool ChatFilter::chatlist() const {
	return _flags & Flag::Chatlist;
}

bool ChatFilter::hasMyLinks() const {
	return _flags & Flag::HasMyLinks;
}

const base::flat_set<not_null<History*>> &ChatFilter::always() const {
	return _always;
}

const std::vector<not_null<History*>> &ChatFilter::pinned() const {
	return _pinned;
}

const base::flat_set<not_null<History*>> &ChatFilter::never() const {
	return _never;
}

bool ChatFilter::contains(not_null<History*> history) const {
	if (_never.contains(history)) {
		return false;
	}
	const auto flag = [&] {
		const auto peer = history->peer;
		if (const auto user = peer->asUser()) {
			return user->isBot()
				? Flag::Bots
				: user->isContact()
				? Flag::Contacts
				: Flag::NonContacts;
		} else if (const auto chat = peer->asChat()) {
			return Flag::Groups;
		} else if (const auto channel = peer->asChannel()) {
			if (channel->isBroadcast()) {
				return Flag::Channels;
			} else {
				return Flag::Groups;
			}
		} else {
			Unexpected("Peer type in ChatFilter::contains.");
		}
	}();
	const auto filterAdmin = [&] {
		if (!(_flags & Flag::Owned)
			&& !(_flags & Flag::NotOwned)
			&& !(_flags & Flag::Admin)
			&& !(_flags & Flag::NotAdmin)) {
			return true;
		}

		const auto peer = history->peer;
		if (const auto chat = peer->asChat()) {
			// if i created the chat:
			// // if the filter excludes owned chats, don't add in list
			// // if the filter excludes non-admin chats, add only if filter includes owned chats
			// else if i am admin in chat:
			// // if the filter excludes admin chats, don't add in list
			// // if the filter excludes non-owned chats, add only if filter includes admin chats
			// else:
			// // add in list only if filter doesn't exclude non-owned or non-admin chats
			if (chat->amCreator()) {
				return !(_flags & Flag::NotOwned) && ((_flags & Flag::Admin)
					? (_flags & Flag::Owned)
					: true);
			} else if (chat->hasAdminRights()) {
				return !(_flags & Flag::NotAdmin) && ((_flags & Flag::Owned)
					? (_flags & Flag::Admin)
					: true);
			} else {
				return !(_flags & Flag::Owned) && !(_flags & Flag::Admin);
			}
		} else if (const auto channel = peer->asChannel()) {
			if (channel->amCreator()) {
				return !(_flags & Flag::NotOwned) && ((_flags & Flag::Admin)
					? (_flags & Flag::Owned)
					: true);
			} else if (channel->hasAdminRights()) {
				return !(_flags & Flag::NotAdmin) && ((_flags & Flag::Owned)
					? (_flags & Flag::Admin)
					: true);
			} else {
				return !(_flags & Flag::Owned) && !(_flags & Flag::Admin);
			}
		}

		return false;
	};
	const auto filterUnfiltered = [&] {
		if (!(_flags & Flag::NoFilter)) {
			return true;
		}

		const auto &list = history->owner().chatsFilters().list();
		for (auto filter : list) {
			if (filter.id() == _id) {
				continue;
			}

			if (filter.contains(history)) {
				return false;
			}
		}

		return true;
	};
	const auto state = (_flags & (Flag::NoMuted | Flag::NoRead))
		? history->chatListBadgesState()
		: Dialogs::BadgesState();
	return false
		|| ((_flags & flag)
			&& filterAdmin()
			&& (!(_flags & Flag::Recent)
				|| history->owner().session().account().isRecent(history->peer->id))
			&& (!(_flags & Flag::NoMuted)
				|| !history->muted()
				|| (state.mention
					&& history->folderKnown()
					&& !history->folder()))
			&& (!(_flags & Flag::NoRead)
				|| state.unread
				|| state.mention
				|| history->fakeUnreadWhileOpened())
			&& (!(_flags & Flag::NoArchived)
				|| (history->folderKnown() && !history->folder()))
			&& filterUnfiltered())
		|| _always.contains(history);
}

bool ChatFilter::isLocal() const {
	return _isLocal;
}

ChatFilters::ChatFilters(not_null<Session*> owner)
: _owner(owner)
, _moreChatsTimer([=] { checkLoadMoreChatsLists(); }) {
	_list.emplace_back();
	crl::on_main(&owner->session(), [=] { load(); });
}

ChatFilters::~ChatFilters() = default;

not_null<Dialogs::MainList*> ChatFilters::chatsList(FilterId filterId) {
	auto &pointer = _chatsLists[filterId];
	if (!pointer) {
		auto limit = rpl::single(rpl::empty_value()) | rpl::then(
			_owner->session().account().appConfig().refreshed()
		) | rpl::map([=] {
			return _owner->pinnedChatsLimit(filterId);
		});
		pointer = std::make_unique<Dialogs::MainList>(
			&_owner->session(),
			filterId,
			_owner->maxPinnedChatsLimitValue(filterId));
	}
	return pointer.get();
}

void ChatFilters::clear() {
	_chatsLists.clear();
	_list.clear();
}

void ChatFilters::setPreloaded(const QVector<MTPDialogFilter> &result) {
	_loadRequestId = -1;
	received(result);
	crl::on_main(&_owner->session(), [=] {
		if (_loadRequestId == -1) {
			_loadRequestId = 0;
		}
	});
}

void ChatFilters::load() {
	load(false);
}

void ChatFilters::reload() {
	_reloading = true;
	load();
}

void ChatFilters::load(bool force) {
	if (_loadRequestId && !force) {
		return;
	}
	auto &api = _owner->session().api();
	api.request(_loadRequestId).cancel();
	_loadRequestId = api.request(MTPmessages_GetDialogFilters(
	)).done([=](const MTPVector<MTPDialogFilter> &result) {
		received(result.v);
		_loadRequestId = 0;
	}).fail([=] {
		_loadRequestId = 0;
		if (_reloading) {
			_reloading = false;
			_listChanged.fire({});
		}
	}).send();
}

void ChatFilters::received(const QVector<MTPDialogFilter> &list) {
	const auto account = &_owner->session().account();
	const auto accountId = account->session().userId().bare;
	const auto isTestAccount = account->mtp().isTestMode();
	auto localFilters = ::Kotato::JsonSettings::GetJsonArray("folders/local", accountId, isTestAccount);
	const auto limit = Data::PremiumLimits(&_owner->session()).dialogFiltersCurrent();
	auto position = 0;
	auto originalPosition = 0;
	auto changed = false;

	auto addToList = [&] (ChatFilter parsed) {
		const auto b = begin(_list) + position, e = end(_list);
		const auto i = ranges::find(b, e, parsed.id(), &ChatFilter::id);
		if (i == e) {
			applyInsert(std::move(parsed), position);
			changed = true;
		} else if (i == b) {
			if (applyChange(*b, std::move(parsed))) {
				changed = true;
			}
		} else {
			std::swap(*i, *b);
			applyChange(*b, std::move(parsed));
			changed = true;
		}
		++position;
	};

	// First we're ensuring that IDs are correct
	auto leastLocalId = limit;
	for (auto localFilter : localFilters) {
		auto local = localFilter.toObject();
		if (leastLocalId > local.value("id").toInt()) {
			leastLocalId = local.value("id").toInt();
		}
	}
	if (leastLocalId < limit) {
		const auto diff = limit - leastLocalId;
		auto localFolders = QJsonArray();
		for (auto localFilter : localFilters) {
			auto local = localFilter.toObject();
			local.insert("id", local.value("id").toInt() + diff);
			localFolders << local;
		}
		::Kotato::JsonSettings::Set("folders/local", localFolders, accountId, isTestAccount);
		::Kotato::JsonSettings::Write();
	}

	// Now we're adding cloud filters and corresponding local filters.
	for (const auto &filter : list) {
		addToList(ChatFilter::FromTL(filter, _owner));
		for (const auto &localFilter : localFilters) {
			auto local = MakeLocalFolder(localFilter.toObject());
			if (local.cloudOrder != originalPosition) {
				continue;
			}
			addToList(ChatFilter::local(local, _owner));
		}
		++originalPosition;
	}

	// Then we adding local filters, retaining cloud order
	while (originalPosition < limit) {
		for (const auto &localFilter : localFilters) {
			auto local = MakeLocalFolder(localFilter.toObject());
			if (local.cloudOrder != originalPosition) {
				continue;
			}
			addToList(ChatFilter::local(local, _owner));
		}
		++originalPosition;
	}

	// And finally we adding other filters
	for (const auto &localFilter : localFilters) {
		auto local = MakeLocalFolder(localFilter.toObject());
		if (local.cloudOrder < limit) {
			continue;
		}
		addToList(ChatFilter::local(local, _owner));
	}

	while (position < _list.size()) {
		applyRemove(position);
		changed = true;
	}
	if (!ranges::contains(begin(_list), end(_list), 0, &ChatFilter::id)) {
		_list.insert(begin(_list), ChatFilter());
	}
	if (changed || !_loaded || _reloading) {
		_loaded = true;
		_reloading = false;
		_listChanged.fire({});
	}
}

void ChatFilters::apply(const MTPUpdate &update) {
	update.match([&](const MTPDupdateDialogFilter &data) {
		if (const auto filter = data.vfilter()) {
			set(ChatFilter::FromTL(*filter, _owner));
		} else {
			remove(data.vid().v);
		}
	}, [&](const MTPDupdateDialogFilters &data) {
		load(true);
	}, [&](const MTPDupdateDialogFilterOrder &data) {
		if (applyOrder(data.vorder().v)) {
			_listChanged.fire({});
		} else {
			load(true);
		}
	}, [](auto&&) {
		Unexpected("Update in ChatFilters::apply.");
	});
}

ChatFilterLink ChatFilters::add(
		FilterId id,
		const MTPExportedChatlistInvite &update) {
	const auto i = ranges::find(_list, id, &ChatFilter::id);
	if (i == end(_list) || !i->chatlist()) {
		LOG(("Api Error: "
			"Attempt to add chatlist link to a non-chatlist filter: %1"
			).arg(id));
		return {};
	}
	auto &links = _chatlistLinks[id];
	const auto &data = update.data();
	const auto url = qs(data.vurl());
	const auto title = qs(data.vtitle());
	auto chats = data.vpeers().v | ranges::views::transform([&](
			const MTPPeer &peer) {
		return _owner->history(peerFromMTP(peer));
	}) | ranges::to_vector;
	const auto j = ranges::find(links, url, &ChatFilterLink::url);
	if (j != end(links)) {
		if (j->title != title || j->chats != chats) {
			j->title = title;
			j->chats = std::move(chats);
			_chatlistLinksUpdated.fire_copy(id);
		}
		return *j;
	}
	links.push_back({
		.id = id,
		.url = url,
		.title = title,
		.chats = std::move(chats),
	});
	_chatlistLinksUpdated.fire_copy(id);
	return links.back();
}

void ChatFilters::edit(
		FilterId id,
		const QString &url,
		const QString &title) {
	auto &links = _chatlistLinks[id];
	const auto i = ranges::find(links, url, &ChatFilterLink::url);
	if (i != end(links)) {
		i->title = title;
		_chatlistLinksUpdated.fire_copy(id);

		_owner->session().api().request(MTPchatlists_EditExportedInvite(
			MTP_flags(MTPchatlists_EditExportedInvite::Flag::f_title),
			MTP_inputChatlistDialogFilter(MTP_int(id)),
			MTP_string(url),
			MTP_string(title),
			MTPVector<MTPInputPeer>() // peers
		)).done([=](const MTPExportedChatlistInvite &result) {
			//const auto &data = result.data();
			//const auto link = _owner->chatsFilters().add(id, result);
			//done(link);
		}).fail([=](const MTP::Error &error) {
			//done({ .id = id });
		}).send();
	}
}

void ChatFilters::destroy(FilterId id, const QString &url) {
	auto &links = _chatlistLinks[id];
	const auto i = ranges::find(links, url, &ChatFilterLink::url);
	if (i != end(links)) {
		links.erase(i);
		_chatlistLinksUpdated.fire_copy(id);

		const auto api = &_owner->session().api();
		api->request(_linksRequestId).cancel();
		_linksRequestId = api->request(MTPchatlists_DeleteExportedInvite(
			MTP_inputChatlistDialogFilter(MTP_int(id)),
			MTP_string(url)
		)).send();
	}
}

rpl::producer<std::vector<ChatFilterLink>> ChatFilters::chatlistLinks(
		FilterId id) const {
	return _chatlistLinksUpdated.events_starting_with_copy(
		id
	) | rpl::filter(rpl::mappers::_1 == id) | rpl::map([=] {
		const auto i = _chatlistLinks.find(id);
		return (i != end(_chatlistLinks))
			? i->second
			: std::vector<ChatFilterLink>();
	});
}

void ChatFilters::reloadChatlistLinks(FilterId id) {
	const auto api = &_owner->session().api();
	api->request(_linksRequestId).cancel();
	_linksRequestId = api->request(MTPchatlists_GetExportedInvites(
		MTP_inputChatlistDialogFilter(MTP_int(id))
	)).done([=](const MTPchatlists_ExportedInvites &result) {
		const auto &data = result.data();
		_owner->processUsers(data.vusers());
		_owner->processChats(data.vchats());
		_chatlistLinks[id].clear();
		for (const auto &link : data.vinvites().v) {
			add(id, link);
		}
		_chatlistLinksUpdated.fire_copy(id);
	}).send();
}

void ChatFilters::set(ChatFilter filter) {
	if (!filter.id()) {
		return;
	}
	const auto i = ranges::find(_list, filter.id(), &ChatFilter::id);
	if (i == end(_list)) {
		applyInsert(std::move(filter), _list.size());
		_listChanged.fire({});
	} else if (applyChange(*i, std::move(filter))) {
		_listChanged.fire({});
	}
}

void ChatFilters::applyInsert(ChatFilter filter, int position) {
	Expects(position >= 0 && position <= _list.size());

	_list.insert(
		begin(_list) + position,
		ChatFilter(filter.id(), {}, {}, {}, {}, {}, {}, false, filter.isLocal()));
	applyChange(*(begin(_list) + position), std::move(filter));
}

void ChatFilters::remove(FilterId id) {
	const auto i = ranges::find(_list, id, &ChatFilter::id);
	if (i == end(_list)) {
		return;
	}
	applyRemove(i - begin(_list));
	_listChanged.fire({});
}

void ChatFilters::moveAllToFront() {
	const auto i = ranges::find(_list, FilterId(), &ChatFilter::id);
	if (!_list.empty() && i == begin(_list)) {
		return;
	} else if (i != end(_list)) {
		_list.erase(i);
	}
	_list.insert(begin(_list), ChatFilter());
}

void ChatFilters::applyRemove(int position) {
	Expects(position >= 0 && position < _list.size());

	const auto i = begin(_list) + position;
	applyChange(*i, ChatFilter(i->id(), {}, {}, {}, {}, {}, {}));
	_list.erase(i);
}

bool ChatFilters::applyChange(ChatFilter &filter, ChatFilter &&updated) {
	Expects(filter.id() == updated.id());

	using Flag = ChatFilter::Flag;

	const auto id = filter.id();
	const auto exceptionsChanged = filter.always() != updated.always();
	const auto rulesMask = ~(Flag::Chatlist | Flag::HasMyLinks);
	const auto rulesChanged = exceptionsChanged
		|| ((filter.flags() & rulesMask) != (updated.flags() & rulesMask))
		|| (filter.never() != updated.never());
	const auto pinnedChanged = (filter.pinned() != updated.pinned());
	const auto chatlistChanged = (filter.chatlist() != updated.chatlist())
		|| (filter.hasMyLinks() != updated.hasMyLinks());
	const auto listUpdated = rulesChanged
		|| pinnedChanged
		|| (filter.title() != updated.title())
		|| (filter.iconEmoji() != updated.iconEmoji());
	if (!listUpdated && !chatlistChanged) {
		return false;
	}
	if (rulesChanged) {
		const auto filterList = _owner->chatsFilters().chatsList(id);
		const auto feedHistory = [&](not_null<History*> history) {
			const auto now = updated.contains(history);
			const auto was = filter.contains(history);
			if (now != was) {
				if (now) {
					history->addToChatList(id, filterList);
				} else {
					history->removeFromChatList(id, filterList);
				}
			}
		};
		const auto feedList = [&](not_null<const Dialogs::MainList*> list) {
			for (const auto &entry : *list->indexed()) {
				if (const auto history = entry->history()) {
					feedHistory(history);
				}
			}
		};
		feedList(_owner->chatsList());
		if (const auto folder = _owner->folderLoaded(Data::Folder::kId)) {
			feedList(folder->chatsList());
		}
		if (exceptionsChanged && !updated.always().empty()) {
			_exceptionsToLoad.push_back(id);
			Ui::PostponeCall(&_owner->session(), [=] {
				_owner->session().api().requestMoreDialogsIfNeeded();
			});
		}
	}
	filter = std::move(updated);
	if (pinnedChanged) {
		const auto filterList = _owner->chatsFilters().chatsList(id);
		filterList->pinned()->applyList(filter.pinned());
	}
	if (chatlistChanged) {
		_isChatlistChanged.fire_copy(id);
	}
	return listUpdated;
}

bool ChatFilters::applyOrder(const QVector<MTPint> &order) {
	if (order.size() != _list.size()) {
		return false;
	} else if (_list.empty()) {
		return true;
	}
	auto indices = ranges::views::all(
		_list
	) | ranges::views::transform(
		&ChatFilter::id
	) | ranges::to_vector;
	auto b = indices.begin(), e = indices.end();
	for (const auto &id : order) {
		const auto i = ranges::find(b, e, id.v);
		if (i == e) {
			return false;
		} else if (i != b) {
			std::swap(*i, *b);
		}
		++b;
	}
	auto changed = false;
	auto begin = _list.begin(), end = _list.end();
	for (const auto &id : order) {
		const auto i = ranges::find(begin, end, id.v, &ChatFilter::id);
		Assert(i != end);
		if (i != begin) {
			changed = true;
			std::swap(*i, *begin);
		}
		++begin;
	}
	if (changed) {
		_listChanged.fire({});
	}
	return true;
}

const ChatFilter &ChatFilters::applyUpdatedPinned(
		FilterId id,
		const std::vector<Dialogs::Key> &dialogs) {
	const auto i = ranges::find(_list, id, &ChatFilter::id);
	Assert(i != end(_list));

	const auto limit = _owner->pinnedChatsLimit(id);
	auto always = i->always();
	auto pinned = std::vector<not_null<History*>>();
	pinned.reserve(dialogs.size());
	for (const auto &row : dialogs) {
		if (const auto history = row.history()) {
			if (always.contains(history)) {
				pinned.push_back(history);
			} else if (always.size() < limit || i->isLocal()) {
				always.insert(history);
				pinned.push_back(history);
			}
		}
	}
	const auto defaultFilterId = _owner->session().account().defaultFilterId();
	set(ChatFilter(
		id,
		i->title(),
		i->iconEmoji(),
		i->flags(),
		std::move(always),
		std::move(pinned),
		i->never(),
		(id == defaultFilterId),
		i->isLocal()));
	return *i;
}

void ChatFilters::saveOrder(
		const std::vector<FilterId> &order,
		mtpRequestId after) {
	if (after) {
		_saveOrderAfterId = after;
	}
	const auto api = &_owner->session().api();
	api->request(_saveOrderRequestId).cancel();
	const auto limit = Data::PremiumLimits(&_owner->session()).dialogFiltersCurrent();

	auto ids = QVector<MTPint>();
	ids.reserve(order.size());
	auto cloudIds = QVector<MTPint>();
	cloudIds.reserve(limit);

	for (const auto id : order) {
		ids.push_back(MTP_int(id));

		auto i = ranges::find(_list, id, &ChatFilter::id);
		Assert(i != end(_list));

		if ((*i).isLocal()) {
			i->setLocalCloudOrder(cloudIds.size());
		} else {
			cloudIds.push_back(MTP_int(id));
		}
	}
	const auto wrapped = MTP_vector<MTPint>(ids);
	apply(MTP_updateDialogFilterOrder(wrapped));

	if (!cloudIds.isEmpty()) {
		const auto cloudWrapped = MTP_vector<MTPint>(cloudIds);
		_saveOrderRequestId = api->request(MTPmessages_UpdateDialogFiltersOrder(
			cloudWrapped
		)).afterRequest(_saveOrderAfterId).send();
	}
}

bool ChatFilters::archiveNeeded() const {
	for (const auto &filter : _list) {
		if (!(filter.flags() & ChatFilter::Flag::NoArchived)) {
			return true;
		}
	}
	return false;
}

const std::vector<ChatFilter> &ChatFilters::list() const {
	return _list;
}

FilterId ChatFilters::defaultId() const {
	return lookupId(0);
}

FilterId ChatFilters::lookupId(int index) const {
	Expects(index >= 0 && index < _list.size());

	if (_owner->session().user()->isPremium() || !_list.front().id()) {
		return _list[index].id();
	}
	const auto i = ranges::find(_list, FilterId(0), &ChatFilter::id);
	return !index
		? FilterId()
		: (index <= int(i - begin(_list)))
		? _list[index - 1].id()
		: _list[index].id();
}

bool ChatFilters::loaded() const {
	return _loaded;
}

bool ChatFilters::has() const {
	return _list.size() > 1;
}

rpl::producer<> ChatFilters::changed() const {
	return _listChanged.events();
}

rpl::producer<FilterId> ChatFilters::isChatlistChanged() const {
	return _isChatlistChanged.events();
}

bool ChatFilters::loadNextExceptions(bool chatsListLoaded) {
	if (_exceptionsLoadRequestId) {
		return true;
	} else if (!chatsListLoaded
		&& (_owner->chatsList()->fullSize().current()
			< kLoadExceptionsAfter)) {
		return false;
	}
	auto inputs = QVector<MTPInputDialogPeer>();
	const auto collectExceptions = [&](FilterId id) {
		auto result = QVector<MTPInputDialogPeer>();
		const auto i = ranges::find(_list, id, &ChatFilter::id);
		if (i != end(_list)) {
			result.reserve(i->always().size());
			for (const auto &history : i->always()) {
				if (!history->folderKnown()) {
					inputs.push_back(
						MTP_inputDialogPeer(history->peer->input));
				}
			}
		}
		return result;
	};
	while (!_exceptionsToLoad.empty()) {
		const auto id = _exceptionsToLoad.front();
		const auto exceptions = collectExceptions(id);
		if (inputs.size() + exceptions.size() > kLoadExceptionsPerRequest) {
			Assert(!inputs.isEmpty());
			break;
		}
		_exceptionsToLoad.pop_front();
		inputs.append(exceptions);
	}
	if (inputs.isEmpty()) {
		return false;
	}
	const auto api = &_owner->session().api();
	_exceptionsLoadRequestId = api->request(MTPmessages_GetPeerDialogs(
		MTP_vector(inputs)
	)).done([=](const MTPmessages_PeerDialogs &result) {
		_exceptionsLoadRequestId = 0;
		_owner->session().data().histories().applyPeerDialogs(result);
		_owner->session().api().requestMoreDialogsIfNeeded();
	}).fail([=] {
		_exceptionsLoadRequestId = 0;
		_owner->session().api().requestMoreDialogsIfNeeded();
	}).send();
	return true;
}

void ChatFilters::refreshHistory(not_null<History*> history) {
	if (history->inChatList() && !list().empty()) {
		_owner->refreshChatListEntry(history);
	}
}

void ChatFilters::requestSuggested() {
	if (_suggestedRequestId) {
		return;
	}
	if (_suggestedLastReceived > 0
		&& crl::now() - _suggestedLastReceived < kRefreshSuggestedTimeout) {
		return;
	}
	const auto api = &_owner->session().api();
	_suggestedRequestId = api->request(MTPmessages_GetSuggestedDialogFilters(
	)).done([=](const MTPVector<MTPDialogFilterSuggested> &data) {
		_suggestedRequestId = 0;
		_suggestedLastReceived = crl::now();

		_suggested = ranges::views::all(
			data.v
		) | ranges::views::transform([&](const MTPDialogFilterSuggested &f) {
			return f.match([&](const MTPDdialogFilterSuggested &data) {
				return SuggestedFilter{
					Data::ChatFilter::FromTL(data.vfilter(), _owner),
					qs(data.vdescription())
				};
			});
		}) | ranges::to_vector;

		_suggestedUpdated.fire({});
	}).fail([=] {
		_suggestedRequestId = 0;
		_suggestedLastReceived = crl::now() + kRefreshSuggestedTimeout / 2;

		_suggestedUpdated.fire({});
	}).send();
}

bool ChatFilters::suggestedLoaded() const {
	return (_suggestedLastReceived > 0);
}

const std::vector<SuggestedFilter> &ChatFilters::suggestedFilters() const {
	return _suggested;
}

rpl::producer<> ChatFilters::suggestedUpdated() const {
	return _suggestedUpdated.events();
}

rpl::producer<Ui::MoreChatsBarContent> ChatFilters::moreChatsContent(
		FilterId id) {
	if (!id) {
		return rpl::single(Ui::MoreChatsBarContent{ .count = 0 });
	}
	return [=](auto consumer) {
		auto result = rpl::lifetime();

		auto &entry = _moreChatsData[id];
		auto watching = entry.watching.lock();
		if (!watching) {
			watching = std::make_shared<bool>(true);
			entry.watching = watching;
		}
		result.add([watching] {});

		_moreChatsUpdated.events_starting_with_copy(
			id
		) | rpl::start_with_next([=] {
			consumer.put_next(Ui::MoreChatsBarContent{
				.count = int(moreChats(id).size()),
			});
		}, result);
		loadMoreChatsList(id);

		return result;
	};
}

const std::vector<not_null<PeerData*>> &ChatFilters::moreChats(
		FilterId id) const {
	static const auto kEmpty = std::vector<not_null<PeerData*>>();
	if (!id) {
		return kEmpty;
	}
	const auto i = _moreChatsData.find(id);
	return (i != end(_moreChatsData)) ? i->second.missing : kEmpty;
}

void ChatFilters::moreChatsHide(FilterId id, bool localOnly) {
	if (!localOnly) {
		const auto api = &_owner->session().api();
		api->request(MTPchatlists_HideChatlistUpdates(
			MTP_inputChatlistDialogFilter(MTP_int(id))
		)).send();
	}

	const auto i = _moreChatsData.find(id);
	if (i != end(_moreChatsData)) {
		if (const auto requestId = base::take(i->second.requestId)) {
			_owner->session().api().request(requestId).cancel();
		}
		i->second.missing = {};
		i->second.lastUpdate = crl::now();
		_moreChatsUpdated.fire_copy(id);
	}
}

void ChatFilters::loadMoreChatsList(FilterId id) {
	Expects(id != 0);

	const auto i = ranges::find(_list, id, &ChatFilter::id);
	if (i == end(_list) || !i->chatlist()) {
		return;
	}

	auto &entry = _moreChatsData[id];
	const auto now = crl::now();
	if (!entry.watching.lock() || entry.requestId) {
		return;
	}
	const auto last = entry.lastUpdate;
	const auto next = last ? (last + RequestUpdatesEach(_owner)) : 0;
	if (next > now) {
		if (!_moreChatsTimer.isActive()) {
			_moreChatsTimer.callOnce(next - now);
		}
		return;
	}
	auto &api = _owner->session().api();
	entry.requestId = api.request(MTPchatlists_GetChatlistUpdates(
		MTP_inputChatlistDialogFilter(MTP_int(id))
	)).done([=](const MTPchatlists_ChatlistUpdates &result) {
		const auto &data = result.data();
		_owner->processUsers(data.vusers());
		_owner->processChats(data.vchats());
		auto list = ranges::views::all(
			data.vmissing_peers().v
		) | ranges::views::transform([&](const MTPPeer &peer) {
			return _owner->peer(peerFromMTP(peer));
		}) | ranges::to_vector;

		auto &entry = _moreChatsData[id];
		entry.requestId = 0;
		entry.lastUpdate = crl::now();
		if (!_moreChatsTimer.isActive()) {
			_moreChatsTimer.callOnce(RequestUpdatesEach(_owner));
		}
		if (entry.missing != list) {
			entry.missing = std::move(list);
			_moreChatsUpdated.fire_copy(id);
		}
	}).fail([=] {
		auto &entry = _moreChatsData[id];
		entry.requestId = 0;
		entry.lastUpdate = crl::now();
	}).send();
}

void ChatFilters::checkLoadMoreChatsLists() {
	for (const auto &[id, entry] : _moreChatsData) {
		loadMoreChatsList(id);
	}
}

void ChatFilters::saveLocal() {
	auto localFolders = QJsonArray();
	const auto account = &_owner->session().account();
	const auto accountId = account->session().userId().bare;
	const auto isTestAccount = account->mtp().isTestMode();

	for (const auto &folder : _list) {
		if (folder.isLocal()) {
			localFolders << folder.toLocal().toJson();
		}
	}

	::Kotato::JsonSettings::Set("folders/local", localFolders, accountId, isTestAccount);
	::Kotato::JsonSettings::Write();
}

} // namespace Data
