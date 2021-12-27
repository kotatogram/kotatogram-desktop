/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "ui/wrap/vertical_layout.h"

namespace Ui {
class Checkbox;
class InputField;
} // namespace Ui

class FontsBox : public Ui::BoxContent {
public:
	FontsBox(QWidget* parent);

protected:
	void prepare() override;
	void setInnerFocus() override;

private:
	void save();
	void resetToDefault();

	object_ptr<Ui::VerticalLayout> _owned;
	not_null<Ui::VerticalLayout*> _content;

	QPointer<Ui::Checkbox> _useSystemFont;
	QPointer<Ui::Checkbox> _useOriginalMetrics;
	QPointer<Ui::InputField> _mainFontName;
	QPointer<Ui::InputField> _semiboldFontName;
	QPointer<Ui::Checkbox> _semiboldIsBold;
	QPointer<Ui::InputField> _monospacedFontName;
};
