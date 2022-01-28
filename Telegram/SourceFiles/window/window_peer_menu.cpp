/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_peer_menu.h"

#include "kotato/kotato_lang.h"
#include "api/api_chat_participants.h"
#include "lang/lang_keys.h"
#include "boxes/delete_messages_box.h"
#include "boxes/max_invite_box.h"
#include "boxes/mute_settings_box.h"
#include "boxes/add_contact_box.h"
#include "boxes/create_poll_box.h"
#include "boxes/pin_messages_box.h"
#include "boxes/peers/add_participants_box.h"
#include "boxes/peers/edit_contact_box.h"
#include "boxes/share_box.h"
#include "kotato/boxes/kotato_unpin_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/report_box.h"
#include "ui/toast/toast.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/layers/generic_box.h"
#include "ui/toasts/common_toasts.h"
#include "core/application.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "api/api_blocked_peers.h"
#include "api/api_chat_filters.h"
#include "api/api_polls.h"
#include "api/api_sending.h"
#include "api/api_updates.h"
#include "api/api_text_entities.h"
#include "mtproto/mtproto_config.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_message.h" // GetErrorTextForSending.
#include "history/history_widget.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_context_menu.h"
#include "window/window_adaptive.h" // Adaptive::isThreeColumn
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "support/support_helper.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "info/profile/info_profile_values.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_poll.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_drafts.h"
#include "data/data_user.h"
#include "data/data_game.h"
#include "data/data_web_page.h"
#include "data/data_scheduled_messages.h"
#include "data/data_histories.h"
#include "data/data_chat_filters.h"
#include "data/data_file_origin.h"
#include "dialogs/dialogs_key.h"
#include "core/application.h"
#include "export/export_manager.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_window.h" // st::windowMinWidth
#include "styles/style_menu_icons.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QAction>

namespace Window {
namespace {

constexpr auto kArchivedToastDuration = crl::time(5000);
constexpr auto kMaxUnreadWithoutConfirmation = 10000;

void SetActionText(not_null<QAction*> action, rpl::producer<QString> &&text) {
	const auto lifetime = Ui::CreateChild<rpl::lifetime>(action.get());
	std::move(
		text
	) | rpl::start_with_next([=](const QString &actionText) {
		action->setText(actionText);
	}, *lifetime);
}

[[nodiscard]] bool IsUnreadHistory(not_null<History*> history) {
	return (history->chatListUnreadCount() > 0)
		|| (history->chatListUnreadMark());
}

void MarkAsReadHistory(not_null<History*> history) {
	const auto read = [&](not_null<History*> history) {
		if (IsUnreadHistory(history)) {
			history->peer->owner().histories().readInbox(history);
		}
	};
	read(history);
	if (const auto migrated = history->migrateSibling()) {
		read(migrated);
	}
}

void MarkAsReadChatList(not_null<Dialogs::MainList*> list) {
	auto mark = std::vector<not_null<History*>>();
	for (const auto &row : list->indexed()->all()) {
		if (const auto history = row->history()) {
			mark.push_back(history);
		}
	}
	ranges::for_each(mark, MarkAsReadHistory);
}

class Filler {
public:
	Filler(
		not_null<SessionController*> controller,
		Dialogs::EntryState request,
		const PeerMenuCallback &addAction);
	void fill();

private:
	using Section = Dialogs::EntryState::Section;

	void fillChatsListActions();
	void fillHistoryActions();
	void fillProfileActions();
	void fillRepliesActions();
	void fillScheduledActions();
	void fillArchiveActions();

	void addHidePromotion();
	void addTogglePin();
	void addToggleMute();
	void addSupportInfo();
	void addInfo();
	//void addToFolder();
	void addToggleUnreadMark();
	void addToggleArchive();
	void addClearHistory();
	void addDeleteChat();
	void addLeaveChat();
	void addManageChat();
	void addCreatePoll();
	void addThemeEdit();
	void addBlockUser();
	void addViewDiscussion();
	void addExportChat();
	void addReport();
	void addNewContact();
	void addShareContact();
	void addEditContact();
	void addBotToGroup();
	void addNewMembers();
	void addDeleteContact();
	void addHidePin();

