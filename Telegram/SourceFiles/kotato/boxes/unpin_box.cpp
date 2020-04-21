/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/boxes/unpin_box.h"

#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "history/history_widget.h"
#include "ui/widgets/labels.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "observer_peer.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

UnpinMessageBox::UnpinMessageBox(
	QWidget*,
	not_null<PeerData*> peer)
: _peer(peer)
, _text(this, tr::lng_pinned_unpin_sure(tr::now), st::boxLabel) {
}

void UnpinMessageBox::prepare() {
	addLeftButton(tr::ktg_hide_pinned_message(), [this] { hideMessage(); });

	addButton(tr::lng_pinned_unpin(), [this] { unpinMessage(); });
	addButton(tr::lng_cancel(), [this] { closeBox(); });

	auto height = st::boxPadding.top() + _text->height() + st::boxPadding.bottom();
	setDimensions(st::boxWidth, height);
}

void UnpinMessageBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_text->moveToLeft(st::boxPadding.left(), st::boxPadding.top());
}

void UnpinMessageBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		unpinMessage();
	} else if (e->key() == Qt::Key_Backspace) {
		hideMessage();
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void UnpinMessageBox::unpinMessage() {
	if (_requestId) return;

	_requestId = MTP::send(
		MTPmessages_UpdatePinnedMessage(
			MTP_flags(0),
			_peer->input,
			MTP_int(0)),
		rpcDone(&UnpinMessageBox::unpinDone),
		rpcFail(&UnpinMessageBox::unpinFail));
}

void UnpinMessageBox::hideMessage() {
	if (_requestId) return;

	auto hidden = HistoryWidget::switchPinnedHidden(_peer, true);
	if (hidden) {
		Notify::peerUpdatedDelayed(
			_peer,
			Notify::PeerUpdate::Flag::PinnedMessageChanged);
	}
	Ui::hideLayer();
}

void UnpinMessageBox::unpinDone(const MTPUpdates &updates) {
	_peer->session().api().applyUpdates(updates);
	Ui::hideLayer();
}

bool UnpinMessageBox::unpinFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;
	Ui::hideLayer();
	return true;
}

