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
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "boxes/confirm_box.h"
#include "core/kotato_settings.h"
#include "lang/lang_keys.h"
#include "app.h"

FontsBox::FontsBox(QWidget* parent)
: _useSystemFont(this, tr::ktg_fonts_use_system_font(tr::now), cUseSystemFont())
, _mainFontName(this, st::defaultInputField, tr::ktg_fonts_main())
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

	setDimensions(st::boxWidth, _useSystemFont->height()
		+ _mainFontName->height()
		+ _semiboldFontName->height()
		+ _semiboldIsBold->height()
		+ _monospacedFontName->height()
		+ _aboutHeight
		+ st::boxLittleSkip * 2);
}


void FontsBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);
	int32 w = st::boxWidth - st::boxPadding.left() * 1.5;
	int32 abouty = _useSystemFont->height()
		+ _mainFontName->height()
		+ _semiboldFontName->height()
		+ _semiboldIsBold->height()
		+ _monospacedFontName->height()
		+ st::boxLittleSkip * 2;
	p.setPen(st::windowSubTextFg);
	_about.drawLeft(p, st::boxPadding.left(), abouty, w, width());

}

void FontsBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	int32 w = st::boxWidth - st::boxPadding.left() - st::boxPadding.right();
	_useSystemFont->resize(w, _useSystemFont->height());
	_useSystemFont->moveToLeft(st::boxPadding.left(), 0);
	_mainFontName->resize(w, _mainFontName->height());
	_mainFontName->moveToLeft(st::boxPadding.left(), _useSystemFont->y() + _useSystemFont->height() + st::boxLittleSkip);
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
	const auto useSystemFont = _useSystemFont->checked();
	const auto mainFont = _mainFontName->getLastText().trimmed();
	const auto semiboldFont = _semiboldFontName->getLastText().trimmed();
	const auto semiboldIsBold = _semiboldIsBold->checked();
	const auto monospacedFont = _monospacedFontName->getLastText().trimmed();

	const auto changeFonts = [=] {
		cSetUseSystemFont(useSystemFont);
		cSetMainFont(mainFont);
		cSetSemiboldFont(semiboldFont);
		cSetSemiboldFontIsBold(semiboldIsBold);
		cSetMonospaceFont(monospacedFont);
		KotatoSettings::Write();
		App::restart();
	};

	const auto box = std::make_shared<QPointer<BoxContent>>();

	*box = getDelegate()->show(
		Box<ConfirmBox>(
			tr::ktg_fonts_restart_new_fonts(tr::now),
			tr::lng_settings_restart_now(tr::now),
			tr::lng_cancel(tr::now),
			changeFonts));
}

void FontsBox::resetToDefault() {
	const auto resetFonts = [=] {
		cSetMainFont(QString());
		cSetSemiboldFont(QString());
		cSetSemiboldFontIsBold(false);
		cSetMonospaceFont(QString());
		KotatoSettings::Write();
		App::restart();
	};

	const auto box = std::make_shared<QPointer<BoxContent>>();

	*box = getDelegate()->show(
		Box<ConfirmBox>(
			tr::ktg_fonts_restart_reset(tr::now),
			tr::lng_settings_restart_now(tr::now),
			tr::lng_cancel(tr::now),
			resetFonts));
}