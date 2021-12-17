/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "base/timer.h"
#include "data/stickers/data_stickers.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class ConfirmBox;
class PlainShadow;
class DropdownMenu;
} // namespace Ui

class StickerSetBox final : public Ui::BoxContent {
public:
	StickerSetBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		const StickerSetIdentifier &set);

	static QPointer<Ui::BoxContent> Show(
		not_null<Window::SessionController*> controller,
		not_null<DocumentData*> document);

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	enum class Error {
		NotFound,
	};

	void updateTitleAndButtons();
	void updateButtons();
	bool showMenu(not_null<Ui::IconButton*> button);
	void addStickers();
	void copyStickersLink();
	void copyTitle();
	void handleError(Error error);

	const not_null<Window::SessionController*> _controller;
	const StickerSetIdentifier _set;
	base::unique_qptr<Ui::DropdownMenu> _menu;

	class Inner;
	QPointer<Inner> _inner;

};
