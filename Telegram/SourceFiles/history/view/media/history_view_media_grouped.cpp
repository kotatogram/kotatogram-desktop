/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_media_grouped.h"

#include "history/history_item_components.h"
#include "history/history_message.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "storage/storage_shared_media.h"
#include "lang/lang_keys.h"
#include "ui/grouped_layout.h"
#include "ui/text_options.h"
#include "layout.h"
#include "styles/style_history.h"

namespace HistoryView {

GroupedMedia::Part::Part(
	not_null<Element*> parent,
	not_null<Data::Media*> media)
: item(media->parent())
, content(media->createView(parent, item)) {
	Assert(media->canBeGrouped());
}

GroupedMedia::GroupedMedia(
	not_null<Element*> parent,
	const std::vector<std::unique_ptr<Data::Media>> &medias)
: Media(parent)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	const auto truncated = ranges::view::all(
		medias
	) | ranges::view::transform([](const std::unique_ptr<Data::Media> &v) {
		return not_null<Data::Media*>(v.get());
	}) | ranges::view::take(kMaxSize);
	const auto result = applyGroup(truncated);

	Ensures(result);
}

GroupedMedia::GroupedMedia(
	not_null<Element*> parent,
	const std::vector<not_null<HistoryItem*>> &items)
: Media(parent)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	const auto medias = ranges::view::all(
		items
	) | ranges::view::transform([](not_null<HistoryItem*> item) {
		return item->media();
	}) | ranges::view::take(kMaxSize);
	const auto result = applyGroup(medias);

	Ensures(result);
}

QSize GroupedMedia::countOptimalSize() {
	if (_caption.hasSkipBlock()) {
		_caption.updateSkipBlock(
			_parent->skipBlockWidth(),
			_parent->skipBlockHeight());
	}

	std::vector<QSize> sizes;
	sizes.reserve(_parts.size());
	for (const auto &part : _parts) {
		const auto &media = part.content;
		media->initDimensions();
		sizes.push_back(media->sizeForGrouping());
	}

	const auto captionWithPaddings = _caption.maxWidth()
		+ st::msgPadding.left()
		+ st::msgPadding.right();
	auto groupMaxWidth = st::historyGroupWidthMax;
	if (cAdaptiveBubbles()) {
		accumulate_max(groupMaxWidth, captionWithPaddings);
	}

	const auto layout = Ui::LayoutMediaGroup(
		sizes,
		groupMaxWidth,
		st::historyGroupWidthMin,
		st::historyGroupSkip);
	Assert(layout.size() == _parts.size());

	auto maxWidth = 0;
	auto minHeight = 0;
	for (auto i = 0, count = int(layout.size()); i != count; ++i) {
		const auto &item = layout[i];
		accumulate_max(maxWidth, item.geometry.x() + item.geometry.width());
		accumulate_max(minHeight, item.geometry.y() + item.geometry.height());
		_parts[i].initialGeometry = item.geometry;
		_parts[i].sides = item.sides;
	}

	if (!_caption.isEmpty()) {
		if (cAdaptiveBubbles()) {
			maxWidth = qMax(maxWidth, captionWithPaddings);
		}
		auto captionw = maxWidth - st::msgPadding.left() - st::msgPadding.right();
		minHeight += st::mediaCaptionSkip + _caption.countHeight(captionw);
		if (isBubbleBottom()) {
			minHeight += st::msgPadding.bottom();
		}
	}
	return { maxWidth, minHeight };
}

QSize GroupedMedia::countCurrentSize(int newWidth) {
	accumulate_min(newWidth, maxWidth());
	auto newHeight = 0;
	if (newWidth < st::historyGroupWidthMin) {
		return { newWidth, newHeight };
	}

	const auto initialSpacing = st::historyGroupSkip;
	const auto factor = newWidth / float64(maxWidth());
	const auto scale = [&](int value) {
		return int(std::round(value * factor));
	};
	const auto spacing = scale(initialSpacing);
	for (auto &part : _parts) {
		const auto sides = part.sides;
		const auto initialGeometry = part.initialGeometry;
		const auto needRightSkip = !(sides & RectPart::Right);
		const auto needBottomSkip = !(sides & RectPart::Bottom);
		const auto initialLeft = initialGeometry.x();
		const auto initialTop = initialGeometry.y();
		const auto initialRight = initialLeft
			+ initialGeometry.width()
			+ (needRightSkip ? initialSpacing : 0);
		const auto initialBottom = initialTop
			+ initialGeometry.height()
			+ (needBottomSkip ? initialSpacing : 0);
		const auto left = scale(initialLeft);
		const auto top = scale(initialTop);
		const auto width = scale(initialRight)
			- left
			- (needRightSkip ? spacing : 0);
		const auto height = scale(initialBottom)
			- top
			- (needBottomSkip ? spacing : 0);
		part.geometry = QRect(left, top, width, height);

		accumulate_max(newHeight, top + height);
	}

	if (!_caption.isEmpty()) {
		const auto captionw = newWidth - st::msgPadding.left() - st::msgPadding.right();
		newHeight += st::mediaCaptionSkip + _caption.countHeight(captionw);
		if (isBubbleBottom()) {
			newHeight += st::msgPadding.bottom();
		}
	}

	return { newWidth, newHeight };
}

