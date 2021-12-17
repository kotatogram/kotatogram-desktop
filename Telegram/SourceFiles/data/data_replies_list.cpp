/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_replies_list.h"

#include "history/history.h"
#include "history/history_item.h"
#include "history/history_service.h"
#include "main/main_session.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_messages.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kMessagesPerPage = 50;

[[nodiscard]] HistoryService *GenerateDivider(
		not_null<History*> history,
		TimeId date,
		const QString &text) {
	return history->makeServiceMessage(
		history->nextNonHistoryEntryId(),
		MessageFlag::FakeHistoryItem,
		date,
		HistoryService::PreparedText{ text },
		PeerId(0),
		nullptr,
		false);
}

} // namespace

struct RepliesList::Viewer {
	MessagesSlice slice;
	MsgId around = 0;
	int limitBefore = 0;
	int limitAfter = 0;
	int injectedForRoot = 0;
	base::has_weak_ptr guard;
	bool scheduled = false;
};

RepliesList::RepliesList(not_null<History*> history, MsgId rootId)
: _history(history)
, _rootId(rootId) {
}

RepliesList::~RepliesList() {
	histories().cancelRequest(base::take(_beforeId));
	histories().cancelRequest(base::take(_afterId));
	if (_divider) {
		_divider->destroy();
	}
}

rpl::producer<MessagesSlice> RepliesList::source(
		MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto around = aroundId.fullId.msg;
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto viewer = lifetime.make_state<Viewer>();
		const auto push = [=] {
			viewer->scheduled = false;
			if (buildFromData(viewer)) {
				appendClientSideMessages(viewer->slice);
				consumer.put_next_copy(viewer->slice);
			}
		};
		const auto pushDelayed = [=] {
			if (!viewer->scheduled) {
				viewer->scheduled = true;
				crl::on_main(&viewer->guard, push);
			}
		};
		viewer->around = around;
		viewer->limitBefore = limitBefore;
		viewer->limitAfter = limitAfter;

		_history->session().changes().messageUpdates(
			MessageUpdate::Flag::NewAdded
			| MessageUpdate::Flag::NewMaybeAdded
			| MessageUpdate::Flag::Destroyed
		) | rpl::filter([=](const MessageUpdate &update) {
			return applyUpdate(viewer, update);
		}) | rpl::start_with_next(pushDelayed, lifetime);

		_history->session().changes().historyUpdates(
			_history,
			Data::HistoryUpdate::Flag::ClientSideMessages
		) | rpl::start_with_next(pushDelayed, lifetime);

		_partLoaded.events(
		) | rpl::start_with_next(pushDelayed, lifetime);

		_history->owner().channelDifferenceTooLong(
		) | rpl::filter([=](not_null<ChannelData*> channel) {
			if (_history->peer != channel || !_skippedAfter.has_value()) {
				return false;
			}
			_skippedAfter = std::nullopt;
			return true;
		}) | rpl::start_with_next(pushDelayed, lifetime);

		push();
		return lifetime;
	};
}

void RepliesList::appendClientSideMessages(MessagesSlice &slice) {
	const auto &messages = _history->clientSideMessages();
	if (messages.empty()) {
		return;
	} else if (slice.ids.empty()) {
		if (slice.skippedBefore != 0 || slice.skippedAfter != 0) {
			return;
		}
		slice.ids.reserve(messages.size());
		for (const auto &item : messages) {
			if (item->replyToTop() != _rootId) {
				continue;
			}
			slice.ids.push_back(item->fullId());
		}
		ranges::sort(slice.ids);
		return;
	}
	auto &owner = _history->owner();
	auto dates = std::vector<TimeId>();
	dates.reserve(slice.ids.size());
	for (const auto &id : slice.ids) {
		const auto message = owner.message(id);
		Assert(message != nullptr);

		dates.push_back(message->date());
	}
	for (const auto &item : messages) {
		if (item->replyToTop() != _rootId) {
			continue;
		}
		const auto date = item->date();
		if (date < dates.front()) {
			if (slice.skippedBefore != 0) {
				if (slice.skippedBefore) {
					++*slice.skippedBefore;
				}
				continue;
			}
			dates.insert(dates.begin(), date);
			slice.ids.insert(slice.ids.begin(), item->fullId());
		} else {
			auto to = dates.size();
			for (; to != 0; --to) {
				const auto checkId = slice.ids[to - 1].msg;
				if (dates[to - 1] > date) {
					continue;
				} else if (dates[to - 1] < date
					|| IsServerMsgId(checkId)
					|| checkId < item->id) {
					break;
				}
			}
			dates.insert(dates.begin() + to, date);
			slice.ids.insert(slice.ids.begin() + to, item->fullId());
		}
	}
}