	not_null<SessionController*> _controller;
	Dialogs::EntryState _request;
	PeerData *_peer = nullptr;
	Data::Folder *_folder = nullptr;
	const PeerMenuCallback &_addAction;

};

History *FindWastedPin(not_null<Data::Session*> data, Data::Folder *folder) {
	const auto &order = data->pinnedChatsOrder(folder, FilterId());
	for (const auto &pinned : order) {
		if (const auto history = pinned.history()) {
			if (history->peer->isChat()
				&& history->peer->asChat()->isDeactivated()
				&& !history->inChatList()) {
				return history;
			}
		}
	}
	return nullptr;
}

void AddChatMembers(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChatData*> chat) {
	AddParticipantsBoxController::Start(navigation, chat);
}

bool PinnedLimitReached(Dialogs::Key key, FilterId filterId) {
	Expects(filterId != 0 || key.entry()->folderKnown());

	const auto entry = key.entry();
	const auto owner = &entry->owner();
	const auto folder = entry->folder();
	const auto pinnedCount = owner->pinnedChatsCount(folder, filterId);
	const auto pinnedMax = owner->pinnedChatsLimit(folder, filterId);
	if (pinnedCount < pinnedMax) {
		return false;
	}
	// Some old chat, that was converted, maybe is still pinned.
	const auto wasted = filterId ? nullptr : FindWastedPin(owner, folder);
	if (wasted) {
		owner->setChatPinned(wasted, FilterId(), false);
		owner->setChatPinned(key, FilterId(), true);
		entry->session().api().savePinnedOrder(folder);
	} else {
		const auto errorText = filterId
			? tr::lng_filters_error_pinned_max(tr::now)
			: tr::lng_error_pinned_max(
				tr::now,
				lt_count,
				pinnedMax);
		Ui::show(Box<Ui::InformBox>(errorText));
	}
	return true;
}

void TogglePinnedDialog(
		not_null<Window::SessionController*> controller,
		Dialogs::Key key) {
	if (!key.entry()->folderKnown()) {
		return;
	}
	const auto owner = &key.entry()->owner();
	const auto isPinned = !key.entry()->isPinnedDialog(0);
	if (isPinned && PinnedLimitReached(key, 0)) {
		return;
	}

	owner->setChatPinned(key, FilterId(), isPinned);
	const auto flags = isPinned
		? MTPmessages_ToggleDialogPin::Flag::f_pinned
		: MTPmessages_ToggleDialogPin::Flag(0);
	if (const auto history = key.history()) {
		history->session().api().request(MTPmessages_ToggleDialogPin(
			MTP_flags(flags),
			MTP_inputDialogPeer(key.history()->peer->input)
		)).done([=] {
			owner->notifyPinnedDialogsOrderUpdated();
		}).send();
	} else if (const auto folder = key.folder()) {
		folder->session().api().request(MTPmessages_ToggleDialogPin(
			MTP_flags(flags),
			MTP_inputDialogPeerFolder(MTP_int(folder->id()))
		)).send();
	}
	if (isPinned) {
		controller->content()->dialogsToUp();
	}
}

void TogglePinnedDialog(
		not_null<Window::SessionController*> controller,
		Dialogs::Key key,
		FilterId filterId) {
	if (!filterId) {
		return TogglePinnedDialog(controller, key);
	}
	const auto owner = &key.entry()->owner();

	// This can happen when you remove this filter from another client.
	if (!ranges::contains(
		(&owner->session())->data().chatsFilters().list(),
		filterId,
		&Data::ChatFilter::id)) {
		Ui::Toast::Show(tr::lng_cant_do_this(tr::now));
		return;
	}

	const auto isPinned = !key.entry()->isPinnedDialog(filterId);
	if (isPinned && PinnedLimitReached(key, filterId)) {
		return;
	}

	owner->setChatPinned(key, filterId, isPinned);
	Api::SaveNewFilterPinned(&owner->session(), filterId);
	if (isPinned) {
		controller->content()->dialogsToUp();
	}
}

Filler::Filler(
	not_null<SessionController*> controller,
	Dialogs::EntryState request,
	const PeerMenuCallback &addAction)
: _controller(controller)
, _request(request)
, _peer(request.key.peer())
, _folder(request.key.folder())
, _addAction(addAction) {
}

void Filler::addHidePromotion() {
	const auto history = _peer->owner().historyLoaded(_peer);
	if (!history
		|| !history->useTopPromotion()
		|| history->topPromotionType().isEmpty()) {
		return;
	}
	_addAction(tr::lng_context_hide_psa(tr::now), [=] {
		history->cacheTopPromotion(false, QString(), QString());
		history->session().api().request(MTPhelp_HidePromoData(
			history->peer->input
		)).send();
	}, &st::menuIconRemove);
}

void Filler::addTogglePin() {
	const auto controller = _controller;
	const auto filterId = _request.filterId;
	const auto peer = _peer;
	const auto history = peer->owner().historyLoaded(peer);
	if (!history || history->fixedOnTopIndex()) {
		return;
	}
	const auto pinText = [=] {
		return history->isPinnedDialog(filterId)
			? tr::lng_context_unpin_from_top(tr::now)
			: tr::lng_context_pin_to_top(tr::now);
	};
	const auto pinToggle = [=] {
		TogglePinnedDialog(controller, history, filterId);
	};
	const auto pinAction = _addAction(
		pinText(),
		pinToggle,
		(history->isPinnedDialog(filterId)
			? &st::menuIconUnpin
			: &st::menuIconPin));

	auto actionText = history->session().changes().historyUpdates(
		history,
		Data::HistoryUpdate::Flag::IsPinned
	) | rpl::map(pinText);
	SetActionText(pinAction, std::move(actionText));
}

void Filler::addToggleMute() {
	if (_peer->isSelf()) {
		return;
	}
	PeerMenuAddMuteAction(_peer, _addAction);
}

void Filler::addSupportInfo() {
	if (!_peer->session().supportMode()) {
		return;
	}
	const auto user = _peer->asUser();
	if (!user) {
		return;
	}
	const auto controller = _controller;
	_addAction("Edit support info", [=] {
		user->session().supportHelper().editInfo(controller, user);
	}, &st::menuIconEdit);
}

void Filler::addInfo() {
	if (_peer->isSelf() || _peer->isRepliesChat()) {
		return;
	} else if (_controller->adaptive().isThreeColumn()) {
		if (Core::App().settings().thirdSectionInfoEnabled()
			|| Core::App().settings().tabbedReplacedWithInfo()) {
			return;
		}
	}
	const auto controller = _controller;
	const auto peer = _peer;
	const auto text = (peer->isChat() || peer->isMegagroup())
		? tr::lng_context_view_group(tr::now)
		: (peer->isUser()
			? tr::lng_context_view_profile(tr::now)
			: tr::lng_context_view_channel(tr::now));
	_addAction(text, [=] {
		controller->showPeerInfo(peer);
	}, peer->isUser() ? &st::menuIconProfile : &st::menuIconInfo);
}

//void Filler::addToFolder() {
//}

void Filler::addToggleUnreadMark() {
	const auto peer = _peer;
	const auto history = peer->owner().history(peer);
	const auto label = [=] {
		return IsUnreadHistory(history)
			? tr::lng_context_mark_read(tr::now)
			: tr::lng_context_mark_unread(tr::now);
	};
	auto action = _addAction(label(), [=] {
		const auto markAsRead = IsUnreadHistory(history);
		if (markAsRead) {
			MarkAsReadHistory(history);
		} else {
			peer->owner().histories().changeDialogUnreadMark(
				history,
				!markAsRead);
		}
	}, (IsUnreadHistory(history)
		? &st::menuIconMarkRead
		: &st::menuIconMarkUnread));

	auto actionText = history->session().changes().historyUpdates(
		history,
		Data::HistoryUpdate::Flag::UnreadView
	) | rpl::map(label);
	SetActionText(action, std::move(actionText));
}

void Filler::addToggleArchive() {
	const auto peer = _peer;
	const auto history = peer->owner().historyLoaded(peer);
	if (history && history->useTopPromotion()) {
		return;
	} else if (peer->isNotificationsUser() || peer->isSelf()) {
		if (!history || !history->folder()) {
			return;
		}
	}
	const auto isArchived = [=] {
		return (history->folder() != nullptr);
	};
	const auto label = [=] {
		return isArchived()
			? tr::lng_archived_remove(tr::now)
			: tr::lng_archived_add(tr::now);
	};
	const auto toggle = [=] {
		ToggleHistoryArchived(history, !isArchived());
	};
	const auto archiveAction = _addAction(
		label(),
		toggle,
		isArchived() ? &st::menuIconUnarchive : &st::menuIconArchive);

	auto actionText = history->session().changes().historyUpdates(
		history,
		Data::HistoryUpdate::Flag::Folder
	) | rpl::map(label);
	SetActionText(archiveAction, std::move(actionText));
}

void Filler::addClearHistory() {
	const auto channel = _peer->asChannel();
	const auto isGroup = _peer->isChat() || _peer->isMegagroup();
	if (channel) {
		if (!channel->amIn()) {
			return;
		} else if (!channel->canDeleteMessages()
			&& (!isGroup || channel->isPublic())) {
			return;
		}
	}
	_addAction(
		tr::lng_profile_clear_history(tr::now),
		ClearHistoryHandler(_peer),
		&st::menuIconClear);
}

void Filler::addDeleteChat() {
	if (_peer->isChannel()) {
		return;
	}
	_addAction(
		(_peer->isUser()
			? tr::lng_profile_delete_conversation(tr::now)
			: tr::lng_profile_clear_and_exit(tr::now)),
		DeleteAndLeaveHandler(_peer),
		&st::menuIconDelete);
}

void Filler::addLeaveChat() {
	const auto channel = _peer->asChannel();
	if (!channel || !channel->amIn()) {
		return;
	}
	_addAction(
		(_peer->isMegagroup()
			? tr::lng_profile_leave_group(tr::now)
			: tr::lng_profile_leave_channel(tr::now)),
		DeleteAndLeaveHandler(_peer),
		&st::menuIconLeave);
}

void Filler::addBlockUser() {
	const auto user = _peer->asUser();
	if (!user
		|| user->isInaccessible()
		|| user->isSelf()
		|| user->isRepliesChat()) {
		return;
	}
	const auto window = &_controller->window();
	const auto blockText = [](not_null<UserData*> user) {
		return user->isBlocked()
			? ((user->isBot() && !user->isSupport())
				? tr::lng_profile_restart_bot(tr::now)
				: tr::lng_profile_unblock_user(tr::now))
			: ((user->isBot() && !user->isSupport())
				? tr::lng_profile_block_bot(tr::now)
				: tr::lng_profile_block_user(tr::now));
	};
	const auto blockAction = _addAction(blockText(user), [=] {
		if (user->isBlocked()) {
			PeerMenuUnblockUserWithBotRestart(user);
		} else if (user->isBot()) {
			user->session().api().blockedPeers().block(user);
		} else {
			window->show(Box(
				PeerMenuBlockUserBox,
				window,
				user,
				v::null,
				v::null));
		}
	}, (!user->isBlocked()
		? &st::menuIconBlock
		: user->isBot()
		? &st::menuIconRestartBot
		: &st::menuIconUnblock));

	auto actionText = _peer->session().changes().peerUpdates(
		_peer,
		Data::PeerUpdate::Flag::IsBlocked
	) | rpl::map([=] { return blockText(user); });
	SetActionText(blockAction, std::move(actionText));

	if (user->blockStatus() == UserData::BlockStatus::Unknown) {
		user->session().api().requestFullPeer(user);
	}
}

void Filler::addViewDiscussion() {
	const auto channel = _peer->asBroadcast();
	if (!channel) {
		return;
	}
	const auto chat = channel->linkedChat();
	if (!chat) {
		return;
	}
	const auto navigation = _controller;
	_addAction(tr::lng_profile_view_discussion(tr::now), [=] {
		if (channel->invitePeekExpires()) {
			Ui::Toast::Show(
				tr::lng_channel_invite_private(tr::now));
			return;
		}
		navigation->showPeerHistory(
			chat,
			Window::SectionShow::Way::Forward);
	}, &st::menuIconDiscussion);
}

void Filler::addExportChat() {
	if (!_peer->canExportChatHistory()) {
		return;
	}
	const auto peer = _peer;
	_addAction(
		tr::lng_profile_export_chat(tr::now),
		[=] { PeerMenuExportChat(peer); },
		&st::menuIconExport);
}

void Filler::addReport() {
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	if ((!chat || chat->amCreator())
		&& (!channel || channel->amCreator())) {
		return;
	}
	const auto peer = _peer;
	const auto navigation = _controller;
	_addAction(tr::lng_profile_report(tr::now), [=] {
		HistoryView::ShowReportPeerBox(navigation, peer);
	}, &st::menuIconReport);
}

void Filler::addNewContact() {
	const auto user = _peer->asUser();
	if (!user || user->isContact() || user->isSelf() || user->isBot()) {
		return;
	}
	const auto controller = _controller;
	_addAction(
		tr::lng_info_add_as_contact(tr::now),
		[=] { controller->show(Box(EditContactBox, controller, user)); },
		&st::menuIconInvite);
}

void Filler::addShareContact() {
	const auto user = _peer->asUser();
	if (!user || !user->canShareThisContact()) {
		return;
	}
	const auto controller = _controller;
	_addAction(
		tr::lng_info_share_contact(tr::now),
		[=] { PeerMenuShareContactBox(controller, user); },
		&st::menuIconShare);
}

void Filler::addEditContact() {
	const auto user = _peer->asUser();
	if (!user || !user->isContact() || user->isSelf()) {
		return;
	}
	const auto controller = _controller;
	_addAction(
		tr::lng_info_edit_contact(tr::now),
		[=] { controller->show(Box(EditContactBox, controller, user)); },
		&st::menuIconEdit);
}

void Filler::addBotToGroup() {
	const auto user = _peer->asUser();
	if (!user
		|| !user->isBot()
		|| user->isRepliesChat()
		|| user->botInfo->cantJoinGroups) {
		return;
	}
	using AddBotToGroup = AddBotToGroupBoxController;
	_addAction(
		tr::lng_profile_invite_to_group(tr::now),
		[=] { AddBotToGroup::Start(user); },
		&st::menuIconInvite);
}

void Filler::addNewMembers() {
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	if ((!chat || !chat->canAddMembers())
		&& (!channel || !channel->canAddMembers())) {
		return;
	}
	const auto navigation = _controller;
	const auto callback = chat
		? Fn<void()>([=] { AddChatMembers(navigation, chat); })
		: [=] { PeerMenuAddChannelMembers(navigation, channel); };
	_addAction(
		((chat || channel->isMegagroup())
			? tr::lng_channel_add_members(tr::now)
			: tr::lng_channel_add_users(tr::now)),
		callback,
		&st::menuIconInvite);
}

void Filler::addDeleteContact() {
	const auto user = _peer->asUser();
	if (!user || !user->isContact() || user->isSelf()) {
		return;
	}
	_addAction(
		tr::lng_info_delete_contact(tr::now),
		[=] { PeerMenuDeleteContact(user); },
		&st::menuIconDelete);
}

void Filler::addHidePin() {
	const auto history = _peer->owner().history(_peer);
	if (history->hasPinnedMessages()) {
		if (history->hasHiddenPinnedMessage()) {
			_addAction(
				ktr("ktg_pinned_message_show"),
				[=] { history->switchPinnedHidden(false); },
				&st::menuIconPin);
		} else {
			_addAction(
				ktr("ktg_pinned_message_hide"),
				[=] { history->switchPinnedHidden(true); },
				&st::menuIconUnpin);
		}
	}
}

void Filler::addManageChat() {
	if (!EditPeerInfoBox::Available(_peer)) {
		return;
	}
	const auto peer = _peer;
	const auto navigation = _controller;
	const auto text = (peer->isChat() || peer->isMegagroup())
		? tr::lng_manage_group_title(tr::now)
		: tr::lng_manage_channel_title(tr::now);
	_addAction(text, [=] {
		navigation->showEditPeerBox(peer);
	}, &st::menuIconManage);
}

void Filler::addCreatePoll() {
	if (!_peer->canSendPolls()) {
		return;
	}
	const auto peer = _peer;
	const auto controller = _controller;
	const auto source = (_request.section == Section::Scheduled)
		? Api::SendType::Scheduled
		: Api::SendType::Normal;
	const auto sendMenuType = (_request.section == Section::Scheduled)
		? SendMenu::Type::Disabled
		: (_request.section == Section::Replies)
		? SendMenu::Type::SilentOnly
		: SendMenu::Type::Scheduled;
	const auto flag = PollData::Flags();
	const auto replyToId = _request.currentReplyToId
		? _request.currentReplyToId
		: _request.rootId;
	auto callback = [=] {
		PeerMenuCreatePoll(
			controller,
			peer,
			replyToId,
			flag,
			flag,
			source,
			sendMenuType);
	};
	_addAction(
		tr::lng_polls_create(tr::now),
		std::move(callback),
		&st::menuIconCreatePoll);
}

void Filler::addThemeEdit() {
	const auto user = _peer->asUser();
	if (!user || user->isBot()) {
		return;
	}
	const auto controller = _controller;
	_addAction(
		tr::lng_chat_theme_change(tr::now),
		[=] { controller->toggleChooseChatTheme(user); },
		&st::menuIconChangeColors);
}

void Filler::fill() {
	if (_folder) {
		fillArchiveActions();
		return;
	}
	switch (_request.section) {
	case Section::ChatsList: fillChatsListActions(); break;
	case Section::History: fillHistoryActions(); break;
	case Section::Profile: fillProfileActions(); break;
	case Section::Replies: fillRepliesActions(); break;
	case Section::Scheduled: fillScheduledActions(); break;
	default: Unexpected("_request.section in Filler::fill.");
	}
}

void Filler::fillChatsListActions() {
	addHidePromotion();
	addInfo();
	addToggleArchive();
	addTogglePin();
	addToggleMute();
	addToggleUnreadMark();
	// addToFolder();
	if (const auto user = _peer->asUser()) {
		if (!user->isContact()) {
			addBlockUser();
		}
	}
	addClearHistory();
	addDeleteChat();
	addLeaveChat();
}

void Filler::fillHistoryActions() {
	addInfo();
	addToggleMute();
	addSupportInfo();
	addHidePin();
	addManageChat();
	addCreatePoll();
	addThemeEdit();
	addViewDiscussion();
	addExportChat();
	addReport();
	addClearHistory();
	addDeleteChat();
	addLeaveChat();
}

void Filler::fillProfileActions() {
	addSupportInfo();
	addNewContact();
	addShareContact();
	addEditContact();
	addBotToGroup();
	addNewMembers();
	addManageChat();
	addViewDiscussion();
	addExportChat();
	addBlockUser();
	addReport();
	addLeaveChat();
	addDeleteContact();
}

void Filler::fillRepliesActions() {
	addCreatePoll();
}

void Filler::fillScheduledActions() {
	addCreatePoll();
}

void Filler::fillArchiveActions() {
	Expects(_folder != nullptr);

	if (_folder->id() != Data::Folder::kId) {
		return;
	}
	const auto controller = _controller;
	if (DialogListLines() != 1) {
		const auto hidden = controller->session().settings().archiveCollapsed();
		const auto text = hidden
			? tr::lng_context_archive_expand(tr::now)
			: tr::lng_context_archive_collapse(tr::now);
		_addAction(text, [=] {
			controller->session().settings().setArchiveCollapsed(!hidden);
			controller->session().saveSettingsDelayed();
		}, hidden ? &st::menuIconExpand : &st::menuIconCollapse);
	}

	_addAction(tr::lng_context_archive_to_menu(tr::now), [=] {
		Ui::Toast::Show(Ui::Toast::Config{
			.text = { tr::lng_context_archive_to_menu_info(tr::now) },
			.st = &st::windowArchiveToast,
			.durationMs = kArchivedToastDuration,
			.multiline = true,
		});

		controller->session().settings().setArchiveInMainMenu(
			!controller->session().settings().archiveInMainMenu());
		controller->session().saveSettingsDelayed();
	}, &st::menuIconToMainMenu);

	MenuAddMarkAsReadChatListAction(
		[folder = _folder] { return folder->chatsList(); },
		_addAction);
}

} // namespace

void PeerMenuExportChat(not_null<PeerData*> peer) {
	Core::App().exportManager().start(peer);
}

void PeerMenuDeleteContact(not_null<UserData*> user) {
	const auto text = tr::lng_sure_delete_contact(
		tr::now,
		lt_contact,
		user->name);
	const auto deleteSure = [=] {
		Ui::hideLayer();
		user->session().api().request(MTPcontacts_DeleteContacts(
			MTP_vector<MTPInputUser>(1, user->inputUser)
		)).done([=](const MTPUpdates &result) {
			user->session().api().applyUpdates(result);
		}).send();
	};
	Ui::show(Box<Ui::ConfirmBox>(
		text,
		tr::lng_box_delete(tr::now),
		deleteSure));
}

void PeerMenuShareContactBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<UserData*> user) {
	const auto weak = std::make_shared<QPointer<PeerListBox>>();
	auto callback = [=](not_null<PeerData*> peer) {
		if (!peer->canWrite()) {
			Ui::show(Box<Ui::InformBox>(
				tr::lng_forward_share_cant(tr::now)),
				Ui::LayerOption::KeepOther);
			return;
		} else if (peer->isSelf()) {
			auto action = Api::SendAction(peer->owner().history(peer));
			action.clearDraft = false;
			user->session().api().shareContact(user, action);
			Ui::Toast::Show(tr::lng_share_done(tr::now));
			if (auto strong = *weak) {
				strong->closeBox();
			}
			return;
		}
		auto recipient = peer->isUser()
			? peer->name
			: '\xAB' + peer->name + '\xBB';
		Ui::show(Box<Ui::ConfirmBox>(
			tr::lng_forward_share_contact(tr::now, lt_recipient, recipient),
			tr::lng_forward_send(tr::now),
			[peer, user, navigation] {
				const auto history = peer->owner().history(peer);
				navigation->showPeerHistory(
					history,
					Window::SectionShow::Way::ClearStack,
					ShowAtTheEndMsgId);
				auto action = Api::SendAction(history);
				action.clearDraft = false;
				user->session().api().shareContact(user, action);
			}), Ui::LayerOption::KeepOther);
	};
	*weak = Ui::show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(
			&navigation->session(),
			std::move(callback)),
		[](not_null<PeerListBox*> box) {
			box->addButton(tr::lng_cancel(), [=] {
				box->closeBox();
			});
		}));
}

