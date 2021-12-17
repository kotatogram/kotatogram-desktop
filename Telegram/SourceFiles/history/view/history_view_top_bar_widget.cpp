/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_top_bar_widget.h"

#include <rpl/combine.h>
#include <rpl/combine_previous.h>
#include "kotato/kotato_lang.h"
#include "history/history.h"
#include "history/view/history_view_send_action.h"
#include "boxes/add_contact_box.h"
#include "ui/boxes/confirm_box.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "storage/storage_shared_media.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "lang/lang_keys.h"
#include "core/shortcuts.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/effects/radial_animation.h"
#include "ui/toasts/common_toasts.h"
#include "ui/boxes/report_box.h" // Ui::ReportReason
#include "ui/text/text.h"
#include "ui/text/text_options.h"
#include "ui/special_buttons.h"
#include "ui/text/text_options.h"
#include "ui/unread_badge.h"
#include "ui/ui_utility.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "calls/calls_instance.h"
#include "data/data_peer_values.h"
#include "data/data_group_call.h" // GroupCall::input.
#include "data/data_folder.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "data/data_send_action.h"
#include "chat_helpers/emoji_interactions.h"
#include "base/unixtime.h"
#include "support/support_helper.h"
#include "apiwrap.h"
#include "api/api_chat_participants.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"

namespace HistoryView {
namespace {

constexpr auto kEmojiInteractionSeenDuration = 3 * crl::time(1000);

} // namespace

struct TopBarWidget::EmojiInteractionSeenAnimation {
	Ui::SendActionAnimation animation;
	Ui::Animations::Basic scheduler;
	Ui::Text::String text = { st::dialogsTextWidthMin };
	crl::time till = 0;
};

TopBarWidget::TopBarWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _clear(this, tr::lng_selected_clear(), st::ktgTopBarClearButton)
, _forward(this, tr::lng_selected_forward(), st::ktgTopBarActiveButton)
, _sendNow(this, tr::lng_selected_send_now(), st::ktgTopBarActiveButton)
, _delete(this, tr::lng_selected_delete(), st::ktgTopBarActiveButton)
, _back(this, st::ktgHistoryTopBarBack)
, _cancelChoose(this, st::topBarCloseChoose)
, _call(this, st::ktgTopBarCall)
, _groupCall(this, st::topBarGroupCall)
, _search(this, st::ktgTopBarSearch)
, _infoToggle(this, st::ktgTopBarInfo)
, _menuToggle(this, st::ktgTopBarMenuToggle)
, _titlePeerText(st::windowMinWidth / 3)
, _onlineUpdater([=] { updateOnlineDisplay(); }) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	Lang::Updated(
	) | rpl::start_with_next([=] {
		refreshLang();
	}, lifetime());

	_forward->setClickedCallback([=] { _forwardSelection.fire({}); });
	_forward->setWidthChangedCallback([=] { updateControlsGeometry(); });
	_sendNow->setClickedCallback([=] { _sendNowSelection.fire({}); });
	_sendNow->setWidthChangedCallback([=] { updateControlsGeometry(); });
	_delete->setClickedCallback([=] { _deleteSelection.fire({}); });
	_delete->setWidthChangedCallback([=] { updateControlsGeometry(); });
	_clear->setClickedCallback([=] { _clearSelection.fire({}); });
	_call->setClickedCallback([=] { call(); });
	_groupCall->setClickedCallback([=] { groupCall(); });
	_search->setClickedCallback([=] { search(); });
	_menuToggle->setClickedCallback([=] { showMenu(); });
	_infoToggle->setClickedCallback([=] { toggleInfoSection(); });
	_back->addClickHandler([=] { backClicked(); });
	_cancelChoose->setClickedCallback(
		[=] { _cancelChooseForReport.fire({}); });

	rpl::combine(
		_controller->activeChatValue(),
		_controller->searchInChat.value()
	) | rpl::combine_previous(
		std::make_tuple(Dialogs::Key(), Dialogs::Key())
	) | rpl::map([](
			const std::tuple<Dialogs::Key, Dialogs::Key> &previous,
			const std::tuple<Dialogs::Key, Dialogs::Key> &current) {
		const auto &active = std::get<0>(current);
		const auto &search = std::get<1>(current);
		const auto activeChanged = (active != std::get<0>(previous));
		const auto searchInChat = search && (active == search);
		return std::make_tuple(searchInChat, activeChanged);
	}) | rpl::start_with_next([=](
			bool searchInActiveChat,
			bool activeChanged) {
		auto animated = activeChanged
			? anim::type::instant
			: anim::type::normal;
		_search->setForceRippled(searchInActiveChat, animated);
	}, lifetime());

	controller->adaptive().changes(
	) | rpl::start_with_next([=] {
		updateAdaptiveLayout();
	}, lifetime());

	refreshUnreadBadge();
	{
		using AnimationUpdate = Data::SendActionManager::AnimationUpdate;
		session().data().sendActionManager().animationUpdated(
		) | rpl::filter([=](const AnimationUpdate &update) {
			return (update.history == _activeChat.key.history());
		}) | rpl::start_with_next([=] {
			update();
		}, lifetime());
	}

