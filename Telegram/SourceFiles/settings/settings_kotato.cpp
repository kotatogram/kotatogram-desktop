/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "settings/settings_kotato.h"

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
#include "boxes/fonts_box.h"
#include "boxes/net_boost_box.h"
#include "boxes/about_box.h"
#include "boxes/confirm_box.h"
#include "platform/platform_specific.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "core/update_checker.h"
#include "core/kotato_settings.h"
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
		KotatoSettings::Write();
	};
	recentStickersLimitSlider->resize(st::settingsAudioVolumeSlider.seekSize);
	recentStickersLimitSlider->setPseudoDiscrete(
		201,
		[](int val) { return val; },
		RecentStickersLimit(),
		updateRecentStickersLimitHeight);
	updateRecentStickersLimitLabel(RecentStickersLimit());

	AddButton(
		container,
		tr::ktg_settings_top_bar_mute(),
		st::settingsButton
	)->toggleOn(
		rpl::single(cProfileTopBarNotifications())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cProfileTopBarNotifications());
	}) | rpl::start_with_next([](bool enabled) {
		cSetProfileTopBarNotifications(enabled);
		KotatoSettings::Write();
	}, container->lifetime());

	AddButton(
		container,
		tr::ktg_settings_disable_up_edit(),
		st::settingsButton
	)->toggleOn(
		rpl::single(cDisableUpEdit())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cDisableUpEdit());
	}) | rpl::start_with_next([](bool enabled) {
		cSetDisableUpEdit(enabled);
		KotatoSettings::Write();
	}, container->lifetime());

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
		KotatoSettings::Write();
	}, container->lifetime());

	AddButton(
		container,
		tr::ktg_settings_always_show_scheduled(),
		st::settingsButton
	)->toggleOn(
		rpl::single(cAlwaysShowScheduled())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cAlwaysShowScheduled());
	}) | rpl::start_with_next([](bool enabled) {
		cSetAlwaysShowScheduled(enabled);
		Notify::showScheduledButtonChanged();
		KotatoSettings::Write();
	}, container->lifetime());

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
		KotatoSettings::Write();
	};
	stickerHeightSlider->resize(st::settingsAudioVolumeSlider.seekSize);
	stickerHeightSlider->setPseudoDiscrete(
		193,
		[](int val) { return val + 64; },
		StickerHeight(),
		updateStickerHeight);
	updateStickerHeightLabel(StickerHeight());

	AddButton(
		container,
		tr::ktg_settings_adaptive_bubbles(),
		st::settingsButton
	)->toggleOn(
		rpl::single(AdaptiveBubbles())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != AdaptiveBubbles());
	}) | rpl::start_with_next([](bool enabled) {
		SetAdaptiveBubbles(enabled);
		KotatoSettings::Write();
	}, container->lifetime());

	AddButton(
		container,
		tr::ktg_settings_emoji_outline(),
		st::settingsButton
	)->toggleOn(
		rpl::single(BigEmojiOutline())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != BigEmojiOutline());
	}) | rpl::start_with_next([](bool enabled) {
		SetBigEmojiOutline(enabled);
		KotatoSettings::Write();
	}, container->lifetime());

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

	AddButton(
		container,
		tr::ktg_settings_filters_only_unmuted_counter(),
		st::settingsButton
	)->toggleOn(
		rpl::single(cUnmutedFilterCounterOnly())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cUnmutedFilterCounterOnly());
	}) | rpl::start_with_next([=](bool enabled) {
		cSetUnmutedFilterCounterOnly(enabled);
		KotatoSettings::Write();
		controller->reloadFiltersMenu();
		App::wnd()->fixOrder();
	}, container->lifetime());

	AddButton(
		container,
		tr::ktg_settings_filters_hide_all(),
		st::settingsButton
	)->toggleOn(
		rpl::single(cHideFilterAllChats())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cHideFilterAllChats());
	}) | rpl::start_with_next([=](bool enabled) {
		cSetHideFilterAllChats(enabled);
		KotatoSettings::Write();
		controller->reloadFiltersMenu();
		App::wnd()->fixOrder();
	}, container->lifetime());

	AddButton(
		container,
		tr::ktg_settings_filters_hide_edit(),
		st::settingsButton
	)->toggleOn(
		rpl::single(cHideFilterEditButton())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cHideFilterEditButton());
	}) | rpl::start_with_next([=](bool enabled) {
		cSetHideFilterEditButton(enabled);
		KotatoSettings::Write();
		controller->reloadFiltersMenu();
		App::wnd()->fixOrder();
	}, container->lifetime());

	AddButton(
		container,
		tr::ktg_settings_filters_hide_folder_names(),
		st::settingsButton
	)->toggleOn(
		rpl::single(cHideFilterNames())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cHideFilterNames());
	}) | rpl::start_with_next([=](bool enabled) {
		cSetHideFilterNames(enabled);
		KotatoSettings::Write();
		controller->reloadFiltersMenu();
		App::wnd()->fixOrder();
	}, container->lifetime());

	AddSkip(container);
}

void SetupKotatoSystem(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::ktg_settings_system());

	AddButton(
		container,
		tr::ktg_settings_no_taskbar_flash(),
		st::settingsButton
	)->toggleOn(
		rpl::single(cNoTaskbarFlashing())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cNoTaskbarFlashing());
	}) | rpl::start_with_next([](bool enabled) {
		cSetNoTaskbarFlashing(enabled);
		KotatoSettings::Write();
	}, container->lifetime());

	AddSkip(container);
}

void SetupKotatoOther(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::ktg_settings_other());

	AddButton(
		container,
		tr::ktg_settings_show_phone_number(),
		st::settingsButton
	)->toggleOn(
		rpl::single(cShowPhoneInDrawer())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cShowPhoneInDrawer());
	}) | rpl::start_with_next([](bool enabled) {
		cSetShowPhoneInDrawer(enabled);
		KotatoSettings::Write();
	}, container->lifetime());

	AddButton(
		container,
		tr::ktg_settings_show_chat_id(),
		st::settingsButton
	)->toggleOn(
		rpl::single(cShowChatId())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cShowChatId());
	}) | rpl::start_with_next([](bool enabled) {
		cSetShowChatId(enabled);
		KotatoSettings::Write();
	}, container->lifetime());

	AddButton(
		container,
		tr::ktg_settings_call_confirm(),
		st::settingsButton
	)->toggleOn(
		rpl::single(cConfirmBeforeCall())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cConfirmBeforeCall());
	}) | rpl::start_with_next([](bool enabled) {
		cSetConfirmBeforeCall(enabled);
		KotatoSettings::Write();
	}, container->lifetime());

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