void PeerMenuCreatePoll(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		MsgId replyToId,
		PollData::Flags chosen,
		PollData::Flags disabled,
		Api::SendType sendType,
		SendMenu::Type sendMenuType) {
	if (peer->isChannel() && !peer->isMegagroup()) {
		chosen &= ~PollData::Flag::PublicVotes;
		disabled |= PollData::Flag::PublicVotes;
	}
	const auto box = Ui::show(Box<CreatePollBox>(
		controller,
		chosen,
		disabled,
		sendType,
		sendMenuType));
	const auto lock = box->lifetime().make_state<bool>(false);
	box->submitRequests(
	) | rpl::start_with_next([=](const CreatePollBox::Result &result) {
		if (std::exchange(*lock, true)) {
			return;
		}
		auto action = Api::SendAction(
			peer->owner().history(peer),
			result.options);
		action.clearDraft = false;
		action.replyTo = replyToId;
		if (const auto localDraft = action.history->localDraft()) {
			action.clearDraft = localDraft->textWithTags.text.isEmpty();
		}
		const auto api = &peer->session().api();
		api->polls().create(result.poll, action, crl::guard(box, [=] {
			box->closeBox();
		}), crl::guard(box, [=](const MTP::Error &error) {
			*lock = false;
			box->submitFailed(tr::lng_attach_failed(tr::now));
		}));
	}, box->lifetime());
}

void PeerMenuBlockUserBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::Controller*> window,
		not_null<PeerData*> peer,
		std::variant<v::null_t, bool> suggestReport,
		std::variant<v::null_t, ClearChat, ClearReply> suggestClear) {
	const auto settings = peer->settings().value_or(PeerSettings(0));
	const auto reportNeeded = v::is_null(suggestReport)
		? ((settings & PeerSetting::ReportSpam) != 0)
		: v::get<bool>(suggestReport);

	const auto user = peer->asUser();
	const auto name = user ? user->shortName() : peer->name;
	if (user) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_blocked_list_confirm_text(
				lt_name,
				rpl::single(Ui::Text::Bold(name)),
				Ui::Text::WithEntities),
			st::blockUserConfirmation));

		box->addSkip(st::boxMediumSkip);
	}
	const auto report = reportNeeded
		? box->addRow(object_ptr<Ui::Checkbox>(
			box,
			tr::lng_report_spam(tr::now),
			true,
			st::defaultBoxCheckbox))
		: nullptr;

	if (report) {
		box->addSkip(st::boxMediumSkip);
	}

	const auto clear = v::is<ClearChat>(suggestClear)
		? box->addRow(object_ptr<Ui::Checkbox>(
			box,
			tr::lng_blocked_list_confirm_clear(tr::now),
			true,
			st::defaultBoxCheckbox))
		: v::is<ClearReply>(suggestClear)
		? box->addRow(object_ptr<Ui::Checkbox>(
			box,
			tr::lng_context_delete_msg(tr::now),
			true,
			st::defaultBoxCheckbox))
		: nullptr;
	if (clear) {
		box->addSkip(st::boxMediumSkip);
	}
	const auto allFromUser = v::is<ClearReply>(suggestClear)
		? box->addRow(object_ptr<Ui::Checkbox>(
			box,
			tr::lng_delete_all_from(tr::now),
			true,
			st::defaultBoxCheckbox))
		: nullptr;

	if (allFromUser) {
		box->addSkip(st::boxLittleSkip);
	}

	box->setTitle(tr::lng_blocked_list_confirm_title(
		lt_name,
		rpl::single(name)));

	box->addButton(tr::lng_blocked_list_confirm_ok(), [=] {
		const auto reportChecked = report && report->checked();
		const auto clearChecked = clear && clear->checked();
		const auto fromUserChecked = allFromUser && allFromUser->checked();

		box->closeBox();

		if (const auto clearReply = std::get_if<ClearReply>(&suggestClear)) {
			using Flag = MTPcontacts_BlockFromReplies::Flag;
			peer->session().api().request(MTPcontacts_BlockFromReplies(
				MTP_flags((clearChecked ? Flag::f_delete_message : Flag(0))
					| (fromUserChecked ? Flag::f_delete_history : Flag(0))
					| (reportChecked ? Flag::f_report_spam : Flag(0))),
				MTP_int(clearReply->replyId.msg)
			)).done([=](const MTPUpdates &result) {
				peer->session().updates().applyUpdates(result);
			}).send();
		} else {
			peer->session().api().blockedPeers().block(peer);
			if (reportChecked) {
				peer->session().api().request(MTPmessages_ReportSpam(
					peer->input
				)).send();
			}
			if (clearChecked) {
				crl::on_main(&peer->session(), [=] {
					peer->session().api().deleteConversation(peer, false);
				});
				window->sessionController()->showBackFromStack();
			}
		}

		Ui::Toast::Show(
			tr::lng_new_contact_block_done(tr::now, lt_user, name));
	}, st::attentionBoxButton);

	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void PeerMenuUnblockUserWithBotRestart(not_null<UserData*> user) {
	user->session().api().blockedPeers().unblock(user, [=] {
		if (user->isBot() && !user->isSupport()) {
			user->session().api().sendBotStart(user);
		}
	});
}