rpl::producer<int> RepliesList::fullCount() const {
	return _fullCount.value() | rpl::filter_optional();
}

std::optional<int> RepliesList::fullUnreadCountAfter(
		MsgId readTillId,
		MsgId wasReadTillId,
		std::optional<int> wasUnreadCountAfter) const {
	Expects(readTillId >= wasReadTillId);

	readTillId = std::max(readTillId, _rootId);
	wasReadTillId = std::max(wasReadTillId, _rootId);
	const auto backLoaded = (_skippedBefore == 0);
	const auto frontLoaded = (_skippedAfter == 0);
	const auto fullLoaded = backLoaded && frontLoaded;
	const auto allUnread = (readTillId == _rootId)
		|| (fullLoaded && _list.empty());
	const auto countIncoming = [&](auto from, auto till) {
		auto &owner = _history->owner();
		const auto channelId = _history->channelId();
		auto count = 0;
		for (auto i = from; i != till; ++i) {
			if (!owner.message(channelId, *i)->out()) {
				++count;
			}
		}
		return count;
	};
	if (allUnread && fullLoaded) {
		// Should not happen too often unless the list is empty.
		return countIncoming(begin(_list), end(_list));
	} else if (frontLoaded && !_list.empty() && readTillId >= _list.front()) {
		// Always "count by local data" if read till the end.
		return 0;
	} else if (wasReadTillId == readTillId) {
		// Otherwise don't recount the same value over and over.
		return wasUnreadCountAfter;
	} else if (frontLoaded && !_list.empty() && readTillId >= _list.back()) {
		// And count by local data if it is available and read-till changed.
		return countIncoming(
			begin(_list),
			ranges::lower_bound(_list, readTillId, std::greater<>()));
	} else if (_list.empty()) {
		return std::nullopt;
	} else if (wasUnreadCountAfter.has_value()
		&& (frontLoaded || readTillId <= _list.front())
		&& (backLoaded || wasReadTillId >= _list.back())) {
		// Count how many were read since previous value.
		const auto from = ranges::lower_bound(
			_list,
			readTillId,
			std::greater<>());
		const auto till = ranges::lower_bound(
			from,
			end(_list),
			wasReadTillId,
			std::greater<>());
		return std::max(*wasUnreadCountAfter - countIncoming(from, till), 0);
	}
	return std::nullopt;
}

void RepliesList::injectRootMessageAndReverse(not_null<Viewer*> viewer) {
	injectRootMessage(viewer);
	ranges::reverse(viewer->slice.ids);
}

void RepliesList::injectRootMessage(not_null<Viewer*> viewer) {
	const auto slice = &viewer->slice;
	viewer->injectedForRoot = 0;
	if (slice->skippedBefore != 0) {
		return;
	}
	const auto root = lookupRoot();
	if (!root) {
		return;
	}
	injectRootDivider(root, slice);

	if (const auto group = _history->owner().groups().find(root)) {
		for (const auto &item : ranges::views::reverse(group->items)) {
			slice->ids.push_back(item->fullId());
		}
		viewer->injectedForRoot = group->items.size();
		if (slice->fullCount) {
			*slice->fullCount += group->items.size();
		}
	} else {
		slice->ids.push_back(root->fullId());
		viewer->injectedForRoot = 1;
	}
	if (slice->fullCount) {
		*slice->fullCount += viewer->injectedForRoot;
	}
}

