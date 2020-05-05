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
bool gNoTaskbarFlashing = false;

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
int gCustomAppIcon = 0;

int gDefaultFilterId = 0;
bool gUnmutedFilterCounterOnly = false;
bool gHideFilterEditButton = false;
bool gHideFilterNames = false;
bool gHideFilterAllChats = false;

bool gProfileTopBarNotifications = false;
