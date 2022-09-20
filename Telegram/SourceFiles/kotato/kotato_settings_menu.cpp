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
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "core/update_checker.h"
#include "core/application.h"
#include "storage/localstorage.h"
#include "data/data_session.h"
#include "data/data_cloud_themes.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "facades.h"
#include "styles/style_settings.h"
#include "ui/platform/ui_platform_utility.h"

namespace Settings {

namespace {

QString FileDialogTypeLabel(int value) {
	const auto typedValue = Platform::FileDialog::ImplementationType(value);
	switch (typedValue) {
	case Platform::FileDialog::ImplementationType::Default:
		return ktr("ktg_file_dialog_type_default");
	}
	return Platform::FileDialog::ImplementationTypeLabel(typedValue);
}

QString FileDialogTypeDescription(int value) {
	const auto typedValue = Platform::FileDialog::ImplementationType(value);
	return Platform::FileDialog::ImplementationTypeDescription(typedValue);
}

QString ForwardModeLabel(int mode) {
	switch (mode) {
		case 0:
			return ktr("ktg_forward_mode_quoted");

		case 1:
			return ktr("ktg_forward_mode_unquoted");

		case 2:
			return ktr("ktg_forward_mode_uncaptioned");

		default:
			Unexpected("Boost in Settings::ForwardModeLabel.");
	}
	return QString();
}

QString GroupingModeLabel(int mode) {
	switch (mode) {
		case 0:
			return ktr("ktg_forward_grouping_mode_preserve_albums");

		case 1:
			return ktr("ktg_forward_grouping_mode_regroup");

		case 2:
			return ktr("ktg_forward_grouping_mode_separate");

		default:
			Unexpected("Boost in Settings::GroupingModeLabel.");
	}
	return QString();
}

QString GroupingModeDescription(int mode) {
	switch (mode) {
		case 0:
		case 2:
			return QString();

		case 1:
			return ktr("ktg_forward_grouping_mode_regroup_desc");

		default:
			Unexpected("Boost in Settings::GroupingModeLabel.");
	}
	return QString();
}

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

QString UserpicRoundingLabel(int rounding) {
	switch (rounding) {
		case 0:
			return ktr("ktg_settings_userpic_rounding_none");

		case 1:
			return ktr("ktg_settings_userpic_rounding_small");

		case 2:
			return ktr("ktg_settings_userpic_rounding_big");

		case 3:
			return ktr("ktg_settings_userpic_rounding_full");

		default:
			Unexpected("Rounding in Settings::UserpicRoundingLabel.");
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

#define SettingsMenuJsonSwitch(LangKey, Option) AddButton( \
	container, \
	rktr(#LangKey), \
	st::settingsButton \
)->toggleOn( \
	rpl::single(::Kotato::JsonSettings::GetBool(#Option)) \
)->toggledValue( \
) | rpl::filter([](bool enabled) { \
	return (enabled != ::Kotato::JsonSettings::GetBool(#Option)); \
}) | rpl::start_with_next([](bool enabled) { \
	::Kotato::JsonSettings::Set(#Option, enabled); \
	::Kotato::JsonSettings::Write(); \
}, container->lifetime());

#define SettingsMenuJsonFilterSwitch(LangKey, Option) AddButton( \
	container, \
	rktr(#LangKey), \
	st::settingsButton \
)->toggleOn( \
	rpl::single(::Kotato::JsonSettings::GetBool(#Option)) \
)->toggledValue( \
) | rpl::filter([](bool enabled) { \
	return (enabled != ::Kotato::JsonSettings::GetBool(#Option)); \
}) | rpl::start_with_next([controller](bool enabled) { \
	::Kotato::JsonSettings::Set(#Option, enabled); \
	::Kotato::JsonSettings::Write(); \
	controller->reloadFiltersMenu(); \
	App::wnd()->fixOrder(); \
}, container->lifetime());

void SetupKotatoChats(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container) {
	AddSkip(container);
	AddSubsectionTitle(container, rktr("ktg_settings_chats"));

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
	recentStickersLimitSlider->resize(st::settingsAudioVolumeSlider.seekSize);
	recentStickersLimitSlider->setPseudoDiscrete(
		201,
		[](int val) { return val; },
		::Kotato::JsonSettings::GetInt("recent_stickers_limit"),
		updateRecentStickersLimitHeight);
	updateRecentStickersLimitLabel(::Kotato::JsonSettings::GetInt("recent_stickers_limit"));

	SettingsMenuJsonSwitch(ktg_settings_top_bar_mute, profile_top_mute);
	SettingsMenuJsonSwitch(ktg_settings_disable_up_edit, disable_up_edit);

	if (Ui::Platform::IsOverlapped(container, QRect()).has_value()) {
		SettingsMenuJsonSwitch(ktg_settings_auto_scroll_unfocused, auto_scroll_unfocused);
	}

	AddButton(
		container,
		rktr("ktg_settings_chat_list_compact"),
		st::settingsButton
	)->toggleOn(
		rpl::single(::Kotato::JsonSettings::GetInt("chat_list_lines") == 1)
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != (::Kotato::JsonSettings::GetInt("chat_list_lines") == 1));
	}) | rpl::start_with_next([](bool enabled) {
		::Kotato::JsonSettings::Set("chat_list_lines", enabled ? 1 : 2);
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	AddButton(
		container,
		rktr("ktg_settings_always_show_scheduled"),
		st::settingsButton
	)->toggleOn(
		rpl::single(::Kotato::JsonSettings::GetBool("always_show_scheduled"))
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != ::Kotato::JsonSettings::GetBool("always_show_scheduled"));
	}) | rpl::start_with_next([controller](bool enabled) {
		::Kotato::JsonSettings::Set("always_show_scheduled", enabled);
		Notify::showScheduledButtonChanged(&controller->session());
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	AddButton(
		container,
		rktr("ktg_settings_fonts"),
		st::settingsButton
	)->addClickHandler([=] {
		Ui::show(Box<FontsBox>());
	});

	const auto userpicCornerButton = container->add(
		object_ptr<Button>(
			container,
			rktr("ktg_settings_userpic_rounding"),
			st::settingsButton));
	auto userpicCornerText = rpl::single(
		UserpicRoundingLabel(::Kotato::JsonSettings::GetIntWithPending("userpic_corner_type"))
	) | rpl::then(
		::Kotato::JsonSettings::EventsWithPending(
			"userpic_corner_type"
		) | rpl::map([] {
			return UserpicRoundingLabel(::Kotato::JsonSettings::GetIntWithPending("userpic_corner_type"));
		})
	);
	CreateRightLabel(
		userpicCornerButton,
		std::move(userpicCornerText),
		st::settingsButton,
		rktr("ktg_settings_userpic_rounding"));
	userpicCornerButton->addClickHandler([=] {
		Ui::show(Box<::Kotato::RadioBox>(
			ktr("ktg_settings_userpic_rounding"),
			ktr("ktg_settings_userpic_rounding_desc"),
			::Kotato::JsonSettings::GetIntWithPending("userpic_corner_type"),
			4,
			UserpicRoundingLabel,
			[=] (int value) {
				::Kotato::JsonSettings::SetAfterRestart("userpic_corner_type", value);
				::Kotato::JsonSettings::Write();
			}, true));
	});

	AddButton(
		container,
		rktr("ktg_disable_chat_themes"),
		st::settingsButton
	)->toggleOn(
		rpl::single(::Kotato::JsonSettings::GetBool("disable_chat_themes"))
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != ::Kotato::JsonSettings::GetBool("disable_chat_themes"));
	}) | rpl::start_with_next([controller](bool enabled) {
		::Kotato::JsonSettings::Set("disable_chat_themes", enabled);
		controller->session().data().cloudThemes().refreshChatThemes();
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	AddButton(
		container,
		rktr("ktg_settings_view_profile_on_top"),
		st::settingsButton
	)->toggleOn(
		rpl::single(::Kotato::JsonSettings::GetBool("view_profile_on_top"))
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != ::Kotato::JsonSettings::GetBool("view_profile_on_top"));
	}) | rpl::start_with_next([](bool enabled) {
		::Kotato::JsonSettings::Set("view_profile_on_top", enabled);
		if (enabled) {
			auto &option = ::base::options::lookup<bool>(Window::kOptionViewProfileInChatsListContextMenu);
			option.set(true);
		}
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	AddSkip(container);
	AddDividerText(container, rktr("ktg_settings_view_profile_on_top_about"));
	AddSkip(container);

	SettingsMenuJsonSwitch(ktg_settings_emoji_sidebar, emoji_sidebar);
	SettingsMenuJsonSwitch(ktg_settings_emoji_sidebar_right_click, emoji_sidebar_right_click);

	AddSkip(container);
	AddDivider(container);
	AddSkip(container);
}

void SetupKotatoMessages(not_null<Ui::VerticalLayout*> container) {
	AddSubsectionTitle(container, rktr("ktg_settings_messages"));

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
			ktr("ktg_settings_sticker_height", { "pixels", pixels }));
	};
	const auto updateStickerHeight = [=](int value) {
		updateStickerHeightLabel(value);
		::Kotato::JsonSettings::Set("sticker_height", value);
		::Kotato::JsonSettings::Write();
	};
	stickerHeightSlider->resize(st::settingsAudioVolumeSlider.seekSize);
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

	AddSkip(container);
	AddDividerText(container, rktr("ktg_settings_sticker_scale_both_about"));
	AddSkip(container);

	auto adaptiveBubblesButton = AddButton(
		container,
		rktr("ktg_settings_adaptive_bubbles"),
		st::settingsButton
	);

	auto monospaceLargeBubblesButton = container->add(
		object_ptr<Ui::SlideWrap<Button>>(
			container,
			CreateButton(
				container,
				rktr("ktg_settings_monospace_large_bubbles"),
				st::settingsButton)));

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

	AddSkip(container);
}

void SetupKotatoForward(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, rktr("ktg_settings_forward"));

	SettingsMenuJsonSwitch(ktg_forward_remember_mode, forward_remember_mode);

	auto forwardModeText = rpl::single(
		ForwardModeLabel(::Kotato::JsonSettings::GetInt("forward_mode"))
	) | rpl::then(
		::Kotato::JsonSettings::Events(
			"forward_mode"
		) | rpl::map([] {
			return ForwardModeLabel(::Kotato::JsonSettings::GetInt("forward_mode"));
		})
	);

	AddButtonWithLabel(
		container,
		rktr("ktg_forward_mode"),
		forwardModeText,
		st::settingsButton
	)->addClickHandler([=] {
		Ui::show(Box<::Kotato::RadioBox>(
			ktr("ktg_forward_mode"),
			::Kotato::JsonSettings::GetInt("forward_mode"),
			3,
			ForwardModeLabel,
			[=] (int value) {
				::Kotato::JsonSettings::Set("forward_mode", value);
				::Kotato::JsonSettings::Write();
			}, false));
	});

	auto forwardGroupingModeText = rpl::single(
		GroupingModeLabel(::Kotato::JsonSettings::GetInt("forward_grouping_mode"))
	) | rpl::then(
		::Kotato::JsonSettings::Events(
			"forward_grouping_mode"
		) | rpl::map([] {
			return GroupingModeLabel(::Kotato::JsonSettings::GetInt("forward_grouping_mode"));
		})
	);

	AddButtonWithLabel(
		container,
		rktr("ktg_forward_grouping_mode"),
		forwardGroupingModeText,
		st::settingsButton
	)->addClickHandler([=] {
		Ui::show(Box<::Kotato::RadioBox>(
			ktr("ktg_forward_grouping_mode"),
			::Kotato::JsonSettings::GetInt("forward_grouping_mode"),
			3,
			GroupingModeLabel,
			GroupingModeDescription,
			[=] (int value) {
				::Kotato::JsonSettings::Set("forward_grouping_mode", value);
				::Kotato::JsonSettings::Write();
			}, false));
	});

	SettingsMenuJsonSwitch(ktg_forward_force_old_unquoted, forward_force_old_unquoted);

	AddSkip(container);
	AddDividerText(container, rktr("ktg_forward_force_old_unquoted_desc"));
	AddSkip(container);

	SettingsMenuJsonSwitch(ktg_settings_forward_retain_selection, forward_retain_selection);
	SettingsMenuJsonSwitch(ktg_settings_forward_chat_on_click, forward_on_click);

	AddSkip(container);
	AddDividerText(container, rktr("ktg_settings_forward_chat_on_click_description"));
}

void SetupKotatoNetwork(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container);
	AddSubsectionTitle(container, rktr("ktg_settings_network"));

	const auto netBoostButton = container->add(
		object_ptr<Button>(
			container,
			rktr("ktg_settings_net_speed_boost"),
			st::settingsButton));
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
		st::settingsButton,
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

	AddSkip(container);
}

void SetupKotatoFolders(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, rktr("ktg_settings_filters"));

	SettingsMenuJsonFilterSwitch(ktg_settings_filters_only_unmuted_counter, folders/count_unmuted_only);
	SettingsMenuJsonFilterSwitch(ktg_settings_filters_hide_all, folders/hide_all_chats);
	SettingsMenuJsonFilterSwitch(ktg_settings_filters_hide_edit, folders/hide_edit_button);
	SettingsMenuJsonFilterSwitch(ktg_settings_filters_hide_folder_names, folders/hide_names);

	AddSkip(container);
}

void SetupKotatoSystem(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, rktr("ktg_settings_system"));

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	AddButton(
		container,
		rktr("ktg_settings_qt_scale"),
		st::settingsButton
	)->toggleOn(
		rpl::single(::Kotato::JsonSettings::GetBoolWithPending("qt_scale"))
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != ::Kotato::JsonSettings::GetBoolWithPending("qt_scale"));
	}) | rpl::start_with_next([=](bool enabled) {
		::Kotato::JsonSettings::SetAfterRestart("qt_scale", enabled);
		::Kotato::JsonSettings::Write();

		Ui::show(Box<Ui::ConfirmBox>(
			tr::lng_settings_need_restart(tr::now),
			tr::lng_settings_restart_now(tr::now),
			tr::lng_settings_restart_later(tr::now),
			[] { Core::Restart(); }));
	}, container->lifetime());
#endif // Qt < 6.0.0

	if (Platform::IsLinux()) {
		auto fileDialogTypeText = rpl::single(
			FileDialogTypeLabel(::Kotato::JsonSettings::GetInt("file_dialog_type"))
		) | rpl::then(
			::Kotato::JsonSettings::Events(
				"file_dialog_type"
			) | rpl::map([] {
				return FileDialogTypeLabel(::Kotato::JsonSettings::GetInt("file_dialog_type"));
			})
		);

		AddButtonWithLabel(
			container,
			rktr("ktg_settings_file_dialog_type"),
			fileDialogTypeText,
			st::settingsButton
		)->addClickHandler([=] {
			Ui::show(Box<::Kotato::RadioBox>(
				ktr("ktg_settings_file_dialog_type"),
				::Kotato::JsonSettings::GetInt("file_dialog_type"),
				int(Platform::FileDialog::ImplementationType::Count),
				FileDialogTypeLabel,
				FileDialogTypeDescription,
				[=](int value) {
					::Kotato::JsonSettings::Set("file_dialog_type", value);
					::Kotato::JsonSettings::Write();
				}, false));
		});
	}

	AddButton(
		container,
		rktr("ktg_settings_disable_tray_counter"),
		st::settingsButton
	)->toggleOn(
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
		AddButton(
			container,
			rktr("ktg_settings_use_telegram_panel_icon"),
			st::settingsButton
		)->toggleOn(
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

	auto trayIconText = rpl::single(
		rpl::empty_value()
	) | rpl::then(
		controller->session().data().unreadBadgeChanges()
	) | rpl::map([] {
		return TrayIconLabel(::Kotato::JsonSettings::GetInt("custom_app_icon"));
	});

	AddButtonWithLabel(
		container,
		rktr("ktg_settings_tray_icon"),
		trayIconText,
		st::settingsButton
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

	AddSkip(container);
}

void SetupKotatoOther(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, rktr("ktg_settings_other"));

	SettingsMenuJsonSwitch(ktg_settings_show_phone_number, show_phone_in_drawer);

	const auto chatIdButton = container->add(
		object_ptr<Button>(
			container,
			rktr("ktg_settings_chat_id"),
			st::settingsButton));
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
		st::settingsButton,
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

	SettingsMenuJsonSwitch(ktg_settings_call_confirm, confirm_before_calls);
	SettingsMenuJsonSwitch(ktg_settings_disable_short_info_box, disable_short_info_box);
	SettingsMenuJsonSwitch(ktg_settings_remember_compress_images, remember_compress_images);
	AddButton(
		container,
		rktr("ktg_settings_compress_images_default"),
		st::settingsButton
	)->toggleOn(
		rpl::single(Core::App().settings().sendFilesWay().sendImagesAsPhotos())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != Core::App().settings().sendFilesWay().sendImagesAsPhotos());
	}) | rpl::start_with_next([](bool enabled) {
		auto way = Core::App().settings().sendFilesWay();
		way.setSendImagesAsPhotos(enabled);
		Core::App().settings().setSendFilesWay(way);
		Core::App().saveSettingsDelayed();
	}, container->lifetime());
	SettingsMenuJsonSwitch(ktg_settings_ffmpeg_multithread, ffmpeg_multithread);

	AddSkip(container);
	AddDividerText(container, rktr("ktg_settings_ffmpeg_multithread_about"));
	AddSkip(container);

	AddButton(
		container,
		rktr("ktg_settings_external_video_player"),
		st::settingsButton
	)->toggleOn(
		rpl::single(cUseExternalVideoPlayer())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cUseExternalVideoPlayer());
	}) | rpl::start_with_next([=](bool enabled) {
		cSetUseExternalVideoPlayer(enabled);
		controller->session().saveSettingsDelayed();
	}, container->lifetime());

	AddSkip(container);
	AddDividerText(container, rktr("ktg_settings_external_video_player_about"));
}

Kotato::Kotato(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
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

