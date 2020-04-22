/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Kotato {
class InformBox;
class ConfirmBox : public Ui::BoxContent {
public:
	ConfirmBox(QWidget*, const QString &text, FnMut<void()> confirmedCallback = FnMut<void()>(), FnMut<void()> cancelledCallback = FnMut<void()>());
	ConfirmBox(QWidget*, const QString &text, const QString &confirmText, FnMut<void()> confirmedCallback = FnMut<void()>(), FnMut<void()> cancelledCallback = FnMut<void()>());
	ConfirmBox(QWidget*, const QString &text, const QString &confirmText, const style::RoundButton &confirmStyle, FnMut<void()> confirmedCallback = FnMut<void()>(), FnMut<void()> cancelledCallback = FnMut<void()>());
	ConfirmBox(QWidget*, const QString &text, const QString &confirmText, const QString &cancelText, FnMut<void()> confirmedCallback = FnMut<void()>(), FnMut<void()> cancelledCallback = FnMut<void()>());
	ConfirmBox(QWidget*, const QString &text, const QString &confirmText, const style::RoundButton &confirmStyle, const QString &cancelText, FnMut<void()> confirmedCallback = FnMut<void()>(), FnMut<void()> cancelledCallback = FnMut<void()>());
	ConfirmBox(QWidget*, const TextWithEntities &text, const QString &confirmText, FnMut<void()> confirmedCallback = nullptr, FnMut<void()> cancelledCallback = nullptr);

	// If strict cancel is set the cancelledCallback is only called if the cancel button was pressed.
	void setStrictCancel(bool strictCancel) {
		_strictCancel = strictCancel;
	}

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	struct InformBoxTag {
	};
	ConfirmBox(const InformBoxTag &, const QString &text, const QString &doneText, Fn<void()> closedCallback);
	ConfirmBox(const InformBoxTag &, const TextWithEntities &text, const QString &doneText, Fn<void()> closedCallback);
	FnMut<void()> generateInformCallback(Fn<void()> closedCallback);
	friend class InformBox;

	void confirmed();
	void init(const QString &text);
	void init(const TextWithEntities &text);

	QString _confirmText;
	QString _cancelText;
	const style::RoundButton &_confirmStyle;
	bool _informative = false;

	object_ptr<Ui::FlatLabel> _text;

	bool _confirmed = false;
	bool _cancelled = false;
	bool _strictCancel = false;
	FnMut<void()> _confirmedCallback;
	FnMut<void()> _cancelledCallback;

};

class InformBox : public ConfirmBox {
public:
	InformBox(QWidget*, const QString &text, Fn<void()> closedCallback = nullptr);
	InformBox(QWidget*, const QString &text, const QString &doneText, Fn<void()> closedCallback = nullptr);
	InformBox(QWidget*, const TextWithEntities &text, Fn<void()> closedCallback = nullptr);
	InformBox(QWidget*, const TextWithEntities &text, const QString &doneText, Fn<void()> closedCallback = nullptr);

};
} // namespace Kotato