	using UpdateFlag = Data::PeerUpdate::Flag;
	session().changes().peerUpdates(
		UpdateFlag::HasCalls
		| UpdateFlag::OnlineStatus
		| UpdateFlag::Members
		| UpdateFlag::SupportInfo
		| UpdateFlag::Rights
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		if (update.flags & UpdateFlag::HasCalls) {
			if (update.peer->isUser()
				&& (update.peer->isSelf()
					|| _activeChat.key.peer() == update.peer)) {
				updateControlsVisibility();
			}
		} else if ((update.flags & UpdateFlag::Rights)
			&& (_activeChat.key.peer() == update.peer)) {
			updateControlsVisibility();
		}
		if (update.flags
			& (UpdateFlag::OnlineStatus
				| UpdateFlag::Members
				| UpdateFlag::SupportInfo)) {
			updateOnlineDisplay();
		}
	}, lifetime());

	session().serverConfig().phoneCallsEnabled.changes(
	) | rpl::start_with_next([=] {
		updateControlsVisibility();
	}, lifetime());

	rpl::combine(
		Core::App().settings().thirdSectionInfoEnabledValue(),
		Core::App().settings().tabbedReplacedWithInfoValue()
	) | rpl::start_with_next([=] {
		updateInfoToggleActive();
	}, lifetime());

	Core::App().settings().proxy().connectionTypeValue(
	) | rpl::start_with_next([=] {
		updateConnectingState();
	}, lifetime());

	setCursor(style::cur_pointer);
}

TopBarWidget::~TopBarWidget() = default;

Main::Session &TopBarWidget::session() const {
	return _controller->session();
}

void TopBarWidget::updateConnectingState() {
	const auto state = _controller->session().mtp().dcstate();
	if (state == MTP::ConnectedState) {
		if (_connecting) {
			_connecting = nullptr;
			update();
		}
	} else if (!_connecting) {
		_connecting = std::make_unique<Ui::InfiniteRadialAnimation>(
			[=] { connectingAnimationCallback(); },
			st::topBarConnectingAnimation);
		_connecting->start();
		update();
	}
}

void TopBarWidget::connectingAnimationCallback() {
	if (!anim::Disabled()) {
		update();
	}
}

void TopBarWidget::refreshLang() {
	InvokeQueued(this, [this] { updateControlsGeometry(); });
}

void TopBarWidget::search() {
	if (_activeChat.key) {
		_controller->content()->searchInChat(_activeChat.key);
	}
}

void TopBarWidget::call() {
	if (const auto peer = _activeChat.key.peer()) {
		if (const auto user = peer->asUser()) {
			if (cConfirmBeforeCall()) {
				Ui::show(Box<Ui::ConfirmBox>(ktr("ktg_call_sure"), ktr("ktg_call_button"), [=] {
					Ui::hideLayer();
					Core::App().calls().startOutgoingCall(user, false);
				}));
			} else {
				Core::App().calls().startOutgoingCall(user, false);
			}
		}
	}
}

void TopBarWidget::groupCall() {
	if (const auto peer = _activeChat.key.peer()) {
		_controller->startOrJoinGroupCall(peer);
	}
}

void TopBarWidget::showChooseMessagesForReport(Ui::ReportReason reason) {
	setChooseForReportReason(reason);
}

void TopBarWidget::clearChooseMessagesForReport() {
	setChooseForReportReason(std::nullopt);
}

void TopBarWidget::setChooseForReportReason(
		std::optional<Ui::ReportReason> reason) {
	if (_chooseForReportReason == reason) {
		return;
	}
	const auto wasNoReason = !_chooseForReportReason;
	_chooseForReportReason = reason;
	const auto nowNoReason = !_chooseForReportReason;
	updateControlsVisibility();
	updateControlsGeometry();
	update();
	if (wasNoReason != nowNoReason && showSelectedState()) {
		toggleSelectedControls(false);
		finishAnimating();
	}
	setCursor((nowNoReason && !showSelectedState())
		? style::cur_pointer
		: style::cur_default);
}

void TopBarWidget::showMenu() {
	if (!_activeChat.key || _menu) {
		return;
	}
	_menu.create(parentWidget());
	_menu->setHiddenCallback([weak = Ui::MakeWeak(this), menu = _menu.data()]{
		menu->deleteLater();
		if (weak && weak->_menu == menu) {
			weak->_menu = nullptr;
			weak->_menuToggle->setForceRippled(false);
		}
	});
	_menu->setShowStartCallback(crl::guard(this, [this, menu = _menu.data()]{
		if (_menu == menu) {
			_menuToggle->setForceRippled(true);
		}
	}));
	_menu->setHideStartCallback(crl::guard(this, [this, menu = _menu.data()]{
		if (_menu == menu) {
			_menuToggle->setForceRippled(false);
		}
	}));
	_menuToggle->installEventFilter(_menu);
	const auto addAction = [&](
		const QString &text,
		Fn<void()> callback) {
		return _menu->addAction(text, std::move(callback));
	};
	Window::FillDialogsEntryMenu(
		_controller,
		_activeChat,
		addAction);
	if (_menu->empty()) {
		_menu.destroy();
	} else {
		_menu->moveToRight((parentWidget()->width() - width()) + st::topBarMenuPosition.x(), st::topBarMenuPosition.y());
		_menu->showAnimated(Ui::PanelAnimation::Origin::TopRight);
	}
}

