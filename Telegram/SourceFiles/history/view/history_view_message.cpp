/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_message.h"

#include "core/click_handler_types.h" // ClickHandlerContext
#include "history/view/history_view_cursor_state.h"
#include "history/history_item_components.h"
#include "history/history_message.h"
#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_web_page.h"
#include "history/view/history_view_react_button.h"
#include "history/view/history_view_reactions.h"
#include "history/view/history_view_group_call_bar.h" // UserpicInRow.
#include "history/view/history_view_view_button.h" // ViewButton.
#include "history/history.h"
#include "ui/effects/ripple_animation.h"
#include "base/unixtime.h"
#include "ui/chat/message_bubble.h"
#include "ui/chat/chat_style.h"
#include "ui/toast/toast.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_entity.h"
#include "ui/cached_round_corners.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_message_reactions.h"
#include "data/data_sponsored_messages.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "ui/text/text_options.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"

#include "styles/style_widgets.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

namespace HistoryView {
namespace {

const auto kPsaTooltipPrefix = "cloud_lng_tooltip_psa_";

std::optional<Window::SessionController*> ExtractController(
		const ClickContext &context) {
	const auto my = context.other.value<ClickHandlerContext>();
	if (const auto controller = my.sessionWindow.get()) {
		return controller;
	}
	return std::nullopt;
}

class KeyboardStyle : public ReplyKeyboard::Style {
public:
	using ReplyKeyboard::Style::Style;

	int buttonRadius() const override;

	void startPaint(
		Painter &p,
		const Ui::ChatStyle *st) const override;
	const style::TextStyle &textStyle() const override;
	void repaint(not_null<const HistoryItem*> item) const override;

protected:
	void paintButtonBg(
		Painter &p,
		const Ui::ChatStyle *st,
		const QRect &rect,
		float64 howMuchOver) const override;
	void paintButtonIcon(
		Painter &p,
		const Ui::ChatStyle *st,
		const QRect &rect,
		int outerWidth,
		HistoryMessageMarkupButton::Type type) const override;
	void paintButtonLoading(
		Painter &p,
		const Ui::ChatStyle *st,
		const QRect &rect) const override;
	int minButtonWidth(HistoryMessageMarkupButton::Type type) const override;

};

void KeyboardStyle::startPaint(
		Painter &p,
		const Ui::ChatStyle *st) const {
	Expects(st != nullptr);

	p.setPen(st->msgServiceFg());
}

const style::TextStyle &KeyboardStyle::textStyle() const {
	return st::serviceTextStyle;
}

void KeyboardStyle::repaint(not_null<const HistoryItem*> item) const {
	item->history()->owner().requestItemRepaint(item);
}

int KeyboardStyle::buttonRadius() const {
	return st::dateRadius;
}

void KeyboardStyle::paintButtonBg(
		Painter &p,
		const Ui::ChatStyle *st,
		const QRect &rect,
		float64 howMuchOver) const {
	Expects(st != nullptr);

	const auto sti = &st->imageStyle(false);
	Ui::FillRoundRect(p, rect, sti->msgServiceBg, sti->msgServiceBgCorners);
	if (howMuchOver > 0) {
		auto o = p.opacity();
		p.setOpacity(o * howMuchOver);
		Ui::FillRoundRect(p, rect, st->msgBotKbOverBgAdd(), st->msgBotKbOverBgAddCorners());
		p.setOpacity(o);
	}
}

void KeyboardStyle::paintButtonIcon(
		Painter &p,
		const Ui::ChatStyle *st,
		const QRect &rect,
		int outerWidth,
		HistoryMessageMarkupButton::Type type) const {
	Expects(st != nullptr);

	using Type = HistoryMessageMarkupButton::Type;
	const auto icon = [&]() -> const style::icon* {
		switch (type) {
		case Type::Url:
		case Type::Auth: return &st->msgBotKbUrlIcon();
		case Type::Buy: return &st->msgBotKbPaymentIcon();
		case Type::SwitchInlineSame:
		case Type::SwitchInline: return &st->msgBotKbSwitchPmIcon();
		}
		return nullptr;
	}();
	if (icon) {
		icon->paint(p, rect.x() + rect.width() - icon->width() - st::msgBotKbIconPadding, rect.y() + st::msgBotKbIconPadding, outerWidth);
	}
}

void KeyboardStyle::paintButtonLoading(
		Painter &p,
		const Ui::ChatStyle *st,
		const QRect &rect) const {
	Expects(st != nullptr);

	const auto &icon = st->historySendingInvertedIcon();
	icon.paint(p, rect.x() + rect.width() - icon.width() - st::msgBotKbIconPadding, rect.y() + rect.height() - icon.height() - st::msgBotKbIconPadding, rect.x() * 2 + rect.width());
}

int KeyboardStyle::minButtonWidth(
		HistoryMessageMarkupButton::Type type) const {
	using Type = HistoryMessageMarkupButton::Type;
	int result = 2 * buttonPadding(), iconWidth = 0;
	switch (type) {
	case Type::Url:
	case Type::Auth: iconWidth = st::msgBotKbUrlIcon.width(); break;
	case Type::Buy: iconWidth = st::msgBotKbPaymentIcon.width(); break;
	case Type::SwitchInlineSame:
	case Type::SwitchInline: iconWidth = st::msgBotKbSwitchPmIcon.width(); break;
	case Type::Callback:
	case Type::CallbackWithPassword:
	case Type::Game: iconWidth = st::historySendingInvertedIcon.width(); break;
	}
	if (iconWidth > 0) {
		result = std::max(result, 2 * iconWidth + 4 * int(st::msgBotKbIconPadding));
	}
	return result;
}

QString FastReplyText() {
	return tr::lng_fast_reply(tr::now);
}

style::color FromNameFg(
		const Ui::ChatPaintContext &context,
		PeerId peerId) {
	const auto st = context.st;
	if (context.selected()) {
		const style::color colors[] = {
			st->historyPeer1NameFgSelected(),
			st->historyPeer2NameFgSelected(),
			st->historyPeer3NameFgSelected(),
			st->historyPeer4NameFgSelected(),
			st->historyPeer5NameFgSelected(),
			st->historyPeer6NameFgSelected(),
			st->historyPeer7NameFgSelected(),
			st->historyPeer8NameFgSelected(),
		};
		return colors[Data::PeerColorIndex(peerId)];
	} else {
		const style::color colors[] = {
			st->historyPeer1NameFg(),
			st->historyPeer2NameFg(),
			st->historyPeer3NameFg(),
			st->historyPeer4NameFg(),
			st->historyPeer5NameFg(),
			st->historyPeer6NameFg(),
			st->historyPeer7NameFg(),
			st->historyPeer8NameFg(),
		};
		return colors[Data::PeerColorIndex(peerId)];
	}
}

} // namespace

struct Message::CommentsButton {
	std::unique_ptr<Ui::RippleAnimation> ripple;
	std::vector<UserpicInRow> userpics;
	QImage cachedUserpics;
	ClickHandlerPtr link;
	QPoint lastPoint;

	QString rightActionCountString;
	int rightActionCount = 0;
	int rightActionCountWidth = 0;
};

LogEntryOriginal::LogEntryOriginal() = default;

LogEntryOriginal::LogEntryOriginal(LogEntryOriginal &&other)
: page(std::move(other.page)) {
}

LogEntryOriginal &LogEntryOriginal::operator=(LogEntryOriginal &&other) {
	page = std::move(other.page);
	return *this;
}

LogEntryOriginal::~LogEntryOriginal() = default;

Message::Message(
	not_null<ElementDelegate*> delegate,
	not_null<HistoryMessage*> data,
	Element *replacing)
: Element(delegate, data, replacing)
, _bottomInfo(
		&data->history()->owner().reactions(),
		BottomInfoDataFromMessage(this)) {
	initLogEntryOriginal();
	initPsa();
	refreshReactions();
}

Message::~Message() {
	if (_comments) {
		_comments = nullptr;
		checkHeavyPart();
	}
}

not_null<HistoryMessage*> Message::message() const {
	return static_cast<HistoryMessage*>(data().get());
}

void Message::refreshRightBadge() {
	const auto text = [&] {
		if (data()->isDiscussionPost()) {
			return (delegate()->elementContext() == Context::Replies)
				? QString()
				: tr::lng_channel_badge(tr::now);
		} else if (data()->author()->isMegagroup()) {
			if (const auto msgsigned = data()->Get<HistoryMessageSigned>()) {
				Assert(msgsigned->isAnonymousRank);
				return msgsigned->author;
			}
		}
		const auto channel = data()->history()->peer->asMegagroup();
		const auto user = data()->author()->asUser();
		if (!channel || !user) {
			return QString();
		}
		const auto info = channel->mgInfo.get();
		const auto i = channel->mgInfo->admins.find(peerToUser(user->id));
		const auto custom = (i != channel->mgInfo->admins.end())
			? i->second
			: (info->creator == user)
			? info->creatorRank
			: QString();
		return !custom.isEmpty()
			? custom
			: (info->creator == user)
			? tr::lng_owner_badge(tr::now)
			: (i != channel->mgInfo->admins.end())
			? tr::lng_admin_badge(tr::now)
			: QString();
	}();
	if (text.isEmpty()) {
		_rightBadge.clear();
	} else {
		_rightBadge.setText(
			st::defaultTextStyle,
			TextUtilities::RemoveEmoji(TextUtilities::SingleLine(text)));
	}
}

void Message::applyGroupAdminChanges(
		const base::flat_set<UserId> &changes) {
	if (!data()->out()
		&& changes.contains(peerToUser(data()->author()->id))) {
		history()->owner().requestViewResize(this);
	}
}

QSize Message::performCountOptimalSize() {
	const auto item = message();
	const auto media = this->media();

	auto maxWidth = 0;
	auto minHeight = 0;

	updateViewButtonExistence();
	updateMediaInBubbleState();
	refreshRightBadge();
	refreshInfoSkipBlock();

	const auto reactionsInBubble = _reactions && embedReactionsInBubble();
	if (_reactions) {
		_reactions->initDimensions();
	}
	if (drawBubble()) {
		const auto forwarded = item->Get<HistoryMessageForwarded>();
		const auto reply = displayedReply();
		const auto via = item->Get<HistoryMessageVia>();
		const auto entry = logEntryOriginal();
		if (forwarded) {
			forwarded->create(via);
		}
		if (reply) {
			reply->updateName();
		}

		auto mediaDisplayed = false;
		if (media) {
			mediaDisplayed = media->isDisplayed();
			media->initDimensions();
		}
		if (entry) {
			entry->initDimensions();
		}

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());
		maxWidth = plainMaxWidth();
		if (context() == Context::Replies && item->isDiscussionPost()) {
			maxWidth = std::max(maxWidth, st::msgMaxWidth);
		}
		minHeight = hasVisibleText() ? item->_text.minHeight() : 0;
		if (reactionsInBubble) {
			const auto reactionsMaxWidth = st::msgPadding.left()
				+ _reactions->maxWidth()
				+ st::msgPadding.right();
			accumulate_max(
				maxWidth,
				std::min(st::msgMaxWidth, reactionsMaxWidth));
			if (!mediaDisplayed) {
				minHeight += st::mediaInBubbleSkip;
			}
			if (maxWidth >= reactionsMaxWidth) {
				minHeight += _reactions->minHeight();
			} else {
				const auto widthForReactions = maxWidth
					- st::msgPadding.left()
					- st::msgPadding.right();
				minHeight += _reactions->resizeGetHeight(widthForReactions);
			}
		}
		if (!mediaOnBottom && (!_viewButton || !reactionsInBubble)) {
			minHeight += st::msgPadding.bottom();
			if (mediaDisplayed) {
				minHeight += st::mediaInBubbleSkip;
			}
		}
		if (!mediaOnTop) {
			minHeight += st::msgPadding.top();
			if (mediaDisplayed) minHeight += st::mediaInBubbleSkip;
			if (entry) minHeight += st::mediaInBubbleSkip;
		}
		if (mediaDisplayed) {
			// Parts don't participate in maxWidth() in case of media message.
			if (media->enforceBubbleWidth()) {
				maxWidth = media->maxWidth();
				const auto innerWidth = maxWidth
					- st::msgPadding.left()
					- st::msgPadding.right();
				if (hasVisibleText() && maxWidth < plainMaxWidth()) {
					minHeight -= item->_text.minHeight();
					minHeight += item->_text.countHeight(innerWidth);
				}
				if (reactionsInBubble) {
					minHeight -= _reactions->minHeight();
					minHeight
						+= _reactions->countCurrentSize(innerWidth).height();
				}
			} else {
				accumulate_max(maxWidth, media->maxWidth());
			}
			minHeight += media->minHeight();
		} else {
			// Count parts in maxWidth(), don't count them in minHeight().
			// They will be added in resizeGetHeight() anyway.
			if (displayFromName()) {
				const auto from = item->displayFrom();
				const auto &name = from
					? from->nameText()
					: item->hiddenSenderInfo()->nameText;
				auto namew = st::msgPadding.left()
					+ name.maxWidth()
					+ st::msgPadding.right();
				if (via && !displayForwardedFrom()) {
					namew += st::msgServiceFont->spacew + via->maxWidth;
				}
				const auto replyWidth = hasFastReply()
					? st::msgFont->width(FastReplyText())
					: 0;
				if (!_rightBadge.isEmpty()) {
					const auto badgeWidth = _rightBadge.maxWidth();
					namew += st::msgPadding.right()
						+ std::max(badgeWidth, replyWidth);
				} else if (replyWidth) {
					namew += st::msgPadding.right() + replyWidth;
				}
				const auto hasChatTypeIcon = [&]() {
					if (item->isSponsored()) {
						return true;
					} else if (!item->isPost() && item->displayFrom()) {
						const auto from = item->displayFrom();
						if (from->isChat() || from->isMegagroup() || from->isChannel()) {
							return true;
						} else if (const auto user = from->asUser()) {
							if (user->isInaccessible()
								|| (user->isBot() && !user->isSupport() && !user->isRepliesChat())) {
								return true;
							}
						}
					}
					return false;
				}();
				if (hasChatTypeIcon) {
					namew += st::dialogsChatTypeSkip;
				}
				accumulate_max(maxWidth, namew);
			} else if (via && !displayForwardedFrom()) {
				accumulate_max(maxWidth, st::msgPadding.left() + via->maxWidth + st::msgPadding.right());
			}
			if (displayForwardedFrom()) {
				const auto skip1 = forwarded->psaType.isEmpty()
					? 0
					: st::historyPsaIconSkip1;
				auto namew = st::msgPadding.left() + forwarded->text.maxWidth() + skip1 + st::msgPadding.right();
				if (via) {
					namew += st::msgServiceFont->spacew + via->maxWidth;
				}
				accumulate_max(maxWidth, namew);
			}
			if (reply) {
				auto replyw = st::msgPadding.left() + reply->maxReplyWidth - st::msgReplyPadding.left() - st::msgReplyPadding.right() + st::msgPadding.right();
				if (reply->replyToVia) {
					replyw += st::msgServiceFont->spacew + reply->replyToVia->maxWidth;
				}
				accumulate_max(maxWidth, replyw);
			}
			if (entry) {
				accumulate_max(maxWidth, entry->maxWidth());
				minHeight += entry->minHeight();
			}
		}
		accumulate_max(maxWidth, minWidthForMedia());
	} else if (media) {
		media->initDimensions();
		maxWidth = media->maxWidth();
		minHeight = media->isDisplayed() ? media->minHeight() : 0;
	} else {
		maxWidth = st::msgMinWidth;
		minHeight = 0;
	}
	if (const auto markup = item->inlineReplyMarkup()) {
		if (!markup->inlineKeyboard) {
			markup->inlineKeyboard = std::make_unique<ReplyKeyboard>(
				item,
				std::make_unique<KeyboardStyle>(st::msgBotKbButton));
		}

		// if we have a text bubble we can resize it to fit the keyboard
		// but if we have only media we don't do that
		if (hasVisibleText()) {
			accumulate_max(maxWidth, markup->inlineKeyboard->naturalWidth());
		}
	}
	return QSize(maxWidth, minHeight);
}

