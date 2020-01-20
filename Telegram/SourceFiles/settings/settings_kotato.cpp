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
#include "facades.h"
#include "app.h"
#include "styles/style_settings.h"

namespace Settings {

void SetupKotatoChats(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container);
	AddSubsectionTitle(container, tr::ktg_settings_chats());

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
		129,
		[](int val) { return val + 128; },
		StickerHeight(),
		updateStickerHeight);
	updateStickerHeightLabel(StickerHeight());

	const auto adaptiveBubblesToggled = Ui::CreateChild<rpl::event_stream<bool>>(
		container.get());
	AddButton(
		container,
		tr::ktg_settings_adaptive_bubbles(),
		st::settingsButton
	)->toggleOn(
		adaptiveBubblesToggled->events_starting_with_copy(cAdaptiveBubbles())
	)->toggledValue(
	) | rpl::filter([](bool enabled) {
		return (enabled != cAdaptiveBubbles());
	}) | rpl::start_with_next([=](bool enabled) {
		const auto confirmed = [=] {
			cSetAdaptiveBubbles(enabled);
			KotatoSettings::Write();
			App::restart();
		};
		const auto cancelled = [=] {
			adaptiveBubblesToggled->fire(cAdaptiveBubbles() == true);
		};
		Ui::show(Box<ConfirmBox>(
			tr::lng_settings_need_restart(tr::now),
			tr::lng_settings_restart_now(tr::now),
			confirmed,
			cancelled));
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
	SetupKotatoNetwork(content);
	SetupKotatoOther(content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings

