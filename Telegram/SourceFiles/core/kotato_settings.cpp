/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "core/kotato_settings.h"

#include "mainwindow.h"
#include "mainwidget.h"
#include "window/window_controller.h"
#include "core/application.h"
#include "base/parse_helper.h"
#include "facades.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>

namespace KotatoSettings {
namespace {

class Manager {
public:
	void fill();
	void clear();

	const QStringList &errors() const;

private:
	void writeDefaultFile();
	bool readCustomFile();

	QStringList _errors;

};

QString DefaultFilePath() {
	return cWorkingDir() + qsl("tdata/kotato-settings-default.json");
}

QString CustomFilePath() {
	return cWorkingDir() + qsl("tdata/kotato-settings-custom.json");
}

bool DefaultFileIsValid() {
	QFile file(DefaultFilePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(
		base::parse::stripComments(file.readAll()),
		&error);
	file.close();

	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return false;
	}
	const auto settings = document.object();

	const auto version = settings.constFind(qsl("version"));
	if (version == settings.constEnd() || (*version).toInt() != AppKotatoVersion) {
		return false;
	}

	return true;
}

void WriteDefaultCustomFile() {
	const auto path = CustomFilePath();
	auto input = QFile(":/misc/default_kotato-settings-custom.json");
	auto output = QFile(path);
	if (input.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly)) {
		output.write(input.readAll());
	}
}

void Manager::fill() {
	if (!DefaultFileIsValid()) {
		writeDefaultFile();
	}
	if (!readCustomFile()) {
		WriteDefaultCustomFile();
	}
}

void Manager::clear() {
	_errors.clear();
}

const QStringList &Manager::errors() const {
	return _errors;
}

bool Manager::readCustomFile() {
	QFile file(CustomFilePath());
	if (!file.exists()) {
		return false;
	}
	const auto guard = gsl::finally([&] {
		if (!_errors.isEmpty()) {
			_errors.push_front(qsl("While reading file '%1'..."
			).arg(file.fileName()));
		}
	});
	if (!file.open(QIODevice::ReadOnly)) {
		_errors.push_back(qsl("Could not read the file!"));
		return true;
	}
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(
		base::parse::stripComments(file.readAll()),
		&error);
	file.close();

	if (error.error != QJsonParseError::NoError) {
		_errors.push_back(qsl("Failed to parse! Error: %2"
		).arg(error.errorString()));
		return true;
	} else if (!document.isObject()) {
		_errors.push_back(qsl("Failed to parse! Error: object expected"));
		return true;
	}
	const auto settings = document.object();

	if (settings.isEmpty()) {
		return true;
	}

	const auto settingsFontsIt = settings.constFind(qsl("fonts"));

	if (settingsFontsIt != settings.constEnd() && (*settingsFontsIt).isObject()) {
		const auto settingsFonts = (*settingsFontsIt).toObject();

		const auto settingsFontsMain = settingsFonts.constFind(qsl("main"));
		if (settingsFontsMain != settingsFonts.constEnd() && (*settingsFontsMain).isString()) {
			cSetMainFont((*settingsFontsMain).toString());
		}

		const auto settingsFontsSemibold = settingsFonts.constFind(qsl("semibold"));
		if (settingsFontsSemibold != settingsFonts.constEnd() && (*settingsFontsSemibold).isString()) {
			cSetSemiboldFont((*settingsFontsSemibold).toString());
		}

		const auto settingsFontsSemiboldIsBold = settingsFonts.constFind(qsl("semibold_is_bold"));
		if (settingsFontsSemiboldIsBold != settingsFonts.constEnd() && (*settingsFontsSemiboldIsBold).isBool()) {
			cSetSemiboldFontIsBold((*settingsFontsSemiboldIsBold).toBool());
		}

		const auto settingsFontsMonospace = settingsFonts.constFind(qsl("monospaced"));
		if (settingsFontsMonospace != settingsFonts.constEnd() && (*settingsFontsMonospace).isString()) {
			cSetMonospaceFont((*settingsFontsMonospace).toString());
		}
	}

	const auto settingsStickerHeightIt = settings.constFind(qsl("sticker_height"));
	if (settingsStickerHeightIt != settings.constEnd()) {
		const auto settingsStickerHeight = (*settingsStickerHeightIt).toInt();
		if (settingsStickerHeight >= 128 || settingsStickerHeight <= 256) {
			cSetStickerHeight(settingsStickerHeight);
		}
	}

	const auto settingsBigEmojiOutlineIt = settings.constFind(qsl("big_emoji_outline"));
	if (settingsBigEmojiOutlineIt != settings.constEnd() && (*settingsBigEmojiOutlineIt).isBool()) {
		cSetBigEmojiOutline((*settingsBigEmojiOutlineIt).toBool());
	}

	const auto settingsAlwaysShowScheduledIt = settings.constFind(qsl("always_show_scheduled"));
	if (settingsAlwaysShowScheduledIt != settings.constEnd() && (*settingsAlwaysShowScheduledIt).isBool()) {
		cSetAlwaysShowScheduled((*settingsAlwaysShowScheduledIt).toBool());
	}

	const auto settingsShowChatIdIt = settings.constFind(qsl("show_chat_id"));
	if (settingsShowChatIdIt != settings.constEnd() && (*settingsShowChatIdIt).isBool()) {
		cSetShowChatId((*settingsShowChatIdIt).toBool());
	}

	const auto settingsNetSpeedIt = settings.constFind(qsl("net_speed_boost"));
	if (settingsNetSpeedIt != settings.constEnd()) { 
		if ((*settingsNetSpeedIt).isString()) {
			
			const auto option = (*settingsNetSpeedIt).toString();
			if (option == "high") {
				cSetNetRequestsCount(8);
				cSetNetDownloadSessionsCount(8);
				cSetNetUploadSessionsCount(8);
				cSetNetMaxFileQueries(64);
				cSetNetUploadRequestInterval(200);
			} else if (option == "medium") {
				cSetNetRequestsCount(6);
				cSetNetDownloadSessionsCount(6);
				cSetNetUploadSessionsCount(6);
				cSetNetMaxFileQueries(48);
				cSetNetUploadRequestInterval(300);
			} else if (option == "low") {
				cSetNetRequestsCount(4);
				cSetNetDownloadSessionsCount(4);
				cSetNetUploadSessionsCount(4);
				cSetNetMaxFileQueries(32);
				cSetNetUploadRequestInterval(400);
			} else {
				cSetNetRequestsCount(2);
				cSetNetDownloadSessionsCount(2);
				cSetNetUploadSessionsCount(2);
				cSetNetMaxFileQueries(16);
				cSetNetUploadRequestInterval(500);
			}

		} else if ((*settingsNetSpeedIt).isNull()) {
			cSetNetRequestsCount(2);
			cSetNetDownloadSessionsCount(2);
			cSetNetUploadSessionsCount(2);
			cSetNetMaxFileQueries(16);
			cSetNetUploadRequestInterval(500);
		}
	}

	const auto settingsShowDrawerPhoneIt = settings.constFind(qsl("show_phone_in_drawer"));
	if (settingsShowDrawerPhoneIt != settings.constEnd() && (*settingsShowDrawerPhoneIt).isBool()) {
		cSetShowPhoneInDrawer((*settingsShowDrawerPhoneIt).toBool());
	}
	return true;
}

void Manager::writeDefaultFile() {
	auto file = QFile(DefaultFilePath());
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}
	const char *defaultHeader = R"HEADER(
// This is a list of default options for Kotatogram Desktop
// Please don't modify it, its content is not used in any way
// You can place your own options in the 'kotato-settings-custom.json' file

)HEADER";
	file.write(defaultHeader);

	auto settings = QJsonObject();
	settings.insert(qsl("version"), QString::number(AppKotatoVersion));

	auto settingsFonts = QJsonObject();
	settingsFonts.insert(qsl("main"), qsl("Open Sans"));
	settingsFonts.insert(qsl("semibold"), qsl("Open Sans Semibold"));
	settingsFonts.insert(qsl("semibold_is_bold"), false);
	settingsFonts.insert(qsl("monospaced"), qsl("Consolas"));

	settings.insert(qsl("fonts"), settingsFonts);

	settings.insert(qsl("sticker_height"), cStickerHeight());
	settings.insert(qsl("big_emoji_outline"), cBigEmojiOutline());
	settings.insert(qsl("always_show_scheduled"), cAlwaysShowScheduled());
	settings.insert(qsl("show_chat_id"), cShowChatId());
	settings.insert(qsl("net_speed_boost"), QJsonValue(QJsonValue::Null));
	settings.insert(qsl("show_phone_in_drawer"), cShowPhoneInDrawer());

	auto document = QJsonDocument();
	document.setObject(settings);
	file.write(document.toJson(QJsonDocument::Indented));
}

Manager Data;

} // namespace

void Start() {
	Data.fill();
}

const QStringList &Errors() {
	return Data.errors();
}

void Finish() {
	Data.clear();
}

} // namespace KotatoSettings
