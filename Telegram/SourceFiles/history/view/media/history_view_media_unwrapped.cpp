/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_media_unwrapped.h"

#include "history/view/media/history_view_media_common.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "data/data_session.h"
#include "layout.h"
#include "facades.h"
#include "app.h"
#include "styles/style_history.h"

namespace HistoryView {

namespace {
	constexpr auto kMaxUnwrappedForwardedBarLines = 4;
} // namespace

UnwrappedMedia::Content::~Content() = default;

UnwrappedMedia::UnwrappedMedia(
	not_null<Element*> parent,
	std::unique_ptr<Content> content)
: Media(parent)
, _content(std::move(content)) {
	StickerHeightChanges(
	) | rpl::start_with_next([=] {
		history()->owner().requestItemViewRefresh(_parent->data());
	}, _lifetime);
}

QSize UnwrappedMedia::countOptimalSize() {
	_content->refreshLink();
	_contentSize = NonEmptySize(DownscaledSize(
		_content->size(),
		{ st::maxStickerSize, StickerHeight() }));
	auto maxWidth = _contentSize.width();
	const auto minimal = st::largeEmojiSize + 2 * st::largeEmojiOutline;
	auto minHeight = std::max(_contentSize.height(), minimal);
	if (_parent->media() == this) {
		const auto item = _parent->data();
		const auto via = item->Get<HistoryMessageVia>();
		const auto reply = item->Get<HistoryMessageReply>();
		auto forwarded = item->Get<HistoryMessageForwarded>();
		if (forwarded) {
			forwarded->create();
		}
		maxWidth += additionalWidth(via, reply, forwarded);
		if (const auto surrounding = surroundingHeight(via, reply, forwarded)) {
			const auto infoHeight = st::msgDateImgPadding.y() * 2
				+ st::msgDateFont->height;
			const auto minimal = surrounding
				+ st::msgDateImgDelta
				+ infoHeight;
			minHeight = std::max(minHeight, minimal);
		}
	}
	return { maxWidth, minHeight };
}

QSize UnwrappedMedia::countCurrentSize(int newWidth) {
	const auto item = _parent->data();
	accumulate_min(newWidth, maxWidth());
	if (_parent->media() == this) {
		const auto infoWidth = _parent->infoWidth() + 2 * st::msgDateImgPadding.x();
		const auto via = item->Get<HistoryMessageVia>();
		const auto reply = item->Get<HistoryMessageReply>();
		const auto forwarded = item->Get<HistoryMessageForwarded>();
		if (via || reply || forwarded) {
			int usew = maxWidth() - additionalWidth(via, reply, forwarded);
			int availw = newWidth - usew - st::msgReplyPadding.left() - st::msgReplyPadding.left() - st::msgReplyPadding.left();
			if (via) {
				via->resize(availw);
			}
			if (reply) {
				reply->resize(availw);
			}
		}
	}
	auto newHeight = minHeight();
	if (_parent->hasOutLayout() && !Adaptive::ChatWide()) {
		// Add some height to isolated emoji for the timestamp info.
		const auto infoHeight = st::msgDateImgPadding.y() * 2
			+ st::msgDateFont->height;
		const auto minimal = st::largeEmojiSize
			+ 2 * st::largeEmojiOutline
			+ (st::msgDateImgDelta + infoHeight);
		accumulate_max(newHeight, minimal);
	}
	return { newWidth, newHeight };
}

void UnwrappedMedia::draw(
		Painter &p,
		const QRect &r,
		TextSelection selection,
		crl::time ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return;
	}
	bool selected = (selection == FullSelection);

	const auto rightAligned = _parent->hasOutLayout() && !Adaptive::ChatWide();
	const auto inWebPage = (_parent->media() != this);
	const auto item = _parent->data();
	const auto via = inWebPage ? nullptr : item->Get<HistoryMessageVia>();
	const auto reply = inWebPage ? nullptr : item->Get<HistoryMessageReply>();
	const auto forwarded = item->Get<HistoryMessageForwarded>();
	auto usex = 0;
	auto usew = maxWidth();
	if (!inWebPage) {
		usew -= additionalWidth(via, reply, forwarded);
		if (rightAligned) {
			usex = width() - usew;
		}
	}
	if (rtl()) {
		usex = width() - usex - usew;
	}

	const auto usey = rightAligned ? 0 : (height() - _contentSize.height());
	const auto useh = rightAligned
		? std::max(
			_contentSize.height(),
			height() - st::msgDateImgPadding.y() * 2 - st::msgDateFont->height)
		: _contentSize.height();
	const auto inner = QRect(usex, usey, usew, useh);
	_content->draw(p, inner, selected);

	if (!inWebPage) {
		drawSurrounding(p, inner, selected, via, reply, forwarded);
	}
}

int UnwrappedMedia::surroundingHeight(
		const HistoryMessageVia *via,
		const HistoryMessageReply *reply,
		const HistoryMessageForwarded *forwarded,
		const int innerw) const {
	if (!via && !reply && !forwarded) {
		return 0;
	}
	auto result = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
	if (forwarded) {
		if (innerw) {
			auto forwardedHeightReal = forwarded->text.countHeight(innerw);
			auto forwardedHeight = qMin(forwardedHeightReal, kMaxUnwrappedForwardedBarLines * st::msgServiceNameFont->height);
			result += forwardedHeight;
		} else {
			result += st::msgServiceNameFont->height;
		}
		result += (!via && reply ? st::msgReplyPadding.top() : 0);
	}
	if (via) {
		result += st::msgServiceNameFont->height
			+ (reply ? st::msgReplyPadding.top() : 0);
	}
	if (reply) {
		result += st::msgReplyBarSize.height();
	}
	return result;
}

void UnwrappedMedia::drawSurrounding(
		Painter &p,
		const QRect &inner,
		bool selected,
		const HistoryMessageVia *via,
		const HistoryMessageReply *reply,
		const HistoryMessageForwarded *forwarded) const {
	const auto rightAligned = _parent->hasOutLayout() && !Adaptive::ChatWide();
	const auto rightAction = _parent->displayRightAction();
	const auto fullRight = calculateFullRight(inner);
	auto fullBottom = height();
	if (needInfoDisplay()) {
		_parent->drawInfo(
			p,
			fullRight,
			fullBottom,
			inner.x() * 2 + inner.width(),
			selected,
			InfoDisplayType::Background);
	}
	auto replyRight = 0;
	int rectw = width() - inner.width() - st::msgReplyPadding.left();
	auto innerw = rectw - (st::msgReplyPadding.left() + st::msgReplyPadding.right());
	if (const auto recth = surroundingHeight(via, reply, forwarded, innerw)) {
		int rectx = rightAligned ? 0 : (inner.width() + st::msgReplyPadding.left());
		int recty = 0;
		if (rtl()) rectx = width() - rectx - rectw;

		App::roundRect(p, rectx, recty, rectw, recth, selected ? st::msgServiceBgSelected : st::msgServiceBg, selected ? StickerSelectedCorners : StickerCorners);
		p.setPen(st::msgServiceFg);
		rectx += st::msgReplyPadding.left();
		rectw -= st::msgReplyPadding.left() + st::msgReplyPadding.right();
		auto forwardedHeightReal = forwarded ? forwarded->text.countHeight(innerw) : 0;
		auto forwardedHeight = qMin(forwardedHeightReal, kMaxUnwrappedForwardedBarLines * st::msgServiceNameFont->height);
		if (forwarded) {
			p.setTextPalette(st::serviceTextPalette);
			auto breakEverywhere = (forwardedHeightReal > forwardedHeight);
			forwarded->text.drawElided(p, rectx, recty + st::msgReplyPadding.top(), rectw, kMaxUnwrappedForwardedBarLines, style::al_left, 0, -1, 0, breakEverywhere);
			p.restoreTextPalette();
			int skip = forwardedHeight + (!via && reply ? st::msgReplyPadding.top() : 0);
			recty += skip;
		}
		if (via) {
			p.setFont(st::msgDateFont);
			p.drawTextLeft(rectx, recty + st::msgReplyPadding.top(), 2 * rectx + rectw, via->text);
			int skip = st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
			recty += skip;
		}
		if (reply) {
			HistoryMessageReply::PaintFlags flags = 0;
			if (selected) {
				flags |= HistoryMessageReply::PaintFlag::Selected;
			}
			reply->paint(p, _parent, rectx, recty, rectw, flags);
		}
		replyRight = rectx + rectw;
	}
	if (rightAction) {
		const auto position = calculateFastActionPosition(
			fullBottom,
			replyRight,
			fullRight);
		const auto outer = 2 * inner.x() + inner.width();
		_parent->drawRightAction(p, position.x(), position.y(), outer);
	}
}