void TopBarWidget::toggleInfoSection() {
	const auto isThreeColumn = _controller->adaptive().isThreeColumn();
	if (isThreeColumn
		&& (Core::App().settings().thirdSectionInfoEnabled()
			|| Core::App().settings().tabbedReplacedWithInfo())) {
		_controller->closeThirdSection();
	} else if (_activeChat.key.peer()) {
		if (_controller->canShowThirdSection()) {
			Core::App().settings().setThirdSectionInfoEnabled(true);
			Core::App().saveSettingsDelayed();
			if (isThreeColumn) {
				_controller->showSection(
					Info::Memento::Default(_activeChat.key.peer()),
					Window::SectionShow().withThirdColumn());
			} else {
				_controller->resizeForThirdSection();
				_controller->updateColumnLayout();
			}
		} else {
			infoClicked();
		}
	} else {
		updateControlsVisibility();
	}
}

bool TopBarWidget::eventFilter(QObject *obj, QEvent *e) {
	if (obj == _membersShowArea) {
		switch (e->type()) {
		case QEvent::MouseButtonPress:
			mousePressEvent(static_cast<QMouseEvent*>(e));
			return true;

		case QEvent::Enter:
			_membersShowAreaActive.fire(true);
			break;

		case QEvent::Leave:
			_membersShowAreaActive.fire(false);
			break;
		}
	}
	return RpWidget::eventFilter(obj, e);
}

int TopBarWidget::resizeGetHeight(int newWidth) {
	return st::topBarHeight;
}

void TopBarWidget::paintEvent(QPaintEvent *e) {
	if (_animatingMode) {
		return;
	}
	Painter p(this);

	auto selectedButtonsTop = countSelectedButtonsTop(
		_selectedShown.value(showSelectedActions() ? 1. : 0.));

	p.fillRect(QRect(0, 0, width(), st::topBarHeight), st::ktgTopBarBg);
	if (selectedButtonsTop < 0) {
		p.translate(0, selectedButtonsTop + st::topBarHeight);
		paintTopBar(p);
	}
}

void TopBarWidget::paintTopBar(Painter &p) {
	if (!_activeChat.key) {
		return;
	}
	auto nameleft = _leftTaken;
	auto nametop = st::topBarArrowPadding.top();
	auto statustop = st::topBarHeight - st::topBarArrowPadding.bottom() - st::dialogsTextFont->height;
	auto availableWidth = width() - _rightTaken - nameleft;

	if (_chooseForReportReason) {
		const auto text = [&] {
			using Reason = Ui::ReportReason;
			switch (*_chooseForReportReason) {
			case Reason::Spam: return tr::lng_report_reason_spam(tr::now);
			case Reason::Violence:
				return tr::lng_report_reason_violence(tr::now);
			case Reason::ChildAbuse:
				return tr::lng_report_reason_child_abuse(tr::now);
			case Reason::Pornography:
				return tr::lng_report_reason_pornography(tr::now);
			}
			Unexpected("reason in TopBarWidget::paintTopBar.");
		}();
		p.setPen(st::dialogsNameFg);
		p.setFont(st::semiboldFont);
		p.drawTextLeft(nameleft, nametop, width(), text);

		p.setFont(st::dialogsTextFont);
		p.setPen(st::historyStatusFg);
		p.drawTextLeft(
			nameleft,
			statustop,
			width(),
			tr::lng_report_select_messages(tr::now));
		return;
	}

	const auto now = crl::now();
	const auto history = _activeChat.key.history();
	const auto folder = _activeChat.key.folder();
	if (folder || history->peer->sharedMediaInfo()) {
		// #TODO feed name emoji.
		auto text = (_activeChat.section == Section::Scheduled)
			? tr::lng_reminder_messages(tr::now)
			: folder
			? folder->chatListName()
			: history->peer->isSelf()
			? tr::lng_saved_messages(tr::now)
			: tr::lng_replies_messages(tr::now);
		const auto textWidth = st::historySavedFont->width(text);
		if (availableWidth < textWidth) {
			text = st::historySavedFont->elided(text, availableWidth);
		}
		p.setPen(st::ktgTopBarNameFg);
		p.setFont(st::historySavedFont);
		p.drawTextLeft(
			nameleft,
			(height() - st::historySavedFont->height) / 2,
			width(),
			text);
	} else if (_activeChat.section == Section::Replies
			|| _activeChat.section == Section::Scheduled
			|| _activeChat.section == Section::Pinned) {
		p.setPen(st::ktgTopBarNameFg);

		Ui::Text::String textStr;
		textStr.setText(
			st::semiboldTextStyle,
			(_activeChat.section == Section::Replies
				? tr::lng_manage_discussion_group(tr::now)
				: history->peer->isSelf()
				? tr::lng_saved_messages(tr::now)
				: history->peer->topBarNameText().toString()),
			Ui::NameTextOptions());
		textStr.drawElided(p, nameleft, nametop, width());

		p.setFont(st::dialogsTextFont);
		if (!paintConnectingState(p, nameleft, statustop, width())
			&& (_activeChat.section != Section::Replies
				|| !paintSendAction(
					p,
					nameleft,
					statustop,
					availableWidth,
					width(),
					st::ktgTopBarStatusFgActive,
					now))) {
			p.setPen(st::ktgTopBarStatusFg);
			p.drawTextLeft(nameleft, statustop, width(), _customTitleText);
		}
	} else if (const auto history = _activeChat.key.history()) {
		const auto peer = history->peer;
		const auto &text = peer->topBarNameText();
		const auto badgeStyle = Ui::PeerBadgeStyle{
			nullptr,
			&st::attentionButtonFg };
		const auto badgeWidth = Ui::DrawPeerBadgeGetWidth(
			peer,
			p,
			QRect(
				nameleft,
				nametop,
				availableWidth,
				st::msgNameStyle.font->height),
			text.maxWidth(),
			width(),
			badgeStyle);
		const auto namewidth = availableWidth - badgeWidth;

		p.setPen(st::ktgTopBarNameFg);
		peer->topBarNameText().drawElided(
			p,
			nameleft,
			nametop,
			namewidth);

		p.setFont(st::dialogsTextFont);
		if (!paintConnectingState(p, nameleft, statustop, width())
			&& !paintSendAction(
				p,
				nameleft,
				statustop,
				availableWidth,
				width(),
				st::ktgTopBarStatusFgActive,
				now)) {
			paintStatus(p, nameleft, statustop, availableWidth, width());
		}
	}
}