void GroupedMedia::refreshParentId(
		not_null<HistoryItem*> realParent) {
	for (const auto &part : _parts) {
		part.content->refreshParentId(part.item);
	}
}

RectParts GroupedMedia::cornersFromSides(RectParts sides) const {
	auto result = Ui::GetCornersFromSides(sides);
	if (!isBubbleTop()) {
		result &= ~(RectPart::TopLeft | RectPart::TopRight);
	}
	if (!isBubbleBottom() || !_caption.isEmpty()) {
		result &= ~(RectPart::BottomLeft | RectPart::BottomRight);
	}
	return result;
}

void GroupedMedia::draw(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		crl::time ms) const {
	for (auto i = 0, count = int(_parts.size()); i != count; ++i) {
		const auto &part = _parts[i];
		const auto partSelection = (selection == FullSelection)
			? FullSelection
			: IsGroupItemSelection(selection, i)
			? FullSelection
			: TextSelection();
		part.content->drawGrouped(
			p,
			clip,
			partSelection,
			ms,
			part.geometry,
			part.sides,
			cornersFromSides(part.sides),
			&part.cacheKey,
			&part.cache);
	}

	// date
	const auto selected = (selection == FullSelection);
	if (!_caption.isEmpty()) {
		const auto captionw = width() - st::msgPadding.left() - st::msgPadding.right();
		const auto outbg = _parent->hasOutLayout();
		const auto captiony = height()
			- (isBubbleBottom() ? st::msgPadding.bottom() : 0)
			- _caption.countHeight(captionw);
		p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
		_caption.draw(p, st::msgPadding.left(), captiony, captionw, style::al_left, 0, -1, selection);
	} else if (_parent->media() == this) {
		auto fullRight = width();
		auto fullBottom = height();
		if (needInfoDisplay()) {
			_parent->drawInfo(p, fullRight, fullBottom, width(), selected, InfoDisplayType::Image);
		}
		if (!_parent->hasBubble() && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			_parent->drawRightAction(p, fastShareLeft, fastShareTop, width());
		}
	}
}

TextState GroupedMedia::getPartState(
		QPoint point,
		StateRequest request) const {
	for (const auto &part : _parts) {
		if (part.geometry.contains(point)) {
			auto result = part.content->getStateGrouped(
				part.geometry,
				part.sides,
				point,
				request);
			result.itemId = part.item->fullId();
			return result;
		}
	}
	return TextState(_parent->data());
}

PointState GroupedMedia::pointState(QPoint point) const {
	if (!QRect(0, 0, width(), height()).contains(point)) {
		return PointState::Outside;
	}
	for (const auto &part : _parts) {
		if (part.geometry.contains(point)) {
			return PointState::GroupPart;
		}
	}
	return PointState::Inside;
}

TextState GroupedMedia::textState(QPoint point, StateRequest request) const {
	auto result = getPartState(point, request);
	if (!result.link && !_caption.isEmpty()) {
		const auto captionw = width() - st::msgPadding.left() - st::msgPadding.right();
		const auto captiony = height()
			- (isBubbleBottom() ? st::msgPadding.bottom() : 0)
			- _caption.countHeight(captionw);
		if (QRect(st::msgPadding.left(), captiony, captionw, height() - captiony).contains(point)) {
			return TextState(_parent->data(), _caption.getState(
				point - QPoint(st::msgPadding.left(), captiony),
				captionw,
				request.forText()));
		}
	} else if (_parent->media() == this) {
		auto fullRight = width();
		auto fullBottom = height();
		if (_parent->pointInTime(fullRight, fullBottom, point, InfoDisplayType::Image)) {
			result.cursor = CursorState::Date;
		}
		if (!_parent->hasBubble() && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			if (QRect(fastShareLeft, fastShareTop, st::historyFastShareSize, st::historyFastShareSize).contains(point)) {
				result.link = _parent->rightActionLink();
			}
		}
	}
	return result;
}

bool GroupedMedia::toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const {
	for (const auto &part : _parts) {
		if (part.content->toggleSelectionByHandlerClick(p)) {
			return true;
		}
	}
	return false;
}

bool GroupedMedia::dragItemByHandler(const ClickHandlerPtr &p) const {
	for (const auto &part : _parts) {
		if (part.content->dragItemByHandler(p)) {
			return true;
		}
	}
	return false;
}

TextSelection GroupedMedia::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	return _caption.adjustSelection(selection, type);
}

TextForMimeData GroupedMedia::selectedText(
		TextSelection selection) const {
	return _caption.toTextForMimeData(selection);
}

void GroupedMedia::clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) {
	for (const auto &part : _parts) {
		part.content->clickHandlerActiveChanged(p, active);
	}
}

void GroupedMedia::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	for (const auto &part : _parts) {
		part.content->clickHandlerPressedChanged(p, pressed);
		if (pressed && part.content->dragItemByHandler(p)) {
			// #TODO drag by item from album
			// App::pressedLinkItem(part.view);
		}
	}
}

template <typename DataMediaRange>
bool GroupedMedia::applyGroup(const DataMediaRange &medias) {
	if (validateGroupParts(medias)) {
		return true;
	}

	for (const auto media : medias) {
		_parts.push_back(Part(_parent, media));
	}
	if (_parts.empty()) {
		return false;
	}

	Ensures(_parts.size() <= kMaxSize);
	return true;
}

template <typename DataMediaRange>
bool GroupedMedia::validateGroupParts(
		const DataMediaRange &medias) const {
	auto i = 0;
	const auto count = _parts.size();
	for (const auto media : medias) {
		if (i >= count || _parts[i].item != media->parent()) {
			return false;
		}
		++i;
	}
	return (i == count);
}

not_null<Media*> GroupedMedia::main() const {
	Expects(!_parts.empty());

	return _parts.back().content.get();
}

TextWithEntities GroupedMedia::getCaption() const {
	return main()->getCaption();
}

Storage::SharedMediaTypesMask GroupedMedia::sharedMediaTypes() const {
	return main()->sharedMediaTypes();
}

PhotoData *GroupedMedia::getPhoto() const {
	return main()->getPhoto();
}

DocumentData *GroupedMedia::getDocument() const {
	return main()->getDocument();
}

HistoryMessageEdited *GroupedMedia::displayedEditBadge() const {
	if (!_caption.isEmpty()) {
		return _parts.front().item->Get<HistoryMessageEdited>();
	}
	return nullptr;
}

void GroupedMedia::updateNeedBubbleState() {
	const auto captionItem = [&]() -> HistoryItem* {
		auto result = (HistoryItem*)nullptr;
		for (const auto &part : _parts) {
			if (!part.item->emptyText()) {
				if (result) {
					return nullptr;
				} else {
					result = part.item;
				}
			}
		}
		return result;
	}();
	if (captionItem) {
		_caption = createCaption(captionItem);
	}
	_needBubble = computeNeedBubble();
}

void GroupedMedia::stopAnimation() {
	for (auto &part : _parts) {
		part.content->stopAnimation();
	}
}

int GroupedMedia::checkAnimationCount() {
	auto result = 0;
	for (auto &part : _parts) {
		result += part.content->checkAnimationCount();
	}
	return result;
}

void GroupedMedia::unloadHeavyPart() {
	for (auto &part : _parts) {
		part.content->unloadHeavyPart();
	}
}

void GroupedMedia::parentTextUpdated() {
	history()->owner().requestViewResize(_parent);
}

bool GroupedMedia::needsBubble() const {
	return _needBubble;
}

bool GroupedMedia::computeNeedBubble() const {
	if (!_caption.isEmpty()) {
		return true;
	}
	if (const auto item = _parent->data()) {
		if (item->viaBot()
			|| item->Has<HistoryMessageReply>()
			|| _parent->displayForwardedFrom()
			|| _parent->displayFromName()
			) {
			return true;
		}
	}
	return false;
}

bool GroupedMedia::needInfoDisplay() const {
	return (_parent->data()->id < 0 || _parent->isUnderCursor());
}

} // namespace HistoryView