void BlockSenderFromRepliesBox(
		not_null<Ui::GenericBox*> box,
		not_null<SessionController*> controller,
		FullMsgId id) {
	const auto item = controller->session().data().message(id);
	Assert(item != nullptr);

	PeerMenuBlockUserBox(
		box,
		&controller->window(),
		item->senderOriginal(),
		true,
		Window::ClearReply{ id });
}

/*
QPointer<Ui::RpWidget> ShowForwardMessagesBox(
		not_null<Window::SessionNavigation*> navigation,
		Data::ForwardDraft &&draft,
		FnMut<void()> &&successCallback) {
	const auto weak = std::make_shared<QPointer<PeerListBox>>();
	auto callback = [
		draft = std::move(draft),
		callback = std::move(successCallback),
		weak,
		navigation
	](not_null<PeerData*> peer) mutable {
		const auto content = navigation->parentController()->content();
		if (peer->isSelf()) {
			const auto history = peer->owner().history(peer);
			auto resolved = history->resolveForwardDraft(draft);
			if (!resolved.items.empty()) {
				const auto api = &peer->session().api();
				auto action = Api::SendAction(peer->owner().history(peer));
				action.clearDraft = false;
				action.generateLocal = false;
				api->forwardMessages(std::move(resolved), action, [] {
					Ui::Toast::Show(tr::lng_share_done(tr::now));
				});
			}
		} else if (!content->setForwardDraft(peer->id, std::move(draft))) {
			return;
		}
		if (const auto strong = *weak) {
			strong->closeBox();
		}
		if (callback) {
			callback();
		}
	};
	auto initBox = [](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_cancel(), [box] {
			box->closeBox();
		});
	};
	*weak = Ui::show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(
			&navigation->session(),
			std::move(callback)),
		std::move(initBox)), Ui::LayerOption::KeepOther);
	return weak->data();
}
*/