void RepliesList::injectRootDivider(
		not_null<HistoryItem*> root,
		not_null<MessagesSlice*> slice) {
	const auto withComments = !slice->ids.empty();
	const auto text = [&] {
		return withComments
			? tr::lng_replies_discussion_started(tr::now)
			: tr::lng_replies_no_comments(tr::now);
	};
	if (!_divider) {
		_dividerWithComments = withComments;
		_divider = GenerateDivider(
			_history,
			root->date(),
			text());
	} else if (_dividerWithComments != withComments) {
		_dividerWithComments = withComments;
		_divider->setNeedTime(false);
		_divider->setServiceText(HistoryService::PreparedText{ text() });
	}
	slice->ids.push_back(_divider->fullId());
}

bool RepliesList::buildFromData(not_null<Viewer*> viewer) {
	if (_list.empty() && _skippedBefore == 0 && _skippedAfter == 0) {
		viewer->slice.ids.clear();
		viewer->slice.nearestToAround = FullMsgId();
		viewer->slice.fullCount
			= viewer->slice.skippedBefore
			= viewer->slice.skippedAfter
			= 0;
		viewer->injectedForRoot = 0;
		injectRootMessageAndReverse(viewer);
		return true;
	}
	const auto around = [&] {
		if (viewer->around != ShowAtUnreadMsgId) {
			return viewer->around;
		} else if (const auto item = lookupRoot()) {
			return item->computeRepliesInboxReadTillFull();
		}
		return viewer->around;
	}();
	if (_list.empty()
		|| (!around && _skippedAfter != 0)
		|| (around > _list.front() && _skippedAfter != 0)
		|| (around > 0 && around < _list.back() && _skippedBefore != 0)) {
		loadAround(around);
		return false;
	}
	const auto i = around
		? ranges::lower_bound(_list, around, std::greater<>())
		: end(_list);
	const auto availableBefore = int(end(_list) - i);
	const auto availableAfter = int(i - begin(_list));
	const auto useBefore = std::min(availableBefore, viewer->limitBefore + 1);
	const auto useAfter = std::min(availableAfter, viewer->limitAfter);
	const auto slice = &viewer->slice;
	if (_skippedBefore.has_value()) {
		slice->skippedBefore
			= (*_skippedBefore + (availableBefore - useBefore));
	}
	if (_skippedAfter.has_value()) {
		slice->skippedAfter
			= (*_skippedAfter + (availableAfter - useAfter));
	}

	const auto channelId = _history->channelId();
	slice->ids.clear();
	auto nearestToAround = std::optional<MsgId>();
	slice->ids.reserve(useAfter + useBefore);
	for (auto j = i - useAfter, e = i + useBefore; j != e; ++j) {
		if (!nearestToAround && *j < around) {
			nearestToAround = (j == i - useAfter)
				? *j
				: *(j - 1);
		}
		slice->ids.emplace_back(channelId, *j);
	}
	slice->nearestToAround = FullMsgId(
		channelId,
		nearestToAround.value_or(
			slice->ids.empty() ? 0 : slice->ids.back().msg));
	slice->fullCount = _fullCount.current();

	injectRootMessageAndReverse(viewer);

	if (_skippedBefore != 0 && useBefore < viewer->limitBefore + 1) {
		loadBefore();
	}
	if (_skippedAfter != 0 && useAfter < viewer->limitAfter) {
		loadAfter();
	}

	return true;
}

bool RepliesList::applyUpdate(
		not_null<Viewer*> viewer,
		const MessageUpdate &update) {
	if (update.item->history() != _history || !update.item->isRegular()) {
		return false;
	}
	if (update.flags & MessageUpdate::Flag::Destroyed) {
		const auto id = update.item->fullId();
		for (auto i = 0; i != viewer->injectedForRoot; ++i) {
			if (viewer->slice.ids[i] == id) {
				return true;
			}
		}
	}
	if (update.item->replyToTop() != _rootId) {
		return false;
	}
	const auto id = update.item->id;
	const auto i = ranges::lower_bound(_list, id, std::greater<>());
	if (update.flags & MessageUpdate::Flag::Destroyed) {
		if (i == end(_list) || *i != id) {
			return false;
		}
		_list.erase(i);
		if (_skippedBefore && _skippedAfter) {
			_fullCount = *_skippedBefore + _list.size() + *_skippedAfter;
		} else if (const auto known = _fullCount.current()) {
			if (*known > 0) {
				_fullCount = (*known - 1);
			}
		}
	} else if (_skippedAfter != 0) {
		return false;
	} else {
		if (i != end(_list) && *i == id) {
			return false;
		}
		_list.insert(i, id);
		if (_skippedBefore && _skippedAfter) {
			_fullCount = *_skippedBefore + _list.size() + *_skippedAfter;
		} else if (const auto known = _fullCount.current()) {
			_fullCount = *known + 1;
		}
	}
	return true;
}

