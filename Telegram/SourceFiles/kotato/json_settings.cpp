/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/json_settings.h"

#include "mainwindow.h"
#include "mainwidget.h"
#include "window/window_controller.h"
#include "core/application.h"
#include "base/parse_helper.h"
#include "facades.h"
#include "ui/widgets/input_fields.h"
#include "data/data_chat_filters.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>
#include <QtCore/QTimer>

namespace Kotato {
namespace JsonSettings {
namespace {

constexpr auto kFiltersLimit = 10;
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

bool ReadOption(QJsonObject obj, QString key, std::function<void(QJsonValue)> callback) {
	const auto it = obj.constFind(key);
	if (it == obj.constEnd()) {
		return false;
	}
	callback(*it);
	return true;
}

bool ReadObjectOption(QJsonObject obj, QString key, std::function<void(QJsonObject)> callback) {
	auto readResult = false;
	auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
		if (v.isObject()) {
			callback(v.toObject());
			readResult = true;
		}
	});
	return (readValueResult && readResult);
}

bool ReadAccountObjectOption(QJsonObject obj, QString key, std::function<void(int, QJsonValue)> callback, std::function<bool(QJsonValue)> test) {
	auto readResult = false;
	auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
		if (!v.isObject()) {
			return;
		}

		auto vObj = v.toObject();

		if (vObj.isEmpty()) {
			return;
		}

		bool isInt;

		for (auto i = vObj.begin(), e = vObj.end(); i != e; ++i) {
			if (!test(i.value())) {
				continue;
			}

			auto key = i.key();
			if (key.startsWith("test_")) {
				key = key.mid(5).prepend("-");
			}

			auto account_id = key.toInt(&isInt, 10);

			if (isInt) {
				callback(account_id, i.value());
				readResult = true;
			}
		}
	});
	return (readValueResult && readResult);
}

bool ReadArrayOption(QJsonObject obj, QString key, std::function<void(QJsonArray)> callback) {
	auto readResult = false;
	auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
		if (v.isArray()) {
			callback(v.toArray());
			readResult = true;
		}
	});
	return (readValueResult && readResult);
}

bool ReadStringOption(QJsonObject obj, QString key, std::function<void(QString)> callback) {
	auto readResult = false;
	auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
		if (v.isString()) {
			callback(v.toString());
			readResult = true;
		}
	});
	return (readValueResult && readResult);
}

bool ReadDoubleOption(QJsonObject obj, QString key, std::function<void(double)> callback) {
	auto readResult = false;
	auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
		if (v.isDouble()) {
			callback(v.toDouble());
			readResult = true;
		}
	});
	return (readValueResult && readResult);
}

bool ReadIntOption(QJsonObject obj, QString key, std::function<void(int)> callback) {
	auto readResult = false;
	auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
		if (v.isDouble()) {
			callback(v.toInt());
			readResult = true;
		}
	});
	return (readValueResult && readResult);
}

bool ReadBoolOption(QJsonObject obj, QString key, std::function<void(bool)> callback) {
	auto readResult = false;
	auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
		if (v.isBool()) {
			callback(v.toBool());
			readResult = true;
		}
	});
	return (readValueResult && readResult);
}

QByteArray GenerateSettingsJson(bool areDefault = false) {
	auto settings = QJsonObject();

	auto settingsFonts = QJsonObject();
	auto settingsFolders = QJsonObject();
	auto settingsFoldersDefault = QJsonObject();
	auto settingsFoldersLocal = QJsonObject();
	auto settingsScales = QJsonArray();
	auto settingsReplaces = QJsonArray();

	if (areDefault) {
		settingsFonts.insert(qsl("main"), qsl("Open Sans"));
		settingsFonts.insert(qsl("semibold"), qsl("Open Sans Semibold"));
		settingsFonts.insert(qsl("monospaced"), qsl("Consolas"));
		settingsFonts.insert(qsl("semibold_is_bold"), false);

		settings.insert(qsl("version"), QString::number(AppKotatoVersion));
		settings.insert(qsl("net_speed_boost"), QJsonValue(QJsonValue::Null));
	} else {
		settings.insert(qsl("net_speed_boost"), cNetSpeedBoost());

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

		const auto currentScales = cInterfaceScales();
		for (int i = 0; i < currentScales.size(); i++) {
			settingsScales << currentScales[i];
		}

		const auto currentReplaces = cCustomReplaces();
		for (auto i = currentReplaces.constBegin(), e = currentReplaces.constEnd(); i != e; ++i) {
			auto a = QJsonArray();
			a << i.key() << i.value();
			settingsReplaces << a;
		}

		const auto defaultFilterIdMap = cDefaultFilterId();
		for (auto i = defaultFilterIdMap.constBegin(), e = defaultFilterIdMap.constEnd(); i != e; ++i) {
			auto value = i.value();
			if (value == 0) {
				continue;
			}

			auto key = QString::number(std::abs(i.key()));

			if (i.key() < 0) {
				key.prepend("test_");
			}

			settingsFoldersDefault.insert(key, value);
		}

		using PeerType = LocalFolder::Peer::Type;

		auto peerTypeToStr = [](PeerType type) {
			return (type == PeerType::Channel)
				? qsl("channel")
				: (type == PeerType::Chat)
				? qsl("chat")
				: qsl("user");
		};

		auto &localFolders = cRefLocalFolders();
		for (auto folder : localFolders) {

			// We can't allow to use cloud IDs for local folders.
			if (folder.id <= kFiltersLimit) {
				continue;
			}

			auto accountId = QString::number(std::abs(folder.ownerId));
			auto flags = base::flags<Data::ChatFilter::Flag>::from_raw(folder.flags);

			if (folder.ownerId < 0) {
				accountId.prepend("test_");
			}

			if (!settingsFoldersLocal.contains(accountId)) {
				settingsFoldersLocal.insert(accountId, QJsonArray());
			}

			auto accountRef = settingsFoldersLocal.find(accountId).value();
			auto accountArray = accountRef.toArray();

			auto folderObject = QJsonObject();

			folderObject.insert(qsl("id"), folder.id);
			folderObject.insert(qsl("order"), folder.cloudOrder);
			folderObject.insert(qsl("name"), folder.name);
			folderObject.insert(qsl("emoticon"), folder.emoticon);

			if (flags & Data::ChatFilter::Flag::Contacts) {
				folderObject.insert(qsl("include_contacts"), true);
			}
			if (flags & Data::ChatFilter::Flag::NonContacts) {
				folderObject.insert(qsl("include_non_contacts"), true);
			}
			if (flags & Data::ChatFilter::Flag::Groups) {
				folderObject.insert(qsl("include_groups"), true);
			}
			if (flags & Data::ChatFilter::Flag::Channels) {
				folderObject.insert(qsl("include_channels"), true);
			}
			if (flags & Data::ChatFilter::Flag::Bots) {
				folderObject.insert(qsl("include_bots"), true);
			}
			if (flags & Data::ChatFilter::Flag::NoMuted) {
				folderObject.insert(qsl("exclude_muted"), true);
			}
			if (flags & Data::ChatFilter::Flag::NoRead) {
				folderObject.insert(qsl("exclude_read"), true);
			}
			if (flags & Data::ChatFilter::Flag::NoArchived) {
				folderObject.insert(qsl("exclude_archived"), true);
			}
			if (flags & Data::ChatFilter::Flag::Owned) {
				folderObject.insert(qsl("exclude_not_owned"), true);
			}
			if (flags & Data::ChatFilter::Flag::Admin) {
				folderObject.insert(qsl("exclude_not_admin"), true);
			}
			if (flags & Data::ChatFilter::Flag::NotOwned) {
				folderObject.insert(qsl("exclude_owned"), true);
			}
			if (flags & Data::ChatFilter::Flag::NotAdmin) {
				folderObject.insert(qsl("exclude_admin"), true);
			}
			if (flags & Data::ChatFilter::Flag::Recent) {
				folderObject.insert(qsl("exclude_non_recent"), true);
			}
			if (flags & Data::ChatFilter::Flag::NoFilter) {
				folderObject.insert(qsl("exclude_filtered"), true);
			}

			auto folderNever = QJsonArray();
			for (auto peer : folder.never) {
				auto peerObj = QJsonObject();
				peerObj.insert(qsl("type"), peerTypeToStr(peer.type));
				peerObj.insert(qsl("id"), peer.id);
				if (peer.accessHash != 0) {
					peerObj.insert(qsl("hash"), QString::number(peer.accessHash));
				}
				folderNever << peerObj;
			}
			folderObject.insert(qsl("never"), folderNever);

			auto folderPinned = QJsonArray();
			for (auto peer : folder.pinned) {
				auto peerObj = QJsonObject();
				peerObj.insert(qsl("type"), peerTypeToStr(peer.type));
				peerObj.insert(qsl("id"), peer.id);
				if (peer.accessHash != 0) {
					peerObj.insert(qsl("hash"), QString::number(peer.accessHash));
				}
				folderPinned << peerObj;
			}
			folderObject.insert(qsl("pinned"), folderPinned);

			auto folderAlways = QJsonArray();
			for (auto peer : folder.always) {
				auto peerObj = QJsonObject();
				peerObj.insert(qsl("type"), peerTypeToStr(peer.type));
				peerObj.insert(qsl("id"), peer.id);
				if (peer.accessHash != 0) {
					peerObj.insert(qsl("hash"), QString::number(peer.accessHash));
				}
				folderAlways << peerObj;
			}
			folderObject.insert(qsl("always"), folderAlways);

			accountArray << folderObject;
			accountRef = accountArray;
		}
	}

	settings.insert(qsl("sticker_height"), StickerHeight());
	settings.insert(qsl("sticker_scale_both"), StickerScaleBoth());
	settings.insert(qsl("adaptive_bubbles"), AdaptiveBubbles());
	settings.insert(qsl("big_emoji_outline"), BigEmojiOutline());
	settings.insert(qsl("always_show_scheduled"), cAlwaysShowScheduled());
	settings.insert(qsl("show_chat_id"), cShowChatId());
	settings.insert(qsl("show_phone_in_drawer"), cShowPhoneInDrawer());
	settings.insert(qsl("chat_list_lines"), DialogListLines());
	settings.insert(qsl("disable_up_edit"), cDisableUpEdit());
	settings.insert(qsl("confirm_before_calls"), cConfirmBeforeCall());
	settings.insert(qsl("native_decorations"), cUseNativeDecorations());
	settings.insert(qsl("recent_stickers_limit"), RecentStickersLimit());
	settings.insert(qsl("userpic_corner_type"), cUserpicCornersType());
	settings.insert(qsl("always_show_top_userpic"), cShowTopBarUserpic());
	settings.insert(qsl("disable_tray_counter"), cDisableTrayCounter());
	settings.insert(qsl("use_telegram_panel_icon"), cUseTelegramPanelIcon());
	settings.insert(qsl("custom_app_icon"), cCustomAppIcon());
	settings.insert(qsl("profile_top_mute"), cProfileTopBarNotifications());
	settings.insert(qsl("hover_emoji_panel"), HoverEmojiPanel());
	settings.insert(qsl("monospace_large_bubbles"), MonospaceLargeBubbles());
	settings.insert(qsl("forward_retain_selection"), cForwardRetainSelection());
	settings.insert(qsl("forward_on_click"), cForwardChatOnClick());

	settingsFonts.insert(qsl("use_system_font"), cUseSystemFont());
	settingsFonts.insert(qsl("use_original_metrics"), cUseOriginalMetrics());

	settingsFolders.insert(qsl("default"), settingsFoldersDefault);
	settingsFolders.insert(qsl("count_unmuted_only"), cUnmutedFilterCounterOnly());
	settingsFolders.insert(qsl("hide_edit_button"), cHideFilterEditButton());
	settingsFolders.insert(qsl("hide_names"), cHideFilterNames());
	settingsFolders.insert(qsl("hide_all_chats"), cHideFilterAllChats());
	settingsFolders.insert(qsl("local"), settingsFoldersLocal);

	settings.insert(qsl("fonts"), settingsFonts);
	settings.insert(qsl("folders"), settingsFolders);
	settings.insert(qsl("scales"), settingsScales);
	settings.insert(qsl("replaces"), settingsReplaces);

	auto document = QJsonDocument();
	document.setObject(settings);
	return document.toJson(QJsonDocument::Indented);
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

	ReadObjectOption(settings, "fonts", [&](auto o) {
		ReadStringOption(o, "main", [&](auto v) {
			cSetMainFont(v);
		});

		ReadStringOption(o, "semibold", [&](auto v) {
			cSetSemiboldFont(v);
		});

		ReadBoolOption(o, "semibold_is_bold", [&](auto v) {
			cSetSemiboldFontIsBold(v);
		});

		ReadStringOption(o, "monospaced", [&](auto v) {
			cSetMonospaceFont(v);
		});

		ReadBoolOption(o, "use_system_font", [&](auto v) {
			cSetUseSystemFont(v);
		});

		ReadBoolOption(o, "use_original_metrics", [&](auto v) {
			cSetUseOriginalMetrics(v);
		});
	});

	ReadIntOption(settings, "sticker_height", [&](auto v) {
		if (v >= 64 && v <= 256) {
			SetStickerHeight(v);
		}
	});

	ReadBoolOption(settings, "sticker_scale_both", [&](auto v) {
		SetStickerScaleBoth(v);
	});

	auto isAdaptiveBubblesSet = ReadBoolOption(settings, "adaptive_bubbles", [&](auto v) {
		SetAdaptiveBubbles(v);
	});

	if (!isAdaptiveBubblesSet) {
		ReadBoolOption(settings, "adaptive_baloons", [&](auto v) {
			SetAdaptiveBubbles(v);
		});
	}

	ReadBoolOption(settings, "monospace_large_bubbles", [&](auto v) {
		SetMonospaceLargeBubbles(v);
	});

	ReadBoolOption(settings, "big_emoji_outline", [&](auto v) {
		SetBigEmojiOutline(v);
	});

	ReadBoolOption(settings, "always_show_scheduled", [&](auto v) {
		cSetAlwaysShowScheduled(v);
	});

	auto isShowChatIdSet = ReadIntOption(settings, "show_chat_id", [&](auto v) {
		if (v >= 0 && v <= 2) {
			cSetShowChatId(v);
		}
	});

	if (!isShowChatIdSet) {
		ReadBoolOption(settings, "show_chat_id", [&](auto v) {
			cSetShowChatId(v ? 1 : 0);
		});
	}

	ReadOption(settings, "net_speed_boost", [&](auto v) {
		if (v.isString()) {

			const auto option = v.toString();
			if (option == "high") {
				SetNetworkBoost(3);
			} else if (option == "medium") {
				SetNetworkBoost(2);
			} else if (option == "low") {
				SetNetworkBoost(1);
			} else {
				SetNetworkBoost(0);
			}

		} else if (v.isNull()) {
			SetNetworkBoost(0);
		} else if (v.isDouble()) {
			SetNetworkBoost(v.toInt());
		}
	});

	ReadBoolOption(settings, "show_phone_in_drawer", [&](auto v) {
		cSetShowPhoneInDrawer(v);
	});

	ReadArrayOption(settings, "scales", [&](auto v) {
		ClearCustomScales();
		for (auto i = v.constBegin(), e = v.constEnd(); i != e; ++i) {
			if (!(*i).isDouble()) {
				continue;
			}

			AddCustomScale((*i).toInt());
		}
	});

	ReadIntOption(settings, "chat_list_lines", [&](auto v) {
		if (v >= 1 && v <= 2) {
			SetDialogListLines(v);
		}
	});

	ReadBoolOption(settings, "disable_up_edit", [&](auto v) {
		cSetDisableUpEdit(v);
	});

	ReadArrayOption(settings, "replaces", [&](auto v) {
		for (auto i = v.constBegin(), e = v.constEnd(); i != e; ++i) {
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
	});

	ReadBoolOption(settings, "confirm_before_calls", [&](auto v) {
		cSetConfirmBeforeCall(v);
	});

	ReadBoolOption(settings, "native_decorations", [&](auto v) {
		cSetUseNativeDecorations(v);
	});

	ReadIntOption(settings, "recent_stickers_limit", [&](auto v) {
		if (v >= 0 && v <= 200) {
			SetRecentStickersLimit(v);
		}
	});

	ReadIntOption(settings, "userpic_corner_type", [&](auto v) {
		if (v >= 0 && v <= 3) {
			cSetUserpicCornersType(v);
		}
	});

	ReadBoolOption(settings, "always_show_top_userpic", [&](auto v) {
		cSetShowTopBarUserpic(v);
	});

	ReadBoolOption(settings, "disable_tray_counter", [&](auto v) {
		cSetDisableTrayCounter(v);
	});

	ReadBoolOption(settings, "use_telegram_panel_icon", [&](auto v) {
		cSetUseTelegramPanelIcon(v);
	});

	ReadIntOption(settings, "custom_app_icon", [&](auto v) {
		if (v >= 0 && v <= 5) {
			cSetCustomAppIcon(v);
		}
	});

	ReadObjectOption(settings, "folders", [&](auto o) {
		auto isDefaultFilterRead = ReadIntOption(o, "default", [&](auto v) {
			SetDefaultFilterId(0, v);
		});

		if (!isDefaultFilterRead) {
			ReadAccountObjectOption(o, "default", [&](auto account_id, auto value) {
				SetDefaultFilterId(account_id, value.toInt(0));
			}, [](auto v) {
				return v.toInt(0) != 0;
			});
		}

		ReadBoolOption(o, "count_unmuted_only", [&](auto v) {
			cSetUnmutedFilterCounterOnly(v);
		});

		ReadBoolOption(o, "hide_edit_button", [&](auto v) {
			cSetHideFilterEditButton(v);
		});

		ReadBoolOption(o, "hide_names", [&](auto v) {
			cSetHideFilterNames(v);
		});

		ReadBoolOption(o, "hide_all_chats", [&](auto v) {
			cSetHideFilterAllChats(v);
		});

		ReadAccountObjectOption(o, "local", [&](auto account_id, auto value) {
			auto v = value.toArray();
			auto &folderOptionRef = cRefLocalFolders();
			for (auto i = v.constBegin(), e = v.constEnd(); i != e; ++i) {
				if (!(*i).isObject()) {
					continue;
				}

				const auto folderObject = (*i).toObject();
				LocalFolder folderStruct;
				folderStruct.ownerId = account_id;
				auto flags = base::flags<Data::ChatFilter::Flag>(0);

				ReadIntOption(folderObject, "id", [&](auto id) {
					folderStruct.id = id;
				});

				// We can't allow to use cloud IDs for local folders.
				if (folderStruct.id <= kFiltersLimit) {
					continue;
				}

				ReadIntOption(folderObject, "order", [&](auto order) {
					folderStruct.cloudOrder = order;
				});

				ReadStringOption(folderObject, "name", [&](auto name) {
					folderStruct.name = name;
				});

				ReadStringOption(folderObject, "emoticon", [&](auto emoticon) {
					folderStruct.emoticon = emoticon;
				});

				ReadBoolOption(folderObject, "include_contacts", [&](auto include_contacts) {
					if (include_contacts) {
						flags |= Data::ChatFilter::Flag::Contacts;
					}
				});

				ReadBoolOption(folderObject, "include_non_contacts", [&](auto include_non_contacts) {
					if (include_non_contacts) {
						flags |= Data::ChatFilter::Flag::NonContacts;
					}
				});

				ReadBoolOption(folderObject, "include_groups", [&](auto include_groups) {
					if (include_groups) {
						flags |= Data::ChatFilter::Flag::Groups;
					}
				});

				ReadBoolOption(folderObject, "include_channels", [&](auto include_channels) {
					if (include_channels) {
						flags |= Data::ChatFilter::Flag::Channels;
					}
				});

				ReadBoolOption(folderObject, "include_bots", [&](auto include_bots) {
					if (include_bots) {
						flags |= Data::ChatFilter::Flag::Bots;
					}
				});

				ReadBoolOption(folderObject, "exclude_muted", [&](auto exclude_muted) {
					if (exclude_muted) {
						flags |= Data::ChatFilter::Flag::NoMuted;
					}
				});

				ReadBoolOption(folderObject, "exclude_read", [&](auto exclude_read) {
					if (exclude_read) {
						flags |= Data::ChatFilter::Flag::NoRead;
					}
				});

				ReadBoolOption(folderObject, "exclude_archived", [&](auto exclude_archived) {
					if (exclude_archived) {
						flags |= Data::ChatFilter::Flag::NoArchived;
					}
				});

				ReadBoolOption(folderObject, "exclude_not_owned", [&](auto exclude_not_owned) {
					if (exclude_not_owned) {
						flags |= Data::ChatFilter::Flag::Owned;
					}
				});

				ReadBoolOption(folderObject, "exclude_not_admin", [&](auto exclude_not_admin) {
					if (exclude_not_admin) {
						flags |= Data::ChatFilter::Flag::Admin;
					}
				});

				ReadBoolOption(folderObject, "exclude_owned", [&](auto exclude_owned) {
					if (exclude_owned) {
						flags |= Data::ChatFilter::Flag::NotOwned;
					}
				});

				ReadBoolOption(folderObject, "exclude_admin", [&](auto exclude_admin) {
					if (exclude_admin) {
						flags |= Data::ChatFilter::Flag::NotAdmin;
					}
				});

				ReadBoolOption(folderObject, "exclude_non_recent", [&](auto exclude_non_recent) {
					if (exclude_non_recent) {
						flags |= Data::ChatFilter::Flag::Recent;
					}
				});

				ReadBoolOption(folderObject, "exclude_filtered", [&](auto exclude_filtered) {
					if (exclude_filtered) {
						flags |= Data::ChatFilter::Flag::NoFilter;
					}
				});

				folderStruct.flags = flags.value();

				ReadArrayOption(folderObject, "never", [&](auto never) {
					for (auto j = never.constBegin(), z = never.constEnd(); j != z; ++j) {
						if (!(*j).isObject()) {
							continue;
						}

						auto peer = (*j).toObject();
						LocalFolder::Peer peerStruct;

						auto isPeerIdRead = ReadIntOption(peer, "id", [&](auto id) {
							peerStruct.id = id;
						});

						if (peerStruct.id == 0 || !isPeerIdRead) {
							continue;
						}

						auto isPeerTypeRead = ReadStringOption(peer, "type", [&](auto type) {
							peerStruct.type = QString::compare(type.toLower(), "channel")
								? LocalFolder::Peer::Type::Channel
								: QString::compare(type.toLower(), "chat")
								? LocalFolder::Peer::Type::Chat
								: LocalFolder::Peer::Type::User;
						});

						if (!isPeerTypeRead) {
							peerStruct.type = LocalFolder::Peer::Type::User;
						}

						ReadStringOption(peer, "hash", [&](auto hashString) {
							const auto hash = hashString.toULongLong();
							if (hash) {
								peerStruct.accessHash = hash;
							}
						});

						folderStruct.never.push_back(peerStruct);
					}
				});

				ReadArrayOption(folderObject, "pinned", [&](auto pinned) {
					for (auto j = pinned.constBegin(), z = pinned.constEnd(); j != z; ++j) {
						if (!(*j).isObject()) {
							continue;
						}

						auto peer = (*j).toObject();
						LocalFolder::Peer peerStruct;

						auto isPeerIdRead = ReadIntOption(peer, "id", [&](auto id) {
							peerStruct.id = id;
						});

						if (peerStruct.id == 0 || !isPeerIdRead) {
							continue;
						}

						auto isPeerTypeRead = ReadStringOption(peer, "type", [&](auto type) {
							peerStruct.type = QString::compare(type.toLower(), "channel")
								? LocalFolder::Peer::Type::Channel
								: QString::compare(type.toLower(), "chat")
								? LocalFolder::Peer::Type::Chat
								: LocalFolder::Peer::Type::User;
						});

						if (!isPeerTypeRead) {
							peerStruct.type = LocalFolder::Peer::Type::User;
						}

						ReadStringOption(peer, "hash", [&](auto hashString) {
							const auto hash = hashString.toULongLong();
							if (hash) {
								peerStruct.accessHash = hash;
							}
						});

						folderStruct.pinned.push_back(peerStruct);
					}
				});

				ReadArrayOption(folderObject, "always", [&](auto always) {
					for (auto j = always.constBegin(), z = always.constEnd(); j != z; ++j) {
						if (!(*j).isObject()) {
							continue;
						}

						auto peer = (*j).toObject();
						LocalFolder::Peer peerStruct;

						auto isPeerIdRead = ReadIntOption(peer, "id", [&](auto id) {
							peerStruct.id = id;
						});

						if (peerStruct.id == 0 || !isPeerIdRead) {
							continue;
						}

						auto isPeerTypeRead = ReadStringOption(peer, "type", [&](auto type) {
							peerStruct.type = QString::compare(type.toLower(), "channel")
								? LocalFolder::Peer::Type::Channel
								: QString::compare(type.toLower(), "chat")
								? LocalFolder::Peer::Type::Chat
								: LocalFolder::Peer::Type::User;
						});

						if (!isPeerTypeRead) {
							peerStruct.type = LocalFolder::Peer::Type::User;
						}

						ReadStringOption(peer, "hash", [&](auto hashString) {
							const auto hash = hashString.toULongLong();
							if (hash) {
								peerStruct.accessHash = hash;
							}
						});

						folderStruct.always.push_back(peerStruct);
					}
				});

				folderOptionRef.push_back(folderStruct);
			}
		}, [](auto v) {
			return v.isArray();
		});
	});

	ReadBoolOption(settings, "profile_top_mute", [&](auto v) {
		cSetProfileTopBarNotifications(v);
	});

	ReadBoolOption(settings, "hover_emoji_panel", [&](auto v) {
		SetHoverEmojiPanel(v);
	});

	ReadBoolOption(settings, "forward_retain_selection", [&](auto v) {
		cSetForwardRetainSelection(v);
	});

	ReadBoolOption(settings, "forward_on_click", [&](auto v) {
		cSetForwardChatOnClick(v);
	});
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

} // namespace JsonSettings
} // namespace Kotato