QPointer<Ui::RpWidget> ShowForwardMessagesBox(
		not_null<Window::SessionNavigation*> navigation,
		Data::ForwardDraft &&draft,
		FnMut<void()> &&successCallback) {
	struct ShareData {
		ShareData(not_null<PeerData*> peer, Data::ForwardDraft &&fwdDraft, FnMut<void()> &&callback)
		: peer(peer)
		, draft(std::move(fwdDraft))
		, submitCallback(std::move(callback)) {
		}
		not_null<PeerData*> peer;
		Data::ForwardDraft draft;
		int requestsLeft = 0;
		FnMut<void()> submitCallback;
	};
	const auto weak = std::make_shared<QPointer<ShareBox>>();
	const auto firstItem = navigation->session().data().message(draft.ids[0]);
	const auto history = firstItem->history();
	const auto owner = &history->owner();
	const auto session = &history->session();
	const auto isGame = firstItem->getMessageBot()
		&& firstItem->media()
		&& (firstItem->media()->game() != nullptr);
	const auto canCopyLink = [=] {
		if (draft.ids.size() > 10) {
			return false;
		}

		const auto groupId = firstItem->groupId();

		for (const auto item : history->owner().idsToItems(draft.ids)) {
			if (groupId != item->groupId()) {
				return false;
			}
		}

		return (firstItem->hasDirectLink() || isGame);
	}();

	const auto hasMediaForGrouping = [=] {
		if (draft.ids.size() > 1) {
			auto grouppableMediaCount = 0;
			for (const auto item : history->owner().idsToItems(draft.ids)) {
				if (item->media() && item->media()->canBeGrouped()) {
					grouppableMediaCount++;
				} else {
					grouppableMediaCount = 0;
				}
				if (grouppableMediaCount > 1) {
					return true;
				}
			}
		}
		return false;
	}();

	const auto data = std::make_shared<ShareData>(history->peer, std::move(draft), std::move(successCallback));

	auto copyCallback = [=]() {
		if (const auto item = owner->message(data->draft.ids[0])) {
			if (item->hasDirectLink()) {
				HistoryView::CopyPostLink(
					session,
					item->fullId(),
					HistoryView::Context::History);
			} else if (const auto bot = item->getMessageBot()) {
				if (const auto media = item->media()) {
					if (const auto game = media->game()) {
						const auto link = session->createInternalLinkFull(
							bot->username
							+ qsl("?game=")
							+ game->shortName);

						QGuiApplication::clipboard()->setText(link);

						Ui::Toast::Show(tr::lng_share_game_link_copied(tr::now));
					}
				}
			}
		}
	};
	auto submitCallback = [=](
			std::vector<not_null<PeerData*>> &&result,
			TextWithTags &&comment,
			Api::SendOptions options,
			Data::ForwardDraft &&newDraft) {
		if (data->requestsLeft > 0) {
			return; // Share clicked already.
		}
		auto items = history->owner().idsToItems(data->draft.ids);
		if (items.empty() || result.empty()) {
			return;
		}

		const auto error = [&] {
			for (const auto peer : result) {
				const auto error = GetErrorTextForSending(
					peer,
					items,
					comment,
					false, /* ignoreSlowmodeCountdown */
					newDraft.options != Data::ForwardOptions::PreserveInfo);
				if (!error.isEmpty()) {
					return std::make_pair(error, peer);
				}
			}
			return std::make_pair(QString(), result.front());
		}();
		if (!error.first.isEmpty()) {
			auto text = TextWithEntities();
			if (result.size() > 1) {
				text.append(
					Ui::Text::Bold(error.second->name)
				).append("\n\n");
			}
			text.append(error.first);
			Ui::show(
				Box<Ui::InformBox>(text),
				Ui::LayerOption::KeepOther);
			return;
		}

		const auto checkAndClose = [=] {
			data->requestsLeft--;
			if (!data->requestsLeft) {
				Ui::Toast::Show(tr::lng_share_done(tr::now));
				Ui::hideLayer();
			}
		};
		auto &api = owner->session().api();

		data->draft.options = newDraft.options;
		data->draft.groupOptions = newDraft.groupOptions;

		for (const auto peer : result) {
			const auto history = owner->history(peer);
			auto action = Api::SendAction(history);
			action.options = options;
			action.clearDraft = false;

			if (!comment.text.isEmpty()) {
				auto message = ApiWrap::MessageToSend(action);
				message.textWithTags = comment;
				api.sendMessage(std::move(message));
			}

			data->requestsLeft++;
			auto resolved = history->resolveForwardDraft(data->draft);

			api.forwardMessages(std::move(resolved), action, [=] {
				checkAndClose();
			});
		}
		if (data->submitCallback && !cForwardRetainSelection()) {
			data->submitCallback();
		}
	};
	auto filterCallback = [](PeerData *peer) {
		return peer->canWrite();
	};
	auto copyLinkCallback = canCopyLink
		? Fn<void()>(std::move(copyCallback))
		: Fn<void()>();
	auto goToChatCallback = [navigation, data](PeerData *peer, Data::ForwardDraft &&newDraft) {
		if (data->submitCallback && !cForwardRetainSelection()) {
			data->submitCallback();
		}
		data->draft.options = newDraft.options;
		data->draft.groupOptions = newDraft.groupOptions;
		navigation->parentController()->content()->setForwardDraft(peer->id, std::move(data->draft));
	};
	*weak = Ui::show(
		Box<ShareBox>(ShareBox::Descriptor{
			.session = session,
			.copyCallback = std::move(copyLinkCallback),
			.submitCallback = std::move(submitCallback),
			.filterCallback = std::move(filterCallback),
			.goToChatCallback = std::move(goToChatCallback),
			.navigation = navigation,
			.hasMedia = hasMediaForGrouping,
			.isShare = false,
			.draft = std::make_unique<Data::ForwardDraft>(Data::ForwardDraft{
				.options = data->draft.options,
				.groupOptions = data->draft.groupOptions,
			}),
		}),
		Ui::LayerOption::KeepOther);
	return weak->data();
}