int Message::marginTop() const {
	auto result = 0;
	if (!isHidden()) {
		if (isAttachedToPrevious()) {
			result += st::msgMarginTopAttached;
		} else {
			result += st::msgMargin.top();
		}
	}
	result += displayedDateHeight();
	if (const auto bar = Get<UnreadBar>()) {
		result += bar->height();
	}
	return result;
}

int Message::marginBottom() const {
	return isHidden() ? 0 : st::msgMargin.bottom();
}

void Message::draw(Painter &p, const PaintContext &context) const {
	auto g = countGeometry();
	if (g.width() < 1) {
		return;
	}

	const auto item = message();
	const auto media = this->media();

	const auto stm = context.messageStyle();
	const auto bubble = drawBubble();

	auto dateh = 0;
	if (const auto date = Get<DateBadge>()) {
		dateh = date->height();
	}
	if (const auto bar = Get<UnreadBar>()) {
		auto unreadbarh = bar->height();
		if (context.clip.intersects(QRect(0, dateh, width(), unreadbarh))) {
			p.translate(0, dateh);
			bar->paint(
				p,
				context,
				0,
				width(),
				delegate()->elementIsChatWide());
			p.translate(0, -dateh);
		}
	}

	if (isHidden()) {
		return;
	}

	auto entry = logEntryOriginal();
	auto mediaDisplayed = media && media->isDisplayed();

	// Entry page is always a bubble bottom.
	auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->isBubbleBottom()*/);
	auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());

	const auto displayInfo = needInfoDisplay();
	const auto reactionsInBubble = _reactions && embedReactionsInBubble();

	auto mediaSelectionIntervals = (!context.selected() && mediaDisplayed)
		? media->getBubbleSelectionIntervals(context.selection)
		: std::vector<Ui::BubbleSelectionInterval>();
	auto localMediaTop = 0;
	const auto customHighlight = mediaDisplayed && media->customHighlight();
	if (!mediaSelectionIntervals.empty() || customHighlight) {
		auto localMediaBottom = g.top() + g.height();
		if (data()->repliesAreComments() || data()->externalReply()) {
			localMediaBottom -= st::historyCommentsButtonHeight;
		}
		if (_viewButton) {
			localMediaBottom -= st::mediaInBubbleSkip + _viewButton->height();
		}
		if (reactionsInBubble) {
			localMediaBottom -= st::mediaInBubbleSkip + _reactions->height();
		}
		if (!mediaOnBottom && (!_viewButton || !reactionsInBubble)) {
			localMediaBottom -= st::msgPadding.bottom();
		}
		if (entry) {
			localMediaBottom -= entry->height();
		}
		localMediaTop = localMediaBottom - media->height();
		for (auto &[top, height] : mediaSelectionIntervals) {
			top += localMediaTop;
		}
	}

	if (customHighlight) {
		media->drawHighlight(p, context, localMediaTop);
	} else {
		paintHighlight(p, context, g.height());
	}

	const auto roll = media ? media->bubbleRoll() : Media::BubbleRoll();
	if (roll) {
		p.save();
		p.translate(g.center());
		p.rotate(roll.rotate);
		p.scale(roll.scale, roll.scale);
		p.translate(-g.center());
	}

	p.setTextPalette(stm->textPalette);

	auto keyboard = item->inlineReplyKeyboard();
	if (keyboard) {
		const auto keyboardHeight = st::msgBotKbButton.margin + keyboard->naturalHeight();
		g.setHeight(g.height() - keyboardHeight);
		const auto keyboardPosition = QPoint(g.left(), g.top() + g.height() + st::msgBotKbButton.margin);
		p.translate(keyboardPosition);
		keyboard->paint(p, context.st, g.width(), context.clip.translated(-keyboardPosition));
		p.translate(-keyboardPosition);
	}

	if (_reactions && !reactionsInBubble) {
		const auto reactionsHeight = st::mediaInBubbleSkip + _reactions->height();
		const auto reactionsLeft = (!bubble && mediaDisplayed)
			? media->contentRectForReactions().x()
			: 0;
		g.setHeight(g.height() - reactionsHeight);
		const auto reactionsPosition = QPoint(reactionsLeft + g.left(), g.top() + g.height() + st::mediaInBubbleSkip);
		p.translate(reactionsPosition);
		_reactions->paint(p, context, g.width(), context.clip.translated(-reactionsPosition));
		p.translate(-reactionsPosition);
	}

	if (bubble) {
		if (displayFromName()
			&& item->displayFrom()
			&& item->displayFrom()->nameVersion > item->_fromNameVersion) {
			fromNameUpdated(g.width());
		}

		const auto skipTail = isAttachedToNext()
			|| (media && media->skipBubbleTail())
			|| (keyboard != nullptr)
			|| (this->context() == Context::Replies
				&& data()->isDiscussionPost());
		const auto displayTail = skipTail
			? RectPart::None
			: (context.outbg && !delegate()->elementIsChatWide())
			? RectPart::Right
			: RectPart::Left;
		Ui::PaintBubble(
			p,
			Ui::ComplexBubble{
				.simple = Ui::SimpleBubble{
					.st = context.st,
					.geometry = g,
					.pattern = context.bubblesPattern,
					.patternViewport = context.viewport,
					.outerWidth = width(),
					.selected = context.selected(),
					.outbg = context.outbg,
					.tailSide = displayTail,
				},
				.selection = mediaSelectionIntervals,
			});

		auto inner = g;
		paintCommentsButton(p, inner, context);

		auto trect = inner.marginsRemoved(st::msgPadding);

		const auto reactionsTop = (reactionsInBubble && !_viewButton)
			? st::mediaInBubbleSkip
			: 0;
		const auto reactionsHeight = reactionsInBubble
			? (reactionsTop + _reactions->height())
			: 0;
		if (reactionsInBubble) {
			trect.setHeight(trect.height() - reactionsHeight);
			const auto reactionsPosition = QPoint(trect.left(), trect.top() + trect.height() + reactionsTop);
			p.translate(reactionsPosition);
			_reactions->paint(p, context, g.width(), context.clip.translated(-reactionsPosition));
			p.translate(-reactionsPosition);
		}

		if (_viewButton) {
			const auto belowInfo = _viewButton->belowMessageInfo();
			const auto infoHeight = reactionsInBubble
				? (reactionsHeight + st::msgPadding.bottom())
				: _bottomInfo.height();
			const auto heightMargins = QMargins(0, 0, 0, infoHeight);
			_viewButton->draw(
				p,
				_viewButton->countRect(belowInfo
					? inner
					: inner - heightMargins),
				context);
			if (belowInfo) {
				inner.setHeight(inner.height() - _viewButton->height());
			}
			trect.setHeight(trect.height() - _viewButton->height());
			if (reactionsInBubble) {
				trect.setHeight(trect.height() + st::msgPadding.bottom());
			} else if (mediaDisplayed) {
				trect.setHeight(trect.height() - st::mediaInBubbleSkip);
			}
		}

		if (mediaOnBottom) {
			trect.setHeight(trect.height() + st::msgPadding.bottom());
		}
		if (mediaOnTop) {
			trect.setY(trect.y() - st::msgPadding.top());
		} else {
			paintFromName(p, trect, context);
			paintForwardedInfo(p, trect, context);
			paintReplyInfo(p, trect, context);
			paintViaBotIdInfo(p, trect, context);
		}
		if (entry) {
			trect.setHeight(trect.height() - entry->height());
		}
		if (displayInfo) {
			trect.setHeight(trect.height()
				- (_bottomInfo.height() - st::msgDateFont->height));
		}
		paintText(p, trect, context);
		if (mediaDisplayed) {
			auto mediaHeight = media->height();
			auto mediaLeft = inner.left();
			auto mediaTop = (trect.y() + trect.height() - mediaHeight);

			p.translate(mediaLeft, mediaTop);
			media->draw(p, context.translated(
				-mediaLeft,
				-mediaTop
			).withSelection(skipTextSelection(context.selection)));
			p.translate(-mediaLeft, -mediaTop);
		}
		if (entry) {
			auto entryLeft = inner.left();
			auto entryTop = trect.y() + trect.height();
			p.translate(entryLeft, entryTop);
			auto entryContext = context.translated(-entryLeft, -entryTop);
			entryContext.selection = skipTextSelection(context.selection);
			if (mediaDisplayed) {
				entryContext.selection = media->skipSelection(
					entryContext.selection);
			}
			entry->draw(p, entryContext);
			p.translate(-entryLeft, -entryTop);
		}
		if (displayInfo) {
			const auto bottomSelected = context.selected()
				|| (!mediaSelectionIntervals.empty()
					&& (mediaSelectionIntervals.back().top
						+ mediaSelectionIntervals.back().height
						>= inner.y() + inner.height()));
			drawInfo(
				p,
				context.withSelection(
					bottomSelected ? FullSelection : TextSelection()),
				inner.left() + inner.width(),
				inner.top() + inner.height(),
				2 * inner.left() + inner.width(),
				InfoDisplayType::Default);
			if (_comments) {
				const auto o = p.opacity();
				p.setOpacity(0.3);
				p.fillRect(g.left(), g.top() + g.height() - st::historyCommentsButtonHeight - st::lineWidth, g.width(), st::lineWidth, stm->msgDateFg);
				p.setOpacity(o);
			}
		}
		if (const auto size = rightActionSize()) {
			const auto fastShareSkip = std::clamp(
				(g.height() - size->height()) / 2,
				0,
				st::historyFastShareBottom);
			const auto fastShareLeft = g.left() + g.width() + st::historyFastShareLeft;
			const auto fastShareTop = g.top() + g.height() - fastShareSkip - size->height();
			drawRightAction(p, context, fastShareLeft, fastShareTop, width());
		}

		if (media) {
			media->paintBubbleFireworks(p, g, context.now);
		}
	} else if (media && media->isDisplayed()) {
		p.translate(g.topLeft());
		media->draw(p, context.translated(
			-g.topLeft()
		).withSelection(skipTextSelection(context.selection)));
		p.translate(-g.topLeft());
	}

	p.restoreTextPalette();

	if (roll) {
		p.restore();
	}

	if (const auto reply = displayedReply()) {
		if (reply->isNameUpdated()) {
			const_cast<Message*>(this)->setPendingResize();
		}
	}
}

void Message::paintCommentsButton(
		Painter &p,
		QRect &g,
		const PaintContext &context) const {
	if (!data()->repliesAreComments() && !data()->externalReply()) {
		return;
	}
	if (!_comments) {
		_comments = std::make_unique<CommentsButton>();
		history()->owner().registerHeavyViewPart(const_cast<Message*>(this));
	}
	const auto stm = context.messageStyle();
	const auto views = data()->Get<HistoryMessageViews>();

	g.setHeight(g.height() - st::historyCommentsButtonHeight);
	const auto top = g.top() + g.height();
	auto left = g.left();
	auto width = g.width();

	if (_comments->ripple) {
		p.setOpacity(st::historyPollRippleOpacity);
		const auto colorOverride = &stm->msgWaveformInactive->c;
		_comments->ripple->paint(p, left, top, width, colorOverride);
		if (_comments->ripple->empty()) {
			_comments->ripple.reset();
		}
		p.setOpacity(1.);
	}

	left += st::historyCommentsSkipLeft;
	width -= st::historyCommentsSkipLeft
		+ st::historyCommentsSkipRight;

	const auto &open = stm->historyCommentsOpen;
	open.paint(p,
		left + width - open.width(),
		top + (st::historyCommentsButtonHeight - open.height()) / 2,
		width);

	if (!views || views->recentRepliers.empty()) {
		const auto &icon = stm->historyComments;
		icon.paint(
			p,
			left,
			top + (st::historyCommentsButtonHeight - icon.height()) / 2,
			width);
		left += icon.width();
	} else {
		auto &list = _comments->userpics;
		const auto limit = HistoryMessageViews::kMaxRecentRepliers;
		const auto count = std::min(int(views->recentRepliers.size()), limit);
		const auto single = st::historyCommentsUserpics.size;
		const auto shift = st::historyCommentsUserpics.shift;
		const auto regenerate = [&] {
			if (list.size() != count) {
				return true;
			}
			for (auto i = 0; i != count; ++i) {
				auto &entry = list[i];
				const auto peer = entry.peer;
				auto &view = entry.view;
				const auto wasView = view.get();
				if (views->recentRepliers[i] != peer->id
					|| peer->userpicUniqueKey(view) != entry.uniqueKey
					|| view.get() != wasView) {
					return true;
				}
			}
			return false;
		}();
		if (regenerate) {
			for (auto i = 0; i != count; ++i) {
				const auto peerId = views->recentRepliers[i];
				if (i == list.size()) {
					list.push_back(UserpicInRow{
						history()->owner().peer(peerId)
					});
				} else if (list[i].peer->id != peerId) {
					list[i].peer = history()->owner().peer(peerId);
				}
			}
			while (list.size() > count) {
				list.pop_back();
			}
			GenerateUserpicsInRow(
				_comments->cachedUserpics,
				list,
				st::historyCommentsUserpics,
				limit);
		}
		p.drawImage(
			left,
			top + (st::historyCommentsButtonHeight - single) / 2,
			_comments->cachedUserpics);
		left += single + (count - 1) * (single - shift);
	}

	left += st::historyCommentsSkipText;
	p.setPen(stm->msgFileThumbLinkFg);
	p.setFont(st::semiboldFont);

	const auto textTop = top + (st::historyCommentsButtonHeight - st::semiboldFont->height) / 2;
	p.drawTextLeft(
		left,
		textTop,
		width,
		views ? views->replies.text : tr::lng_replies_view_original(tr::now),
		views ? views->replies.textWidth : -1);

	if (views && data()->areRepliesUnread()) {
		p.setPen(Qt::NoPen);
		p.setBrush(stm->msgFileBg);

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(style::rtlrect(left + views->replies.textWidth + st::mediaUnreadSkip, textTop + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, width));
		}
	}
}

void Message::paintFromName(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const {
	const auto item = message();
	if (!displayFromName()) {
		return;
	}
	const auto badgeWidth = _rightBadge.isEmpty() ? 0 : _rightBadge.maxWidth();
	const auto replyWidth = [&] {
		if (isUnderCursor() && displayFastReply()) {
			return st::msgFont->width(FastReplyText());
		}
		return 0;
	}();
	const auto rightWidth = replyWidth ? replyWidth : badgeWidth;
	auto availableLeft = trect.left();
	auto availableWidth = trect.width();
	if (rightWidth) {
		availableWidth -= st::msgPadding.right() + rightWidth;
	}

	const auto chatTypeIcon = [&]() -> const style::icon * {
		if (item->isSponsored()) {
			return context.selected()
					? &st::msgNameSponsoredIconSelected
					: &st::msgNameSponsoredIcon;
		} else if (!item->isPost() && item->displayFrom()) {
			const auto from = item->displayFrom();
			if (from->isChat() || from->isMegagroup()) {
				return context.selected()
						? &st::msgNameChatIconSelected
						: &st::msgNameChatIcon;
			} else if (from->isChannel()) {
				return context.selected()
						? &st::msgNameChannelIconSelected
						: &st::msgNameChannelIcon;
			} else if (const auto user = from->asUser()) {
				if (user->isInaccessible()) {
					return context.selected()
							? &st::msgNameDeletedIconSelected
							: &st::msgNameDeletedIcon;
				} else if (user->isBot() && !user->isSupport() && !user->isRepliesChat()) {
					return context.selected()
							? &st::msgNameBotIconSelected
							: &st::msgNameBotIcon;
				}
			}
		}
		return nullptr;
	}();

	if (chatTypeIcon) {
		chatTypeIcon->paint(p, QPoint(availableLeft, trect.top()), availableWidth);
		availableLeft += st::dialogsChatTypeSkip;
		availableWidth -= st::dialogsChatTypeSkip;
	}

	p.setFont(st::msgNameFont);
	const auto stm = context.messageStyle();

	const auto nameText = [&]() -> const Ui::Text::String * {
		const auto from = item->displayFrom();
		const auto service = (context.outbg || item->isPost());
		const auto st = context.st;
		if (from) {
			p.setPen(!service
				? FromNameFg(context, from->id)
				: item->isSponsored()
				? st->boxTextFgGood()
				: stm->msgServiceFg);
			return &from->nameText();
		} else if (const auto info = item->hiddenSenderInfo()) {
			p.setPen(!service
				? FromNameFg(context, info->colorPeerId)
				: item->isSponsored()
				? st->boxTextFgGood()
				: stm->msgServiceFg);
			return &info->nameText;
		} else {
			Unexpected("Corrupt sender information in message.");
		}
	}();
	nameText->drawElided(p, availableLeft, trect.top(), availableWidth);
	const auto skipWidth = nameText->maxWidth() + st::msgServiceFont->spacew;
	availableLeft += skipWidth;
	availableWidth -= skipWidth;

	auto via = item->Get<HistoryMessageVia>();
	if (via && !displayForwardedFrom() && availableWidth > 0) {
		p.setPen(stm->msgServiceFg);
		p.drawText(availableLeft, trect.top() + st::msgServiceFont->ascent, via->text);
		auto skipWidth = via->width + st::msgServiceFont->spacew;
		availableLeft += skipWidth;
		availableWidth -= skipWidth;
	}
	if (rightWidth) {
		p.setPen(stm->msgDateFg);
		p.setFont(ClickHandler::showAsActive(_fastReplyLink)
			? st::msgFont->underline()
			: st::msgFont);
		if (replyWidth) {
			p.drawText(
				trect.left() + trect.width() - rightWidth,
				trect.top() + st::msgFont->ascent,
				FastReplyText());
		} else {
			_rightBadge.draw(
				p,
				trect.left() + trect.width() - rightWidth,
				trect.top(),
				rightWidth);
		}
	}
	trect.setY(trect.y() + st::msgNameFont->height);
}

void Message::paintForwardedInfo(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const {
	if (displayForwardedFrom()) {
		const auto item = message();
		const auto st = context.st;
		const auto stm = context.messageStyle();
		const auto forwarded = item->Get<HistoryMessageForwarded>();

		const auto &serviceFont = st::msgServiceFont;
		const auto skip1 = forwarded->psaType.isEmpty()
			? 0
			: st::historyPsaIconSkip1;
		const auto skip2 = forwarded->psaType.isEmpty()
			? 0
			: st::historyPsaIconSkip2;
		const auto fits = (forwarded->text.maxWidth() + skip1 <= trect.width());
		const auto skip = fits ? skip1 : skip2;
		const auto useWidth = trect.width() - skip;
		const auto countedHeight = forwarded->text.countHeight(useWidth);
		const auto breakEverywhere = (countedHeight > 2 * serviceFont->height);
		p.setPen(!forwarded->psaType.isEmpty()
			? st->boxTextFgGood()
			: stm->msgServiceFg);
		p.setFont(serviceFont);
		p.setTextPalette(!forwarded->psaType.isEmpty()
			? st->historyPsaForwardPalette()
			: stm->fwdTextPalette);
		forwarded->text.drawElided(p, trect.x(), trect.y(), useWidth, 2, style::al_left, 0, -1, 0, breakEverywhere);
		p.setTextPalette(stm->textPalette);

		if (!forwarded->psaType.isEmpty()) {
			const auto entry = Get<PsaTooltipState>();
			Assert(entry != nullptr);
			const auto shown = entry->buttonVisibleAnimation.value(
				entry->buttonVisible ? 1. : 0.);
			if (shown > 0) {
				const auto &icon = stm->historyPsaIcon;
				const auto position = fits
					? st::historyPsaIconPosition1
					: st::historyPsaIconPosition2;
				const auto x = trect.x() + trect.width() - position.x() - icon.width();
				const auto y = trect.y() + position.y();
				if (shown == 1) {
					icon.paint(p, x, y, trect.width());
				} else {
					p.save();
					p.translate(x + icon.width() / 2, y + icon.height() / 2);
					p.scale(shown, shown);
					p.setOpacity(shown);
					icon.paint(p, -icon.width() / 2, -icon.height() / 2, width());
					p.restore();
				}
			}
		}

		trect.setY(trect.y() + ((fits ? 1 : 2) * serviceFont->height));
	}
}

void Message::paintReplyInfo(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const {
	if (const auto reply = displayedReply()) {
		int32 h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		reply->paint(p, this, context, trect.x(), trect.y(), trect.width(), true);
		trect.setY(trect.y() + h);
	}
}

void Message::paintViaBotIdInfo(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const {
	const auto item = message();
	if (!displayFromName() && !displayForwardedFrom()) {
		if (auto via = item->Get<HistoryMessageVia>()) {
			const auto stm = context.messageStyle();
			p.setFont(st::msgServiceNameFont);
			p.setPen(stm->msgServiceFg);
			p.drawTextLeft(trect.left(), trect.top(), width(), via->text);
			trect.setY(trect.y() + st::msgServiceNameFont->height);
		}
	}
}

void Message::paintText(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const {
	if (!hasVisibleText()) {
		return;
	}
	const auto item = message();
	const auto stm = context.messageStyle();
	p.setPen(stm->historyTextFg);
	p.setFont(st::msgFont);
	item->_text.draw(p, trect.x(), trect.y(), trect.width(), style::al_left, 0, -1, context.selection);
}

PointState Message::pointState(QPoint point) const {
	auto g = countGeometry();
	if (g.width() < 1 || isHidden()) {
		return PointState::Outside;
	}

	const auto media = this->media();
	const auto item = message();
	const auto reactionsInBubble = _reactions && embedReactionsInBubble();
	if (drawBubble()) {
		if (!g.contains(point)) {
			return PointState::Outside;
		}
		if (const auto mediaDisplayed = media && media->isDisplayed()) {
			// Hack for grouped media point state.
			auto entry = logEntryOriginal();

			// Entry page is always a bubble bottom.
			auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->isBubbleBottom()*/);

			if (item->repliesAreComments() || item->externalReply()) {
				g.setHeight(g.height() - st::historyCommentsButtonHeight);
			}

			auto trect = g.marginsRemoved(st::msgPadding);
			if (reactionsInBubble) {
				const auto reactionsHeight = (_viewButton ? 0 : st::mediaInBubbleSkip)
					+ _reactions->height();
				trect.setHeight(trect.height() - reactionsHeight);
			}
			if (_viewButton) {
				trect.setHeight(trect.height() - _viewButton->height());
				if (reactionsInBubble) {
					trect.setHeight(trect.height() + st::msgPadding.bottom());
				} else if (mediaDisplayed) {
					trect.setHeight(trect.height() - st::mediaInBubbleSkip);
				}
			}
			if (mediaOnBottom) {
				trect.setHeight(trect.height() + st::msgPadding.bottom());
			}
			//if (mediaOnTop) {
			//	trect.setY(trect.y() - st::msgPadding.top());
			//} else {
			//	if (getStateFromName(point, trect, &result)) return result;
			//	if (getStateForwardedInfo(point, trect, &result, request)) return result;
			//	if (getStateReplyInfo(point, trect, &result)) return result;
			//	if (getStateViaBotIdInfo(point, trect, &result)) return result;
			//}
			if (entry) {
				auto entryHeight = entry->height();
				trect.setHeight(trect.height() - entryHeight);
			}

			auto mediaHeight = media->height();
			auto mediaLeft = trect.x() - st::msgPadding.left();
			auto mediaTop = (trect.y() + trect.height() - mediaHeight);

			if (point.y() >= mediaTop && point.y() < mediaTop + mediaHeight) {
				return media->pointState(point - QPoint(mediaLeft, mediaTop));
			}
		}
		return PointState::Inside;
	} else if (media) {
		return media->pointState(point - g.topLeft());
	}
	return PointState::Outside;
}

bool Message::displayFromPhoto() const {
	return hasFromPhoto() && !isAttachedToNext();
}

void Message::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	Element::clickHandlerPressedChanged(handler, pressed);

	if (!handler) {
		return;
	} else if (_comments && (handler == _comments->link)) {
		toggleCommentsButtonRipple(pressed);
	} else if (_viewButton) {
		_viewButton->checkLink(handler, pressed);
	}
}

void Message::toggleCommentsButtonRipple(bool pressed) {
	Expects(_comments != nullptr);

	if (!drawBubble()) {
		return;
	} else if (pressed) {
		const auto g = countGeometry();
		const auto linkWidth = g.width();
		const auto linkHeight = st::historyCommentsButtonHeight;
		if (!_comments->ripple) {
			const auto drawMask = [&](QPainter &p) {
				const auto radius = st::historyMessageRadius;
				p.drawRoundedRect(
					0,
					0,
					linkWidth,
					linkHeight,
					radius,
					radius);
				p.fillRect(0, 0, linkWidth, radius * 2, Qt::white);
			};
			auto mask = Ui::RippleAnimation::maskByDrawer(
				QSize(linkWidth, linkHeight),
				false,
				drawMask);
			_comments->ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				std::move(mask),
				[=] { history()->owner().requestViewRepaint(this); });
		}
		_comments->ripple->add(_comments->lastPoint);
	} else if (_comments->ripple) {
		_comments->ripple->lastStop();
	}
}

bool Message::hasHeavyPart() const {
	return _comments || Element::hasHeavyPart();
}

void Message::unloadHeavyPart() {
	Element::unloadHeavyPart();
	_comments = nullptr;
}

bool Message::showForwardsFromSender(
		not_null<HistoryMessageForwarded*> forwarded) const {
	const auto peer = message()->history()->peer;
	return peer->isSelf()
		|| peer->isRepliesChat()
		|| forwarded->imported;
}

bool Message::hasFromPhoto() const {
	if (isHidden()) {
		return false;
	}
	switch (context()) {
	case Context::AdminLog:
		return true;
	case Context::History:
	case Context::Pinned:
	case Context::Replies: {
		const auto item = message();
		if (item->isPost()
			|| item->isEmpty()
			|| (context() == Context::Replies && item->isDiscussionPost())) {
			return false;
		} else if (delegate()->elementIsChatWide()) {
			return true;
		} else if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
			const auto peer = item->history()->peer;
			if (peer->isSelf() || peer->isRepliesChat()) {
				return true;
			}
		}
		return !item->out() && !item->history()->peer->isUser();
	} break;
	case Context::ContactPreview:
		return false;
	}
	Unexpected("Context in Message::hasFromPhoto.");
}

TextState Message::textState(
		QPoint point,
		StateRequest request) const {
	const auto item = message();
	const auto media = this->media();

	auto result = TextState(item);

	auto g = countGeometry();
	if (g.width() < 1 || isHidden()) {
		return result;
	}

	const auto bubble = drawBubble();
	const auto reactionsInBubble = _reactions && embedReactionsInBubble();
	const auto mediaDisplayed = media && media->isDisplayed();
	auto keyboard = item->inlineReplyKeyboard();
	auto keyboardHeight = 0;
	if (keyboard) {
		keyboardHeight = keyboard->naturalHeight();
		g.setHeight(g.height() - st::msgBotKbButton.margin - keyboardHeight);
	}

	if (_reactions && !reactionsInBubble) {
		const auto reactionsHeight = st::mediaInBubbleSkip + _reactions->height();
		const auto reactionsLeft = (!bubble && mediaDisplayed)
			? media->contentRectForReactions().x()
			: 0;
		g.setHeight(g.height() - reactionsHeight);
		const auto reactionsPosition = QPoint(reactionsLeft + g.left(), g.top() + g.height() + st::mediaInBubbleSkip);
		if (_reactions->getState(point - reactionsPosition, &result)) {
			return result;
		}
	}

	if (bubble) {
		const auto inBubble = g.contains(point);
		auto entry = logEntryOriginal();

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());

		auto inner = g;
		if (getStateCommentsButton(point, inner, &result)) {
			return result;
		}
		auto trect = inner.marginsRemoved(st::msgPadding);
		const auto reactionsTop = (reactionsInBubble && !_viewButton)
			? st::mediaInBubbleSkip
			: 0;
		const auto reactionsHeight = reactionsInBubble
			? (reactionsTop + _reactions->height())
			: 0;
		if (reactionsInBubble) {
			trect.setHeight(trect.height() - reactionsHeight);
			const auto reactionsPosition = QPoint(trect.left(), trect.top() + trect.height() + reactionsTop);
			if (_reactions->getState(point - reactionsPosition, &result)) {
				return result;
			}
		}
		if (_viewButton) {
			const auto belowInfo = _viewButton->belowMessageInfo();
			const auto infoHeight = reactionsInBubble
				? (reactionsHeight + st::msgPadding.bottom())
				: _bottomInfo.height();
			const auto heightMargins = QMargins(0, 0, 0, infoHeight);
			if (_viewButton->getState(
					point,
					_viewButton->countRect(belowInfo
						? inner
						: inner - heightMargins),
					&result)) {
				return result;
			}
			if (belowInfo) {
				inner -= heightMargins;
			}
			trect.setHeight(trect.height() - _viewButton->height());
			if (reactionsInBubble) {
				trect.setHeight(trect.height() + st::msgPadding.bottom());
			} else if (mediaDisplayed) {
				trect.setHeight(trect.height() - st::mediaInBubbleSkip);
			}
		}
		if (mediaOnBottom) {
			trect.setHeight(trect.height()
				+ st::msgPadding.bottom()
				- viewButtonHeight());
		}
		if (mediaOnTop) {
			trect.setY(trect.y() - st::msgPadding.top());
		} else if (inBubble) {
			if (getStateFromName(point, trect, &result)) {
				return result;
			}
			if (getStateForwardedInfo(point, trect, &result, request)) {
				return result;
			}
			if (getStateReplyInfo(point, trect, &result)) {
				return result;
			}
			if (getStateViaBotIdInfo(point, trect, &result)) {
				return result;
			}
		}
		if (entry) {
			auto entryHeight = entry->height();
			trect.setHeight(trect.height() - entryHeight);
			auto entryLeft = inner.left();
			auto entryTop = trect.y() + trect.height();
			if (point.y() >= entryTop && point.y() < entryTop + entryHeight) {
				result = entry->textState(
					point - QPoint(entryLeft, entryTop),
					request);
				result.symbol += item->_text.length() + (mediaDisplayed ? media->fullSelectionLength() : 0);
			}
		}

		auto checkBottomInfoState = [&] {
			if (mediaOnBottom && (entry || media->customInfoLayout())) {
				return;
			}
			const auto bottomInfoResult = bottomInfoTextState(
				inner.left() + inner.width(),
				inner.top() + inner.height(),
				point,
				InfoDisplayType::Default);
			if (bottomInfoResult.link
				|| bottomInfoResult.cursor != CursorState::None) {
				result = bottomInfoResult;
			}
		};
		if (inBubble) {
			if (mediaDisplayed) {
				auto mediaHeight = media->height();
				auto mediaLeft = trect.x() - st::msgPadding.left();
				auto mediaTop = (trect.y() + trect.height() - mediaHeight);

				if (point.y() >= mediaTop && point.y() < mediaTop + mediaHeight) {
					result = media->textState(point - QPoint(mediaLeft, mediaTop), request);
					result.symbol += item->_text.length();
				} else if (getStateText(point, trect, &result, request)) {
					checkBottomInfoState();
					return result;
				} else if (point.y() >= trect.y() + trect.height()) {
					result.symbol = item->_text.length();
				}
			} else if (getStateText(point, trect, &result, request)) {
				checkBottomInfoState();
				return result;
			} else if (point.y() >= trect.y() + trect.height()) {
				result.symbol = item->_text.length();
			}
		}
		checkBottomInfoState();
		if (const auto size = rightActionSize()) {
			const auto fastShareSkip = std::clamp(
				(g.height() - size->height()) / 2,
				0,
				st::historyFastShareBottom);
			const auto fastShareLeft = g.left() + g.width() + st::historyFastShareLeft;
			const auto fastShareTop = g.top() + g.height() - fastShareSkip - size->height();
			if (QRect(
				fastShareLeft,
				fastShareTop,
				size->width(),
				size->height()
			).contains(point)) {
				result.link = rightActionLink();
			}
		}
	} else if (media && media->isDisplayed()) {
		result = media->textState(point - g.topLeft(), request);
		result.symbol += item->_text.length();
	}

	if (keyboard && item->isHistoryEntry()) {
		auto keyboardTop = g.top() + g.height() + st::msgBotKbButton.margin;
		if (QRect(g.left(), keyboardTop, g.width(), keyboardHeight).contains(point)) {
			result.link = keyboard->getLink(point - QPoint(g.left(), keyboardTop));
			return result;
		}
	}

	return result;
}

bool Message::getStateCommentsButton(
		QPoint point,
		QRect &g,
		not_null<TextState*> outResult) const {
	if (!_comments) {
		return false;
	}
	g.setHeight(g.height() - st::historyCommentsButtonHeight);
	if (data()->isSending()
		|| !QRect(
			g.left(),
			g.top() + g.height(),
			g.width(),
			st::historyCommentsButtonHeight).contains(point)) {
		return false;
	}
	if (!_comments->link && data()->repliesAreComments()) {
		_comments->link = createGoToCommentsLink();
	} else if (!_comments->link && data()->externalReply()) {
		_comments->link = rightActionLink();
	}
	outResult->link = _comments->link;
	_comments->lastPoint = point - QPoint(g.left(), g.top() + g.height());
	return true;
}

ClickHandlerPtr Message::createGoToCommentsLink() const {
	const auto fullId = data()->fullId();
	const auto sessionId = data()->history()->session().uniqueId();
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto controller = ExtractController(context).value_or(nullptr);
		if (!controller) {
			return;
		}
		if (controller->session().uniqueId() != sessionId) {
			return;
		}
		if (const auto item = controller->session().data().message(fullId)) {
			const auto history = item->history();
			if (const auto channel = history->peer->asChannel()) {
				if (channel->invitePeekExpires()) {
					Ui::Toast::Show(
						tr::lng_channel_invite_private(tr::now));
					return;
				}
			}
			controller->showRepliesForMessage(history, item->id);
		}
	});
}

bool Message::getStateFromName(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult) const {
	const auto item = message();
	if (displayFromName()) {
		const auto replyWidth = [&] {
			if (isUnderCursor() && displayFastReply()) {
				return st::msgFont->width(FastReplyText());
			}
			return 0;
		}();
		if (replyWidth
			&& point.x() >= trect.left() + trect.width() - replyWidth
			&& point.x() < trect.left() + trect.width() + st::msgPadding.right()
			&& point.y() >= trect.top() - st::msgPadding.top()
			&& point.y() < trect.top() + st::msgServiceFont->height) {
			outResult->link = fastReplyLink();
			return true;
		}
		if (point.y() >= trect.top() && point.y() < trect.top() + st::msgNameFont->height) {
			auto availableLeft = trect.left();
			auto availableWidth = trect.width();
			const auto hasChatTypeIcon = [&]() {
				if (item->isSponsored()) {
					return true;
				} else if (!item->isPost() && item->displayFrom()) {
					const auto from = item->displayFrom();
					if (from->isChat() || from->isMegagroup() || from->isChannel()) {
						return true;
					} else if (const auto user = from->asUser()) {
						if (user->isInaccessible()
							|| (user->isBot() && !user->isSupport() && !user->isRepliesChat())) {
							return true;
						}
					}
				}
				return false;
			}();
			if (replyWidth) {
				availableWidth -= st::msgPadding.right() + replyWidth;
			}
			const auto chatIconWidth = (hasChatTypeIcon) ? st::dialogsChatTypeSkip : 0;
			const auto from = item->displayFrom();
			const auto nameText = [&]() -> const Ui::Text::String * {
				if (from) {
					return &from->nameText();
				} else if (const auto info = item->hiddenSenderInfo()) {
					return &info->nameText;
				} else {
					Unexpected("Corrupt forwarded information in message.");
				}
			}();
			if (point.x() >= availableLeft
				&& point.x() < availableLeft + availableWidth
				&& point.x() < availableLeft + nameText->maxWidth() + chatIconWidth) {
				outResult->link = fromLink();
				return true;
			}
			auto via = item->Get<HistoryMessageVia>();
			if (via
				&& !displayForwardedFrom()
				&& point.x() >= availableLeft + nameText->maxWidth() + st::msgServiceFont->spacew
				&& point.x() < availableLeft + availableWidth
				&& point.x() < availableLeft + nameText->maxWidth() + st::msgServiceFont->spacew + via->width) {
				outResult->link = via->link;
				return true;
			}
		}
		trect.setTop(trect.top() + st::msgNameFont->height);
	}
	return false;
}

bool Message::getStateForwardedInfo(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult,
		StateRequest request) const {
	if (displayForwardedFrom()) {
		const auto item = message();
		const auto forwarded = item->Get<HistoryMessageForwarded>();
		const auto skip1 = forwarded->psaType.isEmpty()
			? 0
			: st::historyPsaIconSkip1;
		const auto skip2 = forwarded->psaType.isEmpty()
			? 0
			: st::historyPsaIconSkip2;
		const auto fits = (forwarded->text.maxWidth() <= (trect.width() - skip1));
		const auto fwdheight = (fits ? 1 : 2) * st::semiboldFont->height;
		if (point.y() >= trect.top() && point.y() < trect.top() + fwdheight) {
			if (skip1) {
				const auto &icon = st::historyPsaIconIn;
				const auto position = fits
					? st::historyPsaIconPosition1
					: st::historyPsaIconPosition2;
				const auto iconRect = QRect(
					trect.x() + trect.width() - position.x() - icon.width(),
					trect.y() + position.y(),
					icon.width(),
					icon.height());
				if (iconRect.contains(point)) {
					if (const auto link = psaTooltipLink()) {
						outResult->link = link;
						return true;
					}
				}
			}
			const auto useWidth = trect.width() - (fits ? skip1 : skip2);
			const auto breakEverywhere = (forwarded->text.countHeight(useWidth) > 2 * st::semiboldFont->height);
			auto textRequest = request.forText();
			if (breakEverywhere) {
				textRequest.flags |= Ui::Text::StateRequest::Flag::BreakEverywhere;
			}
			*outResult = TextState(item, forwarded->text.getState(
				point - trect.topLeft(),
				useWidth,
				textRequest));
			outResult->symbol = 0;
			outResult->afterSymbol = false;
			if (breakEverywhere) {
				outResult->cursor = CursorState::Forwarded;
			} else {
				outResult->cursor = CursorState::None;
			}
			return true;
		}
		trect.setTop(trect.top() + fwdheight);
	}
	return false;
}

ClickHandlerPtr Message::psaTooltipLink() const {
	const auto state = Get<PsaTooltipState>();
	if (!state || !state->buttonVisible) {
		return nullptr;
	} else if (state->link) {
		return state->link;
	}
	const auto type = state->type;
	const auto handler = [=] {
		const auto custom = type.isEmpty()
			? QString()
			: Lang::GetNonDefaultValue(kPsaTooltipPrefix + type.toUtf8());
		auto text = Ui::Text::RichLangValue(
			(custom.isEmpty()
				? tr::lng_tooltip_psa_default(tr::now)
				: custom));
		TextUtilities::ParseEntities(text, 0);
		psaTooltipToggled(true);
		delegate()->elementShowTooltip(text, crl::guard(this, [=] {
			psaTooltipToggled(false);
		}));
	};
	state->link = std::make_shared<LambdaClickHandler>(
		crl::guard(this, handler));
	return state->link;
}

void Message::psaTooltipToggled(bool tooltipShown) const {
	const auto visible = !tooltipShown;
	const auto state = Get<PsaTooltipState>();
	if (state->buttonVisible == visible) {
		return;
	}
	state->buttonVisible = visible;
	history()->owner().notifyViewLayoutChange(this);
	state->buttonVisibleAnimation.start(
		[=] { history()->owner().requestViewRepaint(this); },
		visible ? 0. : 1.,
		visible ? 1. : 0.,
		st::fadeWrapDuration);
}

bool Message::getStateReplyInfo(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult) const {
	if (auto reply = displayedReply()) {
		int32 h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		if (point.y() >= trect.top() && point.y() < trect.top() + h) {
			if (reply->replyToMsg && QRect(trect.x(), trect.y() + st::msgReplyPadding.top(), trect.width(), st::msgReplyBarSize.height()).contains(point)) {
				outResult->link = reply->replyToLink();
			}
			return true;
		}
		trect.setTop(trect.top() + h);
	}
	return false;
}

bool Message::getStateViaBotIdInfo(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult) const {
	const auto item = message();
	if (const auto via = item->Get<HistoryMessageVia>()) {
		if (!displayFromName() && !displayForwardedFrom()) {
			if (QRect(trect.x(), trect.y(), via->width, st::msgNameFont->height).contains(point)) {
				outResult->link = via->link;
				return true;
			}
			trect.setTop(trect.top() + st::msgNameFont->height);
		}
	}
	return false;
}

bool Message::getStateText(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult,
		StateRequest request) const {
	if (!hasVisibleText()) {
		return false;
	}
	const auto item = message();
	if (base::in_range(point.y(), trect.y(), trect.y() + trect.height())) {
		*outResult = TextState(item, item->_text.getState(
			point - trect.topLeft(),
			trect.width(),
			request.forText()));
		return true;
	}
	return false;
}

// Forward to media.
void Message::updatePressed(QPoint point) {
	const auto item = message();
	const auto media = this->media();
	if (!media) return;

	auto g = countGeometry();
	auto keyboard = item->inlineReplyKeyboard();
	if (keyboard) {
		auto keyboardHeight = st::msgBotKbButton.margin + keyboard->naturalHeight();
		g.setHeight(g.height() - keyboardHeight);
	}

	if (drawBubble()) {
		auto mediaDisplayed = media && media->isDisplayed();
		auto trect = g.marginsAdded(-st::msgPadding);
		if (mediaDisplayed && media->isBubbleTop()) {
			trect.setY(trect.y() - st::msgPadding.top());
		} else {
			if (displayFromName()) {
				trect.setTop(trect.top() + st::msgNameFont->height);
			}
			if (displayForwardedFrom()) {
				auto forwarded = item->Get<HistoryMessageForwarded>();
				auto fwdheight = ((forwarded->text.maxWidth() > trect.width()) ? 2 : 1) * st::semiboldFont->height;
				trect.setTop(trect.top() + fwdheight);
			}
			if (item->Get<HistoryMessageReply>()) {
				auto h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
				trect.setTop(trect.top() + h);
			}
			if (const auto via = item->Get<HistoryMessageVia>()) {
				if (!displayFromName() && !displayForwardedFrom()) {
					trect.setTop(trect.top() + st::msgNameFont->height);
				}
			}
		}
		if (mediaDisplayed && media->isBubbleBottom()) {
			trect.setHeight(trect.height() + st::msgPadding.bottom());
		}

		if (mediaDisplayed) {
			auto mediaHeight = media->height();
			auto mediaLeft = trect.x() - st::msgPadding.left();
			auto mediaTop = (trect.y() + trect.height() - mediaHeight);
			media->updatePressed(point - QPoint(mediaLeft, mediaTop));
		}
	} else {
		media->updatePressed(point - g.topLeft());
	}
}

TextForMimeData Message::selectedText(TextSelection selection) const {
	const auto item = message();
	const auto media = this->media();

	auto logEntryOriginalResult = TextForMimeData();
	auto textResult = item->_text.toTextForMimeData(selection);
	auto skipped = skipTextSelection(selection);
	auto mediaDisplayed = (media && media->isDisplayed());
	auto mediaResult = (mediaDisplayed || isHiddenByGroup())
		? media->selectedText(skipped)
		: TextForMimeData();
	if (auto entry = logEntryOriginal()) {
		const auto originalSelection = mediaDisplayed
			? media->skipSelection(skipped)
			: skipped;
		logEntryOriginalResult = entry->selectedText(originalSelection);
	}
	auto result = textResult;
	if (result.empty()) {
		result = std::move(mediaResult);
	} else if (!mediaResult.empty()) {
		result.append(qstr("\n\n")).append(std::move(mediaResult));
	}
	if (result.empty()) {
		result = std::move(logEntryOriginalResult);
	} else if (!logEntryOriginalResult.empty()) {
		result.append(qstr("\n\n")).append(std::move(logEntryOriginalResult));
	}
	return result;
}

TextSelection Message::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	const auto item = message();
	const auto media = this->media();

	auto result = item->_text.adjustSelection(selection, type);
	auto beforeMediaLength = item->_text.length();
	if (selection.to <= beforeMediaLength) {
		return result;
	}
	auto mediaDisplayed = media && media->isDisplayed();
	if (mediaDisplayed) {
		auto mediaSelection = unskipTextSelection(
			media->adjustSelection(skipTextSelection(selection), type));
		if (selection.from >= beforeMediaLength) {
			result = mediaSelection;
		} else {
			result.to = mediaSelection.to;
		}
	}
	auto beforeEntryLength = beforeMediaLength
		+ (mediaDisplayed ? media->fullSelectionLength() : 0);
	if (selection.to <= beforeEntryLength) {
		return result;
	}
	if (const auto entry = logEntryOriginal()) {
		auto entrySelection = mediaDisplayed
			? media->skipSelection(skipTextSelection(selection))
			: skipTextSelection(selection);
		auto logEntryOriginalSelection = entry->adjustSelection(entrySelection, type);
		if (mediaDisplayed) {
			logEntryOriginalSelection = media->unskipSelection(logEntryOriginalSelection);
		}
		logEntryOriginalSelection = unskipTextSelection(logEntryOriginalSelection);
		if (selection.from >= beforeEntryLength) {
			result = logEntryOriginalSelection;
		} else {
			result.to = logEntryOriginalSelection.to;
		}
	}
	return result;
}

Reactions::ButtonParameters Message::reactionButtonParameters(
		QPoint position,
		const TextState &reactionState) const {
	using namespace Reactions;
	auto result = ButtonParameters{ .context = data()->fullId() };
	const auto outbg = hasOutLayout();
	const auto outsideBubble = (!_comments && !embedReactionsInBubble());
	const auto geometry = countGeometry();
	result.pointer = position;
	const auto onTheLeft = (outbg && !delegate()->elementIsChatWide());

	const auto keyboard = data()->inlineReplyKeyboard();
	const auto keyboardHeight = keyboard
		? (st::msgBotKbButton.margin + keyboard->naturalHeight())
		: 0;
	const auto reactionsHeight = (_reactions && !embedReactionsInBubble())
		? (st::mediaInBubbleSkip + _reactions->height())
		: 0;
	const auto innerHeight = geometry.height()
		- keyboardHeight
		- reactionsHeight;
	const auto maybeRelativeCenter = outsideBubble
		? media()->reactionButtonCenterOverride()
		: std::nullopt;
	const auto addOnTheRight = [&] {
		return (maybeRelativeCenter
			|| !(displayFastShare() || displayGoToOriginal()))
			? st::reactionCornerCenter.x()
			: 0;
	};
	const auto relativeCenter = QPoint(
		maybeRelativeCenter.value_or(onTheLeft
			? -st::reactionCornerCenter.x()
			: (geometry.width() + addOnTheRight())),
		innerHeight + st::reactionCornerCenter.y());
	result.center = geometry.topLeft() + relativeCenter;
	if (reactionState.itemId != result.context
		&& !geometry.contains(position)) {
		result.outside = true;
	}
	const auto minSkip = (st::reactionCornerShadow.left()
		+ st::reactionCornerSize.width()
		+ st::reactionCornerShadow.right()) / 2;
	result.center = QPoint(
		std::min(std::max(result.center.x(), minSkip), width() - minSkip),
		result.center.y());
	return result;
}

void Message::drawInfo(
		Painter &p,
		const PaintContext &context,
		int right,
		int bottom,
		int width,
		InfoDisplayType type) const {
	p.setFont(st::msgDateFont);

	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto stm = context.messageStyle();
	bool invertedsprites = (type == InfoDisplayType::Image)
		|| (type == InfoDisplayType::Background);
	int32 infoRight = right, infoBottom = bottom;
	switch (type) {
	case InfoDisplayType::Default:
		infoRight -= st::msgPadding.right() - st::msgDateDelta.x();
		infoBottom -= st::msgPadding.bottom() - st::msgDateDelta.y();
		p.setPen(stm->msgDateFg);
	break;
	case InfoDisplayType::Image:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
		p.setPen(st->msgDateImgFg());
	break;
	case InfoDisplayType::Background:
		infoRight -= st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgPadding.y();
		p.setPen(st->msgServiceFg());
	break;
	}

	const auto size = _bottomInfo.currentSize();
	const auto dateX = infoRight - size.width();
	const auto dateY = infoBottom - size.height();
	if (type == InfoDisplayType::Image) {
		const auto dateW = size.width() + 2 * st::msgDateImgPadding.x();
		const auto dateH = size.height() + 2 * st::msgDateImgPadding.y();
		Ui::FillRoundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, sti->msgDateImgBg, sti->msgDateImgBgCorners);
	} else if (type == InfoDisplayType::Background) {
		const auto dateW = size.width() + 2 * st::msgDateImgPadding.x();
		const auto dateH = size.height() + 2 * st::msgDateImgPadding.y();
		Ui::FillRoundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, sti->msgServiceBg, sti->msgServiceBgCorners);
	}
	_bottomInfo.paint(
		p,
		{ dateX, dateY },
		width,
		delegate()->elementShownUnread(this),
		invertedsprites,
		context);
}

TextState Message::bottomInfoTextState(
		int right,
		int bottom,
		QPoint point,
		InfoDisplayType type) const {
	auto infoRight = right;
	auto infoBottom = bottom;
	switch (type) {
	case InfoDisplayType::Default:
		infoRight -= st::msgPadding.right() - st::msgDateDelta.x();
		infoBottom -= st::msgPadding.bottom() - st::msgDateDelta.y();
		break;
	case InfoDisplayType::Image:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
		break;
	case InfoDisplayType::Background:
		infoRight -= st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgPadding.y();
		break;
	}
	const auto size = _bottomInfo.currentSize();
	const auto infoLeft = infoRight - size.width();
	const auto infoTop = infoBottom - size.height();
	return _bottomInfo.textState(
		data(),
		point - QPoint{ infoLeft, infoTop });
}

int Message::infoWidth() const {
	return _bottomInfo.optimalSize().width();
}

int Message::bottomInfoFirstLineWidth() const {
	return _bottomInfo.firstLineWidth();
}

bool Message::bottomInfoIsWide() const {
	if (_reactions && embedReactionsInBubble()) {
		return false;
	}
	return _bottomInfo.isWide();
}

bool Message::isSignedAuthorElided() const {
	return _bottomInfo.isSignedAuthorElided();
}

bool Message::embedReactionsInBottomInfo() const {
	return data()->history()->peer->isUser();
}

bool Message::embedReactionsInBubble() const {
	return needInfoDisplay();
}

void Message::refreshReactions() {
	const auto item = data();
	const auto &list = item->reactions();
	if (list.empty() || embedReactionsInBottomInfo()) {
		_reactions = nullptr;
		return;
	}
	using namespace Reactions;
	auto reactionsData = InlineListDataFromMessage(this);
	if (!_reactions) {
		const auto handlerFactory = [=](QString emoji) {
			const auto fullId = data()->fullId();
			return std::make_shared<LambdaClickHandler>([=](
					ClickContext context) {
				const auto my = context.other.value<ClickHandlerContext>();
				if (const auto controller = my.sessionWindow.get()) {
					const auto &data = controller->session().data();
					if (const auto item = data.message(fullId)) {
						item->toggleReaction(emoji);
					}
				}
			});
		};
		_reactions = std::make_unique<InlineList>(
			&item->history()->owner().reactions(),
			handlerFactory,
			std::move(reactionsData));
	} else {
		_reactions->update(std::move(reactionsData), width());
	}
}

void Message::itemDataChanged() {
	const auto wasInfo = _bottomInfo.currentSize();
	const auto wasReactions = _reactions
		? _reactions->currentSize()
		: QSize();
	refreshReactions();
	_bottomInfo.update(BottomInfoDataFromMessage(this), width());
	const auto nowInfo = _bottomInfo.currentSize();
	const auto nowReactions = _reactions
		? _reactions->currentSize()
		: QSize();
	if (wasInfo != nowInfo || wasReactions != nowReactions) {
		history()->owner().requestViewResize(this);
	} else {
		history()->owner().requestViewRepaint(this);
	}
}

auto Message::verticalRepaintRange() const -> VerticalRepaintRange {
	const auto media = this->media();
	const auto add = media ? media->bubbleRollRepaintMargins() : QMargins();
	return {
		.top = -add.top(),
		.height = height() + add.top() + add.bottom()
	};
}

void Message::refreshDataIdHook() {
	if (base::take(_rightActionLink)) {
		_rightActionLink = rightActionLink();
	}
	if (base::take(_fastReplyLink)) {
		_fastReplyLink = fastReplyLink();
	}
	if (_comments) {
		_comments->link = nullptr;
	}
}

int Message::plainMaxWidth() const {
	return st::msgPadding.left()
		+ (hasVisibleText() ? message()->_text.maxWidth() : 0)
		+ st::msgPadding.right();
}

int Message::monospaceMaxWidth() const {
	return st::msgPadding.left()
		+ (hasVisibleText() ? message()->_text.countMaxMonospaceWidth() : 0)
		+ st::msgPadding.right();
}

int Message::viewButtonHeight() const {
	return _viewButton ? _viewButton->height() : 0;
}

void Message::updateViewButtonExistence() {
	const auto item = data();
	const auto sponsored = item->Get<HistoryMessageSponsored>();
	const auto media = sponsored ? nullptr : item->media();
	const auto has = sponsored
		|| (media && ViewButton::MediaHasViewButton(media));
	if (!has) {
		_viewButton = nullptr;
		return;
	} else if (_viewButton) {
		return;
	}
	auto callback = [=] { history()->owner().requestViewRepaint(this); };
	_viewButton = sponsored
		? std::make_unique<ViewButton>(sponsored, std::move(callback))
		: std::make_unique<ViewButton>(media, std::move(callback));
}

void Message::initLogEntryOriginal() {
	if (const auto log = message()->Get<HistoryMessageLogEntryOriginal>()) {
		AddComponents(LogEntryOriginal::Bit());
		const auto entry = Get<LogEntryOriginal>();
		entry->page = std::make_unique<WebPage>(this, log->page);
	}
}

void Message::initPsa() {
	if (const auto forwarded = message()->Get<HistoryMessageForwarded>()) {
		if (!forwarded->psaType.isEmpty()) {
			AddComponents(PsaTooltipState::Bit());
			Get<PsaTooltipState>()->type = forwarded->psaType;
		}
	}
}

WebPage *Message::logEntryOriginal() const {
	if (const auto entry = Get<LogEntryOriginal>()) {
		return entry->page.get();
	}
	return nullptr;
}

HistoryMessageReply *Message::displayedReply() const {
	if (const auto reply = data()->Get<HistoryMessageReply>()) {
		return delegate()->elementHideReply(this) ? nullptr : reply;
	}
	return nullptr;
}

bool Message::toggleSelectionByHandlerClick(
		const ClickHandlerPtr &handler) const {
	if (_comments && _comments->link == handler) {
		return true;
	} else if (_viewButton && _viewButton->link() == handler) {
		return true;
	} else if (const auto media = this->media()) {
		if (media->toggleSelectionByHandlerClick(handler)) {
			return true;
		}
	}
	return false;
}

bool Message::hasFromName() const {
	switch (context()) {
	case Context::AdminLog:
		return true;
	case Context::History:
	case Context::Pinned:
	case Context::Replies: {
		const auto item = message();
		const auto peer = item->history()->peer;
		if (hasOutLayout() && !item->from()->isChannel()) {
			return false;
		} else if (!peer->isUser()) {
			return true;
		}
		if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
			if (forwarded->imported
				&& peer.get() == forwarded->originalSender) {
				return false;
			} else if (showForwardsFromSender(forwarded)) {
				return true;
			}
		}
		return false;
	} break;
	case Context::ContactPreview:
		return false;
	}
	Unexpected("Context in Message::hasFromPhoto.");
}

bool Message::displayFromName() const {
	if (!hasFromName() || isAttachedToPrevious()) {
		return false;
	}
	return !Has<PsaTooltipState>();
}

bool Message::displayForwardedFrom() const {
	const auto item = message();
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		if (showForwardsFromSender(forwarded)) {
			return false;
		}
		if (const auto sender = item->discussionPostOriginalSender()) {
			if (sender == forwarded->originalSender) {
				return false;
			}
		}
		const auto media = item->media();
		return !media || !media->dropForwardedInfo();
	}
	return false;
}

bool Message::hasOutLayout() const {
	const auto item = message();
	if (item->history()->peer->isSelf()) {
		return !item->Has<HistoryMessageForwarded>();
	} else if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		if (!forwarded->imported
			|| !forwarded->originalSender
			|| !forwarded->originalSender->isSelf()) {
			if (showForwardsFromSender(forwarded)) {
				return false;
			}
		}
	}
	return item->out() && !item->isPost();
}

bool Message::drawBubble() const {
	const auto item = message();
	if (isHidden()) {
		return false;
	} else if (logEntryOriginal()) {
		return true;
	}
	const auto media = this->media();
	return media
		? (hasVisibleText() || media->needsBubble())
		: !item->isEmpty();
}

bool Message::hasBubble() const {
	return drawBubble();
}

int Message::minWidthForMedia() const {
	auto result = infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x());
	const auto views = data()->Get<HistoryMessageViews>();
	if (data()->repliesAreComments() && !views->replies.text.isEmpty()) {
		const auto limit = HistoryMessageViews::kMaxRecentRepliers;
		const auto single = st::historyCommentsUserpics.size;
		const auto shift = st::historyCommentsUserpics.shift;
		const auto added = single
			+ (limit - 1) * (single - shift)
			+ st::historyCommentsSkipLeft
			+ st::historyCommentsSkipRight
			+ st::historyCommentsSkipText
			+ st::historyCommentsOpenOutSelected.width()
			+ st::historyCommentsSkipRight
			+ st::mediaUnreadSkip
			+ st::mediaUnreadSize;
		accumulate_max(result, added + views->replies.textWidth);
	} else if (data()->externalReply()) {
		const auto added = st::historyCommentsIn.width()
			+ st::historyCommentsSkipLeft
			+ st::historyCommentsSkipRight
			+ st::historyCommentsSkipText
			+ st::historyCommentsOpenOutSelected.width()
			+ st::historyCommentsSkipRight;
		accumulate_max(result, added + st::semiboldFont->width(
			tr::lng_replies_view_original(tr::now)));
	}
	return result;
}

bool Message::hasFastReply() const {
	if (context() == Context::Replies) {
		if (data()->isDiscussionPost()) {
			return false;
		}
	} else if (context() != Context::History) {
		return false;
	}
	const auto peer = data()->history()->peer;
	return !hasOutLayout() && (peer->isChat() || peer->isMegagroup());
}

bool Message::displayFastReply() const {
	return hasFastReply()
		&& data()->isRegular()
		&& data()->history()->peer->canWrite()
		&& !delegate()->elementInSelectionMode();
}

bool Message::displayRightActionComments() const {
	return !isPinnedContext()
		&& data()->repliesAreComments()
		&& media()
		&& media()->isDisplayed()
		&& !hasBubble();
}

std::optional<QSize> Message::rightActionSize() const {
	if (displayRightActionComments()) {
		const auto views = data()->Get<HistoryMessageViews>();
		Assert(views != nullptr);
		return (views->repliesSmall.textWidth > 0)
			? QSize(
				std::max(
					st::historyFastShareSize,
					2 * st::historyFastShareBottom + views->repliesSmall.textWidth),
				st::historyFastShareSize + st::historyFastShareBottom + st::semiboldFont->height)
			: QSize(st::historyFastShareSize, st::historyFastShareSize);
	}
	return (displayFastShare() || displayGoToOriginal())
		? QSize(st::historyFastShareSize, st::historyFastShareSize)
		: std::optional<QSize>();
}

bool Message::displayFastShare() const {
	const auto item = message();
	const auto peer = item->history()->peer;
	if (!item->allowsForward()) {
		return false;
	} else if (peer->isChannel()) {
		return !peer->isMegagroup();
	} else if (const auto user = peer->asUser()) {
		if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
			return !showForwardsFromSender(forwarded)
				&& !item->out()
				&& forwarded->originalSender
				&& forwarded->originalSender->isChannel()
				&& !forwarded->originalSender->isMegagroup();
		} else if (user->isBot() && !item->out()) {
			if (const auto media = this->media()) {
				return media->allowsFastShare();
			}
		}
	}
	return false;
}

bool Message::displayGoToOriginal() const {
	if (isPinnedContext()) {
		return !hasOutLayout();
	}
	const auto item = message();
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		return forwarded->savedFromPeer
			&& forwarded->savedFromMsgId
			&& (!item->externalReply() || !hasBubble())
			&& !(context() == Context::Replies);
	}
	return false;
}

void Message::drawRightAction(
		Painter &p,
		const PaintContext &context,
		int left,
		int top,
		int outerWidth) const {
	const auto size = rightActionSize();
	const auto st = context.st;
	p.setPen(Qt::NoPen);
	p.setBrush(st->msgServiceBg());
	{
		PainterHighQualityEnabler hq(p);
		const auto rect = style::rtlrect(
			left,
			top,
			size->width(),
			size->height(),
			outerWidth);
		const auto usual = st::historyFastShareSize;
		if (size->width() == size->height() && size->width() == usual) {
			p.drawEllipse(rect);
		} else {
			p.drawRoundedRect(rect, usual / 2, usual / 2);
		}
	}
	if (displayRightActionComments()) {
		const auto &icon = st->historyFastCommentsIcon();
		icon.paint(
			p,
			left + (size->width() - icon.width()) / 2,
			top + (st::historyFastShareSize - icon.height()) / 2,
			outerWidth);
		const auto views = data()->Get<HistoryMessageViews>();
		Assert(views != nullptr);
		if (views->repliesSmall.textWidth > 0) {
			p.setPen(st->msgServiceFg());
			p.setFont(st::semiboldFont);
			p.drawTextLeft(
				left + (size->width() - views->repliesSmall.textWidth) / 2,
				top + st::historyFastShareSize,
				outerWidth,
				views->repliesSmall.text,
				views->repliesSmall.textWidth);
		}
	} else {
		const auto &icon = (displayFastShare() && !isPinnedContext())
			? st->historyFastShareIcon()
			: st->historyGoToOriginalIcon();
		icon.paintInCenter(p, { left, top, size->width(), size->height() });
	}
}

ClickHandlerPtr Message::rightActionLink() const {
	if (_rightActionLink) {
		return _rightActionLink;
	}
	if (isPinnedContext()) {
		_rightActionLink = goToMessageClickHandler(data());
		return _rightActionLink;
	} else if (displayRightActionComments()) {
		_rightActionLink = createGoToCommentsLink();
		return _rightActionLink;
	}
	const auto sessionId = data()->history()->session().uniqueId();
	const auto owner = &data()->history()->owner();
	const auto itemId = data()->fullId();
	const auto forwarded = data()->Get<HistoryMessageForwarded>();
	const auto savedFromPeer = forwarded
		? forwarded->savedFromPeer
		: nullptr;
	const auto savedFromMsgId = forwarded ? forwarded->savedFromMsgId : 0;

	using Callback = FnMut<void(not_null<Window::SessionController*>)>;
	const auto showByThread = std::make_shared<Callback>();
	const auto showByThreadWeak = std::weak_ptr<Callback>(showByThread);
	if (data()->externalReply()) {
		*showByThread = [=, requested = 0](
				not_null<Window::SessionController*> controller) mutable {
			const auto original = savedFromPeer->owner().message(
				savedFromPeer,
				savedFromMsgId);
			if (original && original->replyToTop()) {
				controller->showRepliesForMessage(
					original->history(),
					original->replyToTop(),
					original->id,
					Window::SectionShow::Way::Forward);
			} else if (!requested) {
				const auto prequested = &requested;
				requested = 1;
				savedFromPeer->session().api().requestMessageData(
					savedFromPeer,
					savedFromMsgId,
					[=, weak = base::make_weak(controller.get())] {
						if (const auto strong = showByThreadWeak.lock()) {
							if (const auto strongController = weak.get()) {
								*prequested = 2;
								(*strong)(strongController);
							}
						}
					});
			} else if (requested == 2) {
				controller->showPeerHistory(
					savedFromPeer,
					Window::SectionShow::Way::Forward,
					savedFromMsgId);
			}
		};
	};
	_rightActionLink = std::make_shared<LambdaClickHandler>([=](
			ClickContext context) {
		const auto controller = ExtractController(context).value_or(nullptr);
		if (!controller) {
			return;
		}
		if (controller->session().uniqueId() != sessionId) {
			return;
		}

		if (const auto item = owner->message(itemId)) {
			if (*showByThread) {
				(*showByThread)(controller);
			} else if (savedFromPeer && savedFromMsgId) {
				controller->showPeerHistory(
					savedFromPeer,
					Window::SectionShow::Way::Forward,
					savedFromMsgId);
			} else {
				FastShareMessage(item);
			}
		}
	});
	return _rightActionLink;
}

ClickHandlerPtr Message::fastReplyLink() const {
	if (_fastReplyLink) {
		return _fastReplyLink;
	}
	const auto itemId = data()->fullId();
	_fastReplyLink = std::make_shared<LambdaClickHandler>([=] {
		delegate()->elementReplyTo(itemId);
	});
	return _fastReplyLink;
}

bool Message::isPinnedContext() const {
	return context() == Context::Pinned;
}

void Message::updateMediaInBubbleState() {
	const auto item = message();
	const auto media = this->media();

	if (media) {
		media->updateNeedBubbleState();
	}
	const auto reactionsInBubble = (_reactions && embedReactionsInBubble());
	auto mediaHasSomethingBelow = (_viewButton != nullptr)
		|| reactionsInBubble;
	auto mediaHasSomethingAbove = false;
	auto getMediaHasSomethingAbove = [&] {
		return displayFromName()
			|| displayForwardedFrom()
			|| displayedReply()
			|| item->Has<HistoryMessageVia>();
	};
	auto entry = logEntryOriginal();
	if (entry) {
		mediaHasSomethingBelow = true;
		mediaHasSomethingAbove = getMediaHasSomethingAbove();
		auto entryState = (mediaHasSomethingAbove
			|| hasVisibleText()
			|| (media && media->isDisplayed()))
			? MediaInBubbleState::Bottom
			: MediaInBubbleState::None;
		entry->setInBubbleState(entryState);
	}
	if (!media) {
		return;
	}

	if (!drawBubble()) {
		media->setInBubbleState(MediaInBubbleState::None);
		return;
	}

	if (!entry) {
		mediaHasSomethingAbove = getMediaHasSomethingAbove();
	}
	if (hasVisibleText()) {
		mediaHasSomethingAbove = true;
	}
	const auto state = [&] {
		if (mediaHasSomethingAbove) {
			if (mediaHasSomethingBelow) {
				return MediaInBubbleState::Middle;
			}
			return MediaInBubbleState::Bottom;
		} else if (mediaHasSomethingBelow) {
			return MediaInBubbleState::Top;
		}
		return MediaInBubbleState::None;
	}();
	media->setInBubbleState(state);
}

void Message::fromNameUpdated(int width) const {
	const auto item = message();
	const auto replyWidth = hasFastReply()
		? st::msgFont->width(FastReplyText())
		: 0;
	if (!_rightBadge.isEmpty()) {
		const auto badgeWidth = _rightBadge.maxWidth();
		width -= st::msgPadding.right() + std::max(badgeWidth, replyWidth);
	} else if (replyWidth) {
		width -= st::msgPadding.right() + replyWidth;
	}
	const auto from = item->displayFrom();
	item->_fromNameVersion = from ? from->nameVersion : 1;
	if (const auto via = item->Get<HistoryMessageVia>()) {
		if (!displayForwardedFrom()) {
			const auto nameText = [&]() -> const Ui::Text::String * {
				if (from) {
					return &from->nameText();
				} else if (const auto info = item->hiddenSenderInfo()) {
					return &info->nameText;
				} else {
					Unexpected("Corrupted forwarded information in message.");
				}
			}();
			via->resize(width
				- st::msgPadding.left()
				- st::msgPadding.right()
				- nameText->maxWidth()
				- st::msgServiceFont->spacew);
		}
	}
}

TextSelection Message::skipTextSelection(TextSelection selection) const {
	if (selection.from == 0xFFFF) {
		return selection;
	}
	return HistoryView::UnshiftItemSelection(selection, message()->_text);
}

TextSelection Message::unskipTextSelection(TextSelection selection) const {
	return HistoryView::ShiftItemSelection(selection, message()->_text);
}

QRect Message::countGeometry() const {
	const auto commentsRoot = (context() == Context::Replies)
		&& data()->isDiscussionPost();
	const auto media = this->media();
	const auto mediaWidth = (media && media->isDisplayed())
		? media->width()
		: width();
	const auto outbg = hasOutLayout();
	const auto availableWidth = width()
		- st::msgMargin.left()
		- (commentsRoot ? st::msgMargin.left() : st::msgMargin.right());
	auto contentLeft = (outbg && !delegate()->elementIsChatWide())
		? st::msgMargin.right()
		: st::msgMargin.left();
	auto contentWidth = availableWidth;
	if (hasFromPhoto()) {
		contentLeft += st::msgPhotoSkip;
		if (const auto size = rightActionSize()) {
			contentWidth -= size->width() + (st::msgPhotoSkip - st::historyFastShareSize);
		}
	//} else if (!Adaptive::Wide() && !out() && !fromChannel() && st::msgPhotoSkip - (hmaxwidth - hwidth) > 0) {
	//	contentLeft += st::msgPhotoSkip - (hmaxwidth - hwidth);
	}
	accumulate_min(contentWidth, maxWidth());
	if (!AdaptiveBubbles()) {
		accumulate_min(contentWidth, _bubbleWidthLimit);
	}
	if (mediaWidth < contentWidth) {
		const auto textualWidth = plainMaxWidth();
		if (mediaWidth < textualWidth
			&& (!media || !media->enforceBubbleWidth())) {
			accumulate_min(contentWidth, textualWidth);
		} else {
			contentWidth = mediaWidth;
		}
	}
	if (contentWidth < availableWidth
		&& (!delegate()->elementIsChatWide()
			|| (commentsRoot && AdaptiveBubbles()))) {
		if (outbg) {
			contentLeft += availableWidth - contentWidth;
		} else if (commentsRoot) {
			contentLeft += (availableWidth - contentWidth) / 2;
		}
	} else if (contentWidth < availableWidth && commentsRoot) {
		contentLeft += ((st::msgMaxWidth + 2 * st::msgPhotoSkip) - contentWidth) / 2;
	}

	const auto contentTop = marginTop();
	return QRect(
		contentLeft,
		contentTop,
		contentWidth,
		height() - contentTop - marginBottom());
}

int Message::resizeContentGetHeight(int newWidth) {
	if (isHidden()) {
		return marginTop() + marginBottom();
	} else if (newWidth < st::msgMinWidth) {
		return height();
	}

	auto newHeight = minHeight();

	const auto item = message();
	const auto media = this->media();
	const auto mediaDisplayed = media ? media->isDisplayed() : false;
	const auto bubble = drawBubble();

	// This code duplicates countGeometry() but also resizes media.
	const auto commentsRoot = (context() == Context::Replies)
		&& data()->isDiscussionPost();
	auto contentWidth = newWidth
		- st::msgMargin.left()
		- (commentsRoot ? st::msgMargin.left() : st::msgMargin.right());
	if (hasFromPhoto()) {
		if (const auto size = rightActionSize()) {
			contentWidth -= size->width() + (st::msgPhotoSkip - st::historyFastShareSize);
		}
	}
	accumulate_min(contentWidth, maxWidth());
	_bubbleWidthLimit = (MonospaceLargeBubbles()
		? std::max(st::msgMaxWidth, monospaceMaxWidth())
		: st::msgMaxWidth);
	if (!AdaptiveBubbles()) {
		accumulate_min(contentWidth, _bubbleWidthLimit);
	}
	if (mediaDisplayed) {
		media->resizeGetHeight(contentWidth);
		if (media->width() < contentWidth) {
			const auto textualWidth = plainMaxWidth();
			if (media->width() < textualWidth
				&& !media->enforceBubbleWidth()) {
				accumulate_min(contentWidth, textualWidth);
			} else {
				contentWidth = media->width();
			}
		}
	}
	const auto textWidth = qMax(contentWidth - st::msgPadding.left() - st::msgPadding.right(), 1);
	const auto reactionsInBubble = _reactions && embedReactionsInBubble();
	const auto bottomInfoHeight = _bottomInfo.resizeGetHeight(
		std::min(
			_bottomInfo.optimalSize().width(),
			textWidth - 2 * st::msgDateDelta.x()));

	if (bubble) {
		auto reply = displayedReply();
		auto via = item->Get<HistoryMessageVia>();
		auto entry = logEntryOriginal();

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && media->isBubbleBottom()) || (entry/* && entry->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && media->isBubbleTop()) || (entry && entry->isBubbleTop());

		if (reactionsInBubble) {
			_reactions->resizeGetHeight(textWidth);
		}

		if (!AdaptiveBubbles() && contentWidth == maxWidth()) {
			if (mediaDisplayed) {
				if (entry) {
					newHeight += entry->resizeGetHeight(contentWidth);
				}
			} else if (entry) {
				// In case of text-only message it is counted in minHeight already.
				entry->resizeGetHeight(contentWidth);
			}
		} else {
			if (hasVisibleText()) {
				if (textWidth != item->_textWidth) {
					item->_textWidth = textWidth;
					item->_textHeight = item->_text.countHeight(textWidth);
				}
				newHeight = item->_textHeight;
			} else {
				newHeight = 0;
			}
			if (!mediaOnBottom && (!_viewButton || !reactionsInBubble)) {
				newHeight += st::msgPadding.bottom();
				if (mediaDisplayed) {
					newHeight += st::mediaInBubbleSkip;
				}
			}
			if (!mediaOnTop) {
				newHeight += st::msgPadding.top();
				if (mediaDisplayed) newHeight += st::mediaInBubbleSkip;
				if (entry) newHeight += st::mediaInBubbleSkip;
			}
			if (mediaDisplayed) {
				newHeight += media->height();
				if (entry) {
					newHeight += entry->resizeGetHeight(contentWidth);
				}
			} else if (entry) {
				newHeight += entry->resizeGetHeight(contentWidth);
			}
			if (reactionsInBubble) {
				if (!mediaDisplayed) {
					newHeight += st::mediaInBubbleSkip;
				}
				newHeight += _reactions->height();
			}
		}

		if (displayFromName()) {
			fromNameUpdated(contentWidth);
			newHeight += st::msgNameFont->height;
		} else if (via && !displayForwardedFrom()) {
			via->resize(contentWidth - st::msgPadding.left() - st::msgPadding.right());
			newHeight += st::msgNameFont->height;
		}

		if (displayForwardedFrom()) {
			const auto forwarded = item->Get<HistoryMessageForwarded>();
			const auto skip1 = forwarded->psaType.isEmpty()
				? 0
				: st::historyPsaIconSkip1;
			const auto fwdheight = ((forwarded->text.maxWidth() > (contentWidth - st::msgPadding.left() - st::msgPadding.right() - skip1)) ? 2 : 1) * st::semiboldFont->height;
			newHeight += fwdheight;
		}

		if (reply) {
			reply->resize(contentWidth - st::msgPadding.left() - st::msgPadding.right());
			newHeight += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		}
		if (needInfoDisplay()) {
			newHeight += (bottomInfoHeight - st::msgDateFont->height);
		}

		if (item->repliesAreComments() || item->externalReply()) {
			newHeight += st::historyCommentsButtonHeight;
		} else if (_comments) {
			_comments = nullptr;
			checkHeavyPart();
		}
		newHeight += viewButtonHeight();
	} else if (mediaDisplayed) {
		newHeight = media->height();
	} else {
		newHeight = 0;
	}
	if (_reactions && !reactionsInBubble) {
		const auto reactionsWidth = (!bubble && mediaDisplayed)
			? media->contentRectForReactions().width()
			: contentWidth;
		newHeight += st::mediaInBubbleSkip
			+ _reactions->resizeGetHeight(reactionsWidth);
		if (hasOutLayout() && !delegate()->elementIsChatWide()) {
			_reactions->flipToRight();
		}
	}

	if (const auto keyboard = item->inlineReplyKeyboard()) {
		const auto keyboardHeight = st::msgBotKbButton.margin + keyboard->naturalHeight();
		newHeight += keyboardHeight;
		keyboard->resize(contentWidth, keyboardHeight - st::msgBotKbButton.margin);
	}

	newHeight += marginTop() + marginBottom();
	return newHeight;
}

bool Message::needInfoDisplay() const {
	const auto media = this->media();
	const auto mediaDisplayed = media ? media->isDisplayed() : false;
	const auto entry = logEntryOriginal();
	return entry
		? !entry->customInfoLayout()
		: (mediaDisplayed
			? !media->customInfoLayout()
			: true);
}

bool Message::hasVisibleText() const {
	if (message()->emptyText()) {
		return false;
	}
	const auto media = this->media();
	return !media || !media->hideMessageText();
}

QSize Message::performCountCurrentSize(int newWidth) {
	const auto newHeight = resizeContentGetHeight(newWidth);

	return { newWidth, newHeight };
}

void Message::refreshInfoSkipBlock() {
	const auto item = message();
	const auto media = this->media();
	const auto hasTextSkipBlock = [&] {
		if (item->_text.isEmpty()) {
			return false;
		} else if (item->Has<HistoryMessageLogEntryOriginal>()) {
			return false;
		} else if (media && media->isDisplayed()) {
			return false;
		} else if (_reactions) {
			return false;
		}
		return true;
	}();
	const auto skipWidth = skipBlockWidth();
	const auto skipHeight = skipBlockHeight();
	if (_reactions) {
		if (needInfoDisplay()) {
			_reactions->updateSkipBlock(skipWidth, skipHeight);
		} else {
			_reactions->removeSkipBlock();
		}
	}
	if (!hasTextSkipBlock) {
		if (item->_text.removeSkipBlock()) {
			item->_textWidth = -1;
			item->_textHeight = 0;
		}
	} else if (item->_text.updateSkipBlock(skipWidth, skipHeight)) {
		item->_textWidth = -1;
		item->_textHeight = 0;
	}
}

bool Message::displayEditedBadge() const {
	return (displayedEditDate() != TimeId(0));
}

TimeId Message::displayedEditDate() const {
	const auto item = message();
	const auto overrided = media() && media()->overrideEditedDate();
	if (item->hideEditedBadge() && !overrided) {
		return TimeId(0);
	} else if (const auto edited = displayedEditBadge()) {
		return edited->date;
	}
	return TimeId(0);
}

HistoryMessageEdited *Message::displayedEditBadge() {
	if (const auto media = this->media()) {
		if (media->overrideEditedDate()) {
			return media->displayedEditBadge();
		}
	}
	return message()->Get<HistoryMessageEdited>();
}

const HistoryMessageEdited *Message::displayedEditBadge() const {
	if (const auto media = this->media()) {
		if (media->overrideEditedDate()) {
			return media->displayedEditBadge();
		}
	}
	return message()->Get<HistoryMessageEdited>();
}

} // namespace HistoryView
