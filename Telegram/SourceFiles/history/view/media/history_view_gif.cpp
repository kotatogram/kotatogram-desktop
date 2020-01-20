/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_gif.h"

#include "lang/lang_keys.h"
#include "layout.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/clip/media_clip_reader.h"
#include "media/player/media_player_instance.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_player.h"
#include "media/view/media_view_playback_progress.h"
#include "boxes/confirm_box.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/media/history_view_media_common.h"
#include "window/window_session_controller.h"
#include "core/application.h" // Application::showDocument.
#include "ui/image/image.h"
#include "ui/grouped_layout.h"
#include "data/data_session.h"
#include "data/data_streaming.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "mainwidget.h"
#include "app.h"
#include "styles/style_history.h"

namespace HistoryView {
namespace {

constexpr auto kMaxGifForwardedBarLines = 4;
constexpr auto kUseNonBlurredThreshold = 160;

int gifMaxStatusWidth(DocumentData *document) {
	auto result = st::normalFont->width(formatDownloadText(document->size, document->size));
	accumulate_max(result, st::normalFont->width(formatGifAndSizeText(document->size)));
	return result;
}

} // namespace

struct Gif::Streamed {
	Streamed(
		std::shared_ptr<::Media::Streaming::Document> shared,
		Fn<void()> waitingCallback);
	::Media::Streaming::Instance instance;
	::Media::Streaming::FrameRequest frozenRequest;
	QImage frozenFrame;
	QString frozenStatusText;
};

Gif::Streamed::Streamed(
	std::shared_ptr<::Media::Streaming::Document> shared,
	Fn<void()> waitingCallback)
: instance(std::move(shared), std::move(waitingCallback)) {
}

Gif::Gif(
	not_null<Element*> parent,
	not_null<HistoryItem*> realParent,
	not_null<DocumentData*> document)
: File(parent, realParent)
, _data(document)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right())
, _downloadSize(formatSizeText(_data->size)) {
	setDocumentLinks(_data, realParent);

	setStatusSize(FileStatusSizeReady);

	refreshCaption();
	_data->loadThumbnail(realParent->fullId());
}

Gif::~Gif() {
	if (_streamed) {
		_data->owner().streaming().keepAlive(_data);
		setStreamed(nullptr);
	}
}

QSize Gif::sizeForAspectRatio() const {
	// We use size only for aspect ratio and we want to have it
	// as close to the thumbnail as possible.
	//if (!_data->dimensions.isEmpty()) {
	//	return _data->dimensions;
	//}
	if (const auto thumb = _data->thumbnail()) {
		if (!thumb->size().isEmpty()) {
			return thumb->size();
		}
	}
	return { 1, 1 };
}

