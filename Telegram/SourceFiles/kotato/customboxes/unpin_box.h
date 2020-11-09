/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "mtproto/sender.h"

namespace Ui {
class FlatLabel;
} // namespace Ui

class UnpinMessageBox final : public Ui::BoxContent {
public:
	UnpinMessageBox(QWidget*, not_null<PeerData*> peer, MsgId msgId);

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void unpinMessage();
	void hideMessage();

	const not_null<PeerData*> _peer;
	MTP::Sender _api;
	MsgId _msgId = 0;

	object_ptr<Ui::FlatLabel> _text;

	mtpRequestId _requestId = 0;

};