Histories &RepliesList::histories() {
	return _history->owner().histories();
}

HistoryItem *RepliesList::lookupRoot() {
	return _history->owner().message(_history->channelId(), _rootId);
}

void RepliesList::loadAround(MsgId id) {
	if (_loadingAround && *_loadingAround == id) {
		return;
	}
	histories().cancelRequest(base::take(_beforeId));
	histories().cancelRequest(base::take(_afterId));

	const auto send = [=](Fn<void()> finish) {
		return _history->session().api().request(MTPmessages_GetReplies(
			_history->peer->input,
			MTP_int(_rootId),
			MTP_int(id), // offset_id
			MTP_int(0), // offset_date
			MTP_int(id ? (-kMessagesPerPage / 2) : 0), // add_offset
			MTP_int(kMessagesPerPage), // limit
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_long(0) // hash
		)).done([=](const MTPmessages_Messages &result) {
			_beforeId = 0;
			_loadingAround = std::nullopt;
			finish();

			if (!id) {
				_skippedAfter = 0;
			} else {
				_skippedAfter = std::nullopt;
			}
			_skippedBefore = std::nullopt;
			_list.clear();
			if (processMessagesIsEmpty(result)) {
				_fullCount = _skippedBefore = _skippedAfter = 0;
			} else if (id) {
				Assert(!_list.empty());
				if (_list.front() <= id) {
					_skippedAfter = 0;
				} else if (_list.back() >= id) {
					_skippedBefore = 0;
				}
			}
		}).fail([=] {
			_beforeId = 0;
			_loadingAround = std::nullopt;
			finish();
		}).send();
	};
	_loadingAround = id;
	_beforeId = histories().sendRequest(
		_history,
		Histories::RequestType::History,
		send);
}

void RepliesList::loadBefore() {
	Expects(!_list.empty());

	if (_loadingAround) {
		histories().cancelRequest(base::take(_beforeId));
	} else if (_beforeId) {
		return;
	}

	const auto last = _list.back();
	const auto send = [=](Fn<void()> finish) {
		return _history->session().api().request(MTPmessages_GetReplies(
			_history->peer->input,
			MTP_int(_rootId),
			MTP_int(last), // offset_id
			MTP_int(0), // offset_date
			MTP_int(0), // add_offset
			MTP_int(kMessagesPerPage), // limit
			MTP_int(0), // min_id
			MTP_int(0), // max_id
			MTP_long(0) // hash
		)).done([=](const MTPmessages_Messages &result) {
			_beforeId = 0;
			finish();

			if (_list.empty()) {
				return;
			} else if (_list.back() != last) {
				loadBefore();
			} else if (processMessagesIsEmpty(result)) {
				_skippedBefore = 0;
				if (_skippedAfter == 0) {
					_fullCount = _list.size();
				}
			}
		}).fail([=] {
			_beforeId = 0;
			finish();
		}).send();
	};
	_beforeId = histories().sendRequest(
		_history,
		Histories::RequestType::History,
		send);
}

void RepliesList::loadAfter() {
	Expects(!_list.empty());

	if (_afterId) {
		return;
	}

	const auto first = _list.front();
	const auto send = [=](Fn<void()> finish) {
		return _history->session().api().request(MTPmessages_GetReplies(
			_history->peer->input,
			MTP_int(_rootId),
			MTP_int(first + 1), // offset_id
			MTP_int(0), // offset_date
			MTP_int(-kMessagesPerPage), // add_offset
			MTP_int(kMessagesPerPage), // limit
			MTP_int(0), // min_id
			MTP_int(0), // max_id
			MTP_long(0) // hash
		)).done([=](const MTPmessages_Messages &result) {
			_afterId = 0;
			finish();

			if (_list.empty()) {
				return;
			} else if (_list.front() != first) {
				loadAfter();
			} else if (processMessagesIsEmpty(result)) {
				_skippedAfter = 0;
				if (_skippedBefore == 0) {
					_fullCount = _list.size();
				}
			}
		}).fail([=] {
			_afterId = 0;
			finish();
		}).send();
	};
	_afterId = histories().sendRequest(
		_history,
		Histories::RequestType::History,
		send);
}

