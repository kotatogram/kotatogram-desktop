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
#include "ui/widgets/input_fields.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>
#include <QtCore/QTimer>

namespace KotatoSettings {
namespace {

constexpr auto kWriteJsonTimeout = crl::time(5000);

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

std::unique_ptr<Manager> Data;

} // namespace

Manager::Manager() {
	_jsonWriteTimer.setSingleShot(true);
	connect(&_jsonWriteTimer, SIGNAL(timeout()), this, SLOT(writeTimeout()));
}

void Manager::fill() {
	if (!DefaultFileIsValid()) {
		writeDefaultFile();
	}
	if (!readCustomFile()) {
		WriteDefaultCustomFile();
	}
}

void Manager::write(bool force) {
	if (force && _jsonWriteTimer.isActive()) {
		_jsonWriteTimer.stop();
		writeTimeout();
	} else if (!force && !_jsonWriteTimer.isActive()) {
		_jsonWriteTimer.start(kWriteJsonTimeout);
	}
}

bool Manager::readCustomFile() {
	QFile file(CustomFilePath());
	if (!file.exists()) {
		cSetKotatoFirstRun(true);
		return false;
	}
	cSetKotatoFirstRun(false);
	if (!file.open(QIODevice::ReadOnly)) {
		return true;
	}
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(
		base::parse::stripComments(file.readAll()),
		&error);
	file.close();

	if (error.error != QJsonParseError::NoError) {
		return true;
	} else if (!document.isObject()) {
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

		const auto settingsFontsUseSystemFont = settingsFonts.constFind(qsl("use_system_font"));
		if (settingsFontsUseSystemFont != settingsFonts.constEnd() && (*settingsFontsUseSystemFont).isBool()) {
			cSetUseSystemFont((*settingsFontsUseSystemFont).toBool());
		}

		const auto settingsFontsUseOriginalMetrics = settingsFonts.constFind(qsl("use_original_metrics"));
		if (settingsFontsUseOriginalMetrics != settingsFonts.constEnd() && (*settingsFontsUseOriginalMetrics).isBool()) {
			cSetUseOriginalMetrics((*settingsFontsUseOriginalMetrics).toBool());
		}
	}

	const auto settingsStickerHeightIt = settings.constFind(qsl("sticker_height"));
	if (settingsStickerHeightIt != settings.constEnd()) {
		const auto settingsStickerHeight = (*settingsStickerHeightIt).toInt();
		if (settingsStickerHeight >= 64 || settingsStickerHeight <= 256) {
			SetStickerHeight(settingsStickerHeight);
		}
	}

	const auto settingsAdaptiveBubblesIt = settings.constFind(qsl("adaptive_bubbles"));
	if (settingsAdaptiveBubblesIt != settings.constEnd() && (*settingsAdaptiveBubblesIt).isBool()) {
		SetAdaptiveBubbles((*settingsAdaptiveBubblesIt).toBool());
	} else {
		const auto settingsAdaptiveBaloonsIt = settings.constFind(qsl("adaptive_baloons"));
		if (settingsAdaptiveBaloonsIt != settings.constEnd() && (*settingsAdaptiveBaloonsIt).isBool()) {
			SetAdaptiveBubbles((*settingsAdaptiveBaloonsIt).toBool());
		}
	}

	const auto settingsBigEmojiOutlineIt = settings.constFind(qsl("big_emoji_outline"));
	if (settingsBigEmojiOutlineIt != settings.constEnd() && (*settingsBigEmojiOutlineIt).isBool()) {
		SetBigEmojiOutline((*settingsBigEmojiOutlineIt).toBool());
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
				SetNetworkBoost(3);
			} else if (option == "medium") {
				SetNetworkBoost(2);
			} else if (option == "low") {
				SetNetworkBoost(1);
			} else {
				SetNetworkBoost(0);
			}

		} else if ((*settingsNetSpeedIt).isNull()) {
			SetNetworkBoost(0);
		} else if ((*settingsNetSpeedIt).isDouble()) {
			SetNetworkBoost((*settingsNetSpeedIt).toInt());
		}
	}

	const auto settingsShowDrawerPhoneIt = settings.constFind(qsl("show_phone_in_drawer"));
	if (settingsShowDrawerPhoneIt != settings.constEnd() && (*settingsShowDrawerPhoneIt).isBool()) {
		cSetShowPhoneInDrawer((*settingsShowDrawerPhoneIt).toBool());
	}

	const auto settingsScalesIt = settings.constFind(qsl("scales"));
	if (settingsScalesIt != settings.constEnd() && (*settingsScalesIt).isArray()) {
		const auto settingsScalesArray = (*settingsScalesIt).toArray();
		ClearCustomScales();
		for (auto i = settingsScalesArray.constBegin(), e = settingsScalesArray.constEnd(); i != e; ++i) {
			if (!(*i).isDouble()) {
				continue;
			}

			AddCustomScale((*i).toInt());
		}

	}

	const auto settingsChatListLinesIt = settings.constFind(qsl("chat_list_lines"));
	if (settingsChatListLinesIt != settings.constEnd()) {
		const auto settingsChatListLines = (*settingsChatListLinesIt).toInt();
		if (settingsChatListLines >= 1 || settingsChatListLines <= 2) {
			SetDialogListLines(settingsChatListLines);
		}
	}

	const auto settingsDisableUpEditIt = settings.constFind(qsl("disable_up_edit"));
	if (settingsDisableUpEditIt != settings.constEnd() && (*settingsDisableUpEditIt).isBool()) {
		cSetDisableUpEdit((*settingsDisableUpEditIt).toBool());
	}

	const auto settingsReplacesIt = settings.constFind(qsl("replaces"));
	if (settingsReplacesIt != settings.constEnd() && (*settingsReplacesIt).isArray()) {
		const auto settingsReplacesArray = (*settingsReplacesIt).toArray();
		for (auto i = settingsReplacesArray.constBegin(), e = settingsReplacesArray.constEnd(); i != e; ++i) {
			if (!(*i).isArray()) {
				continue;
			}

			const auto a = (*i).toArray();

			if (a.size() != 2 || !a.at(0).isString() || !a.at(1).isString()) {
				continue;
			}
			const auto from = a.at(0).toString();
			const auto to = a.at(1).toString();

			AddCustomReplace(from, to);
			Ui::AddCustomReplacement(from, to);
		}

	}

	const auto settingsCallConfirmIt = settings.constFind(qsl("confirm_before_calls"));
	if (settingsCallConfirmIt != settings.constEnd() && (*settingsCallConfirmIt).isBool()) {
		cSetConfirmBeforeCall((*settingsCallConfirmIt).toBool());
	}

	const auto settingsNoTaskbarFlashing = settings.constFind(qsl("no_taskbar_flash"));
	if (settingsNoTaskbarFlashing != settings.constEnd() && (*settingsNoTaskbarFlashing).isBool()) {
		cSetNoTaskbarFlashing((*settingsNoTaskbarFlashing).toBool());
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
	settingsFonts.insert(qsl("use_system_font"), cUseSystemFont());
	settingsFonts.insert(qsl("use_original_metrics"), cUseOriginalMetrics());

	settings.insert(qsl("fonts"), settingsFonts);

	settings.insert(qsl("sticker_height"), StickerHeight());
	settings.insert(qsl("adaptive_bubbles"), AdaptiveBubbles());
	settings.insert(qsl("big_emoji_outline"), BigEmojiOutline());
	settings.insert(qsl("always_show_scheduled"), cAlwaysShowScheduled());
	settings.insert(qsl("show_chat_id"), cShowChatId());
	settings.insert(qsl("net_speed_boost"), QJsonValue(QJsonValue::Null));
	settings.insert(qsl("show_phone_in_drawer"), cShowPhoneInDrawer());
	settings.insert(qsl("chat_list_lines"), DialogListLines());
	settings.insert(qsl("disable_up_edit"), cDisableUpEdit());
	settings.insert(qsl("confirm_before_calls"), cConfirmBeforeCall());
	settings.insert(qsl("no_taskbar_flash"), cNoTaskbarFlashing());

	auto settingsScales = QJsonArray();
	settings.insert(qsl("scales"), settingsScales);

	auto settingsReplaces = QJsonArray();
	settings.insert(qsl("replaces"), settingsReplaces);

	auto document = QJsonDocument();
	document.setObject(settings);
	file.write(document.toJson(QJsonDocument::Indented));
}

void Manager::writeCurrentSettings() {
	auto file = QFile(CustomFilePath());
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}
	if (_jsonWriteTimer.isActive()) {
		writing();
	}
	const char *customHeader = R"HEADER(
// This file was automatically generated from current settings
// It's better to edit it with app closed, so there will be no rewrites
// You should restart app to see changes

)HEADER";
	file.write(customHeader);

	auto settings = QJsonObject();

	auto settingsFonts = QJsonObject();

	if (!cMainFont().isEmpty()) {
		settingsFonts.insert(qsl("main"), cMainFont());
	}

	if (!cSemiboldFont().isEmpty()) {
		settingsFonts.insert(qsl("semibold"), cSemiboldFont());
	}

	if (!cMonospaceFont().isEmpty()) {
		settingsFonts.insert(qsl("monospaced"), cMonospaceFont());
	}

	settingsFonts.insert(qsl("semibold_is_bold"), cSemiboldFontIsBold());
	settingsFonts.insert(qsl("use_system_font"), cUseSystemFont());
	settingsFonts.insert(qsl("use_original_metrics"), cUseOriginalMetrics());

	settings.insert(qsl("fonts"), settingsFonts);

	settings.insert(qsl("sticker_height"), StickerHeight());
	settings.insert(qsl("adaptive_bubbles"), AdaptiveBubbles());
	settings.insert(qsl("big_emoji_outline"), BigEmojiOutline());
	settings.insert(qsl("always_show_scheduled"), cAlwaysShowScheduled());
	settings.insert(qsl("show_chat_id"), cShowChatId());
	settings.insert(qsl("net_speed_boost"), cNetSpeedBoost());
	settings.insert(qsl("show_phone_in_drawer"), cShowPhoneInDrawer());
	settings.insert(qsl("chat_list_lines"), DialogListLines());
	settings.insert(qsl("disable_up_edit"), cDisableUpEdit());
	settings.insert(qsl("confirm_before_calls"), cConfirmBeforeCall());
	settings.insert(qsl("no_taskbar_flash"), cNoTaskbarFlashing());

	auto settingsScales = QJsonArray();
	auto currentScales = cInterfaceScales();

	for (int i = 0; i < currentScales.size(); i++) {
		settingsScales << currentScales[i];
	}

	settings.insert(qsl("scales"), settingsScales);

	auto settingsReplaces = QJsonArray();
	auto currentReplaces = cCustomReplaces();

	for (auto i = currentReplaces.constBegin(), e = currentReplaces.constEnd(); i != e; ++i) {
		auto a = QJsonArray();
		a << i.key() << i.value();
		settingsReplaces << a;
	}

	settings.insert(qsl("replaces"), settingsReplaces);

	auto document = QJsonDocument();
	document.setObject(settings);
	file.write(document.toJson(QJsonDocument::Indented));
}

void Manager::writeTimeout() {
	writeCurrentSettings();
}

void Manager::writing() {
	_jsonWriteTimer.stop();
}

void Start() {
	if (Data) return;

	Data = std::make_unique<Manager>();
	Data->fill();
}

void Write() {
	if (!Data) return;

	Data->write();
}

void Finish() {
	if (!Data) return;

	Data->write(true);
}

} // namespace KotatoSettings
