/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_specific.h"

#include <signal.h>

namespace Data {
class LocationPoint;
} // namespace Data

namespace Platform {

inline void SetWatchingMediaKeys(bool watching) {
}

bool InSandbox();
bool InSnap();
bool InAppImage();
bool IsStaticBinary();
bool IsGtkFileDialogForced();

bool IsXDGDesktopPortalPresent();
bool UseXDGDesktopPortal();

QString ProcessNameByPID(const QString &pid);
QString RealExecutablePath(int argc, char *argv[]);
QString CurrentExecutablePath(int argc, char *argv[]);

QString AppRuntimeDirectory();
QString SingleInstanceLocalServerName(const QString &hash);

QString GetLauncherBasename();
QString GetLauncherFilename();

QString GetIconName();

inline void IgnoreApplicationActivationRightNow() {
}

void FallbackFontConfigCheckBegin();
void FallbackFontConfigCheckEnd();

} // namespace Platform

inline void psCheckLocalSocket(const QString &serverName) {
	QFile address(serverName);
	if (address.exists()) {
		address.remove();
	}
}

void psWriteDump();

void psDeleteDir(const QString &dir);

QStringList psInitLogs();
void psClearInitLogs();

void psActivateProcess(uint64 pid = 0);
QString psLocalServerPrefix();
QString psAppDataPath();
void psAutoStart(bool start, bool silent = false);
void psSendToMenu(bool send, bool silent = false);

QRect psDesktopRect();

int psCleanup();
int psFixPrevious();

void psNewVersion();

inline QByteArray psDownloadPathBookmark(const QString &path) {
	return QByteArray();
}
inline QByteArray psPathBookmark(const QString &path) {
	return QByteArray();
}
inline void psDownloadPathEnableAccess() {
}

class PsFileBookmark {
public:
	PsFileBookmark(const QByteArray &bookmark) {
	}
	bool check() const {
		return true;
	}
	bool enable() const {
		return true;
	}
	void disable() const {
	}
	const QString &name(const QString &original) const {
		return original;
	}
	QByteArray bookmark() const {
		return QByteArray();
	}

};

bool linuxMoveFile(const char *from, const char *to);

bool psLaunchMaps(const Data::LocationPoint &point);
