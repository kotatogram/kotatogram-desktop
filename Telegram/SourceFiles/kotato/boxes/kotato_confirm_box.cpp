/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/boxes/kotato_confirm_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace Kotato {

ConfirmBox::ConfirmBox(
	QWidget*,
	const QString &text,
	FnMut<void()> confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(tr::lng_box_ok(tr::now))
, _cancelText(tr::lng_cancel(tr::now))
, _confirmStyle(st::defaultBoxButton)
, _text(this, st::boxLabel)
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	QWidget*,
	const QString &text,
	const QString &confirmText,
	FnMut<void()> confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(tr::lng_cancel(tr::now))
, _confirmStyle(st::defaultBoxButton)
, _text(this, st::boxLabel)
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	QWidget*,
	const TextWithEntities &text,
	const QString &confirmText,
	FnMut<void()> confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(tr::lng_cancel(tr::now))
, _confirmStyle(st::defaultBoxButton)
, _text(this, st::boxLabel)
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	QWidget*,
	const QString &text,
	const QString &confirmText,
	const style::RoundButton &confirmStyle,
	FnMut<void()> confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(tr::lng_cancel(tr::now))
, _confirmStyle(confirmStyle)
, _text(this, st::boxLabel)
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	QWidget*,
	const QString &text,
	const QString &confirmText,
	const QString &cancelText,
	FnMut<void()> confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(cancelText)
, _confirmStyle(st::defaultBoxButton)
, _text(this, st::boxLabel)
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	QWidget*,
	const QString &text,
	const QString &confirmText,
	const style::RoundButton &confirmStyle,
	const QString &cancelText,
	FnMut<void()> confirmedCallback,
	FnMut<void()> cancelledCallback)
: _confirmText(confirmText)
, _cancelText(cancelText)
, _confirmStyle(st::defaultBoxButton)
, _text(this, st::boxLabel)
, _confirmedCallback(std::move(confirmedCallback))
, _cancelledCallback(std::move(cancelledCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	const InformBoxTag &,
	const QString &text,
	const QString &doneText,
	Fn<void()> closedCallback)
: _confirmText(doneText)
, _confirmStyle(st::defaultBoxButton)
, _informative(true)
, _text(this, st::boxLabel)
, _confirmedCallback(generateInformCallback(closedCallback))
, _cancelledCallback(generateInformCallback(closedCallback)) {
	init(text);
}

ConfirmBox::ConfirmBox(
	const InformBoxTag &,
	const TextWithEntities &text,
	const QString &doneText,
	Fn<void()> closedCallback)
: _confirmText(doneText)
, _confirmStyle(st::defaultBoxButton)
, _informative(true)
, _text(this, st::boxLabel)
, _confirmedCallback(generateInformCallback(closedCallback))
, _cancelledCallback(generateInformCallback(closedCallback)) {
	init(text);
}

FnMut<void()> ConfirmBox::generateInformCallback(
		Fn<void()> closedCallback) {
	return crl::guard(this, [=] {
		closeBox();
		if (closedCallback) {
			closedCallback();
		}
	});
}

void ConfirmBox::init(const QString &text) {
	_text->setText(text);
}

void ConfirmBox::init(const TextWithEntities &text) {
	_text->setMarkedText(text);
}

void ConfirmBox::prepare() {
	addButton(
		rpl::single(_confirmText),
		[=] { confirmed(); },
		_confirmStyle);
	if (!_informative) {
		addButton(
			rpl::single(_cancelText),
			[=] { _cancelled = true; closeBox(); });
	}

	boxClosing() | rpl::start_with_next([=] {
		if (!_confirmed && (!_strictCancel || _cancelled)) {
			if (auto callback = std::move(_cancelledCallback)) {
				callback();
			}
		}
	}, lifetime());

	_text->setSelectable(true);
	_text->setLinksTrusted();

	setDimensions(st::boxWidth, st::boxPadding.top() + _text->height() + st::boxPadding.bottom());
}

void ConfirmBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_text->moveToLeft(st::boxPadding.left(), st::boxPadding.top());
}

void ConfirmBox::confirmed() {
	if (!_confirmed) {
		_confirmed = true;
		if (auto callback = std::move(_confirmedCallback)) {
			callback();
		}
	}
}

void ConfirmBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		confirmed();
	} else {
		BoxContent::keyPressEvent(e);
	}
}

InformBox::InformBox(QWidget*, const QString &text, Fn<void()> closedCallback) : ConfirmBox(ConfirmBox::InformBoxTag(), text, tr::lng_box_ok(tr::now), std::move(closedCallback)) {
}

InformBox::InformBox(QWidget*, const QString &text, const QString &doneText, Fn<void()> closedCallback) : ConfirmBox(ConfirmBox::InformBoxTag(), text, doneText, std::move(closedCallback)) {
}

InformBox::InformBox(QWidget*, const TextWithEntities &text, Fn<void()> closedCallback) : ConfirmBox(ConfirmBox::InformBoxTag(), text, tr::lng_box_ok(tr::now), std::move(closedCallback)) {
}

InformBox::InformBox(QWidget*, const TextWithEntities &text, const QString &doneText, Fn<void()> closedCallback) : ConfirmBox(ConfirmBox::InformBoxTag(), text, doneText, std::move(closedCallback)) {
}

} // namespace Kotato