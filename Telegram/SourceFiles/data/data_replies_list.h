/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

class History;
class HistoryService;

namespace Data {

class Histories;
struct MessagePosition;
struct MessagesSlice;
struct MessageUpdate;

class RepliesList final : public base::has_weak_ptr {
public:
	RepliesList(not_null<History*> history, MsgId rootId);
	~RepliesList();

	[[nodiscard]] rpl::producer<MessagesSlice> source(
		MessagePosition aroundId,
		int limitBefore,
		int limitAfter);

	[[nodiscard]] rpl::producer<int> fullCount() const;

	[[nodiscard]] std::optional<int> fullUnreadCountAfter(
		MsgId readTillId,
		MsgId wasReadTillId,
		std::optional<int> wasUnreadCountAfter) const;

private:
	struct Viewer;

	HistoryItem *lookupRoot();
	[[nodiscard]] Histories &histories();

	[[nodiscard]] rpl::producer<MessagesSlice> sourceFromServer(
		MessagePosition aroundId,
		int limitBefore,
		int limitAfter);
	void appendClientSideMessages(MessagesSlice &slice);

	[[nodiscard]] bool buildFromData(not_null<Viewer*> viewer);
	[[nodiscard]] bool applyUpdate(
		not_null<Viewer*> viewer,
		const MessageUpdate &update);
	void injectRootMessageAndReverse(not_null<Viewer*> viewer);
	void injectRootMessage(not_null<Viewer*> viewer);
	void injectRootDivider(
		not_null<HistoryItem*> root,
		not_null<MessagesSlice*> slice);
	bool processMessagesIsEmpty(const MTPmessages_Messages &result);
	void loadAround(MsgId id);
	void loadBefore();
	void loadAfter();

	const not_null<History*> _history;
	const MsgId _rootId = 0;
	std::vector<MsgId> _list;
	std::optional<int> _skippedBefore;
	std::optional<int> _skippedAfter;
	rpl::variable<std::optional<int>> _fullCount;
	rpl::event_stream<> _partLoaded;
	std::optional<MsgId> _loadingAround;
	HistoryService *_divider = nullptr;
	bool _dividerWithComments = false;
	int _beforeId = 0;
	int _afterId = 0;

};

} // namespace Data
