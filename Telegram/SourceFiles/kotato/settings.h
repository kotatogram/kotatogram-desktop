/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once

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

DeclareSetting(bool, UseNativeDecorations);
[[nodiscard]] bool UseNativeDecorations();

void SetRecentStickersLimit(int limit);
[[nodiscard]] int RecentStickersLimit();
[[nodiscard]] rpl::producer<int> RecentStickersLimitChanges();

DeclareSetting(int, UserpicCornersType);
DeclareSetting(bool, ShowTopBarUserpic);
DeclareSetting(bool, GtkIntegration);
DeclareSetting(bool, DisableTrayCounter);
DeclareSetting(bool, UseTelegramPanelIcon);
DeclareSetting(int, CustomAppIcon);

using DefaultFilterMap = QMap<int, int>;
DeclareRefSetting(DefaultFilterMap, DefaultFilterId);
void SetDefaultFilterId(int account, int filter);
int DefaultFilterId(int account);
bool HasDefaultFilterId(int account);
bool ClearDefaultFilterId(int account);
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
	struct Peer {
		enum Type {
			User    = 0,
			Chat    = 1,
			Channel = 2,
		};

		Type type;
		int32 id;

		inline bool operator==(const Peer& other) {
			return type == other.type
				&& id == other.id;
		}
	};

	int id;
	int ownerId;
	int cloudOrder;
	QString name;
	QString emoticon;
	std::vector<Peer> always;
	std::vector<Peer> never;
	std::vector<Peer> pinned;
	ushort flags;
};

using LocalFolderVector = std::vector<LocalFolder>;
DeclareRefSetting(LocalFolderVector, LocalFolders);
