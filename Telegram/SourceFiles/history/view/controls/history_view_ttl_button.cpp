/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_ttl_button.h"

#include "data/data_peer.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_changes.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "ui/boxes/auto_delete_settings.h"
#include "ui/toast/toast.h"
#include "ui/toasts/common_toasts.h"
#include "ui/text/text_utilities.h"
#include "apiwrap.h"
#include "styles/style_chat.h"

namespace HistoryView::Controls {
namespace {

constexpr auto kToastDuration = crl::time(3500);

enum {
	TTLIcon1 = 0,
	TTLIcon1Over,
	TTLIcon2,
	TTLIcon2Over,
	TTLIcon3,
	TTLIcon3Over
};

QList<const style::icon *> defaultLangTTLIcons = {
	nullptr,
	nullptr,
	&st::historyMessagesTTL2Icon,
	&st::historyMessagesTTL2IconOver,
	&st::historyMessagesTTL3Icon,
	&st::historyMessagesTTL3IconOver
};

QList<const style::icon *> ruLangTTLIcons = {
	&st::historyMessagesTTLRUIcon,
	&st::historyMessagesTTLRUIconOver,
	&st::historyMessagesTTLRU2Icon,
	&st::historyMessagesTTLRU2IconOver,
	&st::historyMessagesTTLRU3Icon,
	&st::historyMessagesTTLRU3IconOver
};

const QList<const style::icon *> &TTLIconsList() {
	const auto baseLang = Lang::GetInstance().baseId();
	const auto currentLang = Lang::Id();

	for (const auto language : { "ru", "uk", "be" }) {
		if (baseLang.startsWith(QLatin1String(language)) || currentLang == QString(language)) {
			return ruLangTTLIcons;
		}
	}

	return defaultLangTTLIcons;
}

} // namespace

void ShowAutoDeleteToast(not_null<PeerData*> peer) {
	const auto period = peer->messagesTTL();
	if (!period) {
		Ui::Toast::Show(tr::lng_ttl_about_tooltip_off(tr::now));
		return;
	}

	const auto duration = (period == 5)
		? u"5 seconds"_q
		: (period < 2 * 86400)
		? tr::lng_ttl_about_duration1(tr::now)
		: (period < 8 * 86400)
		? tr::lng_ttl_about_duration2(tr::now)
		: tr::lng_ttl_about_duration3(tr::now);
	const auto text = peer->isBroadcast()
		? tr::lng_ttl_about_tooltip_channel(tr::now, lt_duration, duration)
		: tr::lng_ttl_about_tooltip(tr::now, lt_duration, duration);
	Ui::ShowMultilineToast({
		.text = { text },
		.duration = kToastDuration,
	});
}

void AutoDeleteSettingsBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer) {
	struct State {
		TimeId savingPeriod = 0;
		mtpRequestId savingRequestId = 0;
		QPointer<Ui::GenericBox> weak;
	};
	const auto state = std::make_shared<State>(State{ .weak = box.get() });
	auto callback = [=](TimeId period) {
		auto &api = peer->session().api();
		if (state->savingRequestId) {
			if (period == state->savingPeriod) {
				return;
			}
			api.request(state->savingRequestId).cancel();
		}
		state->savingPeriod = period;
		state->savingRequestId = api.request(MTPmessages_SetHistoryTTL(
			peer->input,
			MTP_int(period)
		)).done([=](const MTPUpdates &result) {
			peer->session().api().applyUpdates(result);
			ShowAutoDeleteToast(peer);
			if (const auto strong = state->weak.data()) {
				strong->closeBox();
			}
		}).fail([=] {
			state->savingRequestId = 0;
		}).send();
	};
	Ui::AutoDeleteSettingsBox(
		box,
		peer->messagesTTL(),
		(peer->isUser()
			? tr::lng_ttl_edit_about(lt_user, rpl::single(peer->shortName()))
			: peer->isBroadcast()
			? tr::lng_ttl_edit_about_channel()
			: tr::lng_ttl_edit_about_group()),
		std::move(callback));
}

TTLButton::TTLButton(not_null<QWidget*> parent, not_null<PeerData*> peer)
: _peer(peer)
, _button(parent, st::historyMessagesTTL) {
	const auto iconsList = TTLIconsList();
	_button.setIconOverride(iconsList[TTLIcon1], iconsList[TTLIcon1Over]);

	_button.setClickedCallback([=] {
		const auto canEdit = peer->isUser()
			|| (peer->isChat()
				&& peer->asChat()->canDeleteMessages())
			|| (peer->isChannel()
				&& peer->asChannel()->canDeleteMessages());
		if (!canEdit) {
			ShowAutoDeleteToast(peer);
			return;
		}
		Ui::show(
			Box(AutoDeleteSettingsBox, peer),
			Ui::LayerOption(0));
	});
	peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::MessagesTTL
	) | rpl::start_with_next([=] {
		const auto ttl = peer->messagesTTL();
		if (ttl < 2 * 86400) {
			_button.setIconOverride(iconsList[TTLIcon1], iconsList[TTLIcon1Over]);
		} else if (ttl < 8 * 86400) {
			_button.setIconOverride(iconsList[TTLIcon2], iconsList[TTLIcon2Over]);
		} else {
			_button.setIconOverride(iconsList[TTLIcon3], iconsList[TTLIcon3Over]);
		}
	}, _button.lifetime());
}

void TTLButton::show() {
	_button.show();
}

void TTLButton::hide() {
	_button.hide();
}

void TTLButton::move(int x, int y) {
	_button.move(x, y);
}

int TTLButton::width() const {
	return _button.width();
}

} // namespace HistoryView::Controls
