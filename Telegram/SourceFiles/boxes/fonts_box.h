/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class Checkbox;
class InputField;
} // namespace Ui

class FontsBox : public BoxContent {
public:
	FontsBox(QWidget* parent);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
private:
	void save();
	void resetToDefault();

	object_ptr<Ui::InputField> _mainFontName = { nullptr };
	object_ptr<Ui::InputField> _semiboldFontName = { nullptr };
	object_ptr<Ui::Checkbox> _semiboldIsBold = { nullptr };
	object_ptr<Ui::InputField> _monospacedFontName = { nullptr };
	Ui::Text::String _about;

	int _aboutHeight = 0;
};