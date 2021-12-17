/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "facades.h"

#include "kotato/kotato_lang.h"
#include "api/api_bot.h"
#include "info/info_memento.h"
#include "core/click_handler_types.h"
#include "core/application.h"
#include "media/clip/media_clip_reader.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "history/history_item_components.h"
#include "base/platform/base_platform_info.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/url_auth_box.h"
#include "ui/layers/layer_widget.h"
#include "lang/lang_keys.h"
#include "base/observer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/media/history_view_media.h"
#include "payments/payments_checkout_process.h"
#include "data/data_session.h"
#include "styles/style_chat.h"

namespace {

[[nodiscard]] MainWidget *CheckMainWidget(not_null<Main::Session*> session) {
	if (const auto m = App::main()) { // multi good
		if (&m->session() == session) {
			return m;
		}
	}
	if (&Core::App().domain().active() != &session->account()) {
		Core::App().domain().activate(&session->account());
	}
	if (const auto m = App::main()) { // multi good
		if (&m->session() == session) {
			return m;
		}
	}
	return nullptr;
}

} // namespace

namespace App {

void hideSingleUseKeyboard(not_null<const HistoryItem*> message) {
	if (const auto m = CheckMainWidget(&message->history()->session())) {
		m->hideSingleUseKeyboard(message->history()->peer, message->id);
	}
}

bool insertBotCommand(const QString &cmd) {
	if (const auto m = App::main()) { // multi good
		return m->insertBotCommand(cmd);
	}
	return false;
}

void activateBotCommand(
		Window::SessionController *sessionController,
		not_null<const HistoryItem*> msg,
		int row,
		int column) {
	const auto button = HistoryMessageMarkupButton::Get(
		&msg->history()->owner(),
		msg->fullId(),
		row,
		column);
	if (!button) {
		return;
	}

	using ButtonType = HistoryMessageMarkupButton::Type;
	switch (button->type) {
	case ButtonType::Default: {
		// Copy string before passing it to the sending method
		// because the original button can be destroyed inside.
		if (sessionController) {
			MsgId replyTo = msg->isRegular() ? msg->id : 0;
			sessionController->content()->sendBotCommand({
				.peer = msg->history()->peer,
				.command = QString(button->text),
				.context = msg->fullId(),
				.replyTo = replyTo,
			});
		}
	} break;

	case ButtonType::Callback:
	case ButtonType::Game: {
		Api::SendBotCallbackData(
			const_cast<HistoryItem*>(msg.get()),
			row,
			column);
	} break;

	case ButtonType::CallbackWithPassword: {
		Api::SendBotCallbackDataWithPassword(
			const_cast<HistoryItem*>(msg.get()),
			row,
			column);
	} break;

	case ButtonType::Buy: {
		Payments::CheckoutProcess::Start(
			msg,
			Payments::Mode::Payment,
			crl::guard(App::wnd(), [] { App::wnd()->activate(); }));
	} break;

	case ButtonType::Url: {
		auto url = QString::fromUtf8(button->data);
		auto skipConfirmation = false;
		if (const auto bot = msg->getMessageBot()) {
			if (bot->isVerified()) {
				skipConfirmation = true;
			}
		}
		if (skipConfirmation) {
			UrlClickHandler::Open(url);
		} else {
			HiddenUrlClickHandler::Open(url);
		}
	} break;

	case ButtonType::RequestLocation: {
		hideSingleUseKeyboard(msg);
		Ui::show(Box<Ui::InformBox>(
			ktr("ktg_bot_share_location_unavailable")));
	} break;

	case ButtonType::RequestPhone: {
		hideSingleUseKeyboard(msg);
		const auto msgId = msg->id;
		const auto history = msg->history();
		Ui::show(Box<Ui::ConfirmBox>(
			tr::lng_bot_share_phone(tr::now),
			tr::lng_bot_share_phone_confirm(tr::now),
			[=] {
				Ui::showPeerHistory(history, ShowAtTheEndMsgId);
				auto action = Api::SendAction(history);
				action.clearDraft = false;
				action.replyTo = msgId;
				history->session().api().shareContact(
					history->session().user(),
					action);
			}));
	} break;

	case ButtonType::RequestPoll: {
		hideSingleUseKeyboard(msg);
		auto chosen = PollData::Flags();
		auto disabled = PollData::Flags();
		if (!button->data.isEmpty()) {
			disabled |= PollData::Flag::Quiz;
			if (button->data[0]) {
				chosen |= PollData::Flag::Quiz;
			}
		}
		if (const auto m = CheckMainWidget(&msg->history()->session())) {
			const auto replyToId = MsgId(0);
			Window::PeerMenuCreatePoll(
				m->controller(),
				msg->history()->peer,
				replyToId,
				chosen,
				disabled);
		}
	} break;

	case ButtonType::SwitchInlineSame:
	case ButtonType::SwitchInline: {
		const auto session = &msg->history()->session();
		if (const auto m = CheckMainWidget(session)) {
			if (const auto bot = msg->getMessageBot()) {
				const auto fastSwitchDone = [&] {
					auto samePeer = (button->type == ButtonType::SwitchInlineSame);
					if (samePeer) {
						Notify::switchInlineBotButtonReceived(session, QString::fromUtf8(button->data), bot, msg->id);
						return true;
					} else if (bot->isBot() && bot->botInfo->inlineReturnTo.key) {
						if (Notify::switchInlineBotButtonReceived(session, QString::fromUtf8(button->data))) {
							return true;
						}
					}
					return false;
				}();
				if (!fastSwitchDone) {
					m->inlineSwitchLayer('@' + bot->username + ' ' + QString::fromUtf8(button->data));
				}
			}
		}
	} break;

	case ButtonType::Auth:
		UrlAuthBox::Activate(msg, row, column);
		break;

	case ButtonType::UserProfile: {
		const auto session = &msg->history()->session();
		const auto userId = UserId(button->data.toULongLong());
		if (const auto user = session->data().userLoaded(userId)) {
			const auto &windows = session->windows();
			if (!windows.empty()) {
				windows.front()->showPeerInfo(user);
			}
		}
	} break;
	}
}

} // namespace App

namespace Ui {

void showPeerProfile(not_null<PeerData*> peer) {
	if (const auto window = App::wnd()) { // multi good
		if (const auto controller = window->sessionController()) {
			if (&controller->session() == &peer->session()) {
				controller->showPeerInfo(peer);
				return;
			}
		}
		if (&Core::App().domain().active() != &peer->session().account()) {
			Core::App().domain().activate(&peer->session().account());
		}
		if (const auto controller = window->sessionController()) {
			if (&controller->session() == &peer->session()) {
				controller->showPeerInfo(peer);
			}
		}
	}
}

void showPeerProfile(not_null<const History*> history) {
	showPeerProfile(history->peer);
}

void showChatsList(not_null<Main::Session*> session) {
	if (const auto m = CheckMainWidget(session)) {
		m->ui_showPeerHistory(
			0,
			::Window::SectionShow::Way::ClearStack,
			0);
	}
}

void showPeerHistory(not_null<const History*> history, MsgId msgId) {
	showPeerHistory(history->peer, msgId);
}

void showPeerHistory(not_null<const PeerData*> peer, MsgId msgId) {
	if (const auto m = CheckMainWidget(&peer->session())) {
		m->ui_showPeerHistory(
			peer->id,
			::Window::SectionShow::Way::ClearStack,
			msgId);
	}
}

bool skipPaintEvent(QWidget *widget, QPaintEvent *event) {
	if (auto w = App::wnd()) {
		if (w->contentOverlapped(widget, event)) {
			return true;
		}
	}
	return false;
}

} // namespace Ui

namespace Notify {

void showScheduledButtonChanged(not_null<Main::Session*> session) {
	if (const auto m = CheckMainWidget(session)) m->notify_showScheduledButtonChanged();
}

bool switchInlineBotButtonReceived(
		not_null<Main::Session*> session,
		const QString &query,
		UserData *samePeerBot,
		MsgId samePeerReplyTo) {
	if (const auto m = CheckMainWidget(session)) {
		return m->notify_switchInlineBotButtonReceived(
			query,
			samePeerBot,
			samePeerReplyTo);
	}
	return false;
}

} // namespace Notify
