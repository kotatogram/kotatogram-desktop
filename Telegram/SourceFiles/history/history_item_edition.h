/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_item_reply_markup.h"

namespace Main {
class Session;
} // namespace Main

struct HistoryMessageEdition {
	explicit HistoryMessageEdition() = default;
	HistoryMessageEdition(
		not_null<Main::Session*> session,
		const MTPDmessage &message);

	bool isEditHide = false;
	int editDate = 0;
	int views = -1;
	int forwards = -1;
	int ttl = 0;
	bool useSameReplies = false;
	bool useSameMarkup = false;
	TextWithEntities textWithEntities;
	HistoryMessageMarkupData replyMarkup;
	HistoryMessageRepliesData replies;
	const MTPMessageMedia *mtpMedia = nullptr;
};
