/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/settings_menu.h"

#include "kotato/kotato_lang.h"
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
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "core/update_checker.h"
#include "kotato/json_settings.h"
#include "core/application.h"
#include "storage/localstorage.h"
#include "data/data_session.h"
#include "data/data_cloud_themes.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "facades.h"
#include "app.h"
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

#define SettingsMenuCSwitch(LangKey, Option) AddButton( \
	container, \
	rktr(#LangKey), \
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
	rktr(#LangKey), \
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
	rktr(#LangKey), \
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

	if (Ui::Platform::IsOverlapped(container, QRect()).has_value()) {
		SettingsMenuCSwitch(ktg_settings_auto_scroll_unfocused, AutoScrollUnfocused);
	}

	AddButton(
		container,
		rktr("ktg_settings_chat_list_compact"),
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

	AddButton(
		container,
		rktr("ktg_settings_always_show_scheduled"),
		st::settingsButton
	)->toggleOn(
		rpl::single(cAlwaysShowScheduled())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cAlwaysShowScheduled());
	}) | rpl::start_with_next([controller](bool enabled) {
		cSetAlwaysShowScheduled(enabled);
		Notify::showScheduledButtonChanged(&controller->session());
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	SettingsMenuSwitch(ktg_emoji_panel_hover, HoverEmojiPanel);

	AddButton(
		container,
		rktr("ktg_settings_fonts"),
		st::settingsButton
	)->addClickHandler([=] {
		Ui::show(Box<FontsBox>());
	});

	AddButtonWithLabel(
		container,
		rktr("ktg_settings_userpic_rounding"),
		rpl::single(UserpicRoundingLabel(cUserpicCornersType())),
		st::settingsButton
	)->addClickHandler([=] {
		Ui::show(Box<::Kotato::RadioBox>(
			ktr("ktg_settings_userpic_rounding"),
			ktr("ktg_settings_userpic_rounding_desc"),
			cUserpicCornersType(),
			4,
			UserpicRoundingLabel,
			[=] (int value) {
				cSetUserpicCornersType(value);
				::Kotato::JsonSettings::Write();
			}, true));
	});

	AddButton(
		container,
		rktr("ktg_disable_chat_themes"),
		st::settingsButton
	)->toggleOn(
		rpl::single(cDisableChatThemes())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cDisableChatThemes());
	}) | rpl::start_with_next([controller](bool enabled) {
		cSetDisableChatThemes(enabled);
		controller->session().data().cloudThemes().refreshChatThemes();
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	AddSkip(container);
}

void SetupKotatoMessages(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
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

	container->add(
		object_ptr<Ui::Checkbox>(
			container,
			ktr("ktg_settings_sticker_scale_both"),
			StickerScaleBoth(),
			st::settingsCheckbox),
		st::settingsCheckboxPadding
	)->checkedChanges(
	) | rpl::filter([](bool checked) {
		return (checked != StickerScaleBoth());
	}) | rpl::start_with_next([](bool checked) {
		SetStickerScaleBoth(checked);
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
		rpl::single(AdaptiveBubbles())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != AdaptiveBubbles());
	}) | rpl::start_with_next([monospaceLargeBubblesButton](bool enabled) {
		monospaceLargeBubblesButton->toggle(!enabled, anim::type::normal);
		SetAdaptiveBubbles(enabled);
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	monospaceLargeBubblesButton->entity()->toggleOn(
		rpl::single(MonospaceLargeBubbles())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != MonospaceLargeBubbles());
	}) | rpl::start_with_next([](bool enabled) {
		SetMonospaceLargeBubbles(enabled);
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	if (adaptiveBubblesButton->toggled()) {
		monospaceLargeBubblesButton->hide(anim::type::instant);
	}

	SettingsMenuSwitch(ktg_settings_emoji_outline, BigEmojiOutline);

	AddSkip(container);
}

void SetupKotatoForward(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, rktr("ktg_settings_forward"));

	SettingsMenuCSwitch(ktg_forward_remember_mode, ForwardRememberMode);

	auto forwardModeText = rpl::single(
		ForwardMode()
	) | rpl::then(
		ForwardModeChanges()
	) | rpl::map([] {
		return ForwardModeLabel(ForwardMode());
	});

	AddButtonWithLabel(
		container,
		rktr("ktg_forward_mode"),
		forwardModeText,
		st::settingsButton
	)->addClickHandler([=] {
		Ui::show(Box<::Kotato::RadioBox>(
			ktr("ktg_forward_mode"),
			ForwardMode(),
			3,
			ForwardModeLabel,
			[=] (int value) {
				SetForwardMode(value);
				::Kotato::JsonSettings::Write();
			}, false));
	});

	auto forwardGroupingModeText = rpl::single(
		ForwardGroupingMode()
	) | rpl::then(
		ForwardGroupingModeChanges()
	) | rpl::map([] {
		return GroupingModeLabel(ForwardGroupingMode());
	});

	AddButtonWithLabel(
		container,
		rktr("ktg_forward_grouping_mode"),
		forwardGroupingModeText,
		st::settingsButton
	)->addClickHandler([=] {
		Ui::show(Box<::Kotato::RadioBox>(
			ktr("ktg_forward_grouping_mode"),
			ForwardGroupingMode(),
			3,
			GroupingModeLabel,
			GroupingModeDescription,
			[=] (int value) {
				SetForwardGroupingMode(value);
				::Kotato::JsonSettings::Write();
			}, false));
	});

	SettingsMenuCSwitch(ktg_forward_force_old_unquoted, ForwardForceOld);

	AddSkip(container);
	AddDividerText(container, rktr("ktg_forward_force_old_unquoted_desc"));
	AddSkip(container);

	SettingsMenuCSwitch(ktg_settings_forward_retain_selection, ForwardRetainSelection);
	SettingsMenuCSwitch(ktg_settings_forward_chat_on_click, ForwardChatOnClick);

	AddSkip(container);
	AddDividerText(container, rktr("ktg_settings_forward_chat_on_click_description"));
}

void SetupKotatoNetwork(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container);
	AddSubsectionTitle(container, rktr("ktg_settings_network"));

	AddButtonWithLabel(
		container,
		rktr("ktg_settings_net_speed_boost"),
		rpl::single(NetBoostLabel(cNetSpeedBoost())),
		st::settingsButton
	)->addClickHandler([=] {
		Ui::show(Box<::Kotato::RadioBox>(
			ktr("ktg_net_speed_boost_title"),
			ktr("ktg_net_speed_boost_desc"),
			cNetSpeedBoost(),
			4,
			NetBoostLabel,
			[=] (int value) {
				SetNetworkBoost(value);
				::Kotato::JsonSettings::Write();
			}, true));
	});

	SettingsMenuCSwitch(ktg_settings_telegram_sites_autologin, TelegramSitesAutologin);

	AddSkip(container);
}

void SetupKotatoFolders(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, rktr("ktg_settings_filters"));

	SettingsMenuCFilterSwitch(ktg_settings_filters_only_unmuted_counter, UnmutedFilterCounterOnly);
	SettingsMenuCFilterSwitch(ktg_settings_filters_hide_all, HideFilterAllChats);
	SettingsMenuCFilterSwitch(ktg_settings_filters_hide_edit, HideFilterEditButton);
	SettingsMenuCFilterSwitch(ktg_settings_filters_hide_folder_names, HideFilterNames);

	AddSkip(container);
}

void SetupKotatoSystem(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, rktr("ktg_settings_system"));

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	const auto qtScaleToggled = Ui::CreateChild<rpl::event_stream<bool>>(
		container.get());
	AddButton(
		container,
		rktr("ktg_settings_qt_scale"),
		st::settingsButton
	)->toggleOn(
		qtScaleToggled->events_starting_with_copy(cQtScale())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cQtScale());
	}) | rpl::start_with_next([=](bool enabled) {
		const auto confirmed = [=] {
			cSetQtScale(enabled);
			::Kotato::JsonSettings::Write();
			App::restart();
		};
		const auto cancelled = [=] {
			qtScaleToggled->fire(cQtScale() == true);
		};
		Ui::show(Box<Ui::ConfirmBox>(
			tr::lng_settings_need_restart(tr::now),
			tr::lng_settings_restart_now(tr::now),
			confirmed,
			cancelled));
	}, container->lifetime());
