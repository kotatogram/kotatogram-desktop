/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

This code is in Public Domain, see license terms in .github/CONTRIBUTING.md
Copyright (C) 2017, Nicholas Guriev <guriev-ns@ya.ru>
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class InputField;
class DropdownMenu;
}

/* This class implements a dialog-box with radio-buttons for pick duration of
 * turning off notifications from a chat. The widget is opened by a context menu
 * in the left list of dialogues. */
class MuteSettingsBox : public Ui::BoxContent {
public:
	MuteSettingsBox(QWidget *parent, not_null<PeerData*> peer);

protected:
	void prepare() override;

	void keyPressEvent(QKeyEvent *e) override;

private:
	enum Period {
		Second = 1,
		Minute = 60,
		Hour = 3600,
		Day = 86400,
	};

	not_null<PeerData*> _peer;
	Fn<void()> _save;

	object_ptr<Ui::InputField> _forNumberInput;
	object_ptr<Ui::InputField> _forPeriodInput;

	Period _period = Period::Hour;
	base::unique_qptr<Ui::DropdownMenu> _menu;

	rpl::lifetime _lifetime;
};
// vi: ts=4 tw=80
