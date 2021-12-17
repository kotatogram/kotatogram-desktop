/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_common.h"

#include "kotato/kotato_lang.h"
#include "settings/settings_chat.h"
#include "settings/settings_advanced.h"
#include "settings/settings_information.h"
#include "settings/settings_main.h"
#include "settings/settings_notifications.h"
#include "settings/settings_privacy_security.h"
#include "settings/settings_folders.h"
#include "settings/settings_calls.h"
#include "kotato/settings_menu.h"
#include "core/application.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/buttons.h"
#include "boxes/abstract_box.h"
#include "boxes/sessions_box.h"
#include "ui/boxes/confirm_box.h"
#include "window/themes/window_theme_editor_box.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "lang/lang_keys.h"
#include "core/file_utilities.h"
#include "mainwindow.h"
#include "app.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {

object_ptr<Section> CreateSection(
	Type type,
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller) {
	switch (type) {
	case Type::Main:
		return object_ptr<Main>(parent, controller);
	case Type::Information:
		return object_ptr<Information>(parent, controller);
	case Type::Notifications:
		return object_ptr<Notifications>(parent, controller);
	case Type::PrivacySecurity:
		return object_ptr<PrivacySecurity>(parent, controller);
	case Type::Sessions:
		return object_ptr<Sessions>(parent, controller);
	case Type::Advanced:
		return object_ptr<Advanced>(parent, controller);
	case Type::Folders:
		return object_ptr<Folders>(parent, controller);
	case Type::Chat:
		return object_ptr<Chat>(parent, controller);
	case Type::Calls:
		return object_ptr<Calls>(parent, controller);
	case Type::Kotato:
		return object_ptr<Kotato>(parent, controller);
	}
	Unexpected("Settings section type in Widget::createInnerWidget.");
}

void AddSkip(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container, st::settingsSectionSkip);
}

void AddSkip(not_null<Ui::VerticalLayout*> container, int skip) {
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		skip));
}

void AddDivider(not_null<Ui::VerticalLayout*> container) {
	container->add(object_ptr<Ui::BoxContentDivider>(container));
}

void AddDividerText(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text) {
	container->add(object_ptr<Ui::DividerLabel>(
		container,
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(text),
			st::boxDividerLabel),
		st::settingsDividerLabelPadding));
}

not_null<Ui::RpWidget*> AddButtonIcon(
		not_null<Ui::AbstractButton*> button,
		const style::icon *leftIcon,
		int iconLeft,
		const style::color *leftIconOver) {
	const auto icon = Ui::CreateChild<Ui::RpWidget>(button.get());
	icon->setAttribute(Qt::WA_TransparentForMouseEvents);
	icon->resize(leftIcon->size());
	button->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		icon->moveToLeft(
			iconLeft ? iconLeft : st::settingsSectionIconLeft,
			(size.height() - icon->height()) / 2,
			size.width());
	}, icon->lifetime());
	icon->paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(icon);
		const auto width = icon->width();
		const auto paintOver = (button->isOver() || button->isDown())
			&& !button->isDisabled();
		if (!paintOver) {
			leftIcon->paint(p, QPoint(), width);
		} else if (leftIconOver) {
			leftIcon->paint(p, QPoint(), width, (*leftIconOver)->c);
		} else {
			leftIcon->paint(p, QPoint(), width, st::menuIconFgOver->c);
		}
	}, icon->lifetime());
	return icon;
}

object_ptr<Button> CreateButton(
		not_null<QWidget*> parent,
		rpl::producer<QString> text,
		const style::SettingsButton &st,
		const style::icon *leftIcon,
		int iconLeft,
		const style::color *leftIconOver) {
	auto result = object_ptr<Button>(parent, std::move(text), st);
	const auto button = result.data();
	if (leftIcon) {
		AddButtonIcon(button, leftIcon, iconLeft, leftIconOver);
	}
	return result;
}

not_null<Button*> AddButton(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		const style::SettingsButton &st,
		const style::icon *leftIcon,
		int iconLeft) {
	return container->add(
		CreateButton(container, std::move(text), st, leftIcon, iconLeft));
}

void CreateRightLabel(
		not_null<Button*> button,
		rpl::producer<QString> label,
		const style::SettingsButton &st,
		rpl::producer<QString> buttonText) {
	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		button.get(),
		st.rightLabel);
	rpl::combine(
		button->widthValue(),
		std::move(buttonText),
		std::move(label)
	) | rpl::start_with_next([=, &st](
			int width,
			const QString &button,
			const QString &text) {
		const auto available = width
			- st.padding.left()
			- st.padding.right()
			- st.font->width(button)
			- st::settingsButtonRightSkip;
		name->setText(text);
		name->resizeToNaturalWidth(available);
		name->moveToRight(st::settingsButtonRightSkip, st.padding.top());
	}, name->lifetime());
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
}

not_null<Button*> AddButtonWithLabel(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text,
		rpl::producer<QString> label,
		const style::SettingsButton &st,
		const style::icon *leftIcon,
		int iconLeft) {
	const auto button = AddButton(
		container,
		rpl::duplicate(text),
		st,
		leftIcon,
		iconLeft);
	CreateRightLabel(button, std::move(label), st, std::move(text));
	return button;
}

not_null<Ui::FlatLabel*> AddSubsectionTitle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text) {
	return container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(text),
			st::settingsSubsectionTitle),
		st::settingsSubsectionTitlePadding);
}

void FillMenu(
		not_null<Window::SessionController*> controller,
		Type type,
		Fn<void(Type)> showOther,
		MenuCallback addAction) {
	const auto window = &controller->window();
	if (type == Type::Chat) {
		addAction(
			tr::lng_settings_bg_theme_create(tr::now),
			[=] { window->show(Box(Window::Theme::CreateBox, window)); });
	} else {
		if (type != Type::Kotato) {
			const auto &list = Core::App().domain().accounts();
			if (list.size() < ::Main::Domain::kMaxAccountsWarn) {
				addAction(tr::lng_menu_add_account(tr::now), [=] {
					Core::App().domain().addActivated(MTP::Environment{});
				});
			} else if (list.size() < ::Main::Domain::kMaxAccounts) {
				addAction(tr::lng_menu_add_account(tr::now), [=] {
					Ui::show(
					Box<Ui::ConfirmBox>(
						ktr("ktg_too_many_accounts_warning"),
						ktr("ktg_account_add_anyway"),
						[=] {
							Core::App().domain().addActivated(MTP::Environment{});
						}),
					Ui::LayerOption::KeepOther);
				});
			}
		}
		const auto customSettingsFile = cWorkingDir() + "tdata/kotato-settings-custom.json";
		if (type != Type::Kotato && !controller->session().supportMode()) {
			addAction(
				tr::lng_settings_information(tr::now),
				[=] { showOther(Type::Information); });
		}
		addAction(
			ktr("ktg_settings_show_json_settings"),
			[=] { File::ShowInFolder(customSettingsFile); });
		addAction(
			ktr("ktg_settings_restart"),
			[=] { App::restart(); });
		if (type != Type::Kotato) {
			addAction(
				tr::lng_settings_logout(tr::now),
				[=] { window->showLogoutConfirmation(); });
		}
	}
}

} // namespace Settings