PointState UnwrappedMedia::pointState(QPoint point) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return PointState::Outside;
	}

	const auto rightAligned = _parent->hasOutLayout() && !Adaptive::ChatWide();
	const auto inWebPage = (_parent->media() != this);
	const auto item = _parent->data();
	const auto via = inWebPage ? nullptr : item->Get<HistoryMessageVia>();
	const auto reply = inWebPage ? nullptr : item->Get<HistoryMessageReply>();
	const auto forwarded = inWebPage ? nullptr : item->Get<HistoryMessageForwarded>();
	auto usex = 0;
	auto usew = maxWidth();
	if (!inWebPage) {
		usew -= additionalWidth(via, reply, forwarded);
		if (rightAligned) {
			usex = width() - usew;
		}
	}
	if (rtl()) {
		usex = width() - usex - usew;
	}

	const auto datey = height() - st::msgDateImgPadding.y() * 2
		- st::msgDateFont->height;
	const auto usey = rightAligned ? 0 : (height() - _contentSize.height());
	const auto useh = rightAligned
		? std::max(_contentSize.height(), datey)
		: _contentSize.height();
	const auto inner = QRect(usex, usey, usew, useh);

	// Rectangle of date bubble.
	if (point.x() < calculateFullRight(inner) && point.y() > datey) {
		return PointState::Inside;
	}

	return inner.contains(point) ? PointState::Inside : PointState::Outside;
}

TextState UnwrappedMedia::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}

	const auto rightAligned = _parent->hasOutLayout() && !Adaptive::ChatWide();
	const auto inWebPage = (_parent->media() != this);
	const auto item = _parent->data();
	const auto via = inWebPage ? nullptr : item->Get<HistoryMessageVia>();
	const auto reply = inWebPage ? nullptr : item->Get<HistoryMessageReply>();
	const auto forwarded = inWebPage ? nullptr : item->Get<HistoryMessageForwarded>();
	auto usex = 0;
	auto usew = maxWidth();
	if (!inWebPage) {
		usew -= additionalWidth(via, reply, forwarded);
		if (rightAligned) {
			usex = width() - usew;
		}
	}
	if (rtl()) {
		usex = width() - usex - usew;
	}

	const auto usey = rightAligned ? 0 : (height() - _contentSize.height());
	const auto useh = rightAligned
		? std::max(
			_contentSize.height(),
			height() - st::msgDateImgPadding.y() * 2 - st::msgDateFont->height)
		: _contentSize.height();
	const auto inner = QRect(usex, usey, usew, useh);

	if (_parent->media() == this) {
		auto replyRight = 0;
		int rectw = width() - inner.width() - st::msgReplyPadding.left();
		auto innerw = rectw - (st::msgReplyPadding.left() + st::msgReplyPadding.right());
		if (auto recth = surroundingHeight(via, reply, forwarded, innerw)) {
			int rectx = rightAligned ? 0 : (inner.width() + st::msgReplyPadding.left());
			int recty = 0;
			if (rtl()) rectx = width() - rectx - rectw;

			if (forwarded) {
				auto forwardedHeightReal = forwarded ? forwarded->text.countHeight(innerw) : 0;
				auto forwardedHeight = qMin(forwardedHeightReal, kMaxUnwrappedForwardedBarLines * st::msgServiceNameFont->height);
				if (QRect(rectx, recty, rectw, st::msgReplyPadding.top() + forwardedHeight).contains(point)) {
					auto breakEverywhere = (forwardedHeightReal > forwardedHeight);
					auto textRequest = request.forText();
					if (breakEverywhere) {
						textRequest.flags |= Ui::Text::StateRequest::Flag::BreakEverywhere;
					}
					result = TextState(_parent, forwarded->text.getState(
						point - QPoint(rectx + st::msgReplyPadding.left(), recty + st::msgReplyPadding.top()),
						innerw,
						textRequest));
					result.symbol = 0;
					result.afterSymbol = false;
					if (breakEverywhere) {
						result.cursor = CursorState::Forwarded;
					} else {
						result.cursor = CursorState::None;
					}
					return result;
				}
				recty += forwardedHeight;
				recth -= forwardedHeight;
			}
			if (via) {
				int viah = st::msgReplyPadding.top() + st::msgServiceNameFont->height + (reply ? 0 : st::msgReplyPadding.bottom());
				if (QRect(rectx, recty, rectw, viah).contains(point)) {
					result.link = via->link;
					return result;
				}
				int skip = st::msgServiceNameFont->height + (reply ? 2 * st::msgReplyPadding.top() : 0);
				recty += skip;
				recth -= skip;
			}
			if (reply) {
				if (QRect(rectx, recty, rectw, recth).contains(point)) {
					result.link = reply->replyToLink();
					return result;
				}
			}
			replyRight = rectx + rectw - st::msgReplyPadding.right();
		}
		const auto fullRight = calculateFullRight(inner);
		const auto rightAction = _parent->displayRightAction();
		auto fullBottom = height();
		if (_parent->pointInTime(fullRight, fullBottom, point, InfoDisplayType::Background)) {
			result.cursor = CursorState::Date;
		}
		if (rightAction) {
			const auto size = st::historyFastShareSize;
			const auto position = calculateFastActionPosition(
				fullBottom,
				replyRight,
				fullRight);
			if (QRect(position.x(), position.y(), size, size).contains(point)) {
				result.link = _parent->rightActionLink();
				return result;
			}
		}
	}

	auto pixLeft = usex + (usew - _contentSize.width()) / 2;
	auto pixTop = (minHeight() - _contentSize.height()) / 2;
	// Link of content can be nullptr (e.g. sticker without stickerpack).
	// So we have to process it to avoid overriding the previous result.
	if (_content->link()
		&& QRect({ pixLeft, pixTop }, _contentSize).contains(point)) {
		result.link = _content->link();
		return result;
	}
	return result;
}

int UnwrappedMedia::calculateFullRight(const QRect &inner) const {
	const auto rightAligned = _parent->hasOutLayout() && !Adaptive::ChatWide();
	const auto infoWidth = _parent->infoWidth()
		+ st::msgDateImgPadding.x() * 2
		+ st::msgReplyPadding.left();
	const auto rightActionWidth = _parent->displayRightAction()
		? (st::historyFastShareLeft * 2
			+ st::historyFastShareSize
			+ st::msgPadding.left()
			+ (_parent->hasFromPhoto()
				? st::msgMargin.right()
				: st::msgPadding.right()))
		: 0;
	auto fullRight = inner.x()
		+ inner.width()
		+ (rightAligned ? 0 : infoWidth);
	if (fullRight + rightActionWidth > _parent->width()) {
		fullRight = _parent->width() - rightActionWidth;
	}
	return fullRight;
}

QPoint UnwrappedMedia::calculateFastActionPosition(
	int fullBottom,
	int replyRight,
	int fullRight) const {
	const auto size = st::historyFastShareSize;
	const auto fastShareTop = (fullBottom
		- st::historyFastShareBottom
		- size);
	const auto doesRightActionHitReply = replyRight && (fastShareTop <
		st::msgReplyBarSize.height()
		+ st::msgReplyPadding.top()
		+ st::msgReplyPadding.bottom());
	const auto fastShareLeft = ((doesRightActionHitReply
		? replyRight
		: fullRight) + st::historyFastShareLeft);
	return QPoint(fastShareLeft, fastShareTop);
}

bool UnwrappedMedia::needInfoDisplay() const {
	return (_parent->data()->id < 0)
		|| (_parent->isUnderCursor())
		|| (_parent->displayRightAction())
		|| (_parent->hasOutLayout()
			&& !Adaptive::ChatWide()
			&& _content->alwaysShowOutTimestamp());
}

int UnwrappedMedia::additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply, const HistoryMessageForwarded *forwarded) const {
	auto result = st::msgReplyPadding.left() + _parent->infoWidth() + 2 * st::msgDateImgPadding.x();
	if (via) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + via->maxWidth + st::msgReplyPadding.left());
	}
	if (reply) {
		accumulate_max(result, st::msgReplyPadding.left() + reply->replyToWidth());
	}
	if (forwarded) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + forwarded->text.maxWidth() + st::msgReplyPadding.right());
	}
	return result;
}

} // namespace HistoryView