#endif // Qt < 6.0.0

	if (Platform::IsLinux()) {
		auto fileDialogTypeText = rpl::single(
			FileDialogType()
		) | rpl::then(
			FileDialogTypeChanges()
		) | rpl::map([] {
			return FileDialogTypeLabel(int(FileDialogType()));
		});

		AddButtonWithLabel(
			container,
			rktr("ktg_settings_file_dialog_type"),
			fileDialogTypeText,
			st::settingsButton
		)->addClickHandler([=] {
			Ui::show(Box<::Kotato::RadioBox>(
				ktr("ktg_settings_file_dialog_type"),
				int(FileDialogType()),
				int(Platform::FileDialog::ImplementationType::Count),
				FileDialogTypeLabel,
				FileDialogTypeDescription,
				[=](int value) {
					SetFileDialogType(Platform::FileDialog::ImplementationType(value));
					::Kotato::JsonSettings::Write();
				}, false));
		});
	}

	if (Platform::IsMac()) {
		const auto useNativeDecorationsToggled = Ui::CreateChild<rpl::event_stream<bool>>(
			container.get());
		AddButton(
			container,
			tr::lng_settings_native_frame(),
			st::settingsButton
		)->toggleOn(
			useNativeDecorationsToggled->events_starting_with_copy(cUseNativeDecorations())
		)->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled != cUseNativeDecorations());
		}) | rpl::start_with_next([=](bool enabled) {
			const auto confirmed = [=] {
				cSetUseNativeDecorations(enabled);
				::Kotato::JsonSettings::Write();
				App::restart();
			};
			const auto cancelled = [=] {
				useNativeDecorationsToggled->fire(cUseNativeDecorations() == true);
			};
			Ui::show(Box<Ui::ConfirmBox>(
				tr::lng_settings_need_restart(tr::now),
				tr::lng_settings_restart_now(tr::now),
				confirmed,
				cancelled));
		}, container->lifetime());
	}

	AddButton(
		container,
		rktr("ktg_settings_disable_tray_counter"),
		st::settingsButton
	)->toggleOn(
		rpl::single(cDisableTrayCounter())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cDisableTrayCounter());
	}) | rpl::start_with_next([controller](bool enabled) {
		cSetDisableTrayCounter(enabled);
		controller->session().data().notifyUnreadBadgeChanged();
		::Kotato::JsonSettings::Write();
	}, container->lifetime());

	if (Platform::IsLinux()) {
		AddButton(
			container,
			rktr("ktg_settings_use_telegram_panel_icon"),
			st::settingsButton
		)->toggleOn(
			rpl::single(cUseTelegramPanelIcon())
		)->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled != cUseTelegramPanelIcon());
		}) | rpl::start_with_next([controller](bool enabled) {
			cSetUseTelegramPanelIcon(enabled);
			controller->session().data().notifyUnreadBadgeChanged();
			::Kotato::JsonSettings::Write();
		}, container->lifetime());
	}

	auto trayIconText = rpl::single(
		rpl::empty_value()
	) | rpl::then(
		controller->session().data().unreadBadgeChanges()
	) | rpl::map([] {
		return TrayIconLabel(cCustomAppIcon());
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
			cCustomAppIcon(),
			6,
			TrayIconLabel,
			[=] (int value) {
				cSetCustomAppIcon(value);
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

	SettingsMenuCSwitch(ktg_settings_show_phone_number, ShowPhoneInDrawer);

	const auto chatIdButton = container->add(
		object_ptr<Button>(
			container,
			rktr("ktg_settings_chat_id"),
			st::settingsButton));
	auto chatIdText = rpl::single(
		ChatIdLabel(ShowChatId())
	) | rpl::then(
		ShowChatIdChanges(
		) | rpl::map([] (int chatIdType) {
			return ChatIdLabel(chatIdType);
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
			ShowChatId(),
			3,
			ChatIdLabel,
			[=] (int value) {
				SetShowChatId(value);
				::Kotato::JsonSettings::Write();
			}));
	});

	SettingsMenuCSwitch(ktg_settings_call_confirm, ConfirmBeforeCall);
	SettingsMenuCSwitch(ktg_settings_remember_compress_images, RememberCompressImages);
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
	SettingsMenuCSwitch(ktg_settings_ffmpeg_multithread, FFmpegMultithread);

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

