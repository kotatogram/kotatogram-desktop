/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "boxes/fonts_box.h"

#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "styles/style_boxes.h"
#include "lang/lang_keys.h"

FontsBox::FontsBox(QWidget* parent)
: _mainFontName(this, st::defaultInputField, tr::ktg_fonts_main())
, _semiboldFontName(this, st::defaultInputField, tr::ktg_fonts_semibold())
, _semiboldIsBold(this, tr::ktg_fonts_semibold_is_bold(tr::now), cSemiboldFontIsBold())
, _monospacedFontName(this, st::defaultInputField, tr::ktg_fonts_monospaced())
, _about(st::boxWidth - st::boxPadding.left() * 1.5)
{
}

void FontsBox::prepare() {
	setTitle(tr::ktg_fonts_title());

	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	addLeftButton(tr::ktg_fonts_reset(), [=] { resetToDefault(); });

	if (!cMainFont().isEmpty()) {
		_mainFontName->setText(cMainFont());
	}

	if (!cSemiboldFont().isEmpty()) {
		_semiboldFontName->setText(cSemiboldFont());
	}

	if (!cMonospaceFont().isEmpty()) {
		_monospacedFontName->setText(cMonospaceFont());
	}

	_about.setText(st::fontsBoxTextStyle, tr::ktg_fonts_about(tr::now));
	_aboutHeight = _about.countHeight(st::boxWidth - st::boxPadding.left() * 1.5);
	
	setDimensions(st::boxWidth, _mainFontName->height() + _semiboldFontName->height() + _semiboldIsBold->height() + _monospacedFontName->height() + _aboutHeight + st::boxLittleSkip * 2);
}


void FontsBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);
	int32 w = st::boxWidth - st::boxPadding.left() * 1.5;
	int32 abouty = _mainFontName->height() + _semiboldFontName->height() + _semiboldIsBold->height() + _monospacedFontName->height() + st::boxLittleSkip * 2;
	p.setPen(st::windowSubTextFg);
	_about.drawLeft(p, st::boxPadding.left(), abouty, w, width());

}

void FontsBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	int32 w = st::boxWidth - st::boxPadding.left() - st::boxPadding.right();
	_mainFontName->resize(w, _mainFontName->height());
	_mainFontName->moveToLeft(st::boxPadding.left(), 0);
	_semiboldFontName->resize(w, _semiboldFontName->height());
	_semiboldFontName->moveToLeft(st::boxPadding.left(), _mainFontName->y() + _mainFontName->height());
	_semiboldIsBold->resize(w, _semiboldIsBold->height());
	_semiboldIsBold->moveToLeft(st::boxPadding.left(), _semiboldFontName->y() + _semiboldFontName->height() + st::boxLittleSkip);
	_monospacedFontName->resize(w, _monospacedFontName->height());
	_monospacedFontName->moveToLeft(st::boxPadding.left(), _semiboldIsBold->y() + _semiboldIsBold->height());
}

void FontsBox::setInnerFocus() {
	_mainFontName->setFocusFast();
}

void FontsBox::save() {
}

void FontsBox::resetToDefault() {
}