/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/settings.h"

bool gKotatoFirstRun = true;

QString gMainFont, gSemiboldFont, gMonospaceFont;
bool gSemiboldFontIsBold = false;

#ifdef DESKTOP_APP_USE_PACKAGED_FONTS
bool gUseSystemFont = true;
#else
bool gUseSystemFont = false;
#endif

bool gUseOriginalMetrics = false;

rpl::variable<int> gStickerHeight = 170;
void SetStickerHeight(int height) {
	gStickerHeight = height;
}
int StickerHeight() {
	return gStickerHeight.current();
}
rpl::producer<int> StickerHeightChanges() {
	return gStickerHeight.changes();
}

rpl::variable<bool> gStickerScaleBoth = true;
void SetStickerScaleBoth(bool scale) {
	gStickerScaleBoth = scale;
}
bool StickerScaleBoth() {
	return gStickerScaleBoth.current();
}
rpl::producer<bool> StickerScaleBothChanges() {
	return gStickerScaleBoth.changes();
}

rpl::variable<bool> gBigEmojiOutline = true;
void SetBigEmojiOutline(bool enabled) {
	gBigEmojiOutline = enabled;
}
bool BigEmojiOutline() {
	return gBigEmojiOutline.current();
}
rpl::producer<bool> BigEmojiOutlineChanges() {
	return gBigEmojiOutline.changes();
}

rpl::variable<bool> gAdaptiveBubbles = false;
void SetAdaptiveBubbles(bool enabled) {
	gAdaptiveBubbles = enabled;
}
bool AdaptiveBubbles() {
	return gAdaptiveBubbles.current();
}
rpl::producer<bool> AdaptiveBubblesChanges() {
	return gAdaptiveBubbles.changes();
}

rpl::variable<bool> gMonospaceLargeBubbles = false;
void SetMonospaceLargeBubbles(bool enabled) {
	gMonospaceLargeBubbles = enabled;
}
bool MonospaceLargeBubbles() {
	return gMonospaceLargeBubbles.current();
}
rpl::producer<bool> MonospaceLargeBubblesChanges() {
	return gMonospaceLargeBubbles.changes();
}

bool gAlwaysShowScheduled = false;
int gShowChatId = 0;

int gNetSpeedBoost = 0;
int gNetRequestsCount = 2;
int gNetUploadSessionsCount = 2;
int gNetUploadRequestInterval = 500;

bool gShowPhoneInDrawer = true;

ScaleVector gInterfaceScales;

bool HasCustomScales() {
	return (!gInterfaceScales.empty());
}

bool AddCustomScale(int scale) {
	if (gInterfaceScales.size() >= 6) {
		return false;
	}
	gInterfaceScales.push_back(style::CheckScale(scale));
	return true;
}

void ClearCustomScales() {
	gInterfaceScales.clear();
}

rpl::variable<int> gDialogListLines = 2;
void SetDialogListLines(int lines) {
	gDialogListLines = lines;
}
int DialogListLines() {
	return gDialogListLines.current();
}
rpl::producer<int> DialogListLinesChanges() {
	return gDialogListLines.changes();
}

bool gDisableUpEdit = false;

CustomReplacementsMap gCustomReplaces;
bool AddCustomReplace(QString from, QString to) {
	gCustomReplaces.insert(from, to);
	return true;
}

bool gConfirmBeforeCall = false;

bool gUseNativeDecorations = false;
bool UseNativeDecorations() {
	static const auto NativeDecorations = cUseNativeDecorations();
	return NativeDecorations;
}

rpl::variable<int> gRecentStickersLimit = 20;
void SetRecentStickersLimit(int limit) {
	if (limit >= 0 || limit <= 200) {
		gRecentStickersLimit = limit;
	}
}
int RecentStickersLimit() {
	return gRecentStickersLimit.current();
}
rpl::producer<int> RecentStickersLimitChanges() {
	return gRecentStickersLimit.changes();
}

int gUserpicCornersType = 3;
bool gShowTopBarUserpic = false;
bool gDisableTrayCounter = false;
bool gUseTelegramPanelIcon = false;
int gCustomAppIcon = 0;

DefaultFilterMap gDefaultFilterId;
void SetDefaultFilterId(int account, int filter) {
	if (gDefaultFilterId.contains(account)) {
		gDefaultFilterId[account] = filter;
	} else {
		gDefaultFilterId.insert(account, filter);
	}
}
int DefaultFilterId(int account) {
	if (gDefaultFilterId.contains(account)) {
		return gDefaultFilterId[account];
	}
	return 0;
}
bool HasDefaultFilterId(int account) {
	return gDefaultFilterId.contains(account);
}
bool ClearDefaultFilterId(int account) {
	return gDefaultFilterId.remove(account);
}
bool gUnmutedFilterCounterOnly = false;
bool gHideFilterEditButton = false;
bool gHideFilterNames = false;
bool gHideFilterAllChats = false;

bool gProfileTopBarNotifications = false;

rpl::variable<bool> gHoverEmojiPanel = true;
void SetHoverEmojiPanel(bool enabled) {
	gHoverEmojiPanel = enabled;
}
bool HoverEmojiPanel() {
	return gHoverEmojiPanel.current();
}
rpl::producer<bool> HoverEmojiPanelChanges() {
	return gHoverEmojiPanel.changes();
}
