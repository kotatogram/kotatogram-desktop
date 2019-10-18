/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once

#include "settings/settings_common.h"

class BoxContent;

namespace Settings {

void SetupKotatoChats(not_null<Ui::VerticalLayout*> container);
//void SetupKotatoFonts(not_null<Ui::VerticalLayout*> container);
void SetupKotatoNetwork(not_null<Ui::VerticalLayout*> container);
//void SetupKotatoOther(not_null<Ui::VerticalLayout*> container);

class Kotato : public Section {
public:
	Kotato(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

} // namespace Settings
