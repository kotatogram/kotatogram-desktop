/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/kotato_settings_menu.h"

#include "kotato/kotato_lang.h"
#include "kotato/kotato_settings.h"
#include "base/options.h"
#include "base/platform/base_platform_info.h"
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
#include "kotato/boxes/kotato_fonts_box.h"
#include "kotato/boxes/kotato_radio_box.h"
#include "boxes/about_box.h"
#include "ui/boxes/confirm_box.h"
#include "platform/platform_specific.h"
#include "platform/platform_file_utilities.h"
#include "window/window_peer_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "core/update_checker.h"
#include "core/application.h"
#include "storage/localstorage.h"
#include "data/data_session.h"
#include "data/data_cloud_themes.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "styles/style_boxes.h"
#include "styles/style_calls.h"
#include "styles/style_settings.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/vertical_list.h"

namespace Settings {

namespace {


QString NetBoostLabel(int boost) {
	switch (boost) {
		case 0:
			return ktr("ktg_net_speed_boost_default");

		case 1:
			return ktr("ktg_net_speed_boost_slight");

		case 2:
			return ktr("ktg_net_speed_boost_medium");

		case 3:
			return ktr("ktg_net_speed_boost_big");

		default:
			Unexpected("Boost in Settings::NetBoostLabel.");
	}
	return QString();
}

QString TrayIconLabel(int icon) {
	switch (icon) {
		case 0:
			return ktr("ktg_settings_tray_icon_default");

		case 1:
			return ktr("ktg_settings_tray_icon_blue");

		case 2:
			return ktr("ktg_settings_tray_icon_green");

		case 3:
			return ktr("ktg_settings_tray_icon_orange");

		case 4:
			return ktr("ktg_settings_tray_icon_red");

		case 5:
			return ktr("ktg_settings_tray_icon_legacy");

		default:
			Unexpected("Icon in Settings::TrayIconLabel.");
	}
	return QString();
}

QString ChatIdLabel(int option) {
	switch (option) {
		case 0:
			return ktr("ktg_settings_chat_id_disable");

		case 1:
			return ktr("ktg_settings_chat_id_telegram");

		case 2:
			return ktr("ktg_settings_chat_id_bot");

		default:
			Unexpected("Option in Settings::ChatIdLabel.");
	}
	return QString();
}

} // namespace

#define SettingsMenuJsonSwitch(LangKey, Option) container->add(object_ptr<Button>( \
	container, \
	rktr(#LangKey), \
	st::settingsButtonNoIcon \
))->toggleOn( \
	rpl::single(::Kotato::JsonSettings::GetBool(#Option)) \
)->toggledValue( \
) | rpl::filter([](bool enabled) { \
	return (enabled != ::Kotato::JsonSettings::GetBool(#Option)); \
}) | rpl::start_with_next([](bool enabled) { \
	::Kotato::JsonSettings::Set(#Option, enabled); \
	::Kotato::JsonSettings::Write(); \
}, container->lifetime());

#define SettingsMenuJsonFilterSwitch(LangKey, Option) container->add(object_ptr<Button>( \
	container, \
	rktr(#LangKey), \
	st::settingsButtonNoIcon \
))->toggleOn( \
	rpl::single(::Kotato::JsonSettings::GetBool(#Option)) \
)->toggledValue( \
) | rpl::filter([](bool enabled) { \
	return (enabled != ::Kotato::JsonSettings::GetBool(#Option)); \
}) | rpl::start_with_next([controller](bool enabled) { \
	::Kotato::JsonSettings::Set(#Option, enabled); \
	::Kotato::JsonSettings::Write(); \
	controller->reloadFiltersMenu(); \
}, container->lifetime());

void SetupKotatoChats(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container) {
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rktr("ktg_settings_chats"));

	const auto recentStickersLimitLabel = container->add(
		object_ptr<Ui::LabelSimple>(
			container,
			st::ktgSettingsSliderLabel),
		st::groupCallDelayLabelMargin);
	const auto recentStickersLimitSlider = container->add(
		object_ptr<Ui::MediaSlider>(
			container,
			st::defaultContinuousSlider),
		st::localStorageLimitMargin);
	const auto updateRecentStickersLimitLabel = [=](int value) {
		if (value == 0) {
			recentStickersLimitLabel->setText(
				ktr("ktg_settings_recent_stickers_limit_none"));
		} else {
			recentStickersLimitLabel->setText(
				ktr("ktg_settings_recent_stickers_limit", value, { "count", QString::number(value) }));
		}
	};
	const auto updateRecentStickersLimitHeight = [=](int value) {
		updateRecentStickersLimitLabel(value);
		::Kotato::JsonSettings::Set("recent_stickers_limit", value);
		::Kotato::JsonSettings::Write();
	};
	recentStickersLimitSlider->resize(st::defaultContinuousSlider.seekSize);
	recentStickersLimitSlider->setPseudoDiscrete(
		201,
		[](int val) { return val; },
		::Kotato::JsonSettings::GetInt("recent_stickers_limit"),
		updateRecentStickersLimitHeight);
	updateRecentStickersLimitLabel(::Kotato::JsonSettings::GetInt("recent_stickers_limit"));

	SettingsMenuJsonSwitch(ktg_settings_top_bar_mute, profile_top_mute);
	SettingsMenuJsonSwitch(ktg_settings_disable_up_edit, disable_up_edit);
	SettingsMenuJsonSwitch(ktg_settings_always_show_scheduled, always_show_scheduled);

	container->add(object_ptr<Button>(
		container,
		rktr("ktg_settings_fonts"),
		st::settingsButtonNoIcon
	))->addClickHandler([=] {
		Ui::show(Box<FontsBox>());
	});


	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);
}

void SetupKotatoMessages(not_null<Ui::VerticalLayout*> container) {
	Ui::AddSubsectionTitle(container, rktr("ktg_settings_messages"));

	const auto stickerHeightLabel = container->add(
		object_ptr<Ui::LabelSimple>(
			container,
			st::ktgSettingsSliderLabel),
		st::groupCallDelayLabelMargin);
	const auto stickerHeightSlider = container->add(
		object_ptr<Ui::MediaSlider>(
			container,
			st::defaultContinuousSlider),
		st::localStorageLimitMargin);
	const auto updateStickerHeightLabel = [=](int value) {
		const auto pixels = QString::number(value);
		stickerHeightLabel->setText(
			ktr("ktg_settings_sticker_height", { "pixels", pixels }));
	};
	const auto updateStickerHeight = [=](int value) {
		updateStickerHeightLabel(value);
		::Kotato::JsonSettings::Set("sticker_height", value);
		::Kotato::JsonSettings::Write();
	};
	stickerHeightSlider->resize(st::defaultContinuousSlider.seekSize);
	stickerHeightSlider->setPseudoDiscrete(
		193,
		[](int val) { return val + 64; },
		::Kotato::JsonSettings::GetInt("sticker_height"),
		updateStickerHeight);
	updateStickerHeightLabel(::Kotato::JsonSettings::GetInt("sticker_height"));

	container->add(
		object_ptr<Ui::Checkbox>(
			container,
			ktr("ktg_settings_sticker_scale_both"),
			::Kotato::JsonSettings::GetBool("sticker_scale_both"),
			st::settingsCheckbox),
		st::settingsCheckboxPadding
	)->checkedChanges(
	) | rpl::filter([](bool checked) {
		return (checked != ::Kotato::JsonSettings::GetBool("sticker_scale_both"));
	}) | rpl::start_with_next([](bool checked) {
		::Kotato::JsonSettings::Set("sticker_scale_both", checked);
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	Ui::AddSkip(container);
	Ui::AddDividerText(container, rktr("ktg_settings_sticker_scale_both_about"));
	Ui::AddSkip(container);

	auto adaptiveBubblesButton = container->add(object_ptr<Button>(
		container,
		rktr("ktg_settings_adaptive_bubbles"),
		st::settingsButtonNoIcon
	));

	auto monospaceLargeBubblesButton = container->add(
		object_ptr<Ui::SlideWrap<Button>>(
			container,
			object_ptr<Ui::SettingsButton>(
				container,
				rktr("ktg_settings_monospace_large_bubbles"),
				st::settingsButtonNoIcon)));

	adaptiveBubblesButton->toggleOn(
		rpl::single(::Kotato::JsonSettings::GetBool("adaptive_bubbles"))
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != ::Kotato::JsonSettings::GetBool("adaptive_bubbles"));
	}) | rpl::start_with_next([monospaceLargeBubblesButton](bool enabled) {
		monospaceLargeBubblesButton->toggle(!enabled, anim::type::normal);
		::Kotato::JsonSettings::Set("adaptive_bubbles", enabled);
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	monospaceLargeBubblesButton->entity()->toggleOn(
		rpl::single(::Kotato::JsonSettings::GetBool("monospace_large_bubbles"))
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != ::Kotato::JsonSettings::GetBool("monospace_large_bubbles"));
	}) | rpl::start_with_next([](bool enabled) {
		::Kotato::JsonSettings::Set("monospace_large_bubbles", enabled);
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	if (adaptiveBubblesButton->toggled()) {
		monospaceLargeBubblesButton->hide(anim::type::instant);
	}

	SettingsMenuJsonSwitch(ktg_settings_emoji_outline, big_emoji_outline);

	Ui::AddSkip(container);
}

void SetupKotatoForward(not_null<Ui::VerticalLayout*> container) {
	Ui::AddDivider(container);
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rktr("ktg_settings_forward"));


	Ui::AddSkip(container);
	Ui::AddDividerText(container, rktr("ktg_settings_forward_chat_on_click_description"));
}

void SetupKotatoNetwork(not_null<Ui::VerticalLayout*> container) {
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rktr("ktg_settings_network"));

	const auto netBoostButton = container->add(
		object_ptr<Button>(
			container,
			rktr("ktg_settings_net_speed_boost"),
			st::settingsButtonNoIcon));
	auto netBoostText = rpl::single(
		NetBoostLabel(::Kotato::JsonSettings::GetIntWithPending("net_speed_boost"))
	) | rpl::then(
		::Kotato::JsonSettings::EventsWithPending(
			"net_speed_boost"
		) | rpl::map([] {
			return NetBoostLabel(::Kotato::JsonSettings::GetIntWithPending("net_speed_boost"));
		})
	);
	CreateRightLabel(
		netBoostButton,
		std::move(netBoostText),
		st::settingsButtonNoIcon,
		rktr("ktg_settings_net_speed_boost"));
	netBoostButton->addClickHandler([=] {
		Ui::show(Box<::Kotato::RadioBox>(
			ktr("ktg_net_speed_boost_title"),
			ktr("ktg_net_speed_boost_desc"),
			::Kotato::JsonSettings::GetIntWithPending("net_speed_boost"),
			4,
			NetBoostLabel,
			[=] (int value) {
				::Kotato::JsonSettings::SetAfterRestart("net_speed_boost", value);
				::Kotato::JsonSettings::Write();
			}, true));
	});

	SettingsMenuJsonSwitch(ktg_settings_telegram_sites_autologin, telegram_sites_autologin);

	Ui::AddSkip(container);
}

void SetupKotatoFolders(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container) {
	Ui::AddDivider(container);
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rktr("ktg_settings_filters"));

	SettingsMenuJsonFilterSwitch(ktg_settings_filters_only_unmuted_counter, folders/count_unmuted_only);
	SettingsMenuJsonFilterSwitch(ktg_settings_filters_hide_all, folders/hide_all_chats);
	SettingsMenuJsonFilterSwitch(ktg_settings_filters_hide_edit, folders/hide_edit_button);
	SettingsMenuJsonFilterSwitch(ktg_settings_filters_hide_folder_names, folders/hide_names);

	Ui::AddSkip(container);
}

void SetupKotatoSystem(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container) {
	Ui::AddDivider(container);
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rktr("ktg_settings_system"));

	container->add(object_ptr<Button>(
		container,
		rktr("ktg_settings_disable_tray_counter"),
		st::settingsButtonNoIcon
	))->toggleOn(
		rpl::single(::Kotato::JsonSettings::GetBool("disable_tray_counter"))
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != ::Kotato::JsonSettings::GetBool("disable_tray_counter"));
	}) | rpl::start_with_next([controller](bool enabled) {
		::Kotato::JsonSettings::Set("disable_tray_counter", enabled);
		controller->session().data().notifyUnreadBadgeChanged();
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	if (Platform::IsLinux()) {
		container->add(object_ptr<Button>(
			container,
			rktr("ktg_settings_use_telegram_panel_icon"),
			st::settingsButtonNoIcon
		))->toggleOn(
			rpl::single(::Kotato::JsonSettings::GetBool("use_telegram_panel_icon"))
		)->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled != ::Kotato::JsonSettings::GetBool("use_telegram_panel_icon"));
		}) | rpl::start_with_next([controller](bool enabled) {
			::Kotato::JsonSettings::Set("use_telegram_panel_icon", enabled);
			controller->session().data().notifyUnreadBadgeChanged();
			::Kotato::JsonSettings::Write();
		}, container->lifetime());
	}

	auto trayIconText = rpl::single(rpl::empty) | rpl::then(
		controller->session().data().unreadBadgeChanges()
	) | rpl::map([] {
		return TrayIconLabel(::Kotato::JsonSettings::GetInt("custom_app_icon"));
	});

	AddButtonWithLabel(
		container,
		rktr("ktg_settings_tray_icon"),
		trayIconText,
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		Ui::show(Box<::Kotato::RadioBox>(
			ktr("ktg_settings_tray_icon"),
			ktr("ktg_settings_tray_icon_desc"),
			::Kotato::JsonSettings::GetInt("custom_app_icon"),
			6,
			TrayIconLabel,
			[=] (int value) {
				::Kotato::JsonSettings::Set("custom_app_icon", value);
				controller->session().data().notifyUnreadBadgeChanged();
				::Kotato::JsonSettings::Write();
			}, false));
	});

	Ui::AddSkip(container);
}

void SetupKotatoOther(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container) {
	Ui::AddDivider(container);
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rktr("ktg_settings_other"));


	const auto chatIdButton = container->add(
		object_ptr<Button>(
			container,
			rktr("ktg_settings_chat_id"),
			st::settingsButtonNoIcon));
	auto chatIdText = rpl::single(
		ChatIdLabel(::Kotato::JsonSettings::GetInt("show_chat_id"))
	) | rpl::then(
		::Kotato::JsonSettings::Events(
			"show_chat_id"
		) | rpl::map([] {
			return ChatIdLabel(::Kotato::JsonSettings::GetInt("show_chat_id"));
		})
	);
	CreateRightLabel(
		chatIdButton,
		std::move(chatIdText),
		st::settingsButtonNoIcon,
		rktr("ktg_settings_chat_id"));
	chatIdButton->addClickHandler([=] {
		Ui::show(Box<::Kotato::RadioBox>(
			ktr("ktg_settings_chat_id"),
			ktr("ktg_settings_chat_id_desc"),
			::Kotato::JsonSettings::GetInt("show_chat_id"),
			3,
			ChatIdLabel,
			[=] (int value) {
				::Kotato::JsonSettings::Set("show_chat_id", value);
				::Kotato::JsonSettings::Write();
			}));
	});

	SettingsMenuJsonSwitch(ktg_settings_ffmpeg_multithread, ffmpeg_multithread);

	Ui::AddSkip(container);
	Ui::AddDividerText(container, rktr("ktg_settings_ffmpeg_multithread_about"));
	Ui::AddSkip(container);
}

Kotato::Kotato(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

rpl::producer<QString> Kotato::title() {
	return rktr("ktg_settings_kotato");
}

void Kotato::setupContent(not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupKotatoChats(controller, content);
	SetupKotatoMessages(content);
	SetupKotatoForward(content);
	SetupKotatoNetwork(content);
	SetupKotatoFolders(controller, content);
	SetupKotatoSystem(controller, content);
	SetupKotatoOther(controller, content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings

