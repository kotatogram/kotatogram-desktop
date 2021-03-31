/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_file_utilities.h"

namespace Platform {
namespace File {

inline QString UrlToLocal(const QUrl &url) {
	return ::File::internal::UrlToLocalDefault(url);
}

inline bool UnsafeShowOpenWithDropdown(const QString &filepath, QPoint menuPosition) {
	return false;
}

inline void PostprocessDownloaded(const QString &filepath) {
}

} // namespace File

namespace FileDialog {

enum class ImplementationType {
	Default,
#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	XDP,
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#ifndef DESKTOP_APP_DISABLE_GTK_INTEGRATION
	GTK,
#endif // !DESKTOP_APP_DISABLE_GTK_INTEGRATION
	Qt,
	Count,
};

inline void InitLastPath() {
	::FileDialog::internal::InitLastPathDefault();
}

} // namespace FileDialog
} // namespace Platform
