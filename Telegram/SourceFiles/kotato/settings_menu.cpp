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
#include "kotato/boxes/net_boost_box.h"
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

#define SettingsMenuСSwitch(LangKey, Option) AddButton( \
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

#define SettingsMenuСFilterSwitch(LangKey, Option) AddButton( \
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

	SettingsMenuСSwitch(ktg_settings_top_bar_mute, ProfileTopBarNotifications);
	SettingsMenuСSwitch(ktg_settings_disable_up_edit, DisableUpEdit);

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

	SettingsMenuСSwitch(ktg_settings_disable_up_edit, AlwaysShowScheduled);

	AddButton(
		container,
		tr::ktg_settings_fonts(),
		st::settingsButton
	)->addClickHandler([=] {
		Ui::show(Box<FontsBox>());
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

	AddButtonWithLabel(
		container,
		tr::ktg_settings_net_speed_boost(),
		rpl::single(NetBoostBox::BoostLabel(cNetSpeedBoost())),
		st::settingsButton
	)->addClickHandler([=] {
		Ui::show(Box<NetBoostBox>());
	});

	AddSkip(container);
}

void SetupKotatoFolders(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::ktg_settings_filters());

	SettingsMenuСFilterSwitch(ktg_settings_filters_only_unmuted_counter, UnmutedFilterCounterOnly);
	SettingsMenuСFilterSwitch(ktg_settings_filters_hide_all, HideFilterAllChats);
	SettingsMenuСFilterSwitch(ktg_settings_filters_hide_edit, HideFilterEditButton);
	SettingsMenuСFilterSwitch(ktg_settings_filters_hide_folder_names, HideFilterNames);

	AddSkip(container);
}

void SetupKotatoSystem(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::ktg_settings_system());

	SettingsMenuСSwitch(ktg_settings_no_taskbar_flash, NoTaskbarFlashing);

	AddSkip(container);
}

void SetupKotatoOther(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::ktg_settings_other());

	SettingsMenuСSwitch(ktg_settings_show_phone_number, ShowPhoneInDrawer);
	SettingsMenuСSwitch(ktg_settings_show_chat_id, ShowChatId);
	SettingsMenuСSwitch(ktg_settings_call_confirm, ConfirmBeforeCall);

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

