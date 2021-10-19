/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/settings.h"

#include "base/platform/base_platform_info.h"
#include "platform/platform_file_utilities.h"

bool gKotatoFirstRun = true;

QString gMainFont, gSemiboldFont, gMonospaceFont;
int gFontSize = 0;
bool gSemiboldFontIsBold = false;

#ifdef DESKTOP_APP_USE_PACKAGED_FONTS
bool gUseSystemFont = true;
#else
bool gUseSystemFont = Platform::IsLinux();
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

rpl::variable<int> gShowChatId = 2;
void SetShowChatId(int chatIdType) {
	if (chatIdType >= 0 && chatIdType <= 2) {
		gShowChatId = chatIdType;
	}
}
int ShowChatId() {
	return gShowChatId.current();
}
rpl::producer<int> ShowChatIdChanges() {
	return gShowChatId.changes();
}

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

bool gConfirmBeforeCall = true;

bool gFFmpegMultithread = true;
uint gFFmpegThreadCount = 0;

bool gUseNativeDecorations = false;
bool UseNativeDecorations() {
	static const auto NativeDecorations = cUseNativeDecorations();
	return NativeDecorations;
}

rpl::variable<int> gRecentStickersLimit = 20;
void SetRecentStickersLimit(int limit) {
	if (limit >= 0 && limit <= 200) {
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

bool gQtScale = false;

rpl::variable<Platform::FileDialog::ImplementationType> gFileDialogType = Platform::FileDialog::ImplementationType::Default;
void SetFileDialogType(Platform::FileDialog::ImplementationType t) {
	if (t == Platform::FileDialog::ImplementationType::Count) {
		t = Platform::FileDialog::ImplementationType::Default;
	}
	gFileDialogType = t;
}
Platform::FileDialog::ImplementationType FileDialogType() {
	return gFileDialogType.current();
}
rpl::producer<Platform::FileDialog::ImplementationType> FileDialogTypeChanges() {
	return gFileDialogType.changes();
}

bool gDisableTrayCounter = Platform::IsLinux();
bool gUseTelegramPanelIcon = false;
int gCustomAppIcon = 0;

DefaultFilterMap gDefaultFilterId;
void SetDefaultFilterId(QString account, int filter) {
	if (gDefaultFilterId.contains(account)) {
		gDefaultFilterId[account] = filter;
	} else {
		gDefaultFilterId.insert(account, filter);
	}
}
int DefaultFilterId(QString account) {
	if (gDefaultFilterId.contains(account)) {
		return gDefaultFilterId[account];
	}
	return 0;
}
bool HasDefaultFilterId(QString account) {
	return gDefaultFilterId.contains(account);
}
bool ClearDefaultFilterId(QString account) {
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

bool gForwardRetainSelection = false;
bool gForwardChatOnClick = false;

#if defined TDESKTOP_API_ID && defined TDESKTOP_API_HASH

int gApiId = TDESKTOP_API_ID;
QString gApiHash = QT_STRINGIFY(TDESKTOP_API_HASH);

#else // TDESKTOP_API_ID && TDESKTOP_API_HASH

// To build your version of Telegram Desktop you're required to provide
// your own 'api_id' and 'api_hash' for the Telegram API access.
//
// How to obtain your 'api_id' and 'api_hash' is described here:
// https://core.telegram.org/api/obtaining_api_id
//
// If you're building the application not for deployment,
// but only for test purposes you can comment out the error below.
//
// This will allow you to use TEST ONLY 'api_id' and 'api_hash' which are
// very limited by the Telegram API server.
//
// Your users will start getting internal server errors on login
// if you deploy an app using those 'api_id' and 'api_hash'.

int gApiId = 0;
QString gApiHash;

#endif // TDESKTOP_API_ID && TDESKTOP_API_HASH

bool gUseEnvApi = true;
bool gApiFromStartParams = false;

bool gAutoScrollUnfocused = false;

LocalFolderVector gLocalFolders;

bool gTelegramSitesAutologin = true;

bool gForwardRememberMode = true;

// 0 - quoted
// 1 - unquoted
// 2 - uncaptioned
rpl::variable<int> gForwardMode = 0;
void SetForwardMode(int mode) {
	gForwardMode = mode;
}
int ForwardMode() {
	return gForwardMode.current();
}
rpl::producer<int> ForwardModeChanges() {
	return gForwardMode.changes();
}

// 0 - preserve albums
// 1 - group all media
// 2 - separate messages
rpl::variable<int> gForwardGroupingMode = 0;
void SetForwardGroupingMode(int mode) {
	gForwardGroupingMode = mode;
}
int ForwardGroupingMode() {
	return gForwardGroupingMode.current();
}
rpl::producer<int> ForwardGroupingModeChanges() {
	return gForwardGroupingMode.changes();
}

bool gForwardForceOld = false;

bool gDisableChatThemes = false;
bool gRememberCompressImages = true;
