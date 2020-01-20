/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings.h"

#include "ui/emoji_config.h"

namespace {

constexpr auto kRecentEmojiLimit = 42;

auto UpdatesRecentEmoji = rpl::event_stream<>();

} // namespace

Qt::LayoutDirection gLangDir = Qt::LeftToRight;

bool gInstallBetaVersion = AppBetaVersion;
uint64 gAlphaVersion = AppAlphaVersion;
uint64 gRealAlphaVersion = AppAlphaVersion;
QByteArray gAlphaPrivateKey;

bool gTestMode = false;
bool gManyInstance = false;
QString gKeyFile;
QString gWorkingDir, gExeDir, gExeName;

QStringList gSendPaths;
QString gStartUrl;

QString gDialogLastPath, gDialogHelperPath; // optimize QFileDialog

bool gStartMinimized = false;
bool gStartInTray = false;
bool gAutoStart = false;
bool gSendToMenu = false;
bool gUseExternalVideoPlayer = false;
bool gAutoUpdate = true;
TWindowPos gWindowPos;
LaunchMode gLaunchMode = LaunchModeNormal;
bool gSupportTray = true;
bool gSeenTrayTooltip = false;
bool gRestartingUpdate = false, gRestarting = false, gRestartingToSettings = false, gWriteProtected = false;
int32 gLastUpdateCheck = 0;
bool gNoStartUpdate = false;
bool gStartToSettings = false;

uint32 gConnectionsInSession = 1;
QString gLoggedPhoneNumber;

QByteArray gLocalSalt;
int gScreenScale = style::kScaleAuto;
int gConfigScale = style::kScaleAuto;

QString gTimeFormat = qsl("hh:mm");

RecentEmojiPack gRecentEmoji;
RecentEmojiPreload gRecentEmojiPreload;
EmojiColorVariants gEmojiVariants;

RecentStickerPreload gRecentStickersPreload;
RecentStickerPack gRecentStickers;

RecentHashtagPack gRecentWriteHashtags, gRecentSearchHashtags;

RecentInlineBots gRecentInlineBots;

bool gPasswordRecovered = false;
int32 gPasscodeBadTries = 0;
crl::time gPasscodeLastTry = 0;

float64 gRetinaFactor = 1.;
int32 gIntRetinaFactor = 1;

int gOtherOnline = 0;

int32 gAutoDownloadPhoto = 0; // all auto download
int32 gAutoDownloadAudio = 0;
int32 gAutoDownloadGif = 0;

RecentEmojiPack &GetRecentEmoji() {
	if (cRecentEmoji().isEmpty()) {
		RecentEmojiPack result;
		auto haveAlready = [&result](EmojiPtr emoji) {
			for (auto &row : result) {
				if (row.first->id() == emoji->id()) {
					return true;
				}
			}
			return false;
		};
		if (!cRecentEmojiPreload().isEmpty()) {
			auto preload = cRecentEmojiPreload();
			cSetRecentEmojiPreload(RecentEmojiPreload());
			result.reserve(preload.size());
			for (auto i = preload.cbegin(), e = preload.cend(); i != e; ++i) {
				if (auto emoji = Ui::Emoji::Find(i->first)) {
					if (!haveAlready(emoji)) {
						result.push_back(qMakePair(emoji, i->second));
					}
				}
			}
		}
		const auto defaultRecent = {
			0xD83DDE02LLU,
			0xD83DDE18LLU,
			0x2764LLU,
			0xD83DDE0DLLU,
			0xD83DDE0ALLU,
			0xD83DDE01LLU,
			0xD83DDC4DLLU,
			0x263ALLU,
			0xD83DDE14LLU,
			0xD83DDE04LLU,
			0xD83DDE2DLLU,
			0xD83DDC8BLLU,
			0xD83DDE12LLU,
			0xD83DDE33LLU,
			0xD83DDE1CLLU,
			0xD83DDE48LLU,
			0xD83DDE09LLU,
			0xD83DDE03LLU,
			0xD83DDE22LLU,
			0xD83DDE1DLLU,
			0xD83DDE31LLU,
			0xD83DDE21LLU,
			0xD83DDE0FLLU,
			0xD83DDE1ELLU,
			0xD83DDE05LLU,
			0xD83DDE1ALLU,
			0xD83DDE4ALLU,
			0xD83DDE0CLLU,
			0xD83DDE00LLU,
			0xD83DDE0BLLU,
			0xD83DDE06LLU,
			0xD83DDC4CLLU,
			0xD83DDE10LLU,
			0xD83DDE15LLU,
		};
		for (const auto emoji : Ui::Emoji::GetDefaultRecent()) {
			if (result.size() >= kRecentEmojiLimit) break;

			if (!haveAlready(emoji)) {
				result.push_back(qMakePair(emoji, 1));
			}
		}
		cSetRecentEmoji(result);
	}
	return cRefRecentEmoji();
}

EmojiPack GetRecentEmojiSection() {
	const auto &recent = GetRecentEmoji();

	auto result = EmojiPack();
	result.reserve(recent.size());
	for (const auto &item : recent) {
		result.push_back(item.first);
	}
	return result;
}

void AddRecentEmoji(EmojiPtr emoji) {
	auto &recent = GetRecentEmoji();
	auto i = recent.begin(), e = recent.end();
	for (; i != e; ++i) {
		if (i->first == emoji) {
			++i->second;
			if (i->second > 0x8000) {
				for (auto j = recent.begin(); j != e; ++j) {
					if (j->second > 1) {
						j->second /= 2;
					} else {
						j->second = 1;
					}
				}
			}
			for (; i != recent.begin(); --i) {
				if ((i - 1)->second > i->second) {
					break;
				}
				std::swap(*i, *(i - 1));
			}
			break;
		}
	}
	if (i == e) {
		while (recent.size() >= kRecentEmojiLimit) {
			recent.pop_back();
		}
		recent.push_back(qMakePair(emoji, 1));
		for (i = recent.end() - 1; i != recent.begin(); --i) {
			if ((i - 1)->second > i->second) {
				break;
			}
			std::swap(*i, *(i - 1));
		}
	}
	UpdatesRecentEmoji.fire({});
}

rpl::producer<> UpdatedRecentEmoji() {
	return UpdatesRecentEmoji.events();
}

QString gMainFont, gSemiboldFont, gMonospaceFont;
bool gSemiboldFontIsBold = false;

rpl::variable<int> gStickerHeight = 128;
void SetStickerHeight(int height) {
	gStickerHeight = height;
}
int StickerHeight() {
	return gStickerHeight.current();
}
rpl::producer<int> StickerHeightChanges() {
	return gStickerHeight.changes();
}

rpl::variable<bool> gBigEmojiOutline = false;
void SetBigEmojiOutline(bool enabled) {
	gBigEmojiOutline = enabled;
}
bool BigEmojiOutline() {
	return gBigEmojiOutline.current();
}
rpl::producer<bool> BigEmojiOutlineChanges() {
	return gBigEmojiOutline.changes();
}

bool gAdaptiveBubbles = false;
bool gAlwaysShowScheduled = true;
bool gShowChatId = true;

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
