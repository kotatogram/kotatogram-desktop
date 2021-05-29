/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once

#include <rpl/producer.h>

namespace Platform {
namespace FileDialog {

enum class ImplementationType;

} // namespace FileDialog
} // namespace Platform

#define DeclareReadSetting(Type, Name) extern Type g##Name; \
inline const Type &c##Name() { \
	return g##Name; \
}

#define DeclareSetting(Type, Name) DeclareReadSetting(Type, Name) \
inline void cSet##Name(const Type &Name) { \
	g##Name = Name; \
}

#define DeclareRefSetting(Type, Name) DeclareSetting(Type, Name) \
inline Type &cRef##Name() { \
	return g##Name; \
}

DeclareSetting(bool, KotatoFirstRun);

DeclareSetting(QString, MainFont);
DeclareSetting(QString, SemiboldFont);
DeclareSetting(bool, SemiboldFontIsBold);
DeclareSetting(QString, MonospaceFont);
DeclareSetting(bool, UseSystemFont);
DeclareSetting(bool, UseOriginalMetrics);

void SetBigEmojiOutline(bool enabled);
[[nodiscard]] bool BigEmojiOutline();
[[nodiscard]] rpl::producer<bool> BigEmojiOutlineChanges();

void SetStickerHeight(int height);
[[nodiscard]] int StickerHeight();
[[nodiscard]] rpl::producer<int> StickerHeightChanges();

void SetStickerScaleBoth(bool scale);
[[nodiscard]] bool StickerScaleBoth();
[[nodiscard]] rpl::producer<bool> StickerScaleBothChanges();

void SetAdaptiveBubbles(bool enabled);
[[nodiscard]] bool AdaptiveBubbles();
[[nodiscard]] rpl::producer<bool> AdaptiveBubblesChanges();

void SetMonospaceLargeBubbles(bool enabled);
[[nodiscard]] bool MonospaceLargeBubbles();
[[nodiscard]] rpl::producer<bool> MonospaceLargeBubblesChanges();

DeclareSetting(bool, AlwaysShowScheduled);
DeclareSetting(int, ShowChatId);

DeclareSetting(int, NetSpeedBoost);
DeclareSetting(int, NetRequestsCount);
DeclareSetting(int, NetUploadSessionsCount);
DeclareSetting(int, NetUploadRequestInterval);

inline void SetNetworkBoost(int boost) {
	if (boost < 0) {
		cSetNetSpeedBoost(0);
	} else if (boost > 3) {
		cSetNetSpeedBoost(3);
	} else {
		cSetNetSpeedBoost(boost);
	}

	cSetNetRequestsCount(2 + (2 * cNetSpeedBoost()));
	cSetNetUploadSessionsCount(2 + (2 * cNetSpeedBoost()));
	cSetNetUploadRequestInterval(500 - (100 * cNetSpeedBoost()));
}

DeclareSetting(bool, ShowPhoneInDrawer);

using ScaleVector = std::vector<int>;
DeclareRefSetting(ScaleVector, InterfaceScales);
bool HasCustomScales();
bool AddCustomScale(int scale);
void ClearCustomScales();

void SetDialogListLines(int lines);
[[nodiscard]] int DialogListLines();
[[nodiscard]] rpl::producer<int> DialogListLinesChanges();

DeclareSetting(bool, DisableUpEdit);

using CustomReplacementsMap = QMap<QString, QString>;
DeclareRefSetting(CustomReplacementsMap, CustomReplaces);
bool AddCustomReplace(QString from, QString to);
DeclareSetting(bool, ConfirmBeforeCall);

DeclareSetting(bool, FFmpegMultithread);
DeclareSetting(uint, FFmpegThreadCount);

DeclareSetting(bool, UseNativeDecorations);
[[nodiscard]] bool UseNativeDecorations();

void SetRecentStickersLimit(int limit);
[[nodiscard]] int RecentStickersLimit();
[[nodiscard]] rpl::producer<int> RecentStickersLimitChanges();

DeclareSetting(int, UserpicCornersType);
DeclareSetting(bool, ShowTopBarUserpic);

DeclareSetting(bool, QtScale);
DeclareSetting(bool, GtkIntegration);

void SetFileDialogType(Platform::FileDialog::ImplementationType t);
[[nodiscard]] Platform::FileDialog::ImplementationType FileDialogType();
[[nodiscard]] rpl::producer<Platform::FileDialog::ImplementationType> FileDialogTypeChanges();

DeclareSetting(bool, DisableTrayCounter);
DeclareSetting(bool, UseTelegramPanelIcon);
DeclareSetting(int, CustomAppIcon);

using DefaultFilterMap = QMap<QString, int>;
DeclareRefSetting(DefaultFilterMap, DefaultFilterId);
void SetDefaultFilterId(QString account, int filter);
int DefaultFilterId(QString account);
bool HasDefaultFilterId(QString account);
bool ClearDefaultFilterId(QString account);
DeclareSetting(bool, UnmutedFilterCounterOnly);
DeclareSetting(bool, HideFilterEditButton);
DeclareSetting(bool, HideFilterNames);
DeclareSetting(bool, HideFilterAllChats);

DeclareSetting(bool, ProfileTopBarNotifications);

void SetHoverEmojiPanel(bool enabled);
[[nodiscard]] bool HoverEmojiPanel();
[[nodiscard]] rpl::producer<bool> HoverEmojiPanelChanges();

DeclareSetting(bool, ForwardRetainSelection);
DeclareSetting(bool, ForwardChatOnClick);

DeclareSetting(int, ApiId);
DeclareSetting(QString, ApiHash);
DeclareSetting(bool, UseEnvApi);
DeclareSetting(bool, ApiFromStartParams);

DeclareSetting(bool, ForwardQuoted);
DeclareSetting(bool, ForwardCaptioned);
DeclareSetting(bool, ForwardAlbumsAsIs);
DeclareSetting(bool, ForwardGrouped);

struct LocalFolder {
	int id = 0;
	uint64 ownerId = 0;
	bool isTest = false;
	int cloudOrder = 0;
	QString name;
	QString emoticon;
	std::vector<uint64> always;
	std::vector<uint64> never;
	std::vector<uint64> pinned;
	ushort flags = 0;
};

using LocalFolderVector = std::vector<LocalFolder>;
DeclareRefSetting(LocalFolderVector, LocalFolders);
