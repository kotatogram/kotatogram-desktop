/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/kotato_settings_menu.h"

#include "kotato/kotato_lang.h"
#include "kotato/kotato_settings.h"
#include "kotato/kotato_radius.h"
#include "base/options.h"
#include "base/platform/base_platform_info.h"
#include "settings/settings_common.h"
#include "settings/sections/settings_chat.h"
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
#include "core/file_utilities.h"
#include "storage/localstorage.h"
#include "data/components/sponsored_messages.h"
#include "data/data_session.h"
#include "data/data_cloud_themes.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "styles/style_boxes.h"
#include "styles/style_calls.h"
#include "styles/style_settings.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/vertical_list.h"
#include "styles/style_menu_icons.h"

namespace Settings {

namespace {

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
}) | rpl::on_next([](bool enabled) { \
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
}) | rpl::on_next([controller](bool enabled) { \
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

	const auto userpicRoundingLabel = container->add(
		object_ptr<Ui::LabelSimple>(
			container,
			st::ktgSettingsSliderLabel),
		st::groupCallDelayLabelMargin);
	const auto userpicRoundingSlider = container->add(
		object_ptr<Ui::MediaSlider>(
			container,
			st::defaultContinuousSlider),
		st::localStorageLimitMargin);
	const auto updateUserpicRoundingLabel = [=](int value) {
		userpicRoundingLabel->setText(
			ktr("ktg_settings_userpic_rounding", { "radius", QString::number(value) }));
	};
	const auto updateUserpicRounding = [=](int value) {
		updateUserpicRoundingLabel(value);
		::Kotato::JsonSettings::Set("userpic_corner_radius", value);
		::Kotato::JsonSettings::Write();
		::Kotato::RefreshRadius();
	};
	userpicRoundingSlider->resize(st::defaultContinuousSlider.seekSize);
	userpicRoundingSlider->setPseudoDiscrete(
		51,
		[](int val) { return val; },
		::Kotato::JsonSettings::GetInt("userpic_corner_radius"),
		updateUserpicRounding);
	updateUserpicRoundingLabel(::Kotato::JsonSettings::GetInt("userpic_corner_radius"));

	const auto userpicForumDefault = container->add(object_ptr<Button>(
		container,
		rktr("ktg_settings_userpic_rounding_forum_use_default"),
		st::settingsButtonNoIcon));

	const auto userpicForumWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto userpicForumInner = userpicForumWrap->entity();
	const auto userpicForumLabel = userpicForumInner->add(
		object_ptr<Ui::LabelSimple>(
			userpicForumInner,
			st::ktgSettingsSliderLabel),
		st::groupCallDelayLabelMargin);
	const auto userpicForumSlider = userpicForumInner->add(
		object_ptr<Ui::MediaSlider>(
			userpicForumInner,
			st::defaultContinuousSlider),
		st::localStorageLimitMargin);
	const auto updateUserpicForumLabel = [=](int value) {
		userpicForumLabel->setText(
			ktr("ktg_settings_userpic_rounding_forum", { "radius", QString::number(value) }));
	};
	const auto updateUserpicForum = [=](int value) {
		updateUserpicForumLabel(value);
		::Kotato::JsonSettings::Set("userpic_corner_radius_forum", value);
		::Kotato::JsonSettings::Write();
		::Kotato::RefreshRadius();
	};
	userpicForumSlider->resize(st::defaultContinuousSlider.seekSize);
	userpicForumSlider->setPseudoDiscrete(
		51,
		[](int val) { return val; },
		::Kotato::JsonSettings::GetInt("userpic_corner_radius_forum"),
		updateUserpicForum);
	updateUserpicForumLabel(::Kotato::JsonSettings::GetInt("userpic_corner_radius_forum"));

	userpicForumWrap->toggle(
		!::Kotato::JsonSettings::GetBool("userpic_corner_radius_forum_use_default"),
		anim::type::instant);

	userpicForumDefault->toggleOn(
		rpl::single(::Kotato::JsonSettings::GetBool("userpic_corner_radius_forum_use_default"))
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != ::Kotato::JsonSettings::GetBool("userpic_corner_radius_forum_use_default"));
	}) | rpl::on_next([=](bool enabled) {
		userpicForumWrap->toggle(!enabled, anim::type::normal);
		::Kotato::JsonSettings::Set("userpic_corner_radius_forum_use_default", enabled);
		::Kotato::JsonSettings::Write();
		::Kotato::RefreshRadius();
	}, container->lifetime());

	SettingsMenuJsonSwitch(ktg_settings_top_bar_mute, profile_top_mute);
	SettingsMenuJsonSwitch(ktg_settings_disable_up_edit, disable_up_edit);
	SettingsMenuJsonSwitch(ktg_settings_always_show_scheduled, always_show_scheduled);

	container->add(object_ptr<Button>(
		container,
		rktr("ktg_settings_chat_list_compact"),
		st::settingsButtonNoIcon
	))->toggleOn(
		rpl::single(::Kotato::JsonSettings::GetInt("chat_list_lines") == 1)
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != (::Kotato::JsonSettings::GetInt("chat_list_lines") == 1));
	}) | rpl::on_next([](bool enabled) {
		::Kotato::JsonSettings::Set("chat_list_lines", enabled ? 1 : 2);
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	container->add(object_ptr<Button>(
		container,
		rktr("ktg_settings_fonts"),
		st::settingsButtonNoIcon
	))->addClickHandler([=] {
		Ui::show(Box<FontsBox>());
	});

	container->add(object_ptr<Button>(
		container,
		rktr("ktg_disable_chat_themes"),
		st::settingsButtonNoIcon
	))->toggleOn(
		rpl::single(::Kotato::JsonSettings::GetBool("disable_chat_themes"))
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != ::Kotato::JsonSettings::GetBool("disable_chat_themes"));
	}) | rpl::on_next([controller](bool enabled) {
		::Kotato::JsonSettings::Set("disable_chat_themes", enabled);
		controller->session().data().cloudThemes().refreshChatThemes();
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	container->add(object_ptr<Button>(
		container,
		rktr("ktg_settings_hide_sponsored_messages"),
		st::settingsButtonNoIcon
	))->toggleOn(
		rpl::single(::Kotato::JsonSettings::GetBool("hide_sponsored_messages"))
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != ::Kotato::JsonSettings::GetBool("hide_sponsored_messages"));
	}) | rpl::on_next([controller](bool enabled) {
		::Kotato::JsonSettings::Set("hide_sponsored_messages", enabled);
		controller->session().sponsoredMessages().clear();
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	container->add(object_ptr<Button>(
		container,
		rktr("ktg_settings_view_profile_on_top"),
		st::settingsButtonNoIcon
	))->toggleOn(
		rpl::single(::Kotato::JsonSettings::GetBool("view_profile_on_top"))
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != ::Kotato::JsonSettings::GetBool("view_profile_on_top"));
	}) | rpl::on_next([](bool enabled) {
		::Kotato::JsonSettings::Set("view_profile_on_top", enabled);
		if (enabled) {
			auto &option = ::base::options::lookup<bool>(Window::kOptionViewProfileInChatsListContextMenu);
			option.set(true);
		}
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	Ui::AddSkip(container);
	Ui::AddDividerText(container, rktr("ktg_settings_view_profile_on_top_about"));
	Ui::AddSkip(container);

	SettingsMenuJsonSwitch(ktg_settings_emoji_sidebar, emoji_sidebar);
	SettingsMenuJsonSwitch(ktg_settings_emoji_sidebar_right_click, emoji_sidebar_right_click);

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
	}) | rpl::on_next([](bool checked) {
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
	}) | rpl::on_next([monospaceLargeBubblesButton](bool enabled) {
		monospaceLargeBubblesButton->toggle(!enabled, anim::type::normal);
		::Kotato::JsonSettings::Set("adaptive_bubbles", enabled);
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	monospaceLargeBubblesButton->entity()->toggleOn(
		rpl::single(::Kotato::JsonSettings::GetBool("monospace_large_bubbles"))
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != ::Kotato::JsonSettings::GetBool("monospace_large_bubbles"));
	}) | rpl::on_next([](bool enabled) {
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
		st::settingsButtonNoIcon
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
		st::settingsButtonNoIcon
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

	Ui::AddSkip(container);
	Ui::AddDividerText(container, rktr("ktg_forward_force_old_unquoted_desc"));
	Ui::AddSkip(container);

	SettingsMenuJsonSwitch(ktg_settings_forward_retain_selection, forward_retain_selection);
	SettingsMenuJsonSwitch(ktg_settings_forward_chat_on_click, forward_on_click);

	Ui::AddSkip(container);
	Ui::AddDividerText(container, rktr("ktg_settings_forward_chat_on_click_description"));
}

void SetupKotatoNetwork(not_null<Ui::VerticalLayout*> container) {
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rktr("ktg_settings_network"));


	SettingsMenuJsonSwitch(ktg_settings_video_download_boost, video_download_boost);
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
	}) | rpl::on_next([controller](bool enabled) {
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
		}) | rpl::on_next([controller](bool enabled) {
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

	SettingsMenuJsonSwitch(ktg_settings_remember_compress_images, remember_compress_images);
	container->add(object_ptr<Button>(
		container,
		rktr("ktg_settings_compress_images_default"),
		st::settingsButtonNoIcon
	))->toggleOn(
		rpl::single(Core::App().settings().sendFilesWay().sendImagesAsPhotos())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != Core::App().settings().sendFilesWay().sendImagesAsPhotos());
	}) | rpl::on_next([](bool enabled) {
		auto way = Core::App().settings().sendFilesWay();
		way.setSendImagesAsPhotos(enabled);
		Core::App().settings().setSendFilesWay(way);
		Core::App().saveSettingsDelayed();
	}, container->lifetime());
	SettingsMenuJsonSwitch(ktg_settings_ffmpeg_multithread, ffmpeg_multithread);

	Ui::AddSkip(container);
	Ui::AddDividerText(container, rktr("ktg_settings_ffmpeg_multithread_about"));
	Ui::AddSkip(container);
}

void KotatoTopBarOptions(const Ui::Menu::MenuCallback &addAction) {
	const auto customSettingsFile = cWorkingDir() + "tdata/kotato-settings-custom.json";

	addAction(
		ktr("ktg_settings_show_json_settings"),
		[=] { File::ShowInFolder(customSettingsFile); },
		&st::menuIconSettings);
	addAction(
		ktr("ktg_settings_restart"),
		[] { Core::Restart(); },
		&st::menuIconRestore);
}

Kotato::Kotato(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent(controller);
}

rpl::producer<QString> Kotato::title() {
	return rktr("ktg_settings_kotato");
}

void Kotato::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	KotatoTopBarOptions(addAction);
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