QSize Gif::countOptimalSize() {
	if (_parent->media() != this) {
		_caption = Ui::Text::String();
	} else if (_caption.hasSkipBlock()) {
		_caption.updateSkipBlock(
			_parent->skipBlockWidth(),
			_parent->skipBlockHeight());
	}

	const auto captionWithPaddings = _caption.maxWidth()
		+ st::msgPadding.left()
		+ st::msgPadding.right();
	const auto maxSize = _data->isVideoFile()
		? st::maxMediaSize
		: _data->isVideoMessage()
		? st::maxVideoMessageSize
		: st::maxGifSize;
	const auto size = style::ConvertScale(videoSize());
	auto tw = size.width();
	auto th = size.height();
	if ((!cAdaptiveBubbles() || captionWithPaddings <= maxSize) && tw > maxSize) {
		th = (maxSize * th) / tw;
		tw = maxSize;
	} else if (cAdaptiveBubbles() && captionWithPaddings > maxSize && tw > captionWithPaddings) {
		th = (captionWithPaddings * th) / tw;
		tw = captionWithPaddings;
	}
	if (th > maxSize) {
		tw = (maxSize * tw) / th;
		th = maxSize;
	}
	if (!tw || !th) {
		tw = th = 1;
	}
	_thumbw = tw;
	_thumbh = th;
	auto maxWidth = qMax(tw, st::minPhotoSize);
	auto minHeight = qMax(th, st::minPhotoSize);
	accumulate_max(maxWidth, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	if (!activeCurrentStreamed()) {
		accumulate_max(maxWidth, gifMaxStatusWidth(_data) + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	}
	if (_parent->hasBubble()) {
		if (!_caption.isEmpty()) {
			if (cAdaptiveBubbles()) {
				accumulate_max(maxWidth, captionWithPaddings);
			}
			auto captionw = maxWidth - st::msgPadding.left() - st::msgPadding.right();
			minHeight += st::mediaCaptionSkip + _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				minHeight += st::msgPadding.bottom();
			}
		}
	} else if (isSeparateRoundVideo()) {
		const auto item = _parent->data();
		auto via = item->Get<HistoryMessageVia>();
		auto reply = item->Get<HistoryMessageReply>();
		auto forwarded = item->Get<HistoryMessageForwarded>();
		if (forwarded) {
			forwarded->create(via);
		}
		maxWidth += additionalWidth(via, reply, forwarded);
	}
	return { maxWidth, minHeight };
}

QSize Gif::countCurrentSize(int newWidth) {
	auto availableWidth = newWidth;

	const auto captionWithPaddings = _caption.maxWidth()
		+ st::msgPadding.left()
		+ st::msgPadding.right();
	const auto maxSize = _data->isVideoFile()
		? st::maxMediaSize
		: _data->isVideoMessage()
		? st::maxVideoMessageSize
		: st::maxGifSize;
	const auto size = style::ConvertScale(videoSize());
	auto tw = size.width();
	auto th = size.height();
	if ((!cAdaptiveBubbles() || captionWithPaddings <= maxSize) && tw > maxSize) {
		th = (maxSize * th) / tw;
		tw = maxSize;
	} else if (cAdaptiveBubbles() && captionWithPaddings > maxSize && tw > captionWithPaddings) {
		th = (captionWithPaddings * th) / tw;
		tw = captionWithPaddings;
	}
	if (th > maxSize) {
		tw = (maxSize * tw) / th;
		th = maxSize;
	}
	if (!tw || !th) {
		tw = th = 1;
	}

	if (newWidth < tw) {
		th = qRound((newWidth / float64(tw)) * th);
		tw = newWidth;
	}
	_thumbw = tw;
	_thumbh = th;

	newWidth = qMax(tw, st::minPhotoSize);
	auto newHeight = qMax(th, st::minPhotoSize);
	accumulate_max(newWidth, _parent->infoWidth() + 2 * st::msgDateImgDelta + st::msgDateImgPadding.x());
	if (!activeCurrentStreamed()) {
		accumulate_max(newWidth, gifMaxStatusWidth(_data) + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	}
	if (_parent->hasBubble()) {
		if (!_caption.isEmpty()) {
			if (cAdaptiveBubbles()) {
				accumulate_max(newWidth, captionWithPaddings);
				accumulate_min(newWidth, availableWidth);
			}
			auto captionw = newWidth - st::msgPadding.left() - st::msgPadding.right();
			newHeight += st::mediaCaptionSkip + _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				newHeight += st::msgPadding.bottom();
			}
		}
	} else if (isSeparateRoundVideo()) {
		const auto item = _parent->data();
		auto via = item->Get<HistoryMessageVia>();
		auto reply = item->Get<HistoryMessageReply>();
		auto forwarded = item->Get<HistoryMessageForwarded>();
		if (via || reply || forwarded) {
			auto additional = additionalWidth(via, reply, forwarded);
			newWidth += additional;
			accumulate_min(newWidth, availableWidth);
			auto usew = maxWidth() - additional;
			auto availw = newWidth - usew - st::msgReplyPadding.left() - st::msgReplyPadding.left() - st::msgReplyPadding.left();
			if (!forwarded && via) {
				via->resize(availw);
			}
			if (reply) {
				reply->resize(availw);
			}
		}
	}

	return { newWidth, newHeight };
}

QSize Gif::videoSize() const {
	if (const auto streamed = activeCurrentStreamed()) {
		return streamed->player().videoSize();
	} else if (!_data->dimensions.isEmpty()) {
		return _data->dimensions;
	} else if (const auto thumbnail = _data->thumbnail()) {
		return thumbnail->size();
	} else {
		return QSize(1, 1);
	}
}

bool Gif::downloadInCorner() const {
	return _data->isVideoFile()
		&& (_data->loading() || !autoplayEnabled())
		&& _data->canBeStreamed()
		&& !_data->inappPlaybackFailed()
		&& IsServerMsgId(_parent->data()->id);
}

bool Gif::autoplayEnabled() const {
	return Data::AutoDownload::ShouldAutoPlay(
		_data->session().settings().autoDownload(),
		_realParent->history()->peer,
		_data);
}

void Gif::draw(Painter &p, const QRect &r, TextSelection selection, crl::time ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	const auto item = _parent->data();
	const auto displayLoading = item->isSending() || _data->displayLoading();
	const auto selected = (selection == FullSelection);
	const auto autoPaused = App::wnd()->sessionController()->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
	const auto cornerDownload = downloadInCorner();
	const auto canBePlayed = _data->canBePlayed();
	const auto autoplay = autoplayEnabled() && canBePlayed;
	const auto activeRoundPlaying = activeRoundStreamed();
	const auto startPlay = autoplay
		&& !_streamed
		&& !activeRoundPlaying;
	if (startPlay) {
		const_cast<Gif*>(this)->playAnimation(true);
	} else {
		checkStreamedIsStarted();
	}
	const auto streamingMode = _streamed || activeRoundPlaying || autoplay;
	const auto activeOwnPlaying = activeOwnStreamed();

	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	bool bubble = _parent->hasBubble();
	auto outbg = _parent->hasOutLayout();
	auto inWebPage = (_parent->media() != this);

	auto captionw = paintw - st::msgPadding.left() - st::msgPadding.right();

	const auto isRound = _data->isVideoMessage();
	auto displayMute = false;
	const auto streamed = activeRoundPlaying
		? activeRoundPlaying
		: activeOwnPlaying
		? &activeOwnPlaying->instance
		: nullptr;
	const auto streamedForWaiting = activeRoundPlaying
		? activeRoundPlaying
		: _streamed
		? &_streamed->instance
		: nullptr;

	if (displayLoading
		&& (!streamedForWaiting
			|| item->isSending()
			|| _data->uploading()
			|| (cornerDownload && _data->loading()))) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(dataProgress());
		}
	}
	updateStatusText();
	const auto radial = isRadialAnimation()
		|| (streamedForWaiting && streamedForWaiting->waitingShown());

	if (bubble) {
		if (!_caption.isEmpty()) {
			painth -= st::mediaCaptionSkip + _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				painth -= st::msgPadding.bottom();
			}
		}
	} else if (!isRound) {
		App::roundShadow(p, 0, 0, paintw, height(), selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	auto usex = 0, usew = paintw;
	auto separateRoundVideo = isSeparateRoundVideo();
	auto via = separateRoundVideo ? item->Get<HistoryMessageVia>() : nullptr;
	auto reply = separateRoundVideo ? item->Get<HistoryMessageReply>() : nullptr;
	auto forwarded = separateRoundVideo ? item->Get<HistoryMessageForwarded>() : nullptr;
	if (via || reply || forwarded) {
		usew = maxWidth() - additionalWidth(via, reply, forwarded);
		if (outbg) {
			usex = width() - usew;
		}
	}
	if (rtl()) usex = width() - usex - usew;

	QRect rthumb(style::rtlrect(usex + paintx, painty, usew, painth, width()));

	auto roundRadius = isRound ? ImageRoundRadius::Ellipse : inWebPage ? ImageRoundRadius::Small : ImageRoundRadius::Large;
	auto roundCorners = (isRound || inWebPage) ? RectPart::AllCorners : ((isBubbleTop() ? (RectPart::TopLeft | RectPart::TopRight) : RectPart::None)
		| ((isBubbleBottom() && _caption.isEmpty()) ? (RectPart::BottomLeft | RectPart::BottomRight) : RectPart::None));
	if (streamed) {
		auto paused = autoPaused;
		if (isRound) {
			if (activeRoundStreamed()) {
				paused = false;
			} else {
				displayMute = true;
			}
		}
		auto request = ::Media::Streaming::FrameRequest();
		request.outer = QSize(usew, painth) * cIntRetinaFactor();
		request.resize = QSize(_thumbw, _thumbh) * cIntRetinaFactor();
		request.corners = roundCorners;
		request.radius = roundRadius;
		if (!activeRoundPlaying && activeOwnPlaying->instance.playerLocked()) {
			if (activeOwnPlaying->frozenFrame.isNull()) {
				activeOwnPlaying->frozenRequest = request;
				activeOwnPlaying->frozenFrame = streamed->frame(request);
				activeOwnPlaying->frozenStatusText = _statusText;
			} else if (activeOwnPlaying->frozenRequest != request) {
				activeOwnPlaying->frozenRequest = request;
				activeOwnPlaying->frozenFrame = streamed->frame(request);
			}
			p.drawImage(rthumb, activeOwnPlaying->frozenFrame);
		} else {
			if (activeOwnPlaying) {
				activeOwnPlaying->frozenFrame = QImage();
				activeOwnPlaying->frozenStatusText = QString();
			}
			p.drawImage(rthumb, streamed->frame(request));
			if (!paused) {
				streamed->markFrameShown();
			}
		}

		if (const auto playback = videoPlayback()) {
			const auto value = playback->value();
			if (value > 0.) {
				auto pen = st::historyVideoMessageProgressFg->p;
				auto was = p.pen();
				pen.setWidth(st::radialLine);
				pen.setCapStyle(Qt::RoundCap);
				p.setPen(pen);
				p.setOpacity(st::historyVideoMessageProgressOpacity);

				auto from = QuarterArcLength;
				auto len = -qRound(FullArcLength * value);
				auto stepInside = st::radialLine / 2;
				{
					PainterHighQualityEnabler hq(p);
					p.drawArc(rthumb.marginsRemoved(QMargins(stepInside, stepInside, stepInside, stepInside)), from, len);
				}

				p.setPen(was);
				p.setOpacity(1.);
			}
		}
	} else {
		const auto good = _data->goodThumbnail();
		if (good && good->loaded()) {
			p.drawPixmap(rthumb.topLeft(), good->pixSingle({}, _thumbw, _thumbh, usew, painth, roundRadius, roundCorners));
		} else {
			if (good) {
				good->load({});
			}
			const auto normal = _data->thumbnail();
			if (normal && normal->loaded()) {
				if (normal->width() >= kUseNonBlurredThreshold
					&& normal->height() >= kUseNonBlurredThreshold) {
					p.drawPixmap(rthumb.topLeft(), normal->pixSingle(_realParent->fullId(), _thumbw, _thumbh, usew, painth, roundRadius, roundCorners));
				} else {
					p.drawPixmap(rthumb.topLeft(), normal->pixBlurredSingle(_realParent->fullId(), _thumbw, _thumbh, usew, painth, roundRadius, roundCorners));
				}
			} else if (const auto blurred = _data->thumbnailInline()) {
				p.drawPixmap(rthumb.topLeft(), blurred->pixBlurredSingle(_realParent->fullId(), _thumbw, _thumbh, usew, painth, roundRadius, roundCorners));
			} else if (!isRound) {
				const auto roundTop = (roundCorners & RectPart::TopLeft);
				const auto roundBottom = (roundCorners & RectPart::BottomLeft);
				const auto margin = inWebPage
					? st::buttonRadius
					: st::historyMessageRadius;
				const auto parts = roundCorners
					| RectPart::NoTopBottom
					| (roundTop ? RectPart::Top : RectPart::None)
					| (roundBottom ? RectPart::Bottom : RectPart::None);
				App::roundRect(p, rthumb.marginsAdded({ 0, roundTop ? 0 : margin, 0, roundBottom ? 0 : margin }), st::imageBg, roundRadius, parts);
			}
		}
	}

	if (selected) {
		App::complexOverlayRect(p, rthumb, roundRadius, roundCorners);
	}

	if (radial
		|| (!streamingMode
			&& ((!_data->loaded() && !_data->loading()) || !autoplay))) {
		const auto radialOpacity = (item->isSending() || _data->uploading())
			? 1.
			: streamedForWaiting
			? streamedForWaiting->waitingOpacity()
			: (radial && _data->loaded())
			? _animation->radial.opacity()
			: 1.;
		auto inner = QRect(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(st::msgDateImgBgSelected);
		} else if (isThumbAnimation()) {
			auto over = _animation->a_thumbOver.value(1.);
			p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, over));
		} else {
			auto over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
			p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
		}
		p.setOpacity(radialOpacity * p.opacity());

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		p.setOpacity(radialOpacity);
		const auto icon = [&]() -> const style::icon * {
			if (streamingMode && !_data->uploading()) {
				return nullptr;
			} else if ((_data->loaded() || canBePlayed) && (!radial || cornerDownload)) {
				return &(selected ? st::historyFileThumbPlaySelected : st::historyFileThumbPlay);
			} else if (radial || _data->loading()) {
				if (!item->isSending() || _data->uploading()) {
					return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
				}
				return nullptr;
			}
			return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
		}();
		if (icon) {
			icon->paintInCenter(p, inner);
		}
		p.setOpacity(1);
		if (radial) {
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			const auto fg = selected
				? st::historyFileThumbRadialFgSelected
				: st::historyFileThumbRadialFg;
			if (streamedForWaiting && !_data->uploading()) {
				Ui::InfiniteRadialAnimation::Draw(
					p,
					streamedForWaiting->waitingState(),
					rinner.topLeft(),
					rinner.size(),
					width(),
					fg,
					st::msgFileRadialLine);
			} else if (!cornerDownload) {
				_animation->radial.draw(
					p,
					rinner,
					st::msgFileRadialLine,
					fg);
			}
		}
	}
	if (displayMute) {
		auto muteRect = style::rtlrect(rthumb.x() + (rthumb.width() - st::historyVideoMessageMuteSize) / 2, rthumb.y() + st::msgDateImgDelta, st::historyVideoMessageMuteSize, st::historyVideoMessageMuteSize, width());
		p.setPen(Qt::NoPen);
		p.setBrush(selected ? st::msgDateImgBgSelected : st::msgDateImgBg);
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(muteRect);
		(selected ? st::historyVideoMessageMuteSelected : st::historyVideoMessageMute).paintInCenter(p, muteRect);
	}

	if (!isRound) {
		drawCornerStatus(p, selected, QPoint());
	}

	if (!inWebPage && isRound) {
		auto mediaUnread = item->hasUnreadMediaFlag();
		auto statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
		auto statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
		auto statusX = usex + paintx + st::msgDateImgDelta + st::msgDateImgPadding.x();
		auto statusY = painty + painth - st::msgDateImgDelta - statusH + st::msgDateImgPadding.y();
		if (mediaUnread) {
			statusW += st::mediaUnreadSkip + st::mediaUnreadSize;
		}
		App::roundRect(p, style::rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, width()), selected ? st::msgServiceBgSelected : st::msgServiceBg, selected ? StickerSelectedCorners : StickerCorners);
		p.setFont(st::normalFont);
		p.setPen(st::msgServiceFg);
		p.drawTextLeft(statusX, statusY, width(), _statusText, statusW - 2 * st::msgDateImgPadding.x());
		if (mediaUnread) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgServiceFg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(style::rtlrect(statusX - st::msgDateImgPadding.x() + statusW - st::msgDateImgPadding.x() - st::mediaUnreadSize, statusY + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, width()));
			}
		}
		if (via || reply || forwarded) {
			auto rectw = width() - usew - st::msgReplyPadding.left();
			auto innerw = rectw - (st::msgReplyPadding.left() + st::msgReplyPadding.right());
			auto recth = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
			auto forwardedHeightReal = forwarded ? forwarded->text.countHeight(innerw) : 0;
			auto forwardedHeight = qMin(forwardedHeightReal, kMaxGifForwardedBarLines * st::msgServiceNameFont->height);
			if (forwarded) {
				recth += forwardedHeight;
			} else if (via) {
				recth += st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
			}
			if (reply) {
				recth += st::msgReplyBarSize.height();
			}
			int rectx = outbg ? 0 : (usew + st::msgReplyPadding.left());
			int recty = painty;
			if (rtl()) rectx = width() - rectx - rectw;

			App::roundRect(p, rectx, recty, rectw, recth, selected ? st::msgServiceBgSelected : st::msgServiceBg, selected ? StickerSelectedCorners : StickerCorners);
			p.setPen(st::msgServiceFg);
			rectx += st::msgReplyPadding.left();
			rectw = innerw;
			if (forwarded) {
				p.setTextPalette(st::serviceTextPalette);
				auto breakEverywhere = (forwardedHeightReal > forwardedHeight);
				forwarded->text.drawElided(p, rectx, recty + st::msgReplyPadding.top(), rectw, kMaxGifForwardedBarLines, style::al_left, 0, -1, 0, breakEverywhere);
				p.restoreTextPalette();
			} else if (via) {
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
		}
	}
	if (!isRound && !_caption.isEmpty()) {
		p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
		_caption.draw(p, st::msgPadding.left(), painty + painth + st::mediaCaptionSkip, captionw, style::al_left, 0, -1, selection);
	} else if (!inWebPage) {
		auto fullRight = paintx + usex + usew;
		auto fullBottom = painty + painth;
		auto maxRight = _parent->width() - st::msgMargin.left();
		if (_parent->hasFromPhoto()) {
			maxRight -= st::msgMargin.right();
		} else {
			maxRight -= st::msgMargin.left();
		}
		if (isRound && !outbg) {
			auto infoWidth = _parent->infoWidth();

			// This is just some arbitrary point,
			// the main idea is to make info left aligned here.
			fullRight += infoWidth - st::normalFont->height;
			if (fullRight > maxRight) {
				fullRight = maxRight;
			}
		}
		if (isRound || needInfoDisplay()) {
			_parent->drawInfo(p, fullRight, fullBottom, 2 * paintx + paintw, selected, isRound ? InfoDisplayType::Background : InfoDisplayType::Image);
		}
		if (!bubble && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			if (fastShareLeft + st::historyFastShareSize > maxRight) {
				fastShareLeft = (fullRight - st::historyFastShareSize - st::msgDateImgDelta);
				fastShareTop -= (st::msgDateImgDelta + st::msgDateImgPadding.y() + st::msgDateFont->height + st::msgDateImgPadding.y());
			}
			_parent->drawRightAction(p, fastShareLeft, fastShareTop, 2 * paintx + paintw);
		}
	}
}

void Gif::drawCornerStatus(Painter &p, bool selected, QPoint position) const {
	if (!needCornerStatusDisplay()) {
		return;
	}
	const auto own = activeOwnStreamed();
	const auto text = (own && !own->frozenStatusText.isEmpty())
		? own->frozenStatusText
		: _statusText;
	const auto padding = st::msgDateImgPadding;
	const auto radial = _animation && _animation->radial.animating();
	const auto cornerDownload = downloadInCorner() && !_data->loaded() && !_data->loadedInMediaCache();
	const auto cornerMute = _streamed && _data->isVideoFile() && !cornerDownload;
	const auto addLeft = cornerDownload ? (st::historyVideoDownloadSize + 2 * padding.y()) : 0;
	const auto addRight = cornerMute ? st::historyVideoMuteSize : 0;
	const auto downloadWidth = cornerDownload ? st::normalFont->width(_downloadSize) : 0;
	const auto statusW = std::max(downloadWidth, st::normalFont->width(text)) + 2 * padding.x() + addLeft + addRight;
	const auto statusH = cornerDownload ? (st::historyVideoDownloadSize + 2 * padding.y()) : (st::normalFont->height + 2 * padding.y());
	const auto statusX = position.x() + st::msgDateImgDelta + padding.x();
	const auto statusY = position.y() + st::msgDateImgDelta + padding.y();
	const auto around = style::rtlrect(statusX - padding.x(), statusY - padding.y(), statusW, statusH, width());
	const auto statusTextTop = statusY + (cornerDownload ? (((statusH - 2 * st::normalFont->height) / 3)  - padding.y()) : 0);
	App::roundRect(p, around, selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
	p.setFont(st::normalFont);
	p.setPen(st::msgDateImgFg);
	p.drawTextLeft(statusX + addLeft, statusTextTop, width(), text, statusW - 2 * padding.x());
	if (cornerDownload) {
		const auto downloadTextTop = statusY + st::normalFont->height + (2 * (statusH - 2 * st::normalFont->height) / 3)  - padding.y();
		p.drawTextLeft(statusX + addLeft, downloadTextTop, width(), _downloadSize, statusW - 2 * padding.x());
		const auto inner = QRect(statusX + padding.y() - padding.x(), statusY, st::historyVideoDownloadSize, st::historyVideoDownloadSize);
		const auto icon = [&]() -> const style::icon * {
			if (_data->loading()) {
				return &(selected ? st::historyVideoCancelSelected : st::historyVideoCancel);
			}
			return &(selected ? st::historyVideoDownloadSelected : st::historyVideoDownload);
		}();
		if (icon) {
			icon->paintInCenter(p, inner);
		}
		if (radial) {
			QRect rinner(inner.marginsRemoved(QMargins(st::historyVideoRadialLine, st::historyVideoRadialLine, st::historyVideoRadialLine, st::historyVideoRadialLine)));
			_animation->radial.draw(p, rinner, st::historyVideoRadialLine, selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg);
		}
	} else if (cornerMute) {
		(selected ? st::historyVideoMessageMuteSelected : st::historyVideoMessageMute).paint(p, statusX - padding.x() - padding.y() + statusW - addRight, statusY - padding.y() + (statusH - st::historyVideoMessageMute.height()) / 2, width());
	}
}

TextState Gif::cornerStatusTextState(
		QPoint point,
		StateRequest request,
		QPoint position) const {
	auto result = TextState(_parent);
	if (!needCornerStatusDisplay() || !downloadInCorner() || _data->loaded()) {
		return result;
	}
	const auto padding = st::msgDateImgPadding;
	const auto addWidth = st::historyVideoDownloadSize + 2 * padding.y() - padding.x();
	const auto statusX = position.x() + st::msgDateImgDelta + padding.x();
	const auto statusY = position.y() + st::msgDateImgDelta + padding.y();
	const auto inner = QRect(statusX + padding.y() - padding.x(), statusY, st::historyVideoDownloadSize, st::historyVideoDownloadSize);
	if (inner.contains(point)) {
		result.link = _data->loading() ? _cancell : _savel;
	}
	return result;
}

TextState Gif::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}
	auto paintx = 0, painty = 0, paintw = width(), painth = height();
	auto bubble = _parent->hasBubble();

	if (bubble && !_caption.isEmpty()) {
		auto captionw = paintw - st::msgPadding.left() - st::msgPadding.right();
		painth -= _caption.countHeight(captionw);
		if (isBubbleBottom()) {
			painth -= st::msgPadding.bottom();
		}
		if (QRect(st::msgPadding.left(), painth, captionw, height() - painth).contains(point)) {
			result = TextState(_parent, _caption.getState(
				point - QPoint(st::msgPadding.left(), painth),
				captionw,
				request.forText()));
			return result;
		}
		painth -= st::mediaCaptionSkip;
	}
	auto outbg = _parent->hasOutLayout();
	auto inWebPage = (_parent->media() != this);
	auto isRound = _data->isVideoMessage();
	auto usew = paintw, usex = 0;
	auto separateRoundVideo = isSeparateRoundVideo();
	const auto item = _parent->data();
	auto via = separateRoundVideo ? item->Get<HistoryMessageVia>() : nullptr;
	auto reply = separateRoundVideo ? item->Get<HistoryMessageReply>() : nullptr;
	auto forwarded = separateRoundVideo ? item->Get<HistoryMessageForwarded>() : nullptr;
	if (via || reply || forwarded) {
		usew = maxWidth() - additionalWidth(via, reply, forwarded);
		if (outbg) {
			usex = width() - usew;
		}
	}
	if (rtl()) usex = width() - usex - usew;

	if (via || reply || forwarded) {
		auto rectw = paintw - usew - st::msgReplyPadding.left();
		auto innerw = rectw - (st::msgReplyPadding.left() + st::msgReplyPadding.right());
		auto recth = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
		auto forwardedHeightReal = forwarded ? forwarded->text.countHeight(innerw) : 0;
		auto forwardedHeight = qMin(forwardedHeightReal, kMaxGifForwardedBarLines * st::msgServiceNameFont->height);
		if (forwarded) {
			recth += forwardedHeight;
		} else if (via) {
			recth += st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
		}
		if (reply) {
			recth += st::msgReplyBarSize.height();
		}
		auto rectx = outbg ? 0 : (usew + st::msgReplyPadding.left());
		auto recty = painty;
		if (rtl()) rectx = width() - rectx - rectw;

		if (forwarded) {
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
		} else if (via) {
			auto viah = st::msgReplyPadding.top() + st::msgServiceNameFont->height + (reply ? 0 : st::msgReplyPadding.bottom());
			if (QRect(rectx, recty, rectw, viah).contains(point)) {
				result.link = via->link;
				return result;
			}
			auto skip = st::msgServiceNameFont->height + (reply ? 2 * st::msgReplyPadding.top() : 0);
			recty += skip;
			recth -= skip;
		}
		if (reply) {
			if (QRect(rectx, recty, rectw, recth).contains(point)) {
				result.link = reply->replyToLink();
				return result;
			}
		}
	}
	if (!isRound) {
		if (const auto state = cornerStatusTextState(point, request, QPoint()); state.link) {
			return state;
		}
	}
	if (QRect(usex + paintx, painty, usew, painth).contains(point)) {
		result.link = _data->uploading()
			? _cancell
			: !IsServerMsgId(_realParent->id)
			? nullptr
			: (_data->loaded() || _data->canBePlayed())
			? _openl
			: _data->loading()
			? _cancell
			: _savel;
	}
	if (isRound || _caption.isEmpty()) {
		auto fullRight = usex + paintx + usew;
		auto fullBottom = painty + painth;
		auto maxRight = _parent->width() - st::msgMargin.left();
		if (_parent->hasFromPhoto()) {
			maxRight -= st::msgMargin.right();
		} else {
			maxRight -= st::msgMargin.left();
		}
		if (isRound && !outbg) {
			auto infoWidth = _parent->infoWidth();

			// This is just some arbitrary point,
			// the main idea is to make info left aligned here.
			fullRight += infoWidth - st::normalFont->height;
			if (fullRight > maxRight) {
				fullRight = maxRight;
			}
		}
		if (!inWebPage) {
			if (_parent->pointInTime(fullRight, fullBottom, point, isRound ? InfoDisplayType::Background : InfoDisplayType::Image)) {
				result.cursor = CursorState::Date;
			}
		}
		if (!bubble && _parent->displayRightAction()) {
			auto fastShareLeft = (fullRight + st::historyFastShareLeft);
			auto fastShareTop = (fullBottom - st::historyFastShareBottom - st::historyFastShareSize);
			if (fastShareLeft + st::historyFastShareSize > maxRight) {
				fastShareLeft = (fullRight - st::historyFastShareSize - st::msgDateImgDelta);
				fastShareTop -= st::msgDateImgDelta + st::msgDateImgPadding.y() + st::msgDateFont->height + st::msgDateImgPadding.y();
			}
			if (QRect(fastShareLeft, fastShareTop, st::historyFastShareSize, st::historyFastShareSize).contains(point)) {
				result.link = _parent->rightActionLink();
			}
		}
	}
	return result;
}

TextForMimeData Gif::selectedText(TextSelection selection) const {
	return _caption.toTextForMimeData(selection);
}

bool Gif::fullFeaturedGrouped(RectParts sides) const {
	return (sides & RectPart::Left) && (sides & RectPart::Right);
}

QSize Gif::sizeForGrouping() const {
	return sizeForAspectRatio();
}

void Gif::drawGrouped(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		crl::time ms,
		const QRect &geometry,
		RectParts sides,
		RectParts corners,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const {
	const auto item = _parent->data();
	const auto displayLoading = (item->id < 0) || _data->displayLoading();
	const auto selected = (selection == FullSelection);
	const auto autoPaused = App::wnd()->sessionController()->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
	const auto fullFeatured = fullFeaturedGrouped(sides);
	const auto cornerDownload = fullFeatured && downloadInCorner();
	const auto canBePlayed = _data->canBePlayed();
	const auto autoplay = fullFeatured && autoplayEnabled() && canBePlayed;
	const auto startPlay = autoplay && !_streamed;
	if (startPlay) {
		const_cast<Gif*>(this)->playAnimation(true);
	} else {
		checkStreamedIsStarted();
	}
	const auto streamingMode = _streamed || autoplay;
	const auto activeOwnPlaying = activeOwnStreamed();

	auto paintx = geometry.x(), painty = geometry.y(), paintw = geometry.width(), painth = geometry.height();

	auto displayMute = false;
	const auto streamed = activeOwnPlaying
		? &activeOwnPlaying->instance
		: nullptr;
	const auto streamedForWaiting = _streamed
		? &_streamed->instance
		: nullptr;

	if (displayLoading
		&& (!streamedForWaiting
			|| item->isSending()
			|| _data->uploading()
			|| (cornerDownload && _data->loading()))) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(dataProgress());
		}
	}
	updateStatusText();
	const auto radial = isRadialAnimation()
		|| (streamedForWaiting && streamedForWaiting->waitingShown());

	const auto roundRadius = ImageRoundRadius::Large;

	auto drawHighlighted = [&](auto painterCallback) {
		const auto animms = _parent->delegate()->elementHighlightTime(_parent);
		const auto realId = _realParent->id;
		const auto mainWidget = App::main();
		const auto highlightedRealId = mainWidget->highlightedOriginalId();
		if (realId != highlightedRealId
			&& animms
			&& animms < st::activeFadeInDuration + st::activeFadeOutDuration) {
			const auto dt = (animms <= st::activeFadeInDuration)
				? ((animms / float64(st::activeFadeInDuration)))
				: (1. - (animms - st::activeFadeInDuration)
					/ float64(st::activeFadeOutDuration));
			const auto o = p.opacity();
			p.setOpacity(o - dt * 0.8);
			painterCallback();
			p.setOpacity(o);
		} else {
			painterCallback();
		}
	};

	if (streamed) {
		const auto paused = autoPaused;
		auto request = ::Media::Streaming::FrameRequest();
		const auto original = sizeForAspectRatio();
		const auto originalWidth = style::ConvertScale(original.width());
		const auto originalHeight = style::ConvertScale(original.height());
		const auto pixSize = Ui::GetImageScaleSizeForGeometry(
			{ originalWidth, originalHeight },
			{ geometry.width(), geometry.height() });
		request.outer = geometry.size() * cIntRetinaFactor();
		request.resize = pixSize * cIntRetinaFactor();
		request.corners = corners;
		request.radius = roundRadius;
		if (activeOwnPlaying->instance.playerLocked()) {
			if (activeOwnPlaying->frozenFrame.isNull()) {
				activeOwnPlaying->frozenRequest = request;
				activeOwnPlaying->frozenFrame = streamed->frame(request);
				activeOwnPlaying->frozenStatusText = _statusText;
			} else if (activeOwnPlaying->frozenRequest != request) {
				activeOwnPlaying->frozenRequest = request;
				activeOwnPlaying->frozenFrame = streamed->frame(request);
			}

			drawHighlighted([&]() {
				p.drawImage(geometry, activeOwnPlaying->frozenFrame);
			});
		} else {
			if (activeOwnPlaying) {
				activeOwnPlaying->frozenFrame = QImage();
				activeOwnPlaying->frozenStatusText = QString();
			}

			drawHighlighted([&]() {
				p.drawImage(geometry, streamed->frame(request));
			});

			if (!paused) {
				streamed->markFrameShown();
			}
		}
	} else {
		validateGroupedCache(geometry, corners, cacheKey, cache);
		drawHighlighted([&]() {
			p.drawPixmap(geometry, *cache);
		});
	}

	if (selected) {
		App::complexOverlayRect(p, geometry, roundRadius, corners);
	}

	if (radial
		|| (!streamingMode
			&& ((!_data->loaded() && !_data->loading()) || !autoplay))) {
		const auto radialOpacity = (item->isSending() || _data->uploading())
			? 1.
			: streamedForWaiting
			? streamedForWaiting->waitingOpacity()
			: (radial && _data->loaded())
			? _animation->radial.opacity()
			: 1.;
		const auto radialSize = st::historyGroupRadialSize;
		const auto inner = QRect(
			geometry.x() + (geometry.width() - radialSize) / 2,
			geometry.y() + (geometry.height() - radialSize) / 2,
			radialSize,
			radialSize);
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(st::msgDateImgBgSelected);
		} else if (isThumbAnimation()) {
			auto over = _animation->a_thumbOver.value(1.);
			p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, over));
		} else {
			auto over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
			p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
		}
		p.setOpacity(radialOpacity * p.opacity());

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		p.setOpacity(radialOpacity);
		const auto icon = [&]() -> const style::icon * {
			if (_data->waitingForAlbum()) {
				return &(selected ? st::historyFileThumbWaitingSelected : st::historyFileThumbWaiting);
			} else if (streamingMode && !_data->uploading()) {
				return nullptr;
			} else if ((_data->loaded() || canBePlayed) && (!radial || cornerDownload)) {
				return &(selected ? st::historyFileThumbPlaySelected : st::historyFileThumbPlay);
			} else if (radial || _data->loading()) {
				if (!item->isSending() || _data->uploading()) {
					return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
				}
				return nullptr;
			}
			return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
		}();
		const auto previous = [&]() -> const style::icon* {
			if (_data->waitingForAlbum()) {
				return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
			}
			return nullptr;
		}();
		if (icon) {
			if (previous && radialOpacity > 0. && radialOpacity < 1.) {
				PaintInterpolatedIcon(p, *icon, *previous, radialOpacity, inner);
			} else {
				icon->paintInCenter(p, inner);
			}
		}
		p.setOpacity(1);
		if (radial) {
			const auto line = st::historyGroupRadialLine;
			const auto rinner = inner.marginsRemoved({ line, line, line, line });
			const auto fg = selected
				? st::historyFileThumbRadialFgSelected
				: st::historyFileThumbRadialFg;
			if (streamedForWaiting && !_data->uploading()) {
				Ui::InfiniteRadialAnimation::Draw(
					p,
					streamedForWaiting->waitingState(),
					rinner.topLeft(),
					rinner.size(),
					width(),
					fg,
					st::msgFileRadialLine);
			} else if (!cornerDownload) {
				_animation->radial.draw(
					p,
					rinner,
					st::msgFileRadialLine,
					fg);
			}
		}
	}
	if (fullFeatured) {
		drawCornerStatus(p, selected, geometry.topLeft());
	}
}

TextState Gif::getStateGrouped(
		const QRect &geometry,
		RectParts sides,
		QPoint point,
		StateRequest request) const {
	if (!geometry.contains(point)) {
		return {};
	}
	if (fullFeaturedGrouped(sides)) {
		if (const auto state = cornerStatusTextState(point, request, geometry.topLeft()); state.link) {
			return state;
		}
	}
	return TextState(_parent, _data->uploading()
		? _cancell
		: !IsServerMsgId(_realParent->id)
		? nullptr
		: (_data->loaded() || _data->canBePlayed())
		? _openl
		: _data->loading()
		? _cancell
		: _savel);
}

bool Gif::uploading() const {
	return _data->uploading();
}

bool Gif::needsBubble() const {
	if (_data->isVideoMessage()) {
		return false;
	}
	if (!_caption.isEmpty()) {
		return true;
	}
	const auto item = _parent->data();
	return item->viaBot()
		|| item->Has<HistoryMessageReply>()
		|| _parent->displayForwardedFrom()
		|| _parent->displayFromName();
	return false;
}

int Gif::additionalWidth() const {
	const auto item = _parent->data();
	return additionalWidth(
		item->Get<HistoryMessageVia>(),
		item->Get<HistoryMessageReply>(),
		item->Get<HistoryMessageForwarded>());
}

QString Gif::mediaTypeString() const {
	return _data->isVideoMessage()
		? tr::lng_in_dlg_video_message(tr::now)
		: qsl("GIF");
}

bool Gif::isSeparateRoundVideo() const {
	return _data->isVideoMessage()
		&& (_parent->media() == this)
		&& !_parent->hasBubble();
}

void Gif::validateGroupedCache(
		const QRect &geometry,
		RectParts corners,
		not_null<uint64*> cacheKey,
		not_null<QPixmap*> cache) const {
	using Option = Images::Option;
	const auto good = _data->goodThumbnail();
	const auto useGood = (good && good->loaded());
	const auto thumb = _data->thumbnail();
	const auto useThumb = (thumb && thumb->loaded());
	const auto image = useGood
		? good
		: useThumb
		? thumb
		: _data->thumbnailInline();
	const auto blur = !useGood
		&& (!useThumb
			|| (thumb->width() < kUseNonBlurredThreshold)
			|| (thumb->height() < kUseNonBlurredThreshold));
	if (good && !useGood) {
		good->load({});
	}

	const auto loadLevel = useGood ? 3 : useThumb ? 2 : image ? 1 : 0;
	const auto width = geometry.width();
	const auto height = geometry.height();
	const auto options = Option::Smooth
		| Option::RoundedLarge
		| (blur ? Option(0) : Option::Blurred)
		| ((corners & RectPart::TopLeft) ? Option::RoundedTopLeft : Option::None)
		| ((corners & RectPart::TopRight) ? Option::RoundedTopRight : Option::None)
		| ((corners & RectPart::BottomLeft) ? Option::RoundedBottomLeft : Option::None)
		| ((corners & RectPart::BottomRight) ? Option::RoundedBottomRight : Option::None);
	const auto key = (uint64(width) << 48)
		| (uint64(height) << 32)
		| (uint64(options) << 16)
		| (uint64(loadLevel));
	if (*cacheKey == key) {
		return;
	}

	const auto original = sizeForAspectRatio();
	const auto originalWidth = style::ConvertScale(original.width());
	const auto originalHeight = style::ConvertScale(original.height());
	const auto pixSize = Ui::GetImageScaleSizeForGeometry(
		{ originalWidth, originalHeight },
		{ width, height });
	const auto pixWidth = pixSize.width() * cIntRetinaFactor();
	const auto pixHeight = pixSize.height() * cIntRetinaFactor();

	*cacheKey = key;
	*cache = (image ? image : Image::BlankMedia().get())->pixNoCache(
		_realParent->fullId(),
		pixWidth,
		pixHeight,
		options,
		width,
		height);
}

