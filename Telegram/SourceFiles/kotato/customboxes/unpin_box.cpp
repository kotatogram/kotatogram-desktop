/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/customboxes/unpin_box.h"

#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "history/history_widget.h"
#include "ui/widgets/labels.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

UnpinMessageBox::UnpinMessageBox(
	QWidget*,
	not_null<PeerData*> peer,
	MsgId msgId)
: _peer(peer)
, _api(&peer->session().mtp())
, _msgId(msgId)
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

	//auto flags = MTPmessages_UpdatePinnedMessage::Flags(0);
	_requestId = _api.request(MTPmessages_UpdatePinnedMessage(
		MTP_flags(MTPmessages_UpdatePinnedMessage::Flag::f_unpin),
		_peer->input,
		MTP_int(_msgId)
	)).done([=](const MTPUpdates &result) {
		_peer->session().api().applyUpdates(result);
		Ui::hideLayer();
	}).fail([=](const RPCError &error) {
		Ui::hideLayer();
	}).send();
}

void UnpinMessageBox::hideMessage() {
	if (_requestId) return;

	auto hidden = HistoryWidget::switchPinnedHidden(_peer, true);
	if (hidden) {
		_peer->session().changes().peerUpdated(_peer, Data::PeerUpdate::Flag::PinnedMessages);
	}
	Ui::hideLayer();
}
