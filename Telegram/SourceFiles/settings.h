/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/style/style_core.h"
#include "emoji.h"

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

DeclareSetting(Qt::LayoutDirection, LangDir);
inline bool rtl() {
	return style::RightToLeft();
}

DeclareSetting(bool, InstallBetaVersion);
DeclareSetting(uint64, AlphaVersion);
DeclareSetting(uint64, RealAlphaVersion);
DeclareSetting(QByteArray, AlphaPrivateKey);

DeclareSetting(bool, TestMode);
DeclareSetting(QString, LoggedPhoneNumber);
DeclareSetting(bool, AutoStart);
DeclareSetting(bool, StartMinimized);
DeclareSetting(bool, StartInTray);
DeclareSetting(bool, SendToMenu);
DeclareSetting(bool, UseExternalVideoPlayer);
enum LaunchMode {
	LaunchModeNormal = 0,
	LaunchModeAutoStart,
	LaunchModeFixPrevious,
	LaunchModeCleanup,
};
DeclareReadSetting(LaunchMode, LaunchMode);
DeclareSetting(QString, WorkingDir);
inline void cForceWorkingDir(const QString &newDir) {
	cSetWorkingDir(newDir);
	if (!gWorkingDir.isEmpty()) {
		QDir().mkpath(gWorkingDir);
		QFile::setPermissions(gWorkingDir,
			QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ExeUser);
	}

}
DeclareReadSetting(QString, ExeName);
DeclareReadSetting(QString, ExeDir);
DeclareSetting(QString, DialogLastPath);
DeclareSetting(QString, DialogHelperPath);
inline const QString &cDialogHelperPathFinal() {
	return cDialogHelperPath().isEmpty() ? cExeDir() : cDialogHelperPath();
}

DeclareSetting(bool, AutoUpdate);

struct TWindowPos {
	TWindowPos() = default;

	int32 moncrc = 0;
	int maximized = 0;
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
};
DeclareSetting(TWindowPos, WindowPos);
DeclareSetting(bool, SupportTray);
DeclareSetting(bool, SeenTrayTooltip);
DeclareSetting(bool, RestartingUpdate);
DeclareSetting(bool, Restarting);
DeclareSetting(bool, RestartingToSettings);
DeclareSetting(bool, WriteProtected);
DeclareSetting(int32, LastUpdateCheck);
DeclareSetting(bool, NoStartUpdate);
DeclareSetting(bool, StartToSettings);
DeclareReadSetting(bool, ManyInstance);

DeclareSetting(QByteArray, LocalSalt);
DeclareSetting(int, ScreenScale);
DeclareSetting(int, ConfigScale);
DeclareSetting(QString, TimeFormat);

using RecentEmojiPreloadOldOld = QVector<QPair<uint32, ushort>>;
using RecentEmojiPreloadOld = QVector<QPair<uint64, ushort>>;
using RecentEmojiPreload = QVector<QPair<QString, ushort>>;
using RecentEmojiPack = QVector<QPair<EmojiPtr, ushort>>;
using EmojiColorVariantsOld = QMap<uint32, uint64>;
using EmojiColorVariants = QMap<QString, int>;
DeclareRefSetting(RecentEmojiPack, RecentEmoji);
DeclareSetting(RecentEmojiPreload, RecentEmojiPreload);
DeclareRefSetting(EmojiColorVariants, EmojiVariants);

class DocumentData;

typedef QList<QPair<DocumentData*, int16>> RecentStickerPackOld;
typedef QVector<QPair<uint64, ushort>> RecentStickerPreload;
typedef QVector<QPair<DocumentData*, ushort>> RecentStickerPack;
DeclareSetting(RecentStickerPreload, RecentStickersPreload);
DeclareRefSetting(RecentStickerPack, RecentStickers);

typedef QList<QPair<QString, ushort>> RecentHashtagPack;
DeclareRefSetting(RecentHashtagPack, RecentWriteHashtags);
DeclareSetting(RecentHashtagPack, RecentSearchHashtags);

class UserData;
typedef QVector<UserData*> RecentInlineBots;
DeclareRefSetting(RecentInlineBots, RecentInlineBots);

DeclareSetting(bool, PasswordRecovered);

DeclareSetting(int32, PasscodeBadTries);
DeclareSetting(crl::time, PasscodeLastTry);

DeclareSetting(QStringList, SendPaths);
DeclareSetting(QString, StartUrl);

DeclareSetting(int, OtherOnline);

inline void cChangeTimeFormat(const QString &newFormat) {
	if (!newFormat.isEmpty()) cSetTimeFormat(newFormat);
}

RecentEmojiPack &GetRecentEmoji();
QVector<EmojiPtr> GetRecentEmojiSection();
void AddRecentEmoji(EmojiPtr emoji);
[[nodiscard]] rpl::producer<> UpdatedRecentEmoji();

inline bool passcodeCanTry() {
	if (cPasscodeBadTries() < 3) return true;
	auto dt = crl::now() - cPasscodeLastTry();
	switch (cPasscodeBadTries()) {
	case 3: return dt >= 5000;
	case 4: return dt >= 10000;
	case 5: return dt >= 15000;
	case 6: return dt >= 20000;
	case 7: return dt >= 25000;
	}
	return dt >= 30000;
}

inline float64 cRetinaFactor() {
	return style::DevicePixelRatio();
}

inline int32 cIntRetinaFactor() {
	return style::DevicePixelRatio();
}

inline int cEvalScale(int scale) {
	return (scale == style::kScaleAuto) ? cScreenScale() : scale;
}

inline int cScale() {
	return style::Scale();
}

inline void SetScaleChecked(int scale) {
	cSetConfigScale(style::CheckScale(scale));
}

inline void ValidateScale() {
	SetScaleChecked(cConfigScale());
	style::SetScale(cEvalScale(cConfigScale()));
}

DeclareSetting(QString, MainFont);
DeclareSetting(QString, SemiboldFont);
DeclareSetting(bool, SemiboldFontIsBold);
DeclareSetting(QString, MonospaceFont);

void SetBigEmojiOutline(bool enabled);
[[nodiscard]] bool BigEmojiOutline();
[[nodiscard]] rpl::producer<bool> BigEmojiOutlineChanges();

void SetStickerHeight(int height);
[[nodiscard]] int StickerHeight();
[[nodiscard]] rpl::producer<int> StickerHeightChanges();

DeclareSetting(bool, AdaptiveBaloons);
DeclareSetting(bool, AlwaysShowScheduled);
DeclareSetting(bool, ShowChatId);

DeclareSetting(int, NetSpeedBoost);
DeclareSetting(int, NetRequestsCount);
DeclareSetting(int, NetDownloadSessionsCount);
DeclareSetting(int, NetUploadSessionsCount);
DeclareSetting(int, NetMaxFileQueries);
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
	cSetNetDownloadSessionsCount(2 + (2 * cNetSpeedBoost()));
	cSetNetUploadSessionsCount(2 + (2 * cNetSpeedBoost()));
	cSetNetMaxFileQueries(16 + (16 * cNetSpeedBoost()));
	cSetNetUploadRequestInterval(500 - (100 * cNetSpeedBoost()));
}

DeclareSetting(bool, ShowPhoneInDrawer);

using ScaleVector = std::vector<int>;
DeclareRefSetting(ScaleVector, InterfaceScales);
bool HasCustomScales();
bool AddCustomScale(int scale);
void ClearCustomScales();