QPointer<Ui::RpWidget> ShowForwardMessagesBox(
		not_null<Window::SessionNavigation*> navigation,
		MessageIdsList &&items,
		FnMut<void()> &&successCallback) {
	const auto options = [] {
		switch (ForwardMode()) {
			case 1: return Data::ForwardOptions::NoSenderNames;
			case 2: return Data::ForwardOptions::NoNamesAndCaptions;
			default: return Data::ForwardOptions::PreserveInfo;
		}
	}();

	const auto groupOptions = [] {
		switch (ForwardGroupingMode()) {
			case 1: return Data::GroupingOptions::RegroupAll;
			case 2: return Data::GroupingOptions::Separate;
			default: return Data::GroupingOptions::GroupAsIs;
		}
	}();

	return ShowForwardMessagesBox(
		navigation,
		Data::ForwardDraft{
			.ids = std::move(items),
			.options = options,
			.groupOptions = groupOptions,
		}, std::move(successCallback));
}

QPointer<Ui::RpWidget> ShowSendNowMessagesBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<History*> history,
		MessageIdsList &&items,
		FnMut<void()> &&successCallback) {
	const auto session = &navigation->session();
	const auto text = (items.size() > 1)
		? tr::lng_scheduled_send_now_many(tr::now, lt_count, items.size())
		: tr::lng_scheduled_send_now(tr::now);

	const auto error = GetErrorTextForSending(
		history->peer,
		session->data().idsToItems(items),
		TextWithTags());
	if (!error.isEmpty()) {
		Ui::ShowMultilineToast({
			.text = { error },
		});
		return { nullptr };
	}
	auto done = [
		=,
		list = std::move(items),
		callback = std::move(successCallback)
	](Fn<void()> &&close) mutable {
		close();
		auto ids = QVector<MTPint>();
		for (const auto item : session->data().idsToItems(list)) {
			if (item->allowsSendNow()) {
				ids.push_back(MTP_int(
					session->data().scheduledMessages().lookupId(item)));
			}
		}
		session->api().request(MTPmessages_SendScheduledMessages(
			history->peer->input,
			MTP_vector<MTPint>(ids)
		)).done([=](const MTPUpdates &result) {
			session->api().applyUpdates(result);
		}).fail([=](const MTP::Error &error) {
			session->api().sendMessageFail(error, history->peer);
		}).send();
		if (callback) {
			callback();
		}
	};
	return Ui::show(
		Box<Ui::ConfirmBox>(text, tr::lng_send_button(tr::now), std::move(done)),
		Ui::LayerOption::KeepOther).data();
}

void PeerMenuAddChannelMembers(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel) {
	if (!channel->isMegagroup()
		&& (channel->membersCount()
			>= channel->session().serverConfig().chatSizeMax)) {
		Ui::show(
			Box<MaxInviteBox>(channel),
			Ui::LayerOption::KeepOther);
		return;
	}
	const auto api = &channel->session().api();
	api->chatParticipants().requestForAdd(channel, crl::guard(navigation, [=](
			const Api::ChatParticipants::TLMembers &data) {
		const auto &[availableCount, list] = Api::ChatParticipants::Parse(
			channel,
			data);
		const auto already = (
			list
		) | ranges::views::transform([&](const Api::ChatParticipant &p) {
			return p.isUser()
				? channel->owner().userLoaded(p.userId())
				: nullptr;
		}) | ranges::views::filter([](UserData *user) {
			return (user != nullptr);
		}) | ranges::to_vector;

		AddParticipantsBoxController::Start(
			navigation,
			channel,
			{ already.begin(), already.end() });
	}));
}