bool RepliesList::processMessagesIsEmpty(const MTPmessages_Messages &result) {
	const auto guard = gsl::finally([&] { _partLoaded.fire({}); });

	const auto fullCount = result.match([&](
			const MTPDmessages_messagesNotModified &) {
		LOG(("API Error: received messages.messagesNotModified! "
			"(HistoryWidget::messagesReceived)"));
		return 0;
	}, [&](const MTPDmessages_messages &data) {
		return int(data.vmessages().v.size());
	}, [&](const MTPDmessages_messagesSlice &data) {
		return data.vcount().v;
	}, [&](const MTPDmessages_channelMessages &data) {
		if (_history->peer->isChannel()) {
			_history->peer->asChannel()->ptsReceived(data.vpts().v);
		} else {
			LOG(("API Error: received messages.channelMessages when "
				"no channel was passed! (HistoryWidget::messagesReceived)"));
		}
		return data.vcount().v;
	});

	auto &owner = _history->owner();
	const auto list = result.match([&](
			const MTPDmessages_messagesNotModified &) {
		LOG(("API Error: received messages.messagesNotModified! "
			"(HistoryWidget::messagesReceived)"));
		return QVector<MTPMessage>();
	}, [&](const auto &data) {
		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());
		return data.vmessages().v;
	});
	if (list.isEmpty()) {
		return true;
	}

	const auto maxId = IdFromMessage(list.front());
	const auto wasSize = int(_list.size());
	const auto toFront = (wasSize > 0) && (maxId > _list.front());
	const auto localFlags = MessageFlags();
	const auto type = NewMessageType::Existing;
	auto refreshed = std::vector<MsgId>();
	if (toFront) {
		refreshed.reserve(_list.size() + list.size());
	}
	auto skipped = 0;
	for (const auto &message : list) {
		if (const auto item = owner.addNewMessage(message, localFlags, type)) {
			if (item->replyToTop() == _rootId) {
				if (toFront) {
					refreshed.push_back(item->id);
				} else {
					_list.push_back(item->id);
				}
			} else {
				++skipped;
			}
		} else {
			++skipped;
		}
	}
	if (toFront) {
		refreshed.insert(refreshed.end(), _list.begin(), _list.end());
		_list = std::move(refreshed);
	}

	const auto nowSize = int(_list.size());
	auto &decrementFrom = toFront ? _skippedAfter : _skippedBefore;
	if (decrementFrom.has_value()) {
		*decrementFrom = std::max(
			*decrementFrom - (nowSize - wasSize),
			0);
	}

	const auto checkedCount = std::max(fullCount - skipped, nowSize);
	if (_skippedBefore && _skippedAfter) {
		auto &correct = toFront ? _skippedBefore : _skippedAfter;
		*correct = std::max(
			checkedCount - *decrementFrom - nowSize,
			0);
		*decrementFrom = checkedCount - *correct - nowSize;
		Assert(*decrementFrom >= 0);
	} else if (_skippedBefore) {
		*_skippedBefore = std::min(*_skippedBefore, checkedCount - nowSize);
		_skippedAfter = checkedCount - *_skippedBefore - nowSize;
	} else if (_skippedAfter) {
		*_skippedAfter = std::min(*_skippedAfter, checkedCount - nowSize);
		_skippedBefore = checkedCount - *_skippedAfter - nowSize;
	}
	_fullCount = checkedCount;

	if (const auto item = lookupRoot()) {
		if (_skippedAfter == 0 && !_list.empty()) {
			item->setRepliesMaxId(_list.front());
		} else {
			item->setRepliesPossibleMaxId(maxId);
		}
		if (const auto original = item->lookupDiscussionPostOriginal()) {
			if (_skippedAfter == 0 && !_list.empty()) {
				original->setRepliesMaxId(_list.front());
			} else {
				original->setRepliesPossibleMaxId(maxId);
			}
		}
	}

	Ensures(list.size() >= skipped);
	return (list.size() == skipped);
}

} // namespace Data