void Gif::setStatusSize(int newSize) const {
	if (newSize < 0) {
		_statusSize = newSize;
		_statusText = formatDurationText(-newSize - 1);
	} else if (_data->isVideoMessage()) {
		_statusSize = newSize;
		_statusText = formatDurationText(_data->getDuration());
	} else {
		File::setStatusSize(
			newSize,
			_data->size,
			_data->isVideoFile() ? _data->getDuration() : -2,
			0);
	}
}

void Gif::updateStatusText() const {
	auto showPause = false;
	auto statusSize = 0;
	auto realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->uploading()) {
		statusSize = _data->uploadingData->offset;
	} else if (!downloadInCorner() && _data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->loaded() || _data->canBePlayed()) {
		statusSize = FileStatusSizeLoaded;
	} else {
		statusSize = FileStatusSizeReady;
	}
	const auto round = activeRoundStreamed();
	const auto own = activeOwnStreamed();
	if (round || (own && own->frozenFrame.isNull() && _data->isVideoFile())) {
		const auto streamed = round ? round : &own->instance;
		const auto state = streamed->player().prepareLegacyState();
		if (state.length) {
			auto position = int64(0);
			if (::Media::Player::IsStoppedAtEnd(state.state)) {
				position = state.length;
			} else if (!::Media::Player::IsStoppedOrStopping(state.state)) {
				position = state.position;
			}
			statusSize = -1 - int((state.length - position) / state.frequency + 1);
		} else {
			statusSize = -1 - _data->getDuration();
		}
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize);
	}
}

QString Gif::additionalInfoString() const {
	if (_data->isVideoMessage()) {
		updateStatusText();
		return _statusText;
	}
	return QString();
}

bool Gif::isReadyForOpen() const {
	return true;
}

void Gif::parentTextUpdated() {
	if (_parent->media() == this) {
		refreshCaption();
		history()->owner().requestViewResize(_parent);
	}
}