void ToggleMessagePinned(
		not_null<Window::SessionNavigation*> navigation,
		FullMsgId itemId,
		bool pin) {
	const auto item = navigation->session().data().message(itemId);
	if (!item || !item->canPin()) {
		return;
	}
	if (pin) {
		Ui::show(Box<PinMessageBox>(item->history()->peer, item->id));
	} else {
		Ui::show(Box<UnpinMessageBox>(item->history()->peer, item->id));
	}
}

void HidePinnedBar(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Fn<void()> onHidden) {
	const auto callback = crl::guard(navigation, [=] {
		Ui::hideLayer();
		auto &session = peer->session();
		const auto migrated = peer->migrateFrom();
		const auto top = Data::ResolveTopPinnedId(peer, migrated);
		const auto universal = !top
			? MsgId(0)
			: (migrated && !peerIsChannel(top.peer))
			? (top.msg - ServerMaxMsgId)
			: top.msg;
		if (universal) {
			session.settings().setHiddenPinnedMessageId(peer->id, universal);
			session.saveSettingsDelayed();
			if (onHidden) {
				onHidden();
			}
		} else {
			session.api().requestFullPeer(peer);
		}
	});
	Ui::show(Box<Ui::ConfirmBox>(
		tr::lng_pinned_hide_all_sure(tr::now),
		tr::lng_pinned_hide_all_hide(tr::now),
		callback));
}

void UnpinAllMessages(
		not_null<Window::SessionNavigation*> navigation,
		not_null<History*> history) {
	const auto callback = crl::guard(navigation, [=] {
		Ui::hideLayer();
		const auto api = &history->session().api();
		const auto sendRequest = [=](auto self) -> void {
			api->request(MTPmessages_UnpinAllMessages(
				history->peer->input
			)).done([=](const MTPmessages_AffectedHistory &result) {
				const auto peer = history->peer;
				const auto offset = api->applyAffectedHistory(peer, result);
				if (offset > 0) {
					self(self);
				} else {
					history->unpinAllMessages();
				}
			}).send();
		};
		sendRequest(sendRequest);
	});
	Ui::show(Box<Ui::ConfirmBox>(
		tr::lng_pinned_unpin_all_sure(tr::now),
		tr::lng_pinned_unpin(tr::now),
		callback));
}

void PeerMenuAddMuteAction(
		not_null<PeerData*> peer,
		const PeerMenuCallback &addAction) {
	peer->owner().requestNotifySettings(peer);
	const auto muteText = [](bool isUnmuted) {
		return isUnmuted
			? tr::lng_context_mute(tr::now)
			: tr::lng_context_unmute(tr::now);
	};
	const auto muteAction = addAction(QString("-"), [=] {
		if (!peer->owner().notifyIsMuted(peer)) {
			Ui::show(Box<MuteSettingsBox>(peer));
		} else {
			peer->owner().updateNotifySettings(peer, 0);
		}
	}, (peer->owner().notifyIsMuted(peer)
		? &st::menuIconUnmute
		: &st::menuIconMute));

	auto actionText = Info::Profile::NotificationsEnabledValue(
		peer
	) | rpl::map(muteText);
	SetActionText(muteAction, std::move(actionText));
}

void MenuAddMarkAsReadAllChatsAction(
		not_null<Data::Session*> data,
		const PeerMenuCallback &addAction) {
	auto callback = [owner = data] {
		auto boxCallback = [=](Fn<void()> &&close) {
			close();

			MarkAsReadChatList(owner->chatsList());
			if (const auto folder = owner->folderLoaded(Data::Folder::kId)) {
				MarkAsReadChatList(folder->chatsList());
			}
		};
		Ui::show(Box<Ui::ConfirmBox>(
			tr::lng_context_mark_read_all_sure(tr::now),
			std::move(boxCallback)));
	};
	addAction(
		tr::lng_context_mark_read_all(tr::now),
		std::move(callback),
		&st::menuIconMarkRead);
}

void MenuAddMarkAsReadChatListAction(
		Fn<not_null<Dialogs::MainList*>()> &&list,
		const PeerMenuCallback &addAction) {
	const auto unreadState = list()->unreadState();
	if (unreadState.empty()) {
		return;
	}

	auto callback = [=] {
		if (unreadState.messages > kMaxUnreadWithoutConfirmation) {
			auto boxCallback = [=](Fn<void()> &&close) {
				MarkAsReadChatList(list());
				close();
			};
			Ui::show(Box<Ui::ConfirmBox>(
				tr::lng_context_mark_read_sure(tr::now),
				std::move(boxCallback)));
		} else {
			MarkAsReadChatList(list());
		}
	};
	addAction(
		tr::lng_context_mark_read(tr::now),
		std::move(callback),
		&st::menuIconMarkRead);
}

void ToggleHistoryArchived(not_null<History*> history, bool archived) {
	const auto callback = [=] {
		Ui::Toast::Show(Ui::Toast::Config{
			.text = { (archived
				? tr::lng_archived_added(tr::now)
				: tr::lng_archived_removed(tr::now)) },
			.st = &st::windowArchiveToast,
			.durationMs = (archived
				? kArchivedToastDuration
				: Ui::Toast::kDefaultDuration),
			.multiline = true,
		});
	};
	history->session().api().toggleHistoryArchived(
		history,
		archived,
		callback);
}

Fn<void()> ClearHistoryHandler(not_null<PeerData*> peer) {
	return [=] {
		Ui::show(
			Box<DeleteMessagesBox>(peer, true),
			Ui::LayerOption::KeepOther);
	};
}

Fn<void()> DeleteAndLeaveHandler(not_null<PeerData*> peer) {
	return [=] {
		Ui::show(
			Box<DeleteMessagesBox>(peer, false),
			Ui::LayerOption::KeepOther);
	};
}

void FillDialogsEntryMenu(
		not_null<SessionController*> controller,
		Dialogs::EntryState request,
		const PeerMenuCallback &callback) {
	Filler(controller, request, callback).fill();
}

} // namespace Window