bool TopBarWidget::paintSendAction(
		Painter &p,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		style::color fg,
		crl::time now) {
	const auto seen = _emojiInteractionSeen.get();
	if (!seen || seen->till <= now) {
		return _sendAction->paint(p, x, y, availableWidth, outerWidth, fg, now);
	}
	const auto animationWidth = seen->animation.width();
	const auto extraAnimationWidth = animationWidth * 2;
	seen->animation.paint(
		p,
		fg,
		x,
		y + st::normalFont->ascent,
		outerWidth,
		now);

	x += animationWidth;
	availableWidth -= extraAnimationWidth;
	p.setPen(fg);
	seen->text.drawElided(p, x, y, availableWidth);
	return true;
}

bool TopBarWidget::paintConnectingState(
		Painter &p,
		int left,
		int top,
		int outerWidth) {
	if (!_connecting) {
		return false;
	}
	_connecting->draw(
		p,
		{
			st::topBarConnectingPosition.x() + left,
			st::topBarConnectingPosition.y() + top
		},
		outerWidth);
	left += st::topBarConnectingPosition.x()
		+ st::topBarConnectingAnimation.size.width()
		+ st::topBarConnectingSkip;
	p.setPen(st::ktgTopBarStatusFg);
	p.drawTextLeft(left, top, outerWidth, tr::lng_status_connecting(tr::now));
	return true;
}

void TopBarWidget::paintStatus(
		Painter &p,
		int left,
		int top,
		int availableWidth,
		int outerWidth) {
	p.setPen(_titlePeerTextOnline
		? st::ktgTopBarStatusFgActive
		: st::ktgTopBarStatusFg);
	_titlePeerText.drawLeftElided(p, left, top, availableWidth, outerWidth);
}

QRect TopBarWidget::getMembersShowAreaGeometry() const {
	int membersTextLeft = _leftTaken;
	int membersTextTop = st::topBarHeight - st::topBarArrowPadding.bottom() - st::dialogsTextFont->height;
	int membersTextWidth = _titlePeerText.maxWidth();
	int membersTextHeight = st::topBarHeight - membersTextTop;

	return myrtlrect(membersTextLeft, membersTextTop, membersTextWidth, membersTextHeight);
}

void TopBarWidget::mousePressEvent(QMouseEvent *e) {
	const auto handleClick = (e->button() == Qt::LeftButton)
		&& (e->pos().y() < st::topBarHeight)
		&& !showSelectedState()
		&& !_chooseForReportReason;
	if (handleClick) {
		if (_animatingMode && _back->rect().contains(e->pos())) {
			backClicked();
		} else  {
			infoClicked();
		}
	}
}

void TopBarWidget::infoClicked() {
	const auto key = _activeChat.key;
	if (!key) {
		return;
	} else if (key.folder()) {
		_controller->closeFolder();
	} else if (key.peer()->isSelf()) {
		_controller->showSection(std::make_shared<Info::Memento>(
			key.peer(),
			Info::Section(Storage::SharedMediaType::Photo)));
	} else if (key.peer()->isRepliesChat()) {
		_controller->showSection(std::make_shared<Info::Memento>(
			key.peer(),
			Info::Section(Storage::SharedMediaType::Photo)));
	} else {
		_controller->showPeerInfo(key.peer());
	}
}

void TopBarWidget::backClicked() {
	if (_activeChat.key.folder()) {
		_controller->closeFolder();
	} else {
		_controller->showBackFromStack();
	}
}

void TopBarWidget::setActiveChat(
		ActiveChat activeChat,
		SendActionPainter *sendAction) {
	if (_activeChat.key == activeChat.key
		&& _activeChat.section == activeChat.section) {
		_activeChat = activeChat;
		return;
	}
	const auto peerChanged = (_activeChat.key.history()
		!= activeChat.key.history());

	_activeChat = activeChat;
	_sendAction = sendAction;
	_titlePeerText.clear();
	_back->clearState();
	update();

	if (peerChanged) {
		_emojiInteractionSeen = nullptr;
		_activeChatLifetime.destroy();
		if (const auto history = _activeChat.key.history()) {
			session().changes().peerFlagsValue(
				history->peer,
				Data::PeerUpdate::Flag::GroupCall
			) | rpl::map([=] {
				return history->peer->groupCall();
			}) | rpl::distinct_until_changed(
			) | rpl::map([](Data::GroupCall *call) {
				return call ? call->fullCountValue() : rpl::single(-1);
			}) | rpl::flatten_latest(
			) | rpl::map([](int count) {
				return (count == 0);
			}) | rpl::distinct_until_changed(
			) | rpl::start_with_next([=] {
				updateControlsVisibility();
				updateControlsGeometry();
			}, _activeChatLifetime);

			using InteractionSeen = ChatHelpers::EmojiInteractionSeen;
			_controller->emojiInteractions().seen(
			) | rpl::filter([=](const InteractionSeen &seen) {
				return (seen.peer == history->peer);
			}) | rpl::start_with_next([=](const InteractionSeen &seen) {
				handleEmojiInteractionSeen(seen.emoticon);
			}, lifetime());
		}
	}
	updateUnreadBadge();
	refreshInfoButton();
	if (_menu) {
		_menuToggle->removeEventFilter(_menu);
		_menu->hideFast();
	}
	updateOnlineDisplay();
	updateControlsVisibility();
	refreshUnreadBadge();
}

void TopBarWidget::handleEmojiInteractionSeen(const QString &emoticon) {
	auto seen = _emojiInteractionSeen.get();
	if (!seen) {
		_emojiInteractionSeen
			= std::make_unique<EmojiInteractionSeenAnimation>();
		seen = _emojiInteractionSeen.get();
		seen->animation.start(Ui::SendActionAnimation::Type::ChooseSticker);
		seen->scheduler.init([=] {
			if (seen->till <= crl::now()) {
				crl::on_main(this, [=] {
					if (_emojiInteractionSeen
						&& _emojiInteractionSeen->till <= crl::now()) {
						_emojiInteractionSeen = nullptr;
						update();
					}
				});
			} else {
				const auto skip = st::topBarArrowPadding.bottom();
				update(
					_leftTaken,
					st::topBarHeight - skip - st::dialogsTextFont->height,
					seen->animation.width(),
					st::dialogsTextFont->height);
			}
		});
		seen->scheduler.start();
	}
	seen->till = crl::now() + kEmojiInteractionSeenDuration;
	seen->text.setText(
		st::dialogsTextStyle,
		tr::lng_user_action_watching_animations(tr::now, lt_emoji, emoticon),
		Ui::NameTextOptions());
	update();
}

void TopBarWidget::setCustomTitle(const QString &title) {
	if (_customTitleText != title) {
		_customTitleText = title;
		update();
	}
}

void TopBarWidget::refreshInfoButton() {
	if (const auto peer = _activeChat.key.peer()) {
		auto info = object_ptr<Ui::UserpicButton>(
			this,
			_controller,
			peer,
			Ui::UserpicButton::Role::Custom,
			st::topBarInfoButton);
		info->showSavedMessagesOnSelf(true);
		_info.destroy();
		_info = std::move(info);
	}
	if (_info) {
		_info->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
}

void TopBarWidget::resizeEvent(QResizeEvent *e) {
	updateSearchVisibility();
	updateControlsGeometry();
}

int TopBarWidget::countSelectedButtonsTop(float64 selectedShown) {
	return (1. - selectedShown) * (-st::topBarHeight);
}

void TopBarWidget::updateSearchVisibility() {
	const auto historyMode = (_activeChat.section == Section::History);
	const auto smallDialogsColumn = _activeChat.key.folder()
		&& (width() < _back->width() + _search->width());
	_search->setVisible(historyMode
		&& !smallDialogsColumn
		&& !_chooseForReportReason);
}

void TopBarWidget::updateControlsGeometry() {
	if (!_activeChat.key) {
		return;
	}
	auto hasSelected = showSelectedActions();
	auto selectedButtonsTop = countSelectedButtonsTop(_selectedShown.value(hasSelected ? 1. : 0.));
	auto otherButtonsTop = selectedButtonsTop + st::topBarHeight;
	auto buttonsLeft = st::topBarActionSkip
		+ (_controller->adaptive().isOneColumn() ? 0 : st::lineWidth);
	auto buttonsWidth = (_forward->isHidden() ? 0 : _forward->contentWidth())
		+ (_sendNow->isHidden() ? 0 : _sendNow->contentWidth())
		+ (_delete->isHidden() ? 0 : _delete->contentWidth())
		+ _clear->width();
	buttonsWidth += buttonsLeft + st::topBarActionSkip * 3;

	auto widthLeft = qMin(width() - buttonsWidth, -2 * st::defaultActiveButton.width);
	auto buttonFullWidth = qMin(-(widthLeft / 2), 0);
	_forward->setFullWidth(buttonFullWidth);
	_sendNow->setFullWidth(buttonFullWidth);
	_delete->setFullWidth(buttonFullWidth);

	selectedButtonsTop += (height() - _forward->height()) / 2;

	_forward->moveToLeft(buttonsLeft, selectedButtonsTop);
	if (!_forward->isHidden()) {
		buttonsLeft += _forward->width() + st::topBarActionSkip;
	}

	_sendNow->moveToLeft(buttonsLeft, selectedButtonsTop);
	if (!_sendNow->isHidden()) {
		buttonsLeft += _sendNow->width() + st::topBarActionSkip;
	}

	_delete->moveToLeft(buttonsLeft, selectedButtonsTop);
	_clear->moveToRight(st::topBarActionSkip, selectedButtonsTop);

	if (!_cancelChoose->isHidden()) {
		_leftTaken = 0;
		_cancelChoose->moveToLeft(_leftTaken, otherButtonsTop);
		_leftTaken += _cancelChoose->width();
	} else if (_back->isHidden()) {
		if (cShowTopBarUserpic()) {
			_leftTaken = st::topBarActionSkip;
		} else {
			_leftTaken = st::topBarArrowPadding.right();
		}
	} else {
		const auto smallDialogsColumn = _activeChat.key.folder()
			&& (width() < _back->width() + _search->width());
		_leftTaken = smallDialogsColumn ? (width() - _back->width()) / 2 : 0;
		_back->moveToLeft(_leftTaken, otherButtonsTop);
		_leftTaken += _back->width();
	}

	if (!_back->isHidden() || cShowTopBarUserpic()) {
		if (_info && !_info->isHidden()) {
			_info->moveToLeft(_leftTaken, otherButtonsTop);
			_leftTaken += _info->width();
		}
	}

	_rightTaken = 0;
	_menuToggle->moveToRight(_rightTaken, otherButtonsTop);
	if (_menuToggle->isHidden()) {
		_rightTaken += (_menuToggle->width() - _search->width());
	} else {
		_rightTaken += _menuToggle->width() + st::topBarSkip;
	}
	_infoToggle->moveToRight(_rightTaken, otherButtonsTop);
	if (!_infoToggle->isHidden()) {
		_infoToggle->moveToRight(_rightTaken, otherButtonsTop);
		_rightTaken += _infoToggle->width();
	}
	if (!_call->isHidden() || !_groupCall->isHidden()) {
		_call->moveToRight(_rightTaken, otherButtonsTop);
		_groupCall->moveToRight(_rightTaken, otherButtonsTop);
		_rightTaken += _call->width();
	}
	_search->moveToRight(_rightTaken, otherButtonsTop);
	_rightTaken += _search->width() + st::topBarCallSkip;

	updateMembersShowArea();
}

void TopBarWidget::finishAnimating() {
	_selectedShown.stop();
	updateControlsVisibility();
	update();
}

void TopBarWidget::setAnimatingMode(bool enabled) {
	if (_animatingMode != enabled) {
		_animatingMode = enabled;
		setAttribute(Qt::WA_OpaquePaintEvent, !_animatingMode);
		finishAnimating();
	}
}

void TopBarWidget::updateControlsVisibility() {
	if (!_activeChat.key) {
		return;
	} else if (_animatingMode) {
		hideChildren();
		return;
	}
	_clear->show();
	_delete->setVisible(_canDelete);
	_forward->setVisible(_canForward);
	_sendNow->setVisible(_canSendNow);

	const auto isOneColumn = _controller->adaptive().isOneColumn();
	auto backVisible = isOneColumn
		|| !_controller->content()->stackIsEmpty()
		|| _activeChat.key.folder();
	_back->setVisible(backVisible && !_chooseForReportReason);
	_cancelChoose->setVisible(_chooseForReportReason.has_value());
	if (_info) {
		_info->setVisible(cShowTopBarUserpic() || (isOneColumn && !_chooseForReportReason));
	}
	if (_unreadBadge) {
		_unreadBadge->setVisible(!_chooseForReportReason);
	}
	const auto section = _activeChat.section;
	const auto historyMode = (section == Section::History);
	const auto hasPollsMenu = _activeChat.key.peer()
		&& _activeChat.key.peer()->canSendPolls();
	const auto hasMenu = !_activeChat.key.folder()
		&& ((section == Section::Scheduled || section == Section::Replies)
			? hasPollsMenu
			: historyMode);
	updateSearchVisibility();
	_menuToggle->setVisible(hasMenu && !_chooseForReportReason);
	_infoToggle->setVisible(historyMode
		&& !_activeChat.key.folder()
		&& !isOneColumn
		&& _controller->canShowThirdSection()
		&& !_chooseForReportReason);
	const auto callsEnabled = [&] {
		if (const auto peer = _activeChat.key.peer()) {
			if (const auto user = peer->asUser()) {
				return !user->isSelf() && !user->isBot();
			}
		}
		return false;
	}();
	_call->setVisible(historyMode
		&& callsEnabled
		&& !_chooseForReportReason);
	const auto groupCallsEnabled = [&] {
		if (const auto peer = _activeChat.key.peer()) {
			if (peer->canManageGroupCall()) {
				return true;
			} else if (const auto call = peer->groupCall()) {
				return (call->fullCount() == 0);
			}
			return false;
		}
		return false;
	}();
	_groupCall->setVisible(historyMode
		&& groupCallsEnabled
		&& !_chooseForReportReason);

	if (_membersShowArea) {
		_membersShowArea->setVisible(!_chooseForReportReason);
	}
	updateControlsGeometry();
}

void TopBarWidget::updateMembersShowArea() {
	const auto membersShowAreaNeeded = [&] {
		const auto peer = _activeChat.key.peer();
		if (showSelectedState() || !peer) {
			return false;
		} else if (const auto chat = peer->asChat()) {
			return chat->amIn();
		} else if (const auto megagroup = peer->asMegagroup()) {
			return megagroup->canViewMembers()
				&& (megagroup->membersCount()
					< megagroup->session().serverConfig().chatSizeMax);
		}
		return false;
	}();
	if (!membersShowAreaNeeded) {
		if (_membersShowArea) {
			_membersShowAreaActive.fire(false);
			_membersShowArea.destroy();
		}
		return;
	} else if (!_membersShowArea) {
		_membersShowArea.create(this);
		_membersShowArea->show();
		_membersShowArea->installEventFilter(this);
	}
	_membersShowArea->setGeometry(getMembersShowAreaGeometry());
}

bool TopBarWidget::showSelectedState() const {
	return (_selectedCount > 0)
		&& (_canDelete || _canForward || _canSendNow);
}

void TopBarWidget::showSelected(SelectedState state) {
	auto canDelete = (state.count > 0 && state.count == state.canDeleteCount);
	auto canForward = (state.count > 0 && state.count == state.canForwardCount);
	auto canSendNow = (state.count > 0 && state.count == state.canSendNowCount);
	auto count = (!canDelete && !canForward && !canSendNow) ? 0 : state.count;
	if (_selectedCount == count
		&& _canDelete == canDelete
		&& _canForward == canForward
		&& _canSendNow == canSendNow) {
		return;
	}
	if (count == 0) {
		// Don't change the visible buttons if the selection is cancelled.
		canDelete = _canDelete;
		canForward = _canForward;
		canSendNow = _canSendNow;
	}

	const auto wasSelectedState = showSelectedState();
	const auto visibilityChanged = (_canDelete != canDelete)
		|| (_canForward != canForward)
		|| (_canSendNow != canSendNow);
	_selectedCount = count;
	_canDelete = canDelete;
	_canForward = canForward;
	_canSendNow = canSendNow;
	const auto nowSelectedState = showSelectedState();
	if (nowSelectedState) {
		_forward->setNumbersText(_selectedCount);
		_sendNow->setNumbersText(_selectedCount);
		_delete->setNumbersText(_selectedCount);
		if (!wasSelectedState) {
			_forward->finishNumbersAnimation();
			_sendNow->finishNumbersAnimation();
			_delete->finishNumbersAnimation();
		}
	}
	if (visibilityChanged) {
		updateControlsVisibility();
	}
	if (wasSelectedState != nowSelectedState && !_chooseForReportReason) {
		setCursor(nowSelectedState
			? style::cur_default
			: style::cur_pointer);

		updateMembersShowArea();
		toggleSelectedControls(nowSelectedState);
	} else {
		updateControlsGeometry();
	}
}

void TopBarWidget::toggleSelectedControls(bool shown) {
	_selectedShown.start(
		[this] { selectedShowCallback(); },
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		st::slideWrapDuration,
		anim::easeOutCirc);
}

bool TopBarWidget::showSelectedActions() const {
	return showSelectedState() && !_chooseForReportReason;
}

void TopBarWidget::selectedShowCallback() {
	updateControlsGeometry();
	update();
}

void TopBarWidget::updateAdaptiveLayout() {
	updateControlsVisibility();
	updateInfoToggleActive();
	refreshUnreadBadge();
}

void TopBarWidget::refreshUnreadBadge() {
	if (!_controller->adaptive().isOneColumn() && !_activeChat.key.folder()) {
		_unreadBadge.destroy();
		return;
	} else if (_unreadBadge) {
		return;
	}
	_unreadBadge.create(this);

	rpl::combine(
		_back->geometryValue(),
		_unreadBadge->widthValue()
	) | rpl::start_with_next([=](QRect geometry, int width) {
		_unreadBadge->move(
			geometry.x() + geometry.width() - width,
			geometry.y() + st::titleUnreadCounterTop);
	}, _unreadBadge->lifetime());

	_unreadBadge->show();
	_unreadBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
	_controller->session().data().unreadBadgeChanges(
	) | rpl::start_with_next([=] {
		updateUnreadBadge();
	}, _unreadBadge->lifetime());
	updateUnreadBadge();
}

void TopBarWidget::updateUnreadBadge() {
	if (!_unreadBadge) return;

	const auto key = _activeChat.key;
	const auto muted = session().data().unreadBadgeMutedIgnoreOne(key);
	const auto counter = session().data().unreadBadgeIgnoreOne(key);
	const auto text = [&] {
		if (!counter) {
			return QString();
		}
		return (counter > 999)
			? qsl("..%1").arg(counter % 100, 2, 10, QChar('0'))
			: QString::number(counter);
	}();
	_unreadBadge->setText(text, !muted);
}

void TopBarWidget::updateInfoToggleActive() {
	auto infoThirdActive = _controller->adaptive().isThreeColumn()
		&& (Core::App().settings().thirdSectionInfoEnabled()
			|| Core::App().settings().tabbedReplacedWithInfo());
	auto iconOverride = infoThirdActive
		? &st::ktgTopBarInfoActive
		: nullptr;
	auto rippleOverride = infoThirdActive
		? &st::ktgTopBarIconBgActiveRipple
		: nullptr;
	_infoToggle->setIconOverride(iconOverride, iconOverride);
	_infoToggle->setRippleColorOverride(rippleOverride);
}

void TopBarWidget::updateOnlineDisplay() {
	const auto peer = _activeChat.key.peer();
	if (!peer) {
		return;
	}

	QString text;
	const auto now = base::unixtime::now();
	bool titlePeerTextOnline = false;
	if (const auto user = peer->asUser()) {
		if (session().supportMode()
			&& !session().supportHelper().infoCurrent(user).text.empty()) {
			text = QString::fromUtf8("\xe2\x9a\xa0\xef\xb8\x8f check info");
			titlePeerTextOnline = false;
		} else {
			text = Data::OnlineText(user, now);
			titlePeerTextOnline = Data::OnlineTextActive(user, now);
		}
	} else if (const auto chat = peer->asChat()) {
		if (!chat->amIn()) {
			text = tr::lng_chat_status_unaccessible(tr::now);
		} else if (chat->participants.empty()) {
			if (!_titlePeerText.isEmpty()) {
				text = _titlePeerText.toString();
			} else if (chat->count <= 0) {
				text = tr::lng_group_status(tr::now);
			} else {
				text = tr::lng_chat_status_members(tr::now, lt_count_decimal, chat->count);
			}
		} else {
			const auto self = session().user();
			auto online = 0;
			auto onlyMe = true;
			for (const auto &user : chat->participants) {
				if (user->onlineTill > now) {
					++online;
					if (onlyMe && user != self) onlyMe = false;
				}
			}
			if (online > 0 && !onlyMe) {
				auto membersCount = tr::lng_chat_status_members(tr::now, lt_count_decimal, chat->participants.size());
				auto onlineCount = tr::lng_chat_status_online(tr::now, lt_count, online);
				text = tr::lng_chat_status_members_online(tr::now, lt_members_count, membersCount, lt_online_count, onlineCount);
			} else if (chat->participants.size() > 0) {
				text = tr::lng_chat_status_members(tr::now, lt_count_decimal, chat->participants.size());
			} else {
				text = tr::lng_group_status(tr::now);
			}
		}
	} else if (const auto channel = peer->asChannel()) {
		if (channel->isMegagroup()
			&& (channel->membersCount() > 0)
			&& (channel->membersCount()
				<= channel->session().serverConfig().chatSizeMax)) {
			if (channel->lastParticipantsRequestNeeded()) {
				session().api().chatParticipants().requestLast(channel);
			}
			const auto self = session().user();
			auto online = 0;
			auto onlyMe = true;
			for (auto &participant : std::as_const(channel->mgInfo->lastParticipants)) {
				if (participant->onlineTill > now) {
					++online;
					if (onlyMe && participant != self) {
						onlyMe = false;
					}
				}
			}
			if (online && !onlyMe) {
				auto membersCount = tr::lng_chat_status_members(tr::now, lt_count_decimal, channel->membersCount());
				auto onlineCount = tr::lng_chat_status_online(tr::now, lt_count, online);
				text = tr::lng_chat_status_members_online(tr::now, lt_members_count, membersCount, lt_online_count, onlineCount);
			} else if (channel->membersCount() > 0) {
				text = tr::lng_chat_status_members(tr::now, lt_count_decimal, channel->membersCount());
			} else {
				text = tr::lng_group_status(tr::now);
			}
		} else if (channel->membersCount() > 0) {
			text = channel->isMegagroup()
				? tr::lng_chat_status_members(tr::now, lt_count_decimal, channel->membersCount())
				: tr::lng_chat_status_subscribers(tr::now, lt_count_decimal, channel->membersCount());

		} else {
			text = channel->isMegagroup() ? tr::lng_group_status(tr::now) : tr::lng_channel_status(tr::now);
		}
	}
	if (_titlePeerText.toString() != text) {
		_titlePeerText.setText(st::dialogsTextStyle, text);
		_titlePeerTextOnline = titlePeerTextOnline;
		updateMembersShowArea();
		update();
	}
	updateOnlineDisplayTimer();
}

void TopBarWidget::updateOnlineDisplayTimer() {
	const auto peer = _activeChat.key.peer();
	if (!peer) {
		return;
	}

	const auto now = base::unixtime::now();
	auto minTimeout = crl::time(86400);
	const auto handleUser = [&](not_null<UserData*> user) {
		auto hisTimeout = Data::OnlineChangeTimeout(user, now);
		accumulate_min(minTimeout, hisTimeout);
	};
	if (const auto user = peer->asUser()) {
		handleUser(user);
	} else if (const auto chat = peer->asChat()) {
		for (const auto &user : chat->participants) {
			handleUser(user);
		}
	} else if (peer->isChannel()) {
	}
	updateOnlineDisplayIn(minTimeout);
}

void TopBarWidget::updateOnlineDisplayIn(crl::time timeout) {
	_onlineUpdater.callOnce(timeout);
}

} // namespace HistoryView