void Gif::refreshParentId(not_null<HistoryItem*> realParent) {
	if (_parent->media() == this) {
		refreshCaption();
	}
}

void Gif::refreshCaption() {
	const auto timestampLinksDuration = _data->isVideoFile()
		? _data->getDuration()
		: 0;
	const auto timestampLinkBase = timestampLinksDuration
		? DocumentTimestampLinkBase(_data, _realParent->fullId())
		: QString();
	_caption = createCaption(
			_parent->data(),
			timestampLinksDuration,
			timestampLinkBase);
}

int Gif::additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply, const HistoryMessageForwarded *forwarded) const {
	int result = 0;
	if (forwarded) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + forwarded->text.maxWidth() + st::msgReplyPadding.right());
	} else if (via) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + via->maxWidth + st::msgReplyPadding.left());
	}
	if (reply) {
		accumulate_max(result, st::msgReplyPadding.left() + reply->replyToWidth());
	}
	return result;
}

::Media::Streaming::Instance *Gif::activeRoundStreamed() const {
	return ::Media::Player::instance()->roundVideoStreamed(_parent->data());
}

Gif::Streamed *Gif::activeOwnStreamed() const {
	return (_streamed
		&& _streamed->instance.player().ready()
		&& !_streamed->instance.player().videoSize().isEmpty())
		? _streamed.get()
		: nullptr;
}

::Media::Streaming::Instance *Gif::activeCurrentStreamed() const {
	if (const auto streamed = activeRoundStreamed()) {
		return streamed;
	} else if (const auto owned = activeOwnStreamed()) {
		return &owned->instance;
	}
	return nullptr;
}

::Media::View::PlaybackProgress *Gif::videoPlayback() const {
	return ::Media::Player::instance()->roundVideoPlayback(_parent->data());
}

void Gif::playAnimation(bool autoplay) {
	if (_data->isVideoMessage() && !autoplay) {
		return;
	} else if (_streamed && autoplay) {
		return;
	} else if ((_streamed && autoplayEnabled())
		|| (!autoplay && _data->isVideoFile())) {
		Core::App().showDocument(_data, _parent->data());
		return;
	}
	if (_streamed) {
		stopAnimation();
	} else if (_data->canBePlayed()) {
		if (!autoplayEnabled()) {
			history()->owner().checkPlayingVideoFiles();
		}
		createStreamedPlayer();
	}
}

void Gif::createStreamedPlayer() {
	auto shared = _data->owner().streaming().sharedDocument(
		_data,
		_realParent->fullId());
	if (!shared) {
		return;
	}
	setStreamed(std::make_unique<Streamed>(
		std::move(shared),
		[=] { repaintStreamedContent(); }));

	_streamed->instance.player().updates(
	) | rpl::start_with_next_error([=](::Media::Streaming::Update &&update) {
		handleStreamingUpdate(std::move(update));
	}, [=](::Media::Streaming::Error &&error) {
		handleStreamingError(std::move(error));
	}, _streamed->instance.lifetime());

	if (_streamed->instance.ready()) {
		streamingReady(base::duplicate(_streamed->instance.info()));
	}
	checkStreamedIsStarted();
}

void Gif::startStreamedPlayer() const {
	Expects(_streamed != nullptr);

	auto options = ::Media::Streaming::PlaybackOptions();
	options.audioId = AudioMsgId(_data, _realParent->fullId());
	options.waitForMarkAsShown = true;
	//if (!_streamed->withSound) {
	options.mode = ::Media::Streaming::Mode::Video;
	options.loop = true;
	//}
	_streamed->instance.play(options);
}

void Gif::checkStreamedIsStarted() const {
	if (!_streamed || _streamed->instance.playerLocked()) {
		return;
	} else if (_streamed->instance.paused()) {
		_streamed->instance.resume();
	}
	if (!_streamed->instance.active() && !_streamed->instance.failed()) {
		startStreamedPlayer();
	}
}

void Gif::setStreamed(std::unique_ptr<Streamed> value) {
	const auto removed = (_streamed && !value);
	const auto set = (!_streamed && value);
	if (removed) {
		history()->owner().unregisterPlayingVideoFile(_parent);
	}
	_streamed = std::move(value);
	if (set) {
		history()->owner().registerPlayingVideoFile(_parent);
	}
}

void Gif::handleStreamingUpdate(::Media::Streaming::Update &&update) {
	using namespace ::Media::Streaming;

	update.data.match([&](Information &update) {
		streamingReady(std::move(update));
	}, [&](const PreloadedVideo &update) {
	}, [&](const UpdateVideo &update) {
		repaintStreamedContent();
	}, [&](const PreloadedAudio &update) {
	}, [&](const UpdateAudio &update) {
	}, [&](const WaitingForData &update) {
	}, [&](MutedByOther) {
	}, [&](Finished) {
	});
}

void Gif::handleStreamingError(::Media::Streaming::Error &&error) {
}

void Gif::repaintStreamedContent() {
	const auto own = activeOwnStreamed();
	if (own && !own->frozenFrame.isNull()) {
		return;
	}
	if (App::wnd()->sessionController()->isGifPausedAtLeastFor(Window::GifPauseReason::Any)
		&& !activeRoundStreamed()) {
		return;
	}
	history()->owner().requestViewRepaint(_parent);
}

void Gif::streamingReady(::Media::Streaming::Information &&info) {
	history()->owner().requestViewResize(_parent);
}

void Gif::stopAnimation() {
	if (_streamed) {
		setStreamed(nullptr);
		history()->owner().requestViewResize(_parent);
		_data->unload();
	}
}

int Gif::checkAnimationCount() {
	if (!_streamed) {
		return 0;
	} else if (autoplayEnabled()) {
		return 1;
	}
	stopAnimation();
	return 0;
}

float64 Gif::dataProgress() const {
	return (_data->uploading() || _parent->data()->id > 0)
		? _data->progress()
		: 0;
}

bool Gif::dataFinished() const {
	return (_parent->data()->id > 0)
		? (!_data->loading() && !_data->uploading())
		: false;
}

bool Gif::dataLoaded() const {
	return (_parent->data()->id > 0) ? _data->loaded() : false;
}

bool Gif::needInfoDisplay() const {
	return _parent->data()->isSending()
		|| _data->uploading()
		|| _parent->isUnderCursor();
}

bool Gif::needCornerStatusDisplay() const {
	return _data->isVideoFile()
		|| needInfoDisplay();
}

} // namespace HistoryView
