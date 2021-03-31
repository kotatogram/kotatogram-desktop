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

inline void UnsafeOpenUrl(const QString &url) {
	return ::File::internal::UnsafeOpenUrlDefault(url);
}

} // namespace File

namespace FileDialog {

enum class ImplementationType {
	Default,
	Count,
};

inline QString ImplementationTypeLabel(ImplementationType value) {
	Unexpected("Value in Platform::FileDialog::ImplementationTypeLabel.");
}

inline QString ImplementationTypeDescription(ImplementationType value) {
	return QString();
}

} // namespace FileDialog
} // namespace Platform
