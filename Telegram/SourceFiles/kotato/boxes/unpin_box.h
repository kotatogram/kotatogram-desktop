/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "mtproto/mtproto_rpc_sender.h"

namespace Ui {
class FlatLabel;
} // namespace Ui

class UnpinMessageBox : public Ui::BoxContent, public RPCSender {
public:
	UnpinMessageBox(QWidget*, not_null<PeerData*> peer);

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void unpinMessage();
	void hideMessage();
	void unpinDone(const MTPUpdates &updates);
	bool unpinFail(const RPCError &error);

	not_null<PeerData*> _peer;

	object_ptr<Ui::FlatLabel> _text;

	mtpRequestId _requestId = 0;

};
