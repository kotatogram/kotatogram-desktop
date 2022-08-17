/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once

#include "settings/settings_common_session.h"

namespace Settings {

void SetupKotatoChats(not_null<Ui::VerticalLayout*> container);
void SetupKotatoMessages(not_null<Ui::VerticalLayout*> container);
void SetupKotatoForward(not_null<Ui::VerticalLayout*> container);
void SetupKotatoNetwork(not_null<Ui::VerticalLayout*> container);
void SetupKotatoFolders(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container);
void SetupKotatoSystem(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container);
void SetupKotatoOther(not_null<Ui::VerticalLayout*> container);

class Kotato : public Section<Kotato> {
public:
	Kotato(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

	void fillTopBarMenu(
		const Ui::Menu::MenuCallback &addAction) override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

	const not_null<Window::SessionController*> _controller;

};

} // namespace Settings
