/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/kotato_settings.h"

#include "kotato/kotato_version.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "window/window_controller.h"
#include "core/application.h"
#include "data/data_peer_id.h"
#include "base/parse_helper.h"
#include "base/timer.h"
#include "facades.h"
#include "ui/widgets/input_fields.h"
#include "data/data_chat_filters.h"
#include "platform/platform_file_utilities.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QTimer>

namespace Kotato {
namespace JsonSettings {
namespace {

constexpr auto kWriteJsonTimeout = crl::time(5000);

class Manager : public QObject {
public:
	Manager();
	void load();
	void fill();
	void write(bool force = false);

	[[nodiscard]] QVariant get(
		const QString &key,
		uint64 accountId = 0,
		bool isTestAccount = false);
	[[nodiscard]] QVariant getWithPending(
		const QString &key,
		uint64 accountId = 0,
		bool isTestAccount = false);
	[[nodiscard]] QVariantMap getAllWithPending(const QString &key);
	[[nodiscard]] rpl::producer<QString> events(
		const QString &key,
		uint64 accountId = 0,
		bool isTestAccount = false);
	[[nodiscard]] rpl::producer<QString> eventsWithPending(
		const QString &key,
		uint64 accountId = 0,
		bool isTestAccount = false);
	void set(
		const QString &key,
		QVariant value,
		uint64 accountId = 0,
		bool isTestAccount = false);
	void setAfterRestart(
		const QString &key,
		QVariant value,
		uint64 accountId = 0,
		bool isTestAccount = false);
	void reset(
		const QString &key,
		uint64 accountId = 0,
		bool isTestAccount = false);
	void resetAfterRestart(
		const QString &key,
		uint64 accountId = 0,
		bool isTestAccount = false);
	void writeTimeout();

private:
	[[nodiscard]] QVariant getDefault(const QString &key);

	void writeDefaultFile();
	void writeCurrentSettings();
	bool readCustomFile();
	void writing();

	base::Timer _jsonWriteTimer;

	rpl::event_stream<QString> _eventStream;
	rpl::event_stream<QString> _pendingEventStream;
	QHash<QString, QVariant> _settingsHashMap;
	QHash<QString, QVariant> _defaultSettingsHashMap;

};

inline QString MakeMapKey(const QString &key, uint64 accountId, bool isTestAccount) {
	return (accountId == 0)	? key : key
				+ (isTestAccount ? qsl(":test_") : qsl(":"))
				+ QString::number(accountId);
}

QVariantMap GetAllWithPending(const QString &key);

enum SettingScope {
	Global,
	Account,
};

enum SettingStorage {
	None,
	MainJson,
};

enum SettingType {
	BoolSetting,
	IntSetting,
	QStringSetting,
	QJsonArraySetting,
};

using CheckHandler = Fn<QVariant(QVariant)>;

CheckHandler IntLimit(int min, int max, int defaultValue) {
	return [=] (QVariant value) -> QVariant {
		if (value.canConvert<int>()) {
			auto intValue = value.toInt();
			if (intValue < min) {
				return min;
			} else if (intValue > max) {
				return max;
			} else {
				return value;
			}
		} else {
			return defaultValue;
		}
	};
}

inline CheckHandler IntLimit(int min, int max) {
	return IntLimit(min, max, min);
}

CheckHandler IntLimitMin(int min) {
	return [=] (QVariant value) -> QVariant {
		if (value.canConvert<int>()) {
			auto intValue = value.toInt();
			if (intValue < min) {
				return min;
			} else {
				return value;
			}
		} else {
			return min;
		}
	};
}

CheckHandler ScalesLimit() {
	return [=] (QVariant value) -> QVariant {
		auto newArrayValue = QJsonArray();
		if (value.canConvert<QJsonArray>()) {
			auto arrayValue = value.toJsonArray();
			for (auto i = arrayValue.begin(); i != arrayValue.end() && arrayValue.size() <= 6; ++i) {
				const auto scaleNumber = (*i).toDouble(); 
				if (scaleNumber >= style::kScaleMin && scaleNumber <= style::kScaleMax) {
					newArrayValue.append(scaleNumber);
				}
			}
		}
		return newArrayValue;
	};
}

CheckHandler ReplacesLimit() {
	return [=] (QVariant value) -> QVariant {
		auto newArrayValue = QJsonArray();
		if (value.canConvert<QJsonArray>()) {
			auto arrayValue = value.toJsonArray();
			for (auto i = arrayValue.begin(); i != arrayValue.end(); ++i) {
				if (!(*i).isArray()) {
					continue;
				}

				const auto a = (*i).toArray();
				if (a.size() != 2 || !a.at(0).isString() || !a.at(1).isString()) {
					continue;
				}

				const auto from = a.at(0).toString();
				const auto to = a.at(1).toString();
				Ui::AddCustomReplacement(from, to);

				newArrayValue.append(a);
			}
		}

		return newArrayValue;
	};
}

CheckHandler FileDialogLimit() {
	return [=] (QVariant value) -> QVariant {
		using Platform::FileDialog::ImplementationType;
		auto newValue = int(ImplementationType::Default);
		if (value.canConvert<int>()) {
			auto intValue = value.toInt();
			if (intValue >= int(ImplementationType::Default)
				&& intValue < int(ImplementationType::Count)) {

				newValue = intValue;
			} else if (intValue >= int(ImplementationType::Count)) {
				newValue = int(ImplementationType::Count) - 1;
			}
		}
		return newValue;
	};
}

CheckHandler NetSpeedBoostConv(CheckHandler wrapped = nullptr) {
	return [=] (QVariant value) -> QVariant {
		auto newValue = 0;
		if (value.canConvert<int>()) {
			newValue = value.value<int>();
		} else if (value.canConvert<QString>()) {
			const auto strValue = value.value<QString>();
			if (strValue == "high") {
				newValue = 3;
			} else if (strValue == "medium") {
				newValue = 2;
			} else if (strValue == "low") {
				newValue = 1;
			}
		}
		return (wrapped) ? wrapped(newValue) : newValue;
	};
}

struct Definition {
	SettingScope scope = SettingScope::Global;
	SettingStorage storage = SettingStorage::MainJson;
	SettingType type = SettingType::BoolSetting;
	QVariant defaultValue;
	QVariant fillerValue;
	CheckHandler limitHandler = nullptr;
};

const std::map<QString, Definition, std::greater<QString>> DefinitionMap {

	// Non-stored settings

	// To build your version of Kotatogram Desktop you're required to provide
	// your own 'api_id' and 'api_hash' for the Telegram API access.
	//
	// How to obtain your 'api_id' and 'api_hash' is described here:
	// https://core.telegram.org/api/obtaining_api_id
	//
	// By default Kotatogram provides empty 'api_id' and 'api_hash'
	// since you can set it them in runtime. They can be set with
	// KTGDESKTOP_API_ID and KTGDESKTOP_API_HASH environment variables.
	// You must set both variables for it to work.
	//
	// As an alternative, you can use -api-id <id> and -api_hash <hash>
	// start parameters. Note that environment variables have priority
	// over start parameters, so you should use -no-env-api if you don't
	// want them. And as with environment variables, both -api-id and
	// -api_hash must be set for it to work.
	//
	// If 'api_id' and 'api_hash' are empty, and they're not set by any
	// of these parameters, you won't be able to connect to Telegram at all.
	// Sessions created on TDesktop + forks (including Kotatogram) might
	// work, but it could be risky, so be careful with it.
	{ "api_id", {
		.storage = SettingStorage::None,
		.type = SettingType::IntSetting,
#if defined TDESKTOP_API_ID && defined TDESKTOP_API_HASH
		.defaultValue = TDESKTOP_API_ID,
#else
		.defaultValue = 0,
#endif
	}},
	{ "api_hash", {
		.storage = SettingStorage::None,
		.type = SettingType::QStringSetting,
#if defined TDESKTOP_API_ID && defined TDESKTOP_API_HASH
		.defaultValue = QT_STRINGIFY(TDESKTOP_API_HASH),
#else
		.defaultValue = QString(),
#endif
	}},
	{ "api_use_env", {
		.storage = SettingStorage::None,
		.type = SettingType::BoolSetting,
		.defaultValue = true, }},
	{ "api_start_params", {
		.storage = SettingStorage::None,
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},

	// Stored settings
	{ "fonts/main", {
		.type = SettingType::QStringSetting,
		.fillerValue = qsl("Open Sans"), }},
	{ "fonts/semibold", {
		.type = SettingType::QStringSetting,
		.fillerValue = qsl("Open Sans Semibold"), }},
	{ "fonts/semibold_is_bold", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "fonts/monospaced", {
		.type = SettingType::QStringSetting,
		.fillerValue = qsl("Consolas"), }},
	{ "fonts/size", {
		.type = SettingType::IntSetting,
		.defaultValue = 0, }},
	{ "fonts/use_system_font", {
		.type = SettingType::BoolSetting,
#ifdef DESKTOP_APP_USE_PACKAGED_FONTS
		.defaultValue = true,
#else
		.defaultValue = Platform::IsLinux(),
#endif
	}},
	{ "fonts/use_original_metrics", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "big_emoji_outline", {
		.type = SettingType::BoolSetting,
		.defaultValue = true, }},
	{ "sticker_height", {
		.type = SettingType::IntSetting,
		.defaultValue = 170,
		.limitHandler = IntLimit(64, 256, 170), }},
	{ "sticker_scale_both", {
		.type = SettingType::BoolSetting,
		.defaultValue = true, }},
	{ "adaptive_bubbles", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "monospace_large_bubbles", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "always_show_scheduled", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "show_chat_id", {
		.type = SettingType::IntSetting,
		.defaultValue = 2,
		.limitHandler = IntLimit(0, 2, 2), }},
	{ "net_speed_boost", {
		.type = SettingType::IntSetting,
		.defaultValue = 0,
		.limitHandler = NetSpeedBoostConv(IntLimit(0, 3)), }},
	{ "show_phone_in_drawer", {
		.type = SettingType::BoolSetting,
		.defaultValue = true, }},
	{ "scales", {
		.type = SettingType::QJsonArraySetting,
		.limitHandler = ScalesLimit(), }},
	{ "chat_list_lines", {
		.type = SettingType::IntSetting,
		.defaultValue = 2,
		.limitHandler = IntLimit(1, 2, 2), }},
	{ "disable_up_edit", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "replaces", {
		.type = SettingType::QJsonArraySetting,
		.limitHandler = ReplacesLimit(), }},
	{ "confirm_before_calls", {
		.type = SettingType::BoolSetting,
		.defaultValue = true, }},
	{ "ffmpeg_multithread", {
		.type = SettingType::BoolSetting,
		.defaultValue = true, }},
	{ "ffmpeg_thread_count", {
		.type = SettingType::IntSetting,
		.defaultValue = 0,
		.limitHandler = IntLimitMin(0) }},
	{ "recent_stickers_limit", {
		.type = SettingType::IntSetting,
		.defaultValue = 20,
		.limitHandler = IntLimit(0, 200, 20), }},
	{ "userpic_corner_type", {
		.type = SettingType::IntSetting,
		.defaultValue = 3,
		.limitHandler = IntLimit(0, 3, 3), }},
	{ "always_show_top_userpic", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	{ "qt_scale", {
		.storage = SettingStorage::None,
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
#endif
	{ "file_dialog_type", {
		.type = SettingType::IntSetting,
		.defaultValue = int(Platform::FileDialog::ImplementationType::Default),
		.limitHandler = FileDialogLimit(), }},
	{ "disable_tray_counter", {
		.type = SettingType::BoolSetting,
		.defaultValue = Platform::IsLinux(), }},
	{ "use_telegram_panel_icon", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "custom_app_icon", {
		.type = SettingType::IntSetting,
		.defaultValue = 0,
		.limitHandler = IntLimit(0, 5), }},
	{ "folders/default", {
		.scope = SettingScope::Account,
		.type = SettingType::IntSetting,
		.defaultValue = 0,
		.limitHandler = IntLimitMin(0), }},
	{ "folders/count_unmuted_only", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "folders/hide_edit_button", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "folders/hide_names", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "folders/hide_all_chats", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "profile_top_mute", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "forward_retain_selection", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "forward_on_click", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "auto_scroll_unfocused", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "folders/local", {
		.scope = SettingScope::Account,
		.type = SettingType::QJsonArraySetting, }},
	{ "telegram_sites_autologin", {
		.type = SettingType::BoolSetting,
		.defaultValue = true, }},
	{ "forward_remember_mode", {
		.type = SettingType::BoolSetting,
		.defaultValue = true, }},
	{ "forward_mode", {
		.type = SettingType::IntSetting,
		.defaultValue = 0,
		.limitHandler = IntLimit(0, 2), }},
	{ "forward_grouping_mode", {
		.type = SettingType::IntSetting,
		.defaultValue = 0,
		.limitHandler = IntLimit(0, 2), }},
	{ "forward_force_old_unquoted", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "disable_chat_themes", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "remember_compress_images", {
		.type = SettingType::BoolSetting,
		.defaultValue = true, }},
	{ "view_profile_on_top", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
	{ "emoji_sidebar", {
		.type = SettingType::BoolSetting,
		.defaultValue = true, }},
	{ "emoji_sidebar_right_click", {
		.type = SettingType::BoolSetting,
		.defaultValue = false, }},
};

using OldOptionKey = QString;
using NewOptionKey = QString;

const std::map<OldOptionKey, NewOptionKey, std::greater<OldOptionKey>> ReplacedOptionsMap {
	{ "adaptive_baloons", "adaptive_bubbles" },
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

QByteArray GenerateSettingsJson(bool areDefault = false) {
	auto settings = QJsonObject();

	auto settingsFoldersLocal = QJsonObject();

	const auto getRef = [&settings] (
			QStringList &keyParts,
			const Definition &def) -> QJsonValueRef {
		const auto firstKey = keyParts.takeFirst();
		if (!settings.contains(firstKey)) {
			settings.insert(firstKey, QJsonObject());
		}
		auto resultRef = settings[firstKey];
		for (const auto &key : keyParts) {
			auto referenced = resultRef.toObject();
			if (!referenced.contains(key)) {
				referenced.insert(key, QJsonObject());
				resultRef = referenced;
			}
			resultRef = referenced[key];
		}
		return resultRef;
	};

	const auto getValue = [=] (
			const QString &key,
			const Definition &def) -> QJsonValue {
		auto value = (!areDefault)
						? GetWithPending(key)
						: def.fillerValue.isValid()
						? def.fillerValue
						: def.defaultValue.isValid()
						? def.defaultValue
						: QVariant();
		switch (def.type) {
			case SettingType::BoolSetting:
				return value.isValid() ? value.toBool() : false;
			case SettingType::IntSetting:
				return value.isValid() ? value.toInt() : 0;
			case SettingType::QStringSetting:
				return value.isValid() ? value.toString() : QString();
			case SettingType::QJsonArraySetting:
				return value.isValid() ? value.toJsonArray() : QJsonArray();
		}

		return QJsonValue();
	};

	const auto getAccountValue = [=] (const QString &key) -> QJsonValue {
		if (areDefault) {
			return QJsonObject();
		}

		auto values = GetAllWithPending(key);
		auto resultObject = QJsonObject();

		for (auto i = values.constBegin(); i != values.constEnd(); ++i) {
			const auto value = i.value();
			const auto jsonValue = (value.userType() == QMetaType::Bool)
									? QJsonValue(value.toBool())
									: (value.userType() == QMetaType::Int)
									? QJsonValue(value.toInt())
									: (value.userType() == QMetaType::QString)
									? QJsonValue(value.toString())
									: (value.userType() == QMetaType::QJsonArray)
									? QJsonValue(value.toJsonArray())
									: QJsonValue(QJsonValue::Null);
			resultObject.insert(i.key(), jsonValue);
		}

		return resultObject;
	};

	for (const auto &[key, def] : DefinitionMap) {
		if (def.storage == SettingStorage::None) {
			continue;
		}

		auto parts = key.split(QChar('/'));
		auto value = (def.scope == SettingScope::Account)
						? getAccountValue(key)
						: getValue(key, def);
		if (parts.size() > 1) {
			const auto lastKey = parts.takeLast();
			auto ref = getRef(parts, def);
			auto referenced = ref.toObject();
			referenced.insert(lastKey, value);
			ref = referenced;
		} else {
			settings.insert(key, value);
		}
	}

	if (areDefault) {
		settings.insert(qsl("version"), QString::number(AppKotatoVersion));
		settings.insert(qsl("net_speed_boost"), QJsonValue(QJsonValue::Null));
	}

	auto document = QJsonDocument();
	document.setObject(settings);
	return document.toJson(QJsonDocument::Indented);
}

std::unique_ptr<Manager> Data;

QVariantMap GetAllWithPending(const QString &key) {
	return (Data) ? Data->getAllWithPending(key) : QVariantMap();
}

} // namespace

Manager::Manager()
: _jsonWriteTimer([=] { writeTimeout(); }) {
}

void Manager::load() {
	if (!DefaultFileIsValid()) {
		writeDefaultFile();
	}
	if (!readCustomFile()) {
		WriteDefaultCustomFile();
	}
}

void Manager::fill() {
	_settingsHashMap.reserve(DefinitionMap.size());
	_defaultSettingsHashMap.reserve(DefinitionMap.size());

	const auto addDefaultValue = [&] (const QString &option, QVariant value) {
		_settingsHashMap.insert(option, value);
	};

	for (const auto &[key, def] : DefinitionMap) {
		if (def.scope != SettingScope::Global) {
			continue;
		}

		auto defaultValue = def.defaultValue;
		if (!defaultValue.isValid()) {
			if (def.type == SettingType::BoolSetting) {
				defaultValue = false;
			} else if (def.type == SettingType::IntSetting) {
				defaultValue = 0;
			} else if (def.type == SettingType::QStringSetting) {
				defaultValue = QString();
			} else if (def.type == SettingType::QJsonArraySetting) {
				defaultValue = QJsonArray();
			} else {
				continue;
			}
		}

		addDefaultValue(key, defaultValue);
	}
}

void Manager::write(bool force) {
	if (force && _jsonWriteTimer.isActive()) {
		_jsonWriteTimer.cancel();
		writeTimeout();
	} else if (!force && !_jsonWriteTimer.isActive()) {
		_jsonWriteTimer.callOnce(kWriteJsonTimeout);
	}
}

QVariant Manager::get(const QString &key, uint64 accountId, bool isTestAccount) {
	const auto mapKey = MakeMapKey(key, accountId, isTestAccount);
	auto result = _settingsHashMap.contains(mapKey)
		? _settingsHashMap.value(mapKey)
		: QVariant();
	if (!result.isValid()) {
		result = _settingsHashMap.contains(key) 
					? _settingsHashMap.value(key)
					: getDefault(key);
		_settingsHashMap.insert(mapKey, result);
	}
	return result;
}

QVariant Manager::getWithPending(const QString &key, uint64 accountId, bool isTestAccount) {
	const auto mapKey = MakeMapKey(key, accountId, isTestAccount);
	auto result = _defaultSettingsHashMap.contains(mapKey)
		? _defaultSettingsHashMap.value(mapKey)
		: _settingsHashMap.contains(mapKey)
		? _settingsHashMap.value(mapKey)
		: QVariant();
	if (!result.isValid()) {
		result = _settingsHashMap.contains(key) 
					? _settingsHashMap.value(key)
					: getDefault(key);
		_settingsHashMap.insert(mapKey, result);
	}
	return result;
}

QVariantMap Manager::getAllWithPending(const QString &key) {
	auto resultMap = QVariantMap();

	if (_defaultSettingsHashMap.contains(key) || _settingsHashMap.contains(key)) {
		resultMap.insert(
			qsl("0"),
			_defaultSettingsHashMap.contains(key)
				? _defaultSettingsHashMap.value(key)
				: _settingsHashMap.value(key));
		return resultMap;
	}

	const auto prefix = key + qsl(":");

	for (auto i = _settingsHashMap.constBegin(); i != _settingsHashMap.constEnd(); ++i) {
		const auto mapKey = i.key();
		if (!mapKey.startsWith(prefix)) {
			continue;
		}

		const auto accountKey = mapKey.mid(prefix.size());
		resultMap.insert(accountKey, i.value());
	}

	for (auto i = _defaultSettingsHashMap.constBegin(); i != _defaultSettingsHashMap.constEnd(); ++i) {
		const auto mapKey = i.key();
		if (!mapKey.startsWith(prefix)) {
			continue;
		}

		const auto accountKey = mapKey.mid(prefix.size());
		resultMap.insert(accountKey, i.value());
	}

	return resultMap;
}

QVariant Manager::getDefault(const QString &key) {
	const auto &defIterator = DefinitionMap.find(key);
	if (defIterator == DefinitionMap.end()) {
		return QVariant();
	}
	const auto defaultValue = &defIterator->second.defaultValue;
	const auto settingType = defIterator->second.type;
	switch (settingType) {
		case SettingType::QStringSetting:
			return QVariant(defaultValue->isValid()
					? defaultValue->toString()
					: QString());
		case SettingType::IntSetting:
			return QVariant(defaultValue->isValid()
					? defaultValue->toInt()
					: 0);
		case SettingType::BoolSetting:
			return QVariant(defaultValue->isValid()
					? defaultValue->toBool()
					: false);
		case SettingType::QJsonArraySetting:
			return QVariant(defaultValue->isValid()
					? defaultValue->toJsonArray()
					: QJsonArray());
	}

	return QVariant();
}

rpl::producer<QString> Manager::events(const QString &key, uint64 accountId, bool isTestAccount) {
	const auto mapKey = MakeMapKey(key, accountId, isTestAccount);
	return _eventStream.events() | rpl::filter(rpl::mappers::_1 == mapKey);
}

rpl::producer<QString> Manager::eventsWithPending(const QString &key, uint64 accountId, bool isTestAccount) {
	const auto mapKey = MakeMapKey(key, accountId, isTestAccount);
	return _pendingEventStream.events() | rpl::filter(rpl::mappers::_1 == mapKey);
}

void Manager::set(const QString &key, QVariant value, uint64 accountId, bool isTestAccount) {
	const auto mapKey = MakeMapKey(key, accountId, isTestAccount);
	_settingsHashMap.insert(mapKey, value);
	_eventStream.fire_copy(mapKey);
}

void Manager::setAfterRestart(const QString &key, QVariant value, uint64 accountId, bool isTestAccount) {
	const auto mapKey = MakeMapKey(key, accountId, isTestAccount);
	if (!_settingsHashMap.contains(mapKey)
		|| _settingsHashMap.value(mapKey) != value) {
		_defaultSettingsHashMap.insert(mapKey, value);
	} else if (_settingsHashMap.contains(mapKey)
		&& _settingsHashMap.value(mapKey) == value) {
		_defaultSettingsHashMap.remove(mapKey);
	}
	_pendingEventStream.fire_copy(mapKey);
}

void Manager::reset(const QString &key, uint64 accountId, bool isTestAccount) {
	set(key, getDefault(key), accountId, isTestAccount);
}

void Manager::resetAfterRestart(const QString &key, uint64 accountId, bool isTestAccount) {
	setAfterRestart(key, getDefault(key), accountId, isTestAccount);
}

bool Manager::readCustomFile() {
	QFile file(CustomFilePath());
	if (!file.exists()) {
		return false;
	}
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

	const auto getObjectValue = [&settings] (
			QStringList &keyParts,
			const Definition &def) -> QJsonValue {
		const auto firstKey = keyParts.takeFirst();
		if (!settings.contains(firstKey)) {
			return QJsonValue();
		}
		auto resultRef = settings.value(firstKey);
		for (const auto &key : keyParts) {
			auto referenced = resultRef.toObject();
			if (!referenced.contains(key)) {
				return QJsonValue();
			}
			resultRef = referenced.value(key);
		}
		return resultRef;
	};

	const auto prepareAccountOptions = [] (
		const QString &key,
		const Definition &def,
		const QJsonValue &val,
		Fn<void(const QString &,
				const Definition &,
				const QJsonValue &,
				uint64,
				bool)> callback) {

		if (val.isUndefined()) {
			return;
		} else if (def.scope == SettingScope::Account && val.isObject()) {
			const auto accounts = val.toObject();
			if (accounts.isEmpty()) {
				return;
			}

			for (auto i = accounts.constBegin(); i != accounts.constEnd(); ++i) {
				auto optionKey = i.key();
				auto isTestAccount = false;
				if (optionKey.startsWith("test_")) {
					isTestAccount = true;
					optionKey = optionKey.mid(5);
				}
				auto accountId = optionKey.toULongLong();
				callback(key, def, i.value(), accountId, (accountId == 0) ? false : isTestAccount);
			}
		} else {
			callback(key, def, val, 0, false);
		}
	};

	const auto setValue = [this] (
		const QString &key,
		const Definition &def,
		const QJsonValue &val,
		uint64 accountId,
		bool isTestAccount) {

		const auto defType = def.type;
		if (defType == SettingType::BoolSetting) {
			if (val.isBool()) {
				set(key, val.toBool(), accountId, isTestAccount);
			} else if (val.isDouble()) {
				set(key, val.toDouble() != 0.0, accountId, isTestAccount);
			}
		} else if (defType == SettingType::IntSetting) {
			if (val.isDouble()) {
				auto intValue = qFloor(val.toDouble());
				set(key,
					(def.limitHandler)
						? def.limitHandler(intValue)
						: intValue,
					accountId,
					isTestAccount);
			}
		} else if (defType == SettingType::QStringSetting) {
			if (val.isString()) {
				set(key, val.toString(), accountId, isTestAccount);
			}
		} else if (defType == SettingType::QJsonArraySetting) {
			if (val.isArray()) {
				auto arrayValue = val.toArray();
				set(key, (def.limitHandler)
					? def.limitHandler(arrayValue)
					: arrayValue,
					accountId,
					isTestAccount);
			}
		}
	};

	for (const auto &[oldkey, newkey] : ReplacedOptionsMap) {
		const auto &defIterator = DefinitionMap.find(newkey);
		if (defIterator == DefinitionMap.end()) {
			continue;
		}
		auto parts = oldkey.split(QChar('/'));
		const auto val = (parts.size() > 1)
							? getObjectValue(parts, defIterator->second)
							: settings.value(oldkey);

		if (!val.isUndefined()) {
			prepareAccountOptions(newkey, defIterator->second, val, setValue);
		}
	}

	for (const auto &[key, def] : DefinitionMap) {
		if (def.storage == SettingStorage::None) {
			continue;
		}
		auto parts = key.split(QChar('/'));
		const auto val = (parts.size() > 1)
							? getObjectValue(parts, def)
							: settings.value(key);

		if (!val.isUndefined()) {
			prepareAccountOptions(key, def, val, setValue);
		}
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
	file.write(GenerateSettingsJson(true));
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
	file.write(GenerateSettingsJson());
}

void Manager::writeTimeout() {
	writeCurrentSettings();
}

void Manager::writing() {
	_jsonWriteTimer.cancel();
}

void Start() {
	if (Data) return;

	Data = std::make_unique<Manager>();
	Data->fill();
}

void Load() {
	if (!Data) return;

	Data->load();
}

void Write() {
	if (!Data) return;

	Data->write();
}

void Finish() {
	if (!Data) return;

	Data->write(true);
}

QVariant Get(const QString &key, uint64 accountId, bool isTestAccount) {
	return (Data) ? Data->get(key, accountId, isTestAccount) : QVariant();
}

QVariant GetWithPending(const QString &key, uint64 accountId, bool isTestAccount) {
	return (Data) ? Data->getWithPending(key, accountId, isTestAccount) : QVariant();
}

rpl::producer<QString> Events(const QString &key, uint64 accountId, bool isTestAccount) {
	return (Data) ? Data->events(key, accountId, isTestAccount) : rpl::single(QString());
}

rpl::producer<QString> EventsWithPending(const QString &key, uint64 accountId, bool isTestAccount) {
	return (Data) ? Data->eventsWithPending(key, accountId, isTestAccount) : rpl::single(QString());
}

void Set(const QString &key, QVariant value, uint64 accountId, bool isTestAccount) {
	if (!Data) return;

	Data->set(key, value, accountId, isTestAccount);
}

void SetAfterRestart(const QString &key, QVariant value, uint64 accountId, bool isTestAccount) {
	if (!Data) return;

	Data->setAfterRestart(key, value, accountId, isTestAccount);
}

void Reset(const QString &key, uint64 accountId, bool isTestAccount) {
	if (!Data) return;

	Data->reset(key, accountId, isTestAccount);
}

void ResetAfterRestart(const QString &key, uint64 accountId, bool isTestAccount) {
	if (!Data) return;

	Data->resetAfterRestart(key, accountId, isTestAccount);
}

} // namespace JsonSettings
} // namespace Kotato
