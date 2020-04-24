/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/settings_menu.h"

#include "settings/settings_common.h"
#include "settings/settings_chat.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/text/text_utilities.h" // Ui::Text::ToUpper
#include "boxes/connection_box.h"
#include "kotato/boxes/fonts_box.h"
#include "kotato/boxes/radio_box.h"
#include "boxes/about_box.h"
#include "boxes/confirm_box.h"
#include "platform/platform_specific.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "core/update_checker.h"
#include "kotato/json_settings.h"
#include "core/application.h"
#include "storage/localstorage.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "layout.h"
#include "mainwindow.h"
#include "facades.h"
#include "app.h"
#include "styles/style_settings.h"

namespace Settings {

namespace {

QString NetBoostLabel(int boost) {
	switch (boost) {
		case 0:
			return tr::ktg_net_speed_boost_default(tr::now);

		case 1:
			return tr::ktg_net_speed_boost_slight(tr::now);

		case 2:
			return tr::ktg_net_speed_boost_medium(tr::now);

		case 3:
			return tr::ktg_net_speed_boost_big(tr::now);

		default:
			Unexpected("Boost in Settings::NetBoostLabel.");
	}
	return QString();
}

QString UserpicRoundingLabel(int rounding) {
	switch (rounding) {
		case 0:
			return tr::ktg_settings_userpic_rounding_none(tr::now);

		case 1:
			return tr::ktg_settings_userpic_rounding_small(tr::now);

		case 2:
			return tr::ktg_settings_userpic_rounding_big(tr::now);

		case 3:
			return tr::ktg_settings_userpic_rounding_full(tr::now);

		default:
			Unexpected("Rounding in Settings::UserpicRoundingLabel.");
	}
	return QString();
}

QString TrayIconLabel(int icon) {
	switch (icon) {
		case 0:
			return tr::ktg_settings_tray_icon_default(tr::now);

		case 1:
			return tr::ktg_settings_tray_icon_blue(tr::now);

		case 2:
			return tr::ktg_settings_tray_icon_green(tr::now);

		case 3:
			return tr::ktg_settings_tray_icon_orange(tr::now);

		case 4:
			return tr::ktg_settings_tray_icon_red(tr::now);

		case 5:
			return tr::ktg_settings_tray_icon_legacy(tr::now);

		default:
			Unexpected("Icon in Settings::TrayIconLabel.");
	}
	return QString();
}

} // namespace

#define SettingsMenuCSwitch(LangKey, Option) AddButton( \
	container, \
	tr::LangKey(), \
	st::settingsButton \
)->toggleOn( \
	rpl::single(c##Option()) \
)->toggledValue( \
) | rpl::filter([](bool enabled) { \
	return (enabled != c##Option()); \
}) | rpl::start_with_next([](bool enabled) { \
	cSet##Option(enabled); \
	::Kotato::JsonSettings::Write(); \
}, container->lifetime());

#define SettingsMenuCFilterSwitch(LangKey, Option) AddButton( \
	container, \
	tr::LangKey(), \
	st::settingsButton \
)->toggleOn( \
	rpl::single(c##Option()) \
)->toggledValue( \
) | rpl::filter([](bool enabled) { \
	return (enabled != c##Option()); \
}) | rpl::start_with_next([controller](bool enabled) { \
	cSet##Option(enabled); \
	::Kotato::JsonSettings::Write(); \
	controller->reloadFiltersMenu(); \
	App::wnd()->fixOrder(); \
}, container->lifetime());

#define SettingsMenuSwitch(LangKey, Option) AddButton( \
	container, \
	tr::LangKey(), \
	st::settingsButton \
)->toggleOn( \
	rpl::single(Option()) \
)->toggledValue( \
) | rpl::filter([](bool enabled) { \
	return (enabled != Option()); \
}) | rpl::start_with_next([](bool enabled) { \
	Set##Option(enabled); \
	::Kotato::JsonSettings::Write(); \
}, container->lifetime());

void SetupKotatoChats(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container);
	AddSubsectionTitle(container, tr::ktg_settings_chats());

	const auto recentStickersLimitLabel = container->add(
		object_ptr<Ui::LabelSimple>(
			container,
			st::settingsAudioVolumeLabel),
		st::settingsAudioVolumeLabelPadding);
	const auto recentStickersLimitSlider = container->add(
		object_ptr<Ui::MediaSlider>(
			container,
			st::settingsAudioVolumeSlider),
		st::settingsAudioVolumeSliderPadding);
	const auto updateRecentStickersLimitLabel = [=](int value) {
		if (value == 0) {
			recentStickersLimitLabel->setText(
				tr::ktg_settings_recent_stickers_limit_none(tr::now));
		} else {
			recentStickersLimitLabel->setText(
				tr::ktg_settings_recent_stickers_limit(tr::now, lt_count_decimal, value));
		}
	};
	const auto updateRecentStickersLimitHeight = [=](int value) {
		updateRecentStickersLimitLabel(value);
		SetRecentStickersLimit(value);
		::Kotato::JsonSettings::Write();
	};
	recentStickersLimitSlider->resize(st::settingsAudioVolumeSlider.seekSize);
	recentStickersLimitSlider->setPseudoDiscrete(
		201,
		[](int val) { return val; },
		RecentStickersLimit(),
		updateRecentStickersLimitHeight);
	updateRecentStickersLimitLabel(RecentStickersLimit());

	SettingsMenuCSwitch(ktg_settings_top_bar_mute, ProfileTopBarNotifications);
	SettingsMenuCSwitch(ktg_settings_disable_up_edit, DisableUpEdit);

	AddButton(
		container,
		tr::ktg_settings_chat_list_compact(),
		st::settingsButton
	)->toggleOn(
		rpl::single(DialogListLines() == 1)
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != (DialogListLines() == 1));
	}) | rpl::start_with_next([](bool enabled) {
		SetDialogListLines(enabled ? 1 : 2);
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	SettingsMenuCSwitch(ktg_settings_disable_up_edit, AlwaysShowScheduled);

	AddButton(
		container,
		tr::ktg_settings_fonts(),
		st::settingsButton
	)->addClickHandler([=] {
		Ui::show(Box<FontsBox>());
	});

	const QMap<int, QString> userpicRoundOptions = {
		{ 0, UserpicRoundingLabel(0) },
		{ 1, UserpicRoundingLabel(1) },
		{ 2, UserpicRoundingLabel(2) },
		{ 3, UserpicRoundingLabel(3) }
	};

	AddButtonWithLabel(
		container,
		tr::ktg_settings_userpic_rounding(),
		rpl::single(UserpicRoundingLabel(cUserpicCornersType())),
		st::settingsButton
	)->addClickHandler([=] {
		Ui::show(Box<::Kotato::RadioBox>(
			tr::ktg_settings_userpic_rounding(tr::now),
			tr::ktg_settings_userpic_rounding_desc(tr::now),
			cUserpicCornersType(),
			userpicRoundOptions,
			[=] (int value) {
				cSetUserpicCornersType(value);
				::Kotato::JsonSettings::Write();
				App::restart();
			}, true));
	});

	AddSkip(container);
}

void SetupKotatoMessages(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::ktg_settings_messages());

	const auto stickerHeightLabel = container->add(
		object_ptr<Ui::LabelSimple>(
			container,
			st::settingsAudioVolumeLabel),
		st::settingsAudioVolumeLabelPadding);
	const auto stickerHeightSlider = container->add(
		object_ptr<Ui::MediaSlider>(
			container,
			st::settingsAudioVolumeSlider),
		st::settingsAudioVolumeSliderPadding);
	const auto updateStickerHeightLabel = [=](int value) {
		const auto pixels = QString::number(value);
		stickerHeightLabel->setText(
			tr::ktg_settings_sticker_height(tr::now, lt_pixels, pixels));
	};
	const auto updateStickerHeight = [=](int value) {
		updateStickerHeightLabel(value);
		SetStickerHeight(value);
		::Kotato::JsonSettings::Write();
	};
	stickerHeightSlider->resize(st::settingsAudioVolumeSlider.seekSize);
	stickerHeightSlider->setPseudoDiscrete(
		193,
		[](int val) { return val + 64; },
		StickerHeight(),
		updateStickerHeight);
	updateStickerHeightLabel(StickerHeight());

	SettingsMenuSwitch(ktg_settings_adaptive_bubbles, AdaptiveBubbles);
	SettingsMenuSwitch(ktg_settings_emoji_outline, BigEmojiOutline);

	AddSkip(container);
}

