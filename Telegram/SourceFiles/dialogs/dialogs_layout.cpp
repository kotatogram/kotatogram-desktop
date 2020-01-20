/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_layout.h"

#include "data/data_abstract_structure.h"
#include "data/data_drafts.h"
#include "data/data_session.h"
#include "dialogs/dialogs_list.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"
#include "storage/localstorage.h"
#include "ui/empty_userpic.h"
#include "ui/text_options.h"
#include "ui/unread_badge.h"
#include "lang/lang_keys.h"
#include "support/support_helper.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_folder.h"
#include "data/data_peer_values.h"
#include "app.h"

namespace Dialogs {
namespace Layout {
namespace {

// Show all dates that are in the last 20 hours in time format.
constexpr int kRecentlyInSeconds = 20 * 3600;

bool ShowUserBotIcon(not_null<UserData*> user) {
	return user->isBot() && !user->isSupport();
}

void PaintRowTopRight(Painter &p, const QString &text, QRect &rectForName, bool active, bool selected) {
	const auto width = st::dialogsDateFont->width(text);
	rectForName.setWidth(rectForName.width() - width - st::dialogsDateSkip);
	p.setFont(st::dialogsDateFont);
	p.setPen(active ? st::dialogsDateFgActive : (selected ? st::dialogsDateFgOver : st::dialogsDateFg));
	p.drawText(rectForName.left() + rectForName.width() + st::dialogsDateSkip, rectForName.top() + st::msgNameFont->height - st::msgDateFont->descent, text);
}

void PaintRowDate(Painter &p, QDateTime date, QRect &rectForName, bool active, bool selected) {
	const auto now = QDateTime::currentDateTime();
	const auto &lastTime = date;
	const auto nowDate = now.date();
	const auto lastDate = lastTime.date();

	const auto dt = [&] {
		const auto wasSameDay = (lastDate == nowDate);
		const auto wasRecently = qAbs(lastTime.secsTo(now)) < kRecentlyInSeconds;
		if (wasSameDay || wasRecently) {
			return lastTime.toString(cTimeFormat());
		} else if (lastDate.year() == nowDate.year()
			&& lastDate.weekNumber() == nowDate.weekNumber()) {
			return langDayOfWeek(lastDate);
		} else {
			return lastDate.toString(qsl("d.MM.yy"));
		}
	}();
	PaintRowTopRight(p, dt, rectForName, active, selected);
}

void PaintNarrowCounter(
		Painter &p,
		bool displayUnreadCounter,
		bool displayUnreadMark,
		bool displayMentionBadge,
		int unreadCount,
		bool active,
		bool unreadMuted,
		bool mentionMuted,
		int lines = 2) {
	auto skipBeforeMention = 0;
	if (displayUnreadCounter || displayUnreadMark) {
		auto counter = (unreadCount > 0)
			? QString::number(unreadCount)
			: QString();
		const auto allowDigits = displayMentionBadge ? 1 : 3;
		auto unreadRight = st::dialogsPadding.x() + (DialogListLines() == 1 ? st::dialogsUnreadHeight : st::dialogsPhotoSize);
		auto unreadTop = (lines == 1
			? st::dialogsPadding.y()
			: st::dialogsPadding.y() + st::dialogsPhotoSize - st::dialogsUnreadHeight);
		auto unreadWidth = 0;

		UnreadBadgeStyle st;
		st.active = active;
		st.muted = unreadMuted;
		paintUnreadCount(p, counter, unreadRight, unreadTop, st, &unreadWidth, allowDigits);
		skipBeforeMention += unreadWidth + st.padding;
	}
	if (displayMentionBadge) {
		auto counter = qsl("@");
		auto unreadRight = st::dialogsPadding.x() + (DialogListLines() == 1 ? st::dialogsUnreadHeight : st::dialogsPhotoSize) - skipBeforeMention;
		auto unreadTop = (lines == 1
			? st::dialogsPadding.y()
			: st::dialogsPadding.y() + st::dialogsPhotoSize - st::dialogsUnreadHeight);
		auto unreadWidth = 0;

		UnreadBadgeStyle st;
		st.active = active;
		st.muted = mentionMuted;
		st.padding = 0;
		st.textTop = 0;
		paintUnreadCount(p, counter, unreadRight, unreadTop, st, &unreadWidth);
	}
}

int PaintWideCounter(
		Painter &p,
		int texttop,
		int availableWidth,
		int fullWidth,
		bool displayUnreadCounter,
		bool displayUnreadMark,
		bool displayMentionBadge,
		bool displayPinnedIcon,
		int unreadCount,
		bool active,
		bool selected,
		bool unreadMuted,
		bool mentionMuted) {
	const auto initial = availableWidth;
	auto hadOneBadge = false;
	if (displayUnreadCounter || displayUnreadMark) {
		auto counter = (unreadCount > 0)
			? QString::number(unreadCount)
			: QString();
		auto unreadRight = fullWidth - st::dialogsPadding.x();
		auto unreadTop = texttop + st::dialogsTextFont->ascent - st::dialogsUnreadFont->ascent - (st::dialogsUnreadHeight - st::dialogsUnreadFont->height) / 2;
		auto unreadWidth = 0;

		UnreadBadgeStyle st;
		st.active = active;
		st.muted = unreadMuted;
		paintUnreadCount(p, counter, unreadRight, unreadTop, st, &unreadWidth);
		availableWidth -= unreadWidth + st.padding;

		hadOneBadge = true;
	} else if (displayPinnedIcon) {
		auto &icon = (active ? st::dialogsPinnedIconActive : (selected ? st::dialogsPinnedIconOver : st::dialogsPinnedIcon));
		icon.paint(p, fullWidth - st::dialogsPadding.x() - icon.width(), texttop, fullWidth);
		availableWidth -= icon.width() + st::dialogsUnreadPadding;

		hadOneBadge = true;
	}
	if (displayMentionBadge) {
		auto counter = qsl("@");
		auto unreadRight = fullWidth - st::dialogsPadding.x() - (initial - availableWidth);
		auto unreadTop = texttop + st::dialogsTextFont->ascent - st::dialogsUnreadFont->ascent - (st::dialogsUnreadHeight - st::dialogsUnreadFont->height) / 2;
		auto unreadWidth = 0;

		UnreadBadgeStyle st;
		st.active = active;
		st.muted = mentionMuted;
		st.padding = 0;
		st.textTop = 0;
		paintUnreadCount(p, counter, unreadRight, unreadTop, st, &unreadWidth);
		availableWidth -= unreadWidth + st.padding + (hadOneBadge ? st::dialogsUnreadPadding : 0);
	}
	return availableWidth;
}

void PaintListEntryText(
		Painter &p,
		QRect rect,
		bool active,
		bool selected,
		not_null<const Row*> row) {
	if (rect.isEmpty()) {
		return;
	}
	row->validateListEntryCache();
	const auto &palette = row->folder()
		? (active
			? st::dialogsTextPaletteArchiveActive
			: selected
			? st::dialogsTextPaletteArchiveOver
			: st::dialogsTextPaletteArchive)
		: (active
			? st::dialogsTextPaletteActive
			: selected
			? st::dialogsTextPaletteOver
			: st::dialogsTextPalette);
	const auto &color = active
		? st::dialogsTextFgActive
		: selected
		? st::dialogsTextFgOver
		: st::dialogsTextFg;
	p.setTextPalette(palette);
	p.setFont(st::dialogsTextFont);
	p.setPen(color);
	row->listEntryCache().drawElided(
		p,
		rect.left(),
		rect.top(),
		rect.width(),
		rect.height() / st::dialogsTextFont->height);
	p.restoreTextPalette();
}

enum class Flag {
	Active           = 0x01,
	Selected         = 0x02,
	SearchResult     = 0x04,
	SavedMessages    = 0x08,
	AllowUserOnline  = 0x10,
	//FeedSearchResult = 0x10, // #feed
};
inline constexpr bool is_flag_type(Flag) { return true; }

template <typename PaintItemCallback, typename PaintCounterCallback>
void paintOneLineRow(
		Painter &p,
		not_null<const BasicRow*> row,
		not_null<Entry*> entry,
		Dialogs::Key chat,
		PeerData *from,
		const HiddenSenderInfo *hiddenSenderInfo,
		HistoryItem *item,
		const Data::Draft *draft,
		QDateTime date,
		int fullWidth,
		base::flags<Flag> flags,
		crl::time ms,
		PaintItemCallback &&paintItemCallback,
		PaintCounterCallback &&paintCounterCallback) {
	const auto supportMode = Auth().supportMode();
	if (supportMode) {
		draft = nullptr;
	}

	auto active = (flags & Flag::Active);
	auto selected = (flags & Flag::Selected);
	auto fullRect = QRect(0, 0, fullWidth, st::dialogsImportantBarHeight);
	auto bg = active
		? st::dialogsBgActive
		: (selected
			? st::dialogsBgOver
			: st::dialogsBg);
	auto ripple = active
		? st::dialogsRippleBgActive
		: st::dialogsRippleBg;
	p.fillRect(fullRect, bg);
	row->paintRipple(p, 0, 0, fullWidth, &ripple->c);

	if (flags & Flag::SavedMessages) {
		Ui::EmptyUserpic::PaintSavedMessages(
			p,
			st::dialogsPadding.x(),
			st::dialogsPadding.y(),
			fullWidth,
			st::dialogsUnreadHeight);
	} else if (from) {
		row->paintUserpic(
			p,
			from,
			(flags & Flag::AllowUserOnline),
			active,
			fullWidth);
	} else if (hiddenSenderInfo) {
		hiddenSenderInfo->userpic.paint(
			p,
			st::dialogsPadding.x(),
			st::dialogsPadding.y(),
			fullWidth,
			st::dialogsUnreadHeight);
	} else {
		entry->paintUserpicLeft(
			p,
			st::dialogsPadding.x(),
			st::dialogsPadding.y(),
			fullWidth,
			st::dialogsUnreadHeight);
	}

	auto nameleft = st::dialogsPadding.x() * 2 + st::dialogsUnreadHeight;
	if (fullWidth <= nameleft) {
		if (!draft && item && !item->isEmpty()) {
			paintCounterCallback();
		}
		return;
	}

	const auto history = chat.history();
	auto namewidth = fullWidth - nameleft - st::dialogsPadding.x();
	auto rectForName = QRect(
		nameleft,
		st::dialogsPadding.y(),
		namewidth,
		st::dialogsTextFont->height);

	const auto promoted = (history && history->useProxyPromotion())
		&& !(flags & (Flag::SearchResult/* | Flag::FeedSearchResult*/)); // #feed
	if (promoted) {
		const auto text = tr::lng_proxy_sponsor(tr::now);
		PaintRowTopRight(p, text, rectForName, active, selected);
	} else if (from && !(flags & Flag::SearchResult/* | Flag::FeedSearchResult*/)) {  // #feed
		if (const auto chatTypeIcon = ChatTypeIcon(from, active, selected)) {
			chatTypeIcon->paint(p, rectForName.topLeft(), fullWidth);
			rectForName.setLeft(rectForName.left() + st::dialogsChatTypeSkip);
		}
	//} else if (const auto feed = chat.feed()) { // #feed
	//	if (const auto feedTypeIcon = FeedTypeIcon(feed, active, selected)) {
	//		feedTypeIcon->paint(p, rectForName.topLeft(), fullWidth);
	//		rectForName.setLeft(rectForName.left() + st::dialogsChatTypeSkip);
	//	}
	}
	if (!draft
		&& !(supportMode
			&& Auth().supportHelper().isOccupiedBySomeone(history))
		&& item
		&& !item->isEmpty()) {
		const auto nameWithoutCounterWidth = paintItemCallback(nameleft, (flags & Flag::SearchResult ? namewidth : rectForName.width()));
		rectForName.setWidth(nameWithoutCounterWidth - st::dialogsPadding.x());
	} else if (entry->isPinnedDialog() && !entry->fixedOnTopIndex()) {
		auto &icon = (active ? st::dialogsPinnedIconActive : (selected ? st::dialogsPinnedIconOver : st::dialogsPinnedIcon));
		icon.paint(p, fullWidth - st::dialogsPadding.x() - icon.width(), st::dialogsPadding.y(), fullWidth);
		rectForName.setWidth(rectForName.width() - icon.width() - st::dialogsUnreadPadding);
	}

	QString text;

	if (flags & Flag::SavedMessages) {
		text = tr::lng_saved_messages(tr::now);
		p.setPen(active
			? st::dialogsNameFgActive
			: selected
			? st::dialogsNameFgOver
			: st::dialogsNameFg);
	} else if (from) {
		if (!(flags & Flag::SearchResult)) {
			const auto badgeStyle = Ui::PeerBadgeStyle{
				(active
					? &st::dialogsVerifiedIconActive
					: selected
					? &st::dialogsVerifiedIconOver
					: &st::dialogsVerifiedIcon),
				(active
					? &st::dialogsScamFgActive
					: selected
					? &st::dialogsScamFgOver
					: &st::dialogsScamFg) };
			const auto badgeWidth = Ui::DrawPeerBadgeGetWidth(
				from,
				p,
				rectForName,
				from->nameText().maxWidth(),
				fullWidth,
				badgeStyle);
			rectForName.setWidth(rectForName.width() - badgeWidth);

			text = from->nameText().toString();
			p.setPen(active
				? st::dialogsNameFgActive
				: selected
				? st::dialogsNameFgOver
				: st::dialogsNameFg);
			p.setFont(st::dialogsTextFont);
		}
	} else if (hiddenSenderInfo) {
		text = hiddenSenderInfo->nameText.toString();
	} else {
		text = entry->chatListName();
		const auto nameFg = active
			? st::dialogsNameFgActive
			: (selected
				? st::dialogsArchiveFgOver
				: st::dialogsArchiveFg);
		p.setPen(nameFg);
	}

	if (!(from && (flags & Flag::SearchResult))) {
		Ui::Text::String textStr = { st::dialogsTextStyle, text };
		textStr.drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
	}
}

template <typename PaintItemCallback, typename PaintCounterCallback>
void paintRow(
		Painter &p,
		not_null<const BasicRow*> row,
		not_null<Entry*> entry,
		Dialogs::Key chat,
		PeerData *from,
		const HiddenSenderInfo *hiddenSenderInfo,
		HistoryItem *item,
		const Data::Draft *draft,
		QDateTime date,
		int fullWidth,
		base::flags<Flag> flags,
		crl::time ms,
		PaintItemCallback &&paintItemCallback,
		PaintCounterCallback &&paintCounterCallback) {
	const auto supportMode = Auth().supportMode();
	if (supportMode) {
		draft = nullptr;
	}

	auto active = (flags & Flag::Active);
	auto selected = (flags & Flag::Selected);
	auto fullRect = QRect(0, 0, fullWidth, st::dialogsRowHeight);
	auto bg = active
		? st::dialogsBgActive
		: (selected
			? st::dialogsBgOver
			: st::dialogsBg);
	auto ripple = active
		? st::dialogsRippleBgActive
		: st::dialogsRippleBg;
	p.fillRect(fullRect, bg);
	row->paintRipple(p, 0, 0, fullWidth, &ripple->c);

	if (flags & Flag::SavedMessages) {
		Ui::EmptyUserpic::PaintSavedMessages(
			p,
			st::dialogsPadding.x(),
			st::dialogsPadding.y(),
			fullWidth,
			st::dialogsPhotoSize);
	} else if (from) {
		row->paintUserpic(
			p,
			from,
			(flags & Flag::AllowUserOnline),
			active,
			fullWidth);
	} else if (hiddenSenderInfo) {
		hiddenSenderInfo->userpic.paint(
			p,
			st::dialogsPadding.x(),
			st::dialogsPadding.y(),
			fullWidth,
			st::dialogsPhotoSize);
	} else {
		entry->paintUserpicLeft(
			p,
			st::dialogsPadding.x(),
			st::dialogsPadding.y(),
			fullWidth,
			st::dialogsPhotoSize);
	}

	auto nameleft = st::dialogsPadding.x()
		+ st::dialogsPhotoSize
		+ st::dialogsPhotoPadding;
	if (fullWidth <= nameleft) {
		if (!draft && item && !item->isEmpty()) {
			paintCounterCallback();
		}
		return;
	}

	const auto history = chat.history();
	auto namewidth = fullWidth - nameleft - st::dialogsPadding.x();
	auto rectForName = QRect(
		nameleft,
		st::dialogsPadding.y() + st::dialogsNameTop,
		namewidth,
		st::msgNameFont->height);

	const auto promoted = (history && history->useProxyPromotion())
		&& !(flags & (Flag::SearchResult/* | Flag::FeedSearchResult*/)); // #feed
	if (promoted) {
		const auto text = tr::lng_proxy_sponsor(tr::now);
		PaintRowTopRight(p, text, rectForName, active, selected);
	} else if (from/* && !(flags & Flag::FeedSearchResult)*/) { // #feed
		if (const auto chatTypeIcon = ChatTypeIcon(from, active, selected)) {
			chatTypeIcon->paint(p, rectForName.topLeft(), fullWidth);
			rectForName.setLeft(rectForName.left() + st::dialogsChatTypeSkip);
		}
	//} else if (const auto feed = chat.feed()) { // #feed
	//	if (const auto feedTypeIcon = FeedTypeIcon(feed, active, selected)) {
	//		feedTypeIcon->paint(p, rectForName.topLeft(), fullWidth);
	//		rectForName.setLeft(rectForName.left() + st::dialogsChatTypeSkip);
	//	}
	}
	auto texttop = st::dialogsPadding.y()
		+ st::msgNameFont->height
		+ st::dialogsSkip;
	if (draft
		|| (supportMode
			&& Auth().supportHelper().isOccupiedBySomeone(history))) {
		if (!promoted) {
			PaintRowDate(p, date, rectForName, active, selected);
		}

		auto availableWidth = namewidth;
		if (entry->isPinnedDialog() && !entry->fixedOnTopIndex()) {
			auto &icon = (active ? st::dialogsPinnedIconActive : (selected ? st::dialogsPinnedIconOver : st::dialogsPinnedIcon));
			icon.paint(p, fullWidth - st::dialogsPadding.x() - icon.width(), texttop, fullWidth);
			availableWidth -= icon.width() + st::dialogsUnreadPadding;
		}

		p.setFont(st::dialogsTextFont);
		auto &color = active ? st::dialogsTextFgServiceActive : (selected ? st::dialogsTextFgServiceOver : st::dialogsTextFgService);
		if (history && !history->paintSendAction(p, nameleft, texttop, availableWidth, fullWidth, color, ms)) {
			if (history->cloudDraftTextCache.isEmpty()) {
				auto draftWrapped = textcmdLink(1, tr::lng_dialogs_text_from_wrapped(tr::now, lt_from, tr::lng_from_draft(tr::now)));
				auto draftText = supportMode
					? textcmdLink(1, Support::ChatOccupiedString(history))
					: tr::lng_dialogs_text_with_from(tr::now, lt_from_part, draftWrapped, lt_message, TextUtilities::Clean(draft->textWithTags.text));
				history->cloudDraftTextCache.setText(st::dialogsTextStyle, draftText, Ui::DialogTextOptions());
			}
			p.setPen(active ? st::dialogsTextFgActive : (selected ? st::dialogsTextFgOver : st::dialogsTextFg));
			if (supportMode) {
				p.setTextPalette(active ? st::dialogsTextPaletteTakenActive : (selected ? st::dialogsTextPaletteTakenOver : st::dialogsTextPaletteTaken));
			} else {
				p.setTextPalette(active ? st::dialogsTextPaletteDraftActive : (selected ? st::dialogsTextPaletteDraftOver : st::dialogsTextPaletteDraft));
			}
			history->cloudDraftTextCache.drawElided(p, nameleft, texttop, availableWidth, 1);
			p.restoreTextPalette();
		}
	} else if (!item) {
		auto availableWidth = namewidth;
		if (entry->isPinnedDialog() && !entry->fixedOnTopIndex()) {
			auto &icon = (active ? st::dialogsPinnedIconActive : (selected ? st::dialogsPinnedIconOver : st::dialogsPinnedIcon));
			icon.paint(p, fullWidth - st::dialogsPadding.x() - icon.width(), texttop, fullWidth);
			availableWidth -= icon.width() + st::dialogsUnreadPadding;
		}

		auto &color = active ? st::dialogsTextFgServiceActive : (selected ? st::dialogsTextFgServiceOver : st::dialogsTextFgService);
		p.setFont(st::dialogsTextFont);
		if (history && !history->paintSendAction(p, nameleft, texttop, availableWidth, fullWidth, color, ms)) {
			// Empty history
		}
	} else if (!item->isEmpty()) {
		if (history && !promoted) {
			PaintRowDate(p, date, rectForName, active, selected);
		}

		paintItemCallback(nameleft, namewidth);
	} else if (entry->isPinnedDialog() && !entry->fixedOnTopIndex()) {
		auto availableWidth = namewidth;
		auto &icon = (active ? st::dialogsPinnedIconActive : (selected ? st::dialogsPinnedIconOver : st::dialogsPinnedIcon));
		icon.paint(p, fullWidth - st::dialogsPadding.x() - icon.width(), texttop, fullWidth);
		availableWidth -= icon.width() + st::dialogsUnreadPadding;
	}
	auto sendStateIcon = [&]() -> const style::icon* {
		if (draft) {
			if (draft->saveRequestId) {
				return &(active
					? st::dialogsSendingIconActive
					: (selected
						? st::dialogsSendingIconOver
						: st::dialogsSendingIcon));
			}
		} else if (item && !item->isEmpty() && item->needCheck()) {
			if (item->id > 0) {
				if (item->unread()) {
					return &(active
						? st::dialogsSentIconActive
						: (selected
							? st::dialogsSentIconOver
							: st::dialogsSentIcon));
				}
				return &(active
					? st::dialogsReceivedIconActive
					: (selected
						? st::dialogsReceivedIconOver
						: st::dialogsReceivedIcon));
			}
			return &(active
				? st::dialogsSendingIconActive
				: (selected
					? st::dialogsSendingIconOver
					: st::dialogsSendingIcon));
		}
		return nullptr;
	}();
	if (sendStateIcon && history) {
		rectForName.setWidth(rectForName.width() - st::dialogsSendStateSkip);
		sendStateIcon->paint(p, rectForName.topLeft() + QPoint(rectForName.width(), 0), fullWidth);
	}

	if (flags & Flag::SavedMessages) {
		auto text = tr::lng_saved_messages(tr::now);
		const auto textWidth = st::msgNameFont->width(text);
		if (textWidth > rectForName.width()) {
			text = st::msgNameFont->elided(text, rectForName.width());
		}
		p.setFont(st::msgNameFont);
		p.setPen(active
			? st::dialogsNameFgActive
			: selected
			? st::dialogsNameFgOver
			: st::dialogsNameFg);
		p.drawTextLeft(rectForName.left(), rectForName.top(), fullWidth, text);
	} else if (from) {
		if (!(flags & Flag::SearchResult)) {
			const auto badgeStyle = Ui::PeerBadgeStyle{
				(active
					? &st::dialogsVerifiedIconActive
					: selected
					? &st::dialogsVerifiedIconOver
					: &st::dialogsVerifiedIcon),
				(active
					? &st::dialogsScamFgActive
					: selected
					? &st::dialogsScamFgOver
					: &st::dialogsScamFg) };
			const auto badgeWidth = Ui::DrawPeerBadgeGetWidth(
				from,
				p,
				rectForName,
				from->nameText().maxWidth(),
				fullWidth,
				badgeStyle);
			rectForName.setWidth(rectForName.width() - badgeWidth);
		}
		p.setPen(active
			? st::dialogsNameFgActive
			: selected
			? st::dialogsNameFgOver
			: st::dialogsNameFg);
		from->nameText().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
	} else if (hiddenSenderInfo) {
		hiddenSenderInfo->nameText.drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
	} else {
		const auto nameFg = active
			? st::dialogsNameFgActive
			: (selected
				? st::dialogsArchiveFgOver
				: st::dialogsArchiveFg);
		p.setPen(nameFg);
		p.setFont(st::msgNameFont);
		auto text = entry->chatListName(); // TODO feed name with emoji
		auto textWidth = st::msgNameFont->width(text);
		if (textWidth > rectForName.width()) {
			text = st::msgNameFont->elided(text, rectForName.width());
		}
		p.drawTextLeft(rectForName.left(), rectForName.top(), fullWidth, text);
	}
}

struct UnreadBadgeSizeData {
	QImage circle;
	QPixmap left[6], right[6];
};
class UnreadBadgeStyleData : public Data::AbstractStructure {
public:
	UnreadBadgeSizeData sizes[UnreadBadgeSizesCount];
	style::color bg[6] = {
		st::dialogsUnreadBg,
		st::dialogsUnreadBgOver,
		st::dialogsUnreadBgActive,
		st::dialogsUnreadBgMuted,
		st::dialogsUnreadBgMutedOver,
		st::dialogsUnreadBgMutedActive
	};
};
Data::GlobalStructurePointer<UnreadBadgeStyleData> unreadBadgeStyle;

void createCircleMask(UnreadBadgeSizeData *data, int size) {
	if (!data->circle.isNull()) return;

	data->circle = style::createCircleMask(size);
}

QImage colorizeCircleHalf(UnreadBadgeSizeData *data, int size, int half, int xoffset, style::color color) {
	auto result = style::colorizeImage(data->circle, color, QRect(xoffset, 0, half, size));
	result.setDevicePixelRatio(cRetinaFactor());
	return result;
}

} // namepsace

const style::icon *ChatTypeIcon(
		not_null<PeerData*> peer,
		bool active,
		bool selected) {
	if (peer->isChat() || peer->isMegagroup()) {
		return &(active
			? st::dialogsChatIconActive
			: (selected ? st::dialogsChatIconOver : st::dialogsChatIcon));
	} else if (peer->isChannel()) {
		return &(active
			? st::dialogsChannelIconActive
			: (selected
				? st::dialogsChannelIconOver
				: st::dialogsChannelIcon));
	} else if (const auto user = peer->asUser()) {
		if (user->isInaccessible()) {
			return &(active
				? st::dialogsDeletedIconActive
				: (selected
					? st::dialogsDeletedIconOver
					: st::dialogsDeletedIcon));
		} else if (ShowUserBotIcon(user)) {
			return &(active
				? st::dialogsBotIconActive
				: (selected
					? st::dialogsBotIconOver
					: st::dialogsBotIcon));
		}
	}
	return nullptr;
}

//const style::icon *FeedTypeIcon( // #feed
//		not_null<Data::Feed*> feed,
//		bool active,
//		bool selected) {
//	return &(active ? st::dialogsFeedIconActive
//		: (selected ? st::dialogsFeedIconOver : st::dialogsFeedIcon));
//}
//
void paintUnreadBadge(Painter &p, const QRect &rect, const UnreadBadgeStyle &st) {
	Assert(rect.height() == st.size);

	int index = (st.muted ? 0x03 : 0x00) + (st.active ? 0x02 : (st.selected ? 0x01 : 0x00));
	int size = st.size, sizehalf = size / 2;

	unreadBadgeStyle.createIfNull();
	auto badgeData = unreadBadgeStyle->sizes;
	if (st.sizeId > 0) {
		Assert(st.sizeId < UnreadBadgeSizesCount);
		badgeData = &unreadBadgeStyle->sizes[st.sizeId];
	}
	auto bg = unreadBadgeStyle->bg[index];
	if (badgeData->left[index].isNull()) {
		int imgsize = size * cIntRetinaFactor(), imgsizehalf = sizehalf * cIntRetinaFactor();
		createCircleMask(badgeData, size);
		badgeData->left[index] = App::pixmapFromImageInPlace(colorizeCircleHalf(badgeData, imgsize, imgsizehalf, 0, bg));
		badgeData->right[index] = App::pixmapFromImageInPlace(colorizeCircleHalf(badgeData, imgsize, imgsizehalf, imgsize - imgsizehalf, bg));
	}

	int bar = rect.width() - 2 * sizehalf;
	p.drawPixmap(rect.x(), rect.y(), badgeData->left[index]);
	if (bar) {
		p.fillRect(rect.x() + sizehalf, rect.y(), bar, rect.height(), bg);
	}
	p.drawPixmap(rect.x() + sizehalf + bar, rect.y(), badgeData->right[index]);
}

UnreadBadgeStyle::UnreadBadgeStyle()
: align(style::al_right)
, active(false)
, selected(false)
, muted(false)
, size(st::dialogsUnreadHeight)
, padding(st::dialogsUnreadPadding)
, sizeId(UnreadBadgeInDialogs)
, font(st::dialogsUnreadFont) {
}

void paintUnreadCount(
		Painter &p,
		const QString &unreadCount,
		int x,
		int y,
		const UnreadBadgeStyle &st,
		int *outUnreadWidth,
		int allowDigits) {
	const auto text = (allowDigits > 0) && (unreadCount.size() > allowDigits + 1)
		? qsl("..") + unreadCount.mid(unreadCount.size() - allowDigits)
		: unreadCount;

	int unreadWidth = st.font->width(text);
	int unreadRectWidth = unreadWidth + 2 * st.padding;
	int unreadRectHeight = st.size;
	accumulate_max(unreadRectWidth, unreadRectHeight);

	int unreadRectLeft = x;
	if ((st.align & Qt::AlignHorizontal_Mask) & style::al_center) {
		unreadRectLeft = (x - unreadRectWidth) / 2;
	} else if ((st.align & Qt::AlignHorizontal_Mask) & style::al_right) {
		unreadRectLeft = x - unreadRectWidth;
	}
	int unreadRectTop = y;
	if (outUnreadWidth) {
		*outUnreadWidth = unreadRectWidth;
	}

	paintUnreadBadge(p, QRect(unreadRectLeft, unreadRectTop, unreadRectWidth, unreadRectHeight), st);

	auto textTop = st.textTop ? st.textTop : (unreadRectHeight - st.font->height) / 2;
	p.setFont(st.font);
	p.setPen(st.active ? st::dialogsUnreadFgActive : (st.selected ? st::dialogsUnreadFgOver : st::dialogsUnreadFg));
	p.drawText(unreadRectLeft + (unreadRectWidth - unreadWidth) / 2, unreadRectTop + textTop + st.font->ascent, text);
}

void RowPainter::paint(
		Painter &p,
		not_null<const Row*> row,
		int fullWidth,
		bool active,
		bool selected,
		crl::time ms) {
	const auto entry = row->entry();
	const auto history = row->history();
	const auto peer = history ? history->peer.get() : nullptr;
	const auto unreadCount = entry->chatListUnreadCount();
	const auto unreadMark = entry->chatListUnreadMark();
	const auto unreadMuted = entry->chatListMutedBadge();
	const auto mentionMuted = (entry->folder() != nullptr);
	const auto item = entry->chatListMessage();
	const auto cloudDraft = [&]() -> const Data::Draft*{
		if (history && (!item || (!unreadCount && !unreadMark))) {
			// Draw item, if there are unread messages.
			if (const auto draft = history->cloudDraft()) {
				if (!Data::draftIsNull(draft)) {
					return draft;
				}
			}
		}
		return nullptr;
	}();
	const auto displayDate = [&] {
		if (item) {
			if (cloudDraft) {
				return (item->date() > cloudDraft->date)
					? ItemDateTime(item)
					: base::unixtime::parse(cloudDraft->date);
			}
			return ItemDateTime(item);
		}
		return cloudDraft
			? base::unixtime::parse(cloudDraft->date)
			: QDateTime();
	}();
	const auto displayMentionBadge = history
		? history->hasUnreadMentions()
		: false;
	const auto displayUnreadCounter = [&] {
		if (fullWidth < st::columnMinimalWidthLeft && DialogListLines() == 1) {
			return false;
		}

		if (displayMentionBadge
			&& unreadCount == 1
			&& item
			&& item->isUnreadMention()) {
			return false;
		}
		return (unreadCount > 0);
	}();
	const auto displayUnreadMark = !displayUnreadCounter
		&& !displayMentionBadge
		&& history
		&& unreadMark;
	const auto displayPinnedIcon = !displayUnreadCounter
		&& !displayMentionBadge
		&& !displayUnreadMark
		&& entry->isPinnedDialog()
		&& !entry->fixedOnTopIndex();

	const auto from = history
		? (history->peer->migrateTo()
			? history->peer->migrateTo()
			: history->peer.get())
		: nullptr;
	const auto allowUserOnline = (fullWidth >= st::columnMinimalWidthLeft)
		|| (!displayUnreadCounter && !displayUnreadMark);
	const auto flags = (active ? Flag::Active : Flag(0))
		| (selected ? Flag::Selected : Flag(0))
		| (allowUserOnline ? Flag::AllowUserOnline : Flag(0))
		| (peer && peer->isSelf() ? Flag::SavedMessages : Flag(0));
	const auto paintItemCallback = [&](int nameleft, int namewidth) {
		const auto texttop = (DialogListLines() == 1
			? st::dialogsPadding.y()
			: st::dialogsPadding.y()
				+ st::msgNameFont->height
				+ st::dialogsSkip);

		const auto availableWidth = PaintWideCounter(
			p,
			texttop,
			namewidth,
			fullWidth,
			displayUnreadCounter,
			displayUnreadMark,
			displayMentionBadge,
			displayPinnedIcon,
			unreadCount,
			active,
			selected,
			unreadMuted,
			mentionMuted);
		if (DialogListLines() > 1 || flags & Flag::SearchResult) {
			const auto &color = active
				? st::dialogsTextFgServiceActive
				: (selected
					? st::dialogsTextFgServiceOver
					: st::dialogsTextFgService);
			const auto itemRect = QRect(
				nameleft,
				texttop,
				availableWidth,
				st::dialogsTextFont->height);
			const auto actionWasPainted = history ? history->paintSendAction(
				p,
				itemRect.x(),
				itemRect.y(),
				itemRect.width(),
				fullWidth,
				color,
				ms) : false;
			if (const auto folder = row->folder()) {
				PaintListEntryText(p, itemRect, active, selected, row);
			} else if (!actionWasPainted) {
				item->drawInDialog(
					p,
					itemRect,
					active,
					selected,
					HistoryItem::DrawInDialog::Normal,
					entry->textCachedFor,
					entry->lastItemTextCache);
			}
		}

		return availableWidth;
	};
	const auto paintCounterCallback = [&] {
		PaintNarrowCounter(
			p,
			displayUnreadCounter,
			displayUnreadMark,
			displayMentionBadge,
			unreadCount,
			active,
			unreadMuted,
			mentionMuted,
			DialogListLines());
	};
	if (DialogListLines() == 1) {
		paintOneLineRow(
			p,
			row,
			entry,
			row->key(),
			from,
			nullptr,
			item,
			cloudDraft,
			displayDate,
			fullWidth,
			flags,
			ms,
			paintItemCallback,
			paintCounterCallback);
	} else {
		paintRow(
			p,
			row,
			entry,
			row->key(),
			from,
			nullptr,
			item,
			cloudDraft,
			displayDate,
			fullWidth,
			flags,
			ms,
			paintItemCallback,
			paintCounterCallback);
	}
}

void RowPainter::paint(
		Painter &p,
		not_null<const FakeRow*> row,
		int fullWidth,
		bool active,
		bool selected,
		crl::time ms,
		bool displayUnreadInfo) {
	auto item = row->item();
	auto history = item->history();
	auto cloudDraft = nullptr;
	const auto from = [&] {
		if (row->searchInChat()) {
			return item->displayFrom();
		}
		return history->peer->migrateTo()
			? history->peer->migrateTo()
			: history->peer.get();
	}();
	const auto hiddenSenderInfo = [&]() -> const HiddenSenderInfo* {
		if (const auto searchChat = row->searchInChat()) {
			if (const auto peer = searchChat.peer()) {
				if (peer->isSelf()) {
					return item->hiddenForwardedInfo();
				}
			}
		}
		return nullptr;
	}();
	const auto drawInDialogWay = [&] {
		if (const auto searchChat = row->searchInChat()) {
			if (const auto peer = searchChat.peer()) {
				if (!peer->isChannel() || peer->isMegagroup()) {
					return HistoryItem::DrawInDialog::WithoutSender;
				}
			}
		}
		return HistoryItem::DrawInDialog::Normal;
	}();

	const auto unreadCount = displayUnreadInfo
		? history->chatListUnreadCount()
		: 0;
	const auto unreadMark = displayUnreadInfo
		&& history->chatListUnreadMark();
	const auto unreadMuted = history->chatListMutedBadge();
	const auto mentionMuted = (history->folder() != nullptr);
	const auto displayMentionBadge = displayUnreadInfo
		&& history->hasUnreadMentions();
	const auto displayUnreadCounter = (unreadCount > 0);
	const auto displayUnreadMark = !displayUnreadCounter
		&& !displayMentionBadge
		&& unreadMark;
	const auto displayPinnedIcon = false;
	const auto showSavedMessages = history->peer->isSelf()
		&& !row->searchInChat();
	const auto flags = (active ? Flag::Active : Flag(0))
		| (selected ? Flag::Selected : Flag(0))
		| Flag::SearchResult
		| (showSavedMessages ? Flag::SavedMessages : Flag(0))/* // #feed
		| (row->searchInChat().feed() ? Flag::FeedSearchResult : Flag(0))*/;
	const auto paintItemCallback = [&](int nameleft, int namewidth) {
		const auto texttop = (DialogListLines() == 1
			? st::dialogsPadding.y()
			: st::dialogsPadding.y()
				+ st::msgNameFont->height
				+ st::dialogsSkip);
		const auto availableWidth = PaintWideCounter(
			p,
			texttop,
			namewidth,
			fullWidth,
			displayUnreadCounter,
			displayUnreadMark,
			displayMentionBadge,
			displayPinnedIcon,
			unreadCount,
			active,
			selected,
			unreadMuted,
			mentionMuted);

		if (DialogListLines() > 1 || flags & Flag::SearchResult) {
			const auto itemRect = QRect(
				nameleft,
				texttop,
				availableWidth,
				st::dialogsTextFont->height);
			item->drawInDialog(
				p,
				itemRect,
				active,
				selected,
				drawInDialogWay,
				row->_cacheFor,
				row->_cache);
		}

		return availableWidth;
	};
	const auto paintCounterCallback = [&] {
		PaintNarrowCounter(
			p,
			displayUnreadCounter,
			displayUnreadMark,
			displayMentionBadge,
			unreadCount,
			active,
			unreadMuted,
			mentionMuted,
			DialogListLines());
	};
	if (DialogListLines() == 1) {
		paintOneLineRow(
			p,
			row,
			history,
			history,
			from,
			hiddenSenderInfo,
			item,
			cloudDraft,
			ItemDateTime(item),
			fullWidth,
			flags,
			ms,
			paintItemCallback,
			paintCounterCallback);
	} else {
		paintRow(
			p,
			row,
			history,
			history,
			from,
			hiddenSenderInfo,
			item,
			cloudDraft,
			ItemDateTime(item),
			fullWidth,
			flags,
			ms,
			paintItemCallback,
			paintCounterCallback);
	}
}

QRect RowPainter::sendActionAnimationRect(int animationWidth, int animationHeight, int fullWidth, bool textUpdated) {
	auto nameleft = st::dialogsPadding.x() + (DialogListLines() == 1 ? st::dialogsUnreadHeight : st::dialogsPhotoSize) + st::dialogsPhotoPadding;
	auto namewidth = fullWidth - nameleft - st::dialogsPadding.x();
	auto texttop = st::dialogsPadding.y() + st::msgNameFont->height + st::dialogsSkip;
	return QRect(nameleft, texttop, textUpdated ? namewidth : animationWidth, animationHeight);
}

void PaintCollapsedRow(
		Painter &p,
		const BasicRow &row,
		Data::Folder *folder,
		const QString &text,
		int unread,
		int fullWidth,
		bool selected) {
	p.fillRect(0, 0, fullWidth, st::dialogsImportantBarHeight, selected ? st::dialogsBgOver : st::dialogsBg);

	row.paintRipple(p, 0, 0, fullWidth);

	const auto smallWidth = st::dialogsPadding.x()
		+ st::dialogsPhotoSize
		+ st::dialogsPhotoPadding;
	const auto narrow = (fullWidth <= smallWidth);

	const auto unreadTop = (st::dialogsImportantBarHeight - st::dialogsUnreadHeight) / 2;
	if (!folder) {
		p.setFont(st::semiboldFont);
		p.setPen(st::dialogsNameFg);

		const auto textBaseline = unreadTop
			+ (st::dialogsUnreadHeight - st::dialogsUnreadFont->height) / 2
			+ st::dialogsUnreadFont->ascent;
		const auto left = narrow
			? ((fullWidth - st::semiboldFont->width(text)) / 2)
			: st::dialogsPadding.x();
		p.drawText(left, textBaseline, text);
	} else if (!narrow) {

		const auto textBaseline = unreadTop
			+ (st::dialogsUnreadHeight - st::dialogsUnreadFont->height) / 2
			+ st::dialogsUnreadFont->ascent;
		const auto left = st::dialogsPadding.x();
		const auto textLeft = left * 2 + st::dialogsUnreadHeight;

		folder->paintUserpicLeft(
			p,
			left,
			unreadTop,
			fullWidth,
			st::dialogsUnreadHeight);

		p.setFont(st::semiboldFont);
		p.setPen(st::dialogsNameFg);
		p.drawText(textLeft, textBaseline, text);

	} else {
		folder->paintUserpicLeft(
			p,
			(fullWidth - st::dialogsUnreadHeight) / 2,
			unreadTop,
			fullWidth,
			st::dialogsUnreadHeight);
	}
	if (!narrow && unread) {
		const auto unreadRight = fullWidth - st::dialogsPadding.x();
		UnreadBadgeStyle st;
		st.muted = true;
		paintUnreadCount(
			p,
			QString::number(unread),
			unreadRight,
			unreadTop,
			st,
			nullptr);
	}
}

void clearUnreadBadgesCache() {
	if (unreadBadgeStyle) {
		for (auto &data : unreadBadgeStyle->sizes) {
			for (auto &left : data.left) {
				left = QPixmap();
			}
			for (auto &right : data.right) {
				right = QPixmap();
			}
		}
	}
}

} // namespace Layout
} // namespace Dialogs
