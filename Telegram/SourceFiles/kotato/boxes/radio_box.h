/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class RadiobuttonGroup;
class Radiobutton;
class FlatLabel;
} // namespace Ui

namespace Kotato {

class RadioBox : public Ui::BoxContent {
public:
	RadioBox(QWidget* parent, const QString &title, int currentValue, const QMap<int, QString> &options, Fn<void(int)> saveCallback, bool warnRestart = false);
	RadioBox(QWidget* parent, const QString &title, const QString &description, int currentValue, const QMap<int, QString> &options, Fn<void(int)> saveCallback, bool warnRestart = false);

protected:
	void prepare() override;

private:
	void save();

	QString _title;
	QString _description;
	int _startValue;
	QMap<int, QString> _options;
	Fn<void(int)> _saveCallback;
	bool _warnRestart = false;
	std::shared_ptr<Ui::RadiobuttonGroup> _group;
};

} // namespace Kotato