void SetupKotatoNetwork(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::ktg_settings_network());

	const QMap<int, QString> netBoostOptions = {
		{ 0, NetBoostLabel(0) },
		{ 1, NetBoostLabel(1) },
		{ 2, NetBoostLabel(2) },
		{ 3, NetBoostLabel(3) }
	};

	AddButtonWithLabel(
		container,
		tr::ktg_settings_net_speed_boost(),
		rpl::single(NetBoostLabel(cNetSpeedBoost())),
		st::settingsButton
	)->addClickHandler([=] {
		Ui::show(Box<::Kotato::RadioBox>(
			tr::ktg_net_speed_boost_title(tr::now),
			tr::ktg_net_speed_boost_desc(tr::now),
			cNetSpeedBoost(),
			netBoostOptions,
			[=] (int value) {
				SetNetworkBoost(value);
				::Kotato::JsonSettings::Write();
				App::restart();
			}, true));
	});

	AddSkip(container);
}

void SetupKotatoFolders(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::ktg_settings_filters());

	SettingsMenuCFilterSwitch(ktg_settings_filters_only_unmuted_counter, UnmutedFilterCounterOnly);
	SettingsMenuCFilterSwitch(ktg_settings_filters_hide_all, HideFilterAllChats);
	SettingsMenuCFilterSwitch(ktg_settings_filters_hide_edit, HideFilterEditButton);
	SettingsMenuCFilterSwitch(ktg_settings_filters_hide_folder_names, HideFilterNames);

	AddSkip(container);
}

