/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/cached_round_corners.h"
#include "ui/style/style_core_palette.h"
#include "layout/layout_selection.h"

enum class ImageRoundRadius;

namespace style {
struct TwoIconButton;
struct ScrollArea;
} // namespace style

namespace Ui {

class ChatTheme;
class ChatStyle;
struct BubblePattern;

struct MessageStyle {
	CornersPixmaps msgBgCorners;
	style::color msgBg;
	style::color msgShadow;
	style::color msgServiceFg;
	style::color msgDateFg;
	style::color msgFileThumbLinkFg;
	style::color msgFileBg;
	style::color msgReplyBarColor;
	style::color msgWaveformActive;
	style::color msgWaveformInactive;
	style::color historyTextFg;
	style::color historyFileNameFg;
	style::color historyFileRadialFg;
	style::color mediaFg;
	style::TextPalette textPalette;
	style::TextPalette semiboldPalette;
	style::TextPalette fwdTextPalette;
	style::TextPalette replyTextPalette;
	style::icon tailLeft = { Qt::Uninitialized };
	style::icon tailRight = { Qt::Uninitialized };
	style::icon historyRepliesIcon = { Qt::Uninitialized };
	style::icon historyViewsIcon = { Qt::Uninitialized };
	style::icon historyPinIcon = { Qt::Uninitialized };
	style::icon historySentIcon = { Qt::Uninitialized };
	style::icon historyReceivedIcon = { Qt::Uninitialized };
	style::icon historyPsaIcon = { Qt::Uninitialized };
	style::icon historyCommentsOpen = { Qt::Uninitialized };
	style::icon historyComments = { Qt::Uninitialized };
	style::icon historyCallArrow = { Qt::Uninitialized };
	style::icon historyCallArrowMissed = { Qt::Uninitialized };
	style::icon historyCallIcon = { Qt::Uninitialized };
	style::icon historyCallCameraIcon = { Qt::Uninitialized };
	style::icon historyFilePlay = { Qt::Uninitialized };
	style::icon historyFileWaiting = { Qt::Uninitialized };
	style::icon historyFileDownload = { Qt::Uninitialized };
	style::icon historyFileCancel = { Qt::Uninitialized };
	style::icon historyFilePause = { Qt::Uninitialized };
	style::icon historyFileImage = { Qt::Uninitialized };
	style::icon historyFileDocument = { Qt::Uninitialized };
	style::icon historyAudioDownload = { Qt::Uninitialized };
	style::icon historyAudioCancel = { Qt::Uninitialized };
	style::icon historyQuizTimer = { Qt::Uninitialized };
	style::icon historyQuizExplain = { Qt::Uninitialized };
	style::icon historyPollChosen = { Qt::Uninitialized };
	style::icon historyPollChoiceRight = { Qt::Uninitialized };
};

struct MessageImageStyle {
	CornersPixmaps msgDateImgBgCorners;
	CornersPixmaps msgServiceBgCorners;
	CornersPixmaps msgShadowCorners;
	style::color msgServiceBg;
	style::color msgDateImgBg;
	style::color msgShadow;
	style::color historyFileThumbRadialFg;
	style::icon historyFileThumbPlay = { Qt::Uninitialized };
	style::icon historyFileThumbWaiting = { Qt::Uninitialized };
	style::icon historyFileThumbDownload = { Qt::Uninitialized };
	style::icon historyFileThumbCancel = { Qt::Uninitialized };
	style::icon historyFileThumbPause = { Qt::Uninitialized };
	style::icon historyVideoDownload = { Qt::Uninitialized };
	style::icon historyVideoCancel = { Qt::Uninitialized };
	style::icon historyVideoMessageMute = { Qt::Uninitialized };
};

struct ReactionPaintInfo {
	QPoint position;
	QPoint effectOffset;
	Fn<QRect(QPainter&)> effectPaint;
};

struct ChatPaintContext {
	not_null<const ChatStyle*> st;
	const BubblePattern *bubblesPattern = nullptr;
	ReactionPaintInfo *reactionInfo = nullptr;
	QRect viewport;
	QRect clip;
	TextSelection selection;
	bool outbg = false;
	crl::time now = 0;

	void translate(int x, int y) {
		viewport.translate(x, y);
		clip.translate(x, y);
	}
	void translate(QPoint point) {
		translate(point.x(), point.y());
	}

	[[nodiscard]] bool selected() const {
		return (selection == FullSelection);
	}
	[[nodiscard]] not_null<const MessageStyle*> messageStyle() const;
	[[nodiscard]] not_null<const MessageImageStyle*> imageStyle() const;

	[[nodiscard]] ChatPaintContext translated(int x, int y) const {
		auto result = *this;
		result.translate(x, y);
		return result;
	}
	[[nodiscard]] ChatPaintContext translated(QPoint point) const {
		return translated(point.x(), point.y());
	}
	[[nodiscard]] ChatPaintContext withSelection(
		TextSelection selection) const {
		auto result = *this;
		result.selection = selection;
		return result;
	}

};

[[nodiscard]] int HistoryServiceMsgRadius();
[[nodiscard]] int HistoryServiceMsgInvertedRadius();
[[nodiscard]] int HistoryServiceMsgInvertedShrink();

class ChatStyle final : public style::palette {
public:
	ChatStyle();

	void apply(not_null<ChatTheme*> theme);

	[[nodiscard]] rpl::producer<> paletteChanged() const {
		return _paletteChanged.events();
	}

	template <typename Type>
	[[nodiscard]] Type value(const Type &original) const {
		auto my = Type();
		make(my, original);
		return my;
	}

	template <typename Type>
	[[nodiscard]] const Type &value(
			rpl::lifetime &parentLifetime,
			const Type &original) const {
		const auto my = parentLifetime.make_state<Type>();
		make(*my, original);
		return *my;
	}

	[[nodiscard]] const CornersPixmaps &serviceBgCornersNormal() const;
	[[nodiscard]] const CornersPixmaps &serviceBgCornersInverted() const;

	[[nodiscard]] const MessageStyle &messageStyle(
		bool outbg,
		bool selected) const;
	[[nodiscard]] const MessageImageStyle &imageStyle(bool selected) const;

	[[nodiscard]] const CornersPixmaps &msgBotKbOverBgAddCorners() const;
	[[nodiscard]] const CornersPixmaps &msgSelectOverlayCornersSmall() const;
	[[nodiscard]] const CornersPixmaps &msgSelectOverlayCornersLarge() const;

	[[nodiscard]] const style::TextPalette &historyPsaForwardPalette() const {
		return _historyPsaForwardPalette;
	}
	[[nodiscard]] const style::TextPalette &imgReplyTextPalette() const {
		return _imgReplyTextPalette;
	}
	[[nodiscard]] const style::TextPalette &serviceTextPalette() const {
		return _serviceTextPalette;
	}
	[[nodiscard]] const style::icon &historyRepliesInvertedIcon() const {
		return _historyRepliesInvertedIcon;
	}
	[[nodiscard]] const style::icon &historyViewsInvertedIcon() const {
		return _historyViewsInvertedIcon;
	}
	[[nodiscard]] const style::icon &historyViewsSendingIcon() const {
		return _historyViewsSendingIcon;
	}
	[[nodiscard]] const style::icon &historyViewsSendingInvertedIcon() const {
		return _historyViewsSendingInvertedIcon;
	}
	[[nodiscard]] const style::icon &historyPinInvertedIcon() const {
		return _historyPinInvertedIcon;
	}
	[[nodiscard]] const style::icon &historySendingIcon() const {
		return _historySendingIcon;
	}
	[[nodiscard]] const style::icon &historySendingInvertedIcon() const {
		return _historySendingInvertedIcon;
	}
	[[nodiscard]] const style::icon &historySentInvertedIcon() const {
		return _historySentInvertedIcon;
	}
	[[nodiscard]] const style::icon &historyReceivedInvertedIcon() const {
		return _historyReceivedInvertedIcon;
	}
	[[nodiscard]] const style::icon &msgBotKbUrlIcon() const {
		return _msgBotKbUrlIcon;
	}
	[[nodiscard]] const style::icon &msgBotKbPaymentIcon() const {
		return _msgBotKbPaymentIcon;
	}
	[[nodiscard]] const style::icon &msgBotKbSwitchPmIcon() const {
		return _msgBotKbSwitchPmIcon;
	}
	[[nodiscard]] const style::icon &historyFastCommentsIcon() const {
		return _historyFastCommentsIcon;
	}
	[[nodiscard]] const style::icon &historyFastShareIcon() const {
		return _historyFastShareIcon;
	}
	[[nodiscard]] const style::icon &historyGoToOriginalIcon() const {
		return _historyGoToOriginalIcon;
	}
	[[nodiscard]] const style::icon &historyMapPoint() const {
		return _historyMapPoint;
	}
	[[nodiscard]] const style::icon &historyMapPointInner() const {
		return _historyMapPointInner;
	}
	[[nodiscard]] const style::icon &youtubeIcon() const {
		return _youtubeIcon;
	}
	[[nodiscard]] const style::icon &videoIcon() const {
		return _videoIcon;
	}
	[[nodiscard]] const style::icon &historyPollChoiceRight() const {
		return _historyPollChoiceRight;
	}
	[[nodiscard]] const style::icon &historyPollChoiceWrong() const {
		return _historyPollChoiceWrong;
	}
	[[nodiscard]] const style::icon &msgNameChat1Icon() const {
		return _msgNameChat1Icon;
	}
	[[nodiscard]] const style::icon &msgNameChat1IconSelected() const {
		return _msgNameChat1IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameChat2Icon() const {
		return _msgNameChat2Icon;
	}
	[[nodiscard]] const style::icon &msgNameChat2IconSelected() const {
		return _msgNameChat2IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameChat3Icon() const {
		return _msgNameChat3Icon;
	}
	[[nodiscard]] const style::icon &msgNameChat3IconSelected() const {
		return _msgNameChat3IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameChat4Icon() const {
		return _msgNameChat4Icon;
	}
	[[nodiscard]] const style::icon &msgNameChat4IconSelected() const {
		return _msgNameChat4IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameChat5Icon() const {
		return _msgNameChat5Icon;
	}
	[[nodiscard]] const style::icon &msgNameChat5IconSelected() const {
		return _msgNameChat5IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameChat6Icon() const {
		return _msgNameChat6Icon;
	}
	[[nodiscard]] const style::icon &msgNameChat6IconSelected() const {
		return _msgNameChat6IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameChat7Icon() const {
		return _msgNameChat7Icon;
	}
	[[nodiscard]] const style::icon &msgNameChat7IconSelected() const {
		return _msgNameChat7IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameChat8Icon() const {
		return _msgNameChat8Icon;
	}
	[[nodiscard]] const style::icon &msgNameChat8IconSelected() const {
		return _msgNameChat8IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameChannel1Icon() const {
		return _msgNameChannel1Icon;
	}
	[[nodiscard]] const style::icon &msgNameChannel1IconSelected() const {
		return _msgNameChannel1IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameChannel2Icon() const {
		return _msgNameChannel2Icon;
	}
	[[nodiscard]] const style::icon &msgNameChannel2IconSelected() const {
		return _msgNameChannel2IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameChannel3Icon() const {
		return _msgNameChannel3Icon;
	}
	[[nodiscard]] const style::icon &msgNameChannel3IconSelected() const {
		return _msgNameChannel3IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameChannel4Icon() const {
		return _msgNameChannel4Icon;
	}
	[[nodiscard]] const style::icon &msgNameChannel4IconSelected() const {
		return _msgNameChannel4IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameChannel5Icon() const {
		return _msgNameChannel5Icon;
	}
	[[nodiscard]] const style::icon &msgNameChannel5IconSelected() const {
		return _msgNameChannel5IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameChannel6Icon() const {
		return _msgNameChannel6Icon;
	}
	[[nodiscard]] const style::icon &msgNameChannel6IconSelected() const {
		return _msgNameChannel6IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameChannel7Icon() const {
		return _msgNameChannel7Icon;
	}
	[[nodiscard]] const style::icon &msgNameChannel7IconSelected() const {
		return _msgNameChannel7IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameChannel8Icon() const {
		return _msgNameChannel8Icon;
	}
	[[nodiscard]] const style::icon &msgNameChannel8IconSelected() const {
		return _msgNameChannel8IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameBot1Icon() const {
		return _msgNameBot1Icon;
	}
	[[nodiscard]] const style::icon &msgNameBot1IconSelected() const {
		return _msgNameBot1IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameBot2Icon() const {
		return _msgNameBot2Icon;
	}
	[[nodiscard]] const style::icon &msgNameBot2IconSelected() const {
		return _msgNameBot2IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameBot3Icon() const {
		return _msgNameBot3Icon;
	}
	[[nodiscard]] const style::icon &msgNameBot3IconSelected() const {
		return _msgNameBot3IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameBot4Icon() const {
		return _msgNameBot4Icon;
	}
	[[nodiscard]] const style::icon &msgNameBot4IconSelected() const {
		return _msgNameBot4IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameBot5Icon() const {
		return _msgNameBot5Icon;
	}
	[[nodiscard]] const style::icon &msgNameBot5IconSelected() const {
		return _msgNameBot5IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameBot6Icon() const {
		return _msgNameBot6Icon;
	}
	[[nodiscard]] const style::icon &msgNameBot6IconSelected() const {
		return _msgNameBot6IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameBot7Icon() const {
		return _msgNameBot7Icon;
	}
	[[nodiscard]] const style::icon &msgNameBot7IconSelected() const {
		return _msgNameBot7IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameBot8Icon() const {
		return _msgNameBot8Icon;
	}
	[[nodiscard]] const style::icon &msgNameBot8IconSelected() const {
		return _msgNameBot8IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameDeleted1Icon() const {
		return _msgNameDeleted1Icon;
	}
	[[nodiscard]] const style::icon &msgNameDeleted1IconSelected() const {
		return _msgNameDeleted1IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameDeleted2Icon() const {
		return _msgNameDeleted2Icon;
	}
	[[nodiscard]] const style::icon &msgNameDeleted2IconSelected() const {
		return _msgNameDeleted2IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameDeleted3Icon() const {
		return _msgNameDeleted3Icon;
	}
	[[nodiscard]] const style::icon &msgNameDeleted3IconSelected() const {
		return _msgNameDeleted3IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameDeleted4Icon() const {
		return _msgNameDeleted4Icon;
	}
	[[nodiscard]] const style::icon &msgNameDeleted4IconSelected() const {
		return _msgNameDeleted4IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameDeleted5Icon() const {
		return _msgNameDeleted5Icon;
	}
	[[nodiscard]] const style::icon &msgNameDeleted5IconSelected() const {
		return _msgNameDeleted5IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameDeleted6Icon() const {
		return _msgNameDeleted6Icon;
	}
	[[nodiscard]] const style::icon &msgNameDeleted6IconSelected() const {
		return _msgNameDeleted6IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameDeleted7Icon() const {
		return _msgNameDeleted7Icon;
	}
	[[nodiscard]] const style::icon &msgNameDeleted7IconSelected() const {
		return _msgNameDeleted7IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameDeleted8Icon() const {
		return _msgNameDeleted8Icon;
	}
	[[nodiscard]] const style::icon &msgNameDeleted8IconSelected() const {
		return _msgNameDeleted8IconSelected;
	}
	[[nodiscard]] const style::icon &msgNameSponsoredIcon() const {
		return _msgNameSponsoredIcon;
	}
	[[nodiscard]] const style::icon &msgNameSponsoredIconSelected() const {
		return _msgNameSponsoredIconSelected;
	}

private:
	void assignPalette(not_null<const style::palette*> palette);

	void make(style::color &my, const style::color &original) const;
	void make(style::icon &my, const style::icon &original) const;
	void make(
		style::TextPalette &my,
		const style::TextPalette &original) const;
	void make(
		style::TwoIconButton &my,
		const style::TwoIconButton &original) const;
	void make(
		style::ScrollArea &my,
		const style::ScrollArea &original) const;

	[[nodiscard]] MessageStyle &messageStyleRaw(
		bool outbg,
		bool selected) const;
	[[nodiscard]] MessageStyle &messageIn();
	[[nodiscard]] MessageStyle &messageInSelected();
	[[nodiscard]] MessageStyle &messageOut();
	[[nodiscard]] MessageStyle &messageOutSelected();

	[[nodiscard]] MessageImageStyle &imageStyleRaw(bool selected) const;
	[[nodiscard]] MessageImageStyle &image();
	[[nodiscard]] MessageImageStyle &imageSelected();

	template <typename Type>
	void make(
		Type MessageStyle::*my,
		const Type &originalIn,
		const Type &originalInSelected,
		const Type &originalOut,
		const Type &originalOutSelected);

	template <typename Type>
	void make(
		Type MessageImageStyle::*my,
		const Type &original,
		const Type &originalSelected);

	mutable CornersPixmaps _serviceBgCornersNormal;
	mutable CornersPixmaps _serviceBgCornersInverted;

	mutable std::array<MessageStyle, 4> _messageStyles;
	mutable std::array<MessageImageStyle, 2> _imageStyles;

	mutable CornersPixmaps _msgBotKbOverBgAddCorners;
	mutable CornersPixmaps _msgSelectOverlayCornersSmall;
	mutable CornersPixmaps _msgSelectOverlayCornersLarge;

	style::TextPalette _historyPsaForwardPalette;
	style::TextPalette _imgReplyTextPalette;
	style::TextPalette _serviceTextPalette;
	style::icon _historyRepliesInvertedIcon = { Qt::Uninitialized };
	style::icon _historyViewsInvertedIcon = { Qt::Uninitialized };
	style::icon _historyViewsSendingIcon = { Qt::Uninitialized };
	style::icon _historyViewsSendingInvertedIcon = { Qt::Uninitialized };
	style::icon _historyPinInvertedIcon = { Qt::Uninitialized };
	style::icon _historySendingIcon = { Qt::Uninitialized };
	style::icon _historySendingInvertedIcon = { Qt::Uninitialized };
	style::icon _historySentInvertedIcon = { Qt::Uninitialized };
	style::icon _historyReceivedInvertedIcon = { Qt::Uninitialized };
	style::icon _msgBotKbUrlIcon = { Qt::Uninitialized };
	style::icon _msgBotKbPaymentIcon = { Qt::Uninitialized };
	style::icon _msgBotKbSwitchPmIcon = { Qt::Uninitialized };
	style::icon _historyFastCommentsIcon = { Qt::Uninitialized };
	style::icon _historyFastShareIcon = { Qt::Uninitialized };
	style::icon _historyGoToOriginalIcon = { Qt::Uninitialized };
	style::icon _historyMapPoint = { Qt::Uninitialized };
	style::icon _historyMapPointInner = { Qt::Uninitialized };
	style::icon _youtubeIcon = { Qt::Uninitialized };
	style::icon _videoIcon = { Qt::Uninitialized };
	style::icon _historyPollChoiceRight = { Qt::Uninitialized };
	style::icon _historyPollChoiceWrong = { Qt::Uninitialized };
	style::icon _msgNameChat1Icon = { Qt::Uninitialized };
	style::icon _msgNameChat1IconSelected = { Qt::Uninitialized };
	style::icon _msgNameChat2Icon = { Qt::Uninitialized };
	style::icon _msgNameChat2IconSelected = { Qt::Uninitialized };
	style::icon _msgNameChat3Icon = { Qt::Uninitialized };
	style::icon _msgNameChat3IconSelected = { Qt::Uninitialized };
	style::icon _msgNameChat4Icon = { Qt::Uninitialized };
	style::icon _msgNameChat4IconSelected = { Qt::Uninitialized };
	style::icon _msgNameChat5Icon = { Qt::Uninitialized };
	style::icon _msgNameChat5IconSelected = { Qt::Uninitialized };
	style::icon _msgNameChat6Icon = { Qt::Uninitialized };
	style::icon _msgNameChat6IconSelected = { Qt::Uninitialized };
	style::icon _msgNameChat7Icon = { Qt::Uninitialized };
	style::icon _msgNameChat7IconSelected = { Qt::Uninitialized };
	style::icon _msgNameChat8Icon = { Qt::Uninitialized };
	style::icon _msgNameChat8IconSelected = { Qt::Uninitialized };
	style::icon _msgNameChannel1Icon = { Qt::Uninitialized };
	style::icon _msgNameChannel1IconSelected = { Qt::Uninitialized };
	style::icon _msgNameChannel2Icon = { Qt::Uninitialized };
	style::icon _msgNameChannel2IconSelected = { Qt::Uninitialized };
	style::icon _msgNameChannel3Icon = { Qt::Uninitialized };
	style::icon _msgNameChannel3IconSelected = { Qt::Uninitialized };
	style::icon _msgNameChannel4Icon = { Qt::Uninitialized };
	style::icon _msgNameChannel4IconSelected = { Qt::Uninitialized };
	style::icon _msgNameChannel5Icon = { Qt::Uninitialized };
	style::icon _msgNameChannel5IconSelected = { Qt::Uninitialized };
	style::icon _msgNameChannel6Icon = { Qt::Uninitialized };
	style::icon _msgNameChannel6IconSelected = { Qt::Uninitialized };
	style::icon _msgNameChannel7Icon = { Qt::Uninitialized };
	style::icon _msgNameChannel7IconSelected = { Qt::Uninitialized };
	style::icon _msgNameChannel8Icon = { Qt::Uninitialized };
	style::icon _msgNameChannel8IconSelected = { Qt::Uninitialized };
	style::icon _msgNameBot1Icon = { Qt::Uninitialized };
	style::icon _msgNameBot1IconSelected = { Qt::Uninitialized };
	style::icon _msgNameBot2Icon = { Qt::Uninitialized };
	style::icon _msgNameBot2IconSelected = { Qt::Uninitialized };
	style::icon _msgNameBot3Icon = { Qt::Uninitialized };
	style::icon _msgNameBot3IconSelected = { Qt::Uninitialized };
	style::icon _msgNameBot4Icon = { Qt::Uninitialized };
	style::icon _msgNameBot4IconSelected = { Qt::Uninitialized };
	style::icon _msgNameBot5Icon = { Qt::Uninitialized };
	style::icon _msgNameBot5IconSelected = { Qt::Uninitialized };
	style::icon _msgNameBot6Icon = { Qt::Uninitialized };
	style::icon _msgNameBot6IconSelected = { Qt::Uninitialized };
	style::icon _msgNameBot7Icon = { Qt::Uninitialized };
	style::icon _msgNameBot7IconSelected = { Qt::Uninitialized };
	style::icon _msgNameBot8Icon = { Qt::Uninitialized };
	style::icon _msgNameBot8IconSelected = { Qt::Uninitialized };
	style::icon _msgNameDeleted1Icon = { Qt::Uninitialized };
	style::icon _msgNameDeleted1IconSelected = { Qt::Uninitialized };
	style::icon _msgNameDeleted2Icon = { Qt::Uninitialized };
	style::icon _msgNameDeleted2IconSelected = { Qt::Uninitialized };
	style::icon _msgNameDeleted3Icon = { Qt::Uninitialized };
	style::icon _msgNameDeleted3IconSelected = { Qt::Uninitialized };
	style::icon _msgNameDeleted4Icon = { Qt::Uninitialized };
	style::icon _msgNameDeleted4IconSelected = { Qt::Uninitialized };
	style::icon _msgNameDeleted5Icon = { Qt::Uninitialized };
	style::icon _msgNameDeleted5IconSelected = { Qt::Uninitialized };
	style::icon _msgNameDeleted6Icon = { Qt::Uninitialized };
	style::icon _msgNameDeleted6IconSelected = { Qt::Uninitialized };
	style::icon _msgNameDeleted7Icon = { Qt::Uninitialized };
	style::icon _msgNameDeleted7IconSelected = { Qt::Uninitialized };
	style::icon _msgNameDeleted8Icon = { Qt::Uninitialized };
	style::icon _msgNameDeleted8IconSelected = { Qt::Uninitialized };
	style::icon _msgNameSponsoredIcon = { Qt::Uninitialized };
	style::icon _msgNameSponsoredIconSelected = { Qt::Uninitialized };

	rpl::event_stream<> _paletteChanged;

	rpl::lifetime _defaultPaletteChangeLifetime;

};

void FillComplexOverlayRect(
	Painter &p,
	not_null<const ChatStyle*> st,
	QRect rect,
	ImageRoundRadius radius,
	RectParts roundCorners);
void FillComplexLocationRect(
	Painter &p,
	not_null<const ChatStyle*> st,
	QRect rect,
	ImageRoundRadius radius,
	RectParts roundCorners);

} // namespace Ui