void SetupKotatoSystem(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::ktg_settings_system());

	SettingsMenuCSwitch(ktg_settings_no_taskbar_flash, NoTaskbarFlashing);

	const QMap<int, QString> trayIconOptions = {
		{ 0, TrayIconLabel(0) },
		{ 1, TrayIconLabel(1) },
		{ 2, TrayIconLabel(2) },
		{ 3, TrayIconLabel(3) },
		{ 4, TrayIconLabel(4) },
		{ 5, TrayIconLabel(5) },
	};

	AddButtonWithLabel(
		container,
		tr::ktg_settings_tray_icon(),
		rpl::single(TrayIconLabel(cCustomAppIcon())),
		st::settingsButton
	)->addClickHandler([=] {
		Ui::show(Box<::Kotato::RadioBox>(
			tr::ktg_settings_tray_icon(tr::now),
			tr::ktg_settings_tray_icon_desc(tr::now),
			cCustomAppIcon(),
			trayIconOptions,
			[=] (int value) {
				cSetCustomAppIcon(value);
				::Kotato::JsonSettings::Write();
				App::restart();
			}, true));
	});

	AddSkip(container);
}

void SetupKotatoOther(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::ktg_settings_other());

	SettingsMenuCSwitch(ktg_settings_show_phone_number, ShowPhoneInDrawer);
	SettingsMenuCSwitch(ktg_settings_show_chat_id, ShowChatId);
	SettingsMenuCSwitch(ktg_settings_call_confirm, ConfirmBeforeCall);

	AddSkip(container);
}

Kotato::Kotato(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

void Kotato::setupContent(not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupKotatoChats(content);
	SetupKotatoMessages(content);
	SetupKotatoNetwork(content);
	SetupKotatoFolders(controller, content);
	SetupKotatoSystem(content);
	SetupKotatoOther(content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings

