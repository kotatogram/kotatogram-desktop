/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_peer_menu.h"

#include "lang/lang_keys.h"
#include "api/api_sending.h"
#include "boxes/confirm_box.h"
#include "boxes/mute_settings_box.h"
#include "boxes/add_contact_box.h"
#include "boxes/report_box.h"
#include "boxes/create_poll_box.h"
#include "boxes/peers/add_participants_box.h"
#include "boxes/peers/edit_contact_box.h"
#include "boxes/share_box.h"
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
#include "api/api_chat_filters.h"
#include "api/api_updates.h"
#include "api/api_text_entities.h"
#include "mtproto/mtproto_config.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_message.h" // GetErrorTextForSending.
#include "history/history_widget.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_context_menu.h" // CopyPostLink.
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "support/support_helper.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
//#include "info/feed/info_feed_channels_controllers.h" // #feed
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
#include "facades.h" // Adaptive::ThreeColumn
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_window.h" // st::windowMinWidth

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtWidgets/QAction>

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
		not_null<PeerData*> peer,
		FilterId filterId,
		const PeerMenuCallback &addAction,
		PeerMenuSource source);
	void fill();

private:
	[[nodiscard]] bool showInfo();
	[[nodiscard]] bool showHidePromotion();
	[[nodiscard]] bool showToggleArchived();
	[[nodiscard]] bool showTogglePin();
	void addHidePromotion();
	void addTogglePin();
	void addInfo();
	//void addSearch();
	void addToggleUnreadMark();
	void addToggleArchive();
	void addUserActions(not_null<UserData*> user);
	void addBlockUser(not_null<UserData*> user);
	void addChatActions(not_null<ChatData*> chat);
	void addChannelActions(not_null<ChannelData*> channel);

	void addPollAction(not_null<PeerData*> peer);

	not_null<SessionController*> _controller;
	not_null<PeerData*> _peer;
	FilterId _filterId = 0;
	const PeerMenuCallback &_addAction;
	PeerMenuSource _source;

};

class FolderFiller {
public:
	FolderFiller(
		not_null<SessionController*> controller,
		not_null<Data::Folder*> folder,
		const PeerMenuCallback &addAction,
		PeerMenuSource source);
	void fill();

private:
	void addTogglesForArchive();
	//bool showInfo();
	//void addTogglePin();
	//void addInfo();
	//void addSearch();
	//void addNotifications();
	//void addUngroup();

	not_null<SessionController*> _controller;
	not_null<Data::Folder*> _folder;
	const PeerMenuCallback &_addAction;
	PeerMenuSource _source;

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
	Expects(key.entry()->folderKnown());

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
		Ui::show(Box<InformBox>(errorText));
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
		)).done([=](const MTPBool &result) {
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
	not_null<PeerData*> peer,
	FilterId filterId,
	const PeerMenuCallback &addAction,
	PeerMenuSource source)
: _controller(controller)
, _peer(peer)
, _filterId(filterId)
, _addAction(addAction)
, _source(source) {
}

bool Filler::showInfo() {
	if (_source == PeerMenuSource::Profile
		|| _peer->isSelf()
		|| _peer->isRepliesChat()) {
		return false;
	} else if (_controller->activeChatCurrent().peer() != _peer) {
		return true;
	} else if (!Adaptive::ThreeColumn()) {
		return true;
	} else if (!Core::App().settings().thirdSectionInfoEnabled()
		&& !Core::App().settings().tabbedReplacedWithInfo()) {
		return true;
	}
	return false;
}

bool Filler::showHidePromotion() {
	if (_source != PeerMenuSource::ChatsList) {
		return false;
	}
	const auto history = _peer->owner().historyLoaded(_peer);
	return history
		&& history->useTopPromotion()
		&& !history->topPromotionType().isEmpty();
}

bool Filler::showToggleArchived() {
	if (_source != PeerMenuSource::ChatsList) {
		return false;
	}
	const auto history = _peer->owner().historyLoaded(_peer);
	if (history && history->useTopPromotion()) {
		return false;
	} else if (!_peer->isNotificationsUser() && !_peer->isSelf()) {
		return true;
	}
	return history && (history->folder() != nullptr);
}

bool Filler::showTogglePin() {
	if (_source != PeerMenuSource::ChatsList) {
		return false;
	}
	const auto history = _peer->owner().historyLoaded(_peer);
	return history && !history->fixedOnTopIndex();
}

void Filler::addHidePromotion() {
	const auto history = _peer->owner().history(_peer);
	_addAction(tr::lng_context_hide_psa(tr::now), [=] {
		history->cacheTopPromotion(false, QString(), QString());
		history->session().api().request(MTPhelp_HidePromoData(
			history->peer->input
		)).send();
	});
}

void Filler::addTogglePin() {
	const auto controller = _controller;
	const auto filterId = _filterId;
	const auto peer = _peer;
	const auto history = peer->owner().history(peer);
	const auto pinText = [=] {
		return history->isPinnedDialog(filterId)
			? tr::lng_context_unpin_from_top(tr::now)
			: tr::lng_context_pin_to_top(tr::now);
	};
	const auto pinToggle = [=] {
		TogglePinnedDialog(controller, history, filterId);
	};
	const auto pinAction = _addAction(pinText(), pinToggle);

	auto actionText = history->session().changes().historyUpdates(
		history,
		Data::HistoryUpdate::Flag::IsPinned
	) | rpl::map(pinText);
	SetActionText(pinAction, std::move(actionText));
}

void Filler::addInfo() {
	const auto controller = _controller;
	const auto peer = _peer;
	const auto text = (peer->isChat() || peer->isMegagroup())
		? tr::lng_context_view_group(tr::now)
		: (peer->isUser()
			? tr::lng_context_view_profile(tr::now)
			: tr::lng_context_view_channel(tr::now));
	_addAction(text, [=] {
		controller->showPeerInfo(peer);
	});
}

//void Filler::addSearch() {
//	const auto controller = _controller;
//	const auto peer = _peer;
//	_addAction(tr::lng_profile_search_messages(tr::now), [=] {
//		controller->content()->searchInChat(peer->owner().history(peer));
//	});
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
	});

	auto actionText = history->session().changes().historyUpdates(
		history,
		Data::HistoryUpdate::Flag::UnreadView
	) | rpl::map(label);
	SetActionText(action, std::move(actionText));
}

void Filler::addToggleArchive() {
	const auto peer = _peer;
	const auto history = peer->owner().history(peer);
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
	const auto archiveAction = _addAction(label(), toggle);

	auto actionText = history->session().changes().historyUpdates(
		history,
		Data::HistoryUpdate::Flag::Folder
	) | rpl::map(label);
	SetActionText(archiveAction, std::move(actionText));
}

void Filler::addBlockUser(not_null<UserData*> user) {
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
			user->session().api().blockPeer(user);
		} else {
			window->show(Box(
				PeerMenuBlockUserBox,
				window,
				user,
				v::null,
				v::null));
		}
	});

	auto actionText = _peer->session().changes().peerUpdates(
		_peer,
		Data::PeerUpdate::Flag::IsBlocked
	) | rpl::map([=] { return blockText(user); });
	SetActionText(blockAction, std::move(actionText));

	if (user->blockStatus() == UserData::BlockStatus::Unknown) {
		user->session().api().requestFullPeer(user);
	}
}

void Filler::addUserActions(not_null<UserData*> user) {
	const auto controller = _controller;
	const auto window = &_controller->window();
	if (_source != PeerMenuSource::ChatsList) {
		if (user->session().supportMode()) {
			_addAction("Edit support info", [=] {
				user->session().supportHelper().editInfo(controller, user);
			});
		}
		if (user->isSelf() && user->pinnedMessageId()) {
			auto hasHidden = HistoryWidget::hasHiddenPinnedMessage(user);
			if (hasHidden) {
				_addAction(
					tr::ktg_pinned_message_show(tr::now),
					[=] { PeerMenuUnhidePinnedMessage(user); });
			} else {
				_addAction(
					tr::ktg_pinned_message_hide(tr::now),
					[=] { PeerMenuHidePinnedMessage(user); });
			}
		}
		if (!user->isContact() && !user->isSelf() && !user->isBot()) {
			_addAction(
				tr::lng_info_add_as_contact(tr::now),
				[=] { window->show(Box(EditContactBox, controller, user)); });
		}
		if (user->canShareThisContact()) {
			_addAction(
				tr::lng_info_share_contact(tr::now),
				[=] { PeerMenuShareContactBox(controller, user); });
		}
		if (user->isContact() && !user->isSelf()) {
			_addAction(
				tr::lng_info_edit_contact(tr::now),
				[=] { window->show(Box(EditContactBox, controller, user)); });
			_addAction(
				tr::lng_info_delete_contact(tr::now),
				[=] { PeerMenuDeleteContact(user); });
		}
		if (user->isBot()
			&& !user->isRepliesChat()
			&& !user->botInfo->cantJoinGroups) {
			using AddBotToGroup = AddBotToGroupBoxController;
			_addAction(
				tr::lng_profile_invite_to_group(tr::now),
				[=] { AddBotToGroup::Start(controller, user); });
		}
		addPollAction(user);
		if (user->canExportChatHistory()) {
			_addAction(
				tr::lng_profile_export_chat(tr::now),
				[=] { PeerMenuExportChat(user); });
		}
	}
	_addAction(
		tr::lng_profile_delete_conversation(tr::now),
		DeleteAndLeaveHandler(user));
	_addAction(
		tr::lng_profile_clear_history(tr::now),
		ClearHistoryHandler(user));
	if (!user->isInaccessible()
		&& user != user->session().user()
		&& !user->isRepliesChat()
		&& _source != PeerMenuSource::ChatsList) {
		addBlockUser(user);
	}
}

void Filler::addChatActions(not_null<ChatData*> chat) {
	if (_source != PeerMenuSource::ChatsList) {
		const auto controller = _controller;
		if (EditPeerInfoBox::Available(chat)) {
			const auto text = tr::lng_manage_group_title(tr::now);
			_addAction(text, [=] {
				controller->showEditPeerBox(chat);
			});
		}
		if (chat->pinnedMessageId()) {
			auto hasHidden = HistoryWidget::hasHiddenPinnedMessage(chat);
			if (hasHidden) {
				_addAction(
					tr::ktg_pinned_message_show(tr::now),
					[=] { PeerMenuUnhidePinnedMessage(chat); });
			} else {
				_addAction(
					tr::ktg_pinned_message_hide(tr::now),
					[=] { PeerMenuHidePinnedMessage(chat); });
			}
		}
		if (chat->canAddMembers()) {
			_addAction(
				tr::lng_profile_add_participant(tr::now),
				[=] { AddChatMembers(controller, chat); });
		}
		addPollAction(chat);
		if (chat->canExportChatHistory()) {
			_addAction(
				tr::lng_profile_export_chat(tr::now),
				[=] { PeerMenuExportChat(chat); });
		}
	}
	_addAction(
		tr::lng_profile_clear_and_exit(tr::now),
		DeleteAndLeaveHandler(_peer));
	_addAction(
		tr::lng_profile_clear_history(tr::now),
		ClearHistoryHandler(_peer));
}

void Filler::addChannelActions(not_null<ChannelData*> channel) {
	const auto isGroup = channel->isMegagroup();
	const auto navigation = _controller;
	//if (!isGroup) { // #feed
	//	const auto feed = channel->feed();
	//	const auto grouped = (feed != nullptr);
	//	if (!grouped || feed->channels().size() > 1) {
	//		_addAction( // #feed
	//			(grouped ? tr::lng_feed_ungroup(tr::now) : tr::lng_feed_group(tr::now)),
	//			[=] { ToggleChannelGrouping(channel, !grouped); });
	//	}
	//}
	if (_source != PeerMenuSource::ChatsList) {
		if (channel->isBroadcast()) {
			if (const auto chat = channel->linkedChat()) {
				_addAction(tr::lng_profile_view_discussion(tr::now), [=] {
					navigation->showPeerHistory(
						chat,
						Window::SectionShow::Way::Forward);
				});
			}
		}
		if (EditPeerInfoBox::Available(channel)) {
			const auto text = isGroup
				? tr::lng_manage_group_title(tr::now)
				: tr::lng_manage_channel_title(tr::now);
			_addAction(text, [=] {
				navigation->showEditPeerBox(channel);
			});
		}
		if (channel->pinnedMessageId()) {
			auto hasHidden = HistoryWidget::hasHiddenPinnedMessage(channel);
			if (hasHidden) {
				_addAction(
					tr::ktg_pinned_message_show(tr::now),
					[=] { PeerMenuUnhidePinnedMessage(channel); });
			} else {
				_addAction(
					tr::ktg_pinned_message_hide(tr::now),
					[=] { PeerMenuHidePinnedMessage(channel); });
			}
		}
		if (channel->canAddMembers()) {
			_addAction(
				tr::lng_channel_add_members(tr::now),
				[=] { PeerMenuAddChannelMembers(navigation, channel); });
		}
		addPollAction(channel);
		if (channel->canExportChatHistory()) {
			_addAction(
				(isGroup
					? tr::lng_profile_export_chat(tr::now)
					: tr::lng_profile_export_channel(tr::now)),
				[=] { PeerMenuExportChat(channel); });
		}
	}
	if (channel->amIn()) {
		if (isGroup && !channel->isPublic()) {
			_addAction(
				tr::lng_profile_clear_history(tr::now),
				ClearHistoryHandler(channel));
		}
		auto text = isGroup
			? tr::lng_profile_leave_group(tr::now)
			: tr::lng_profile_leave_channel(tr::now);
		_addAction(text, DeleteAndLeaveHandler(channel));
	} else {
		auto text = isGroup
			? tr::lng_profile_join_group(tr::now)
			: tr::lng_profile_join_channel(tr::now);
		_addAction(
			text,
			[=] { channel->session().api().joinChannel(channel); });
	}
	if (_source != PeerMenuSource::ChatsList) {
		const auto needReport = !channel->amCreator()
			&& (!isGroup || channel->isPublic());
		if (needReport) {
			_addAction(tr::lng_profile_report(tr::now), [channel] {
				Ui::show(Box<ReportBox>(channel));
			});
		}
	}
}

void Filler::addPollAction(not_null<PeerData*> peer) {
	if (!peer->canSendPolls()) {
		return;
	}
	const auto controller = _controller;
	const auto source = (_source == PeerMenuSource::ScheduledSection)
		? Api::SendType::Scheduled
		: Api::SendType::Normal;
	const auto flag = PollData::Flags();
	auto callback = [=] {
		PeerMenuCreatePoll(controller, peer, flag, flag, source);
	};
	_addAction(tr::lng_polls_create(tr::now), std::move(callback));
}

void Filler::fill() {
	if (_source == PeerMenuSource::ScheduledSection) {
		addPollAction(_peer);
		return;
	}
	if (showHidePromotion()) {
		addHidePromotion();
	}
	if (showToggleArchived()) {
		addToggleArchive();
	}
	if (showTogglePin()) {
		addTogglePin();
	}
	if (showInfo()) {
		addInfo();
	}
	if (_source != PeerMenuSource::Profile && !_peer->isSelf()) {
		PeerMenuAddMuteAction(_peer, _addAction);
	}
	if (_source == PeerMenuSource::ChatsList) {
		//addSearch();
		addToggleUnreadMark();
	}

	if (const auto user = _peer->asUser()) {
		addUserActions(user);
	} else if (const auto chat = _peer->asChat()) {
		addChatActions(chat);
	} else if (const auto channel = _peer->asChannel()) {
		addChannelActions(channel);
	}
}

FolderFiller::FolderFiller(
	not_null<SessionController*> controller,
	not_null<Data::Folder*> folder,
	const PeerMenuCallback &addAction,
	PeerMenuSource source)
: _controller(controller)
, _folder(folder)
, _addAction(addAction)
, _source(source) {
}

void FolderFiller::fill() {
	if (_source == PeerMenuSource::ChatsList) {
		addTogglesForArchive();
	}
}

void FolderFiller::addTogglesForArchive() {
	if (_folder->id() != Data::Folder::kId) {
		return;
	}
	const auto controller = _controller;
	if (DialogListLines() != 1) {
		const auto hidden = (controller->session().settings().archiveCollapsed());
		const auto text = hidden
			? tr::lng_context_archive_expand(tr::now)
			: tr::lng_context_archive_collapse(tr::now);
		_addAction(text, [=] {
			controller->session().settings().setArchiveCollapsed(!hidden);
			controller->session().saveSettingsDelayed();
		});
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
	});

	MenuAddMarkAsReadChatListAction(
		[folder = _folder] { return folder->chatsList(); },
		_addAction);
}
//
//void FolderFiller::addInfo() {
//	const auto controller = _controller;
//	const auto feed = _feed;
//	_addAction(tr::lng_context_view_feed_info(tr::now), [=] {
//		controller->showSection(Info::Memento(
//			feed,
//			Info::Section(Info::Section::Type::Profile)));
//	});
//}
//
//void FolderFiller::addNotifications() {
//	const auto feed = _feed;
//	_addAction(tr::lng_feed_notifications(tr::now), [=] {
//		Info::FeedProfile::NotificationsController::Start(feed);
//	});
//}
//
//void FolderFiller::addSearch() {
//	const auto feed = _feed;
//	const auto controller = _controller;
//	_addAction(tr::lng_profile_search_messages(tr::now), [=] {
//		controller->content()->searchInChat(feed);
//	});
//}
//
//void FolderFiller::addUngroup() {
//	const auto feed = _feed;
//	//_addAction(tr::lng_feed_ungroup_all(tr::now), [=] { // #feed
//	//	PeerMenuUngroupFeed(feed);
//	//});
//}

} // namespace

void PeerMenuHidePinnedMessage(not_null<PeerData*> peer) {
	auto hidden = HistoryWidget::switchPinnedHidden(peer, true);
	if (hidden) {
		peer->session().changes().peerUpdated(
			peer,
			Data::PeerUpdate::Flag::PinnedMessage);
	}
}

void PeerMenuUnhidePinnedMessage(not_null<PeerData*> peer) {
	auto unhidden = HistoryWidget::switchPinnedHidden(peer, false);
	if (unhidden) {
		peer->session().changes().peerUpdated(
			peer,
			Data::PeerUpdate::Flag::PinnedMessage);
	}
}

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
	Ui::show(Box<ConfirmBox>(
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
			Ui::show(Box<InformBox>(
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
		Ui::show(Box<ConfirmBox>(
			tr::lng_forward_share_contact(tr::now, lt_recipient, recipient),
			tr::lng_forward_send(tr::now),
			[peer, user] {
				const auto history = peer->owner().history(peer);
				Ui::showPeerHistory(history, ShowAtTheEndMsgId);
				auto action = Api::SendAction(history);
				action.clearDraft = false;
				user->session().api().shareContact(user, action);
			}), Ui::LayerOption::KeepOther);
	};
	*weak = Ui::show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(
			navigation,
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
		PollData::Flags chosen,
		PollData::Flags disabled,
		Api::SendType sendType) {
	if (peer->isChannel() && !peer->isMegagroup()) {
		chosen &= ~PollData::Flag::PublicVotes;
		disabled |= PollData::Flag::PublicVotes;
	}
	const auto box = Ui::show(Box<CreatePollBox>(
		controller,
		chosen,
		disabled,
		sendType));
	const auto lock = box->lifetime().make_state<bool>(false);
	box->submitRequests(
	) | rpl::start_with_next([=](const CreatePollBox::Result &result) {
		if (std::exchange(*lock, true)) {
			return;
		}
		auto action = Api::SendAction(peer->owner().history(peer));
		action.clearDraft = false;
		action.options = result.options;
		if (const auto id = controller->content()->currentReplyToIdFor(action.history)) {
			action.replyTo = id;
		}
		if (const auto localDraft = action.history->localDraft()) {
			action.clearDraft = localDraft->textWithTags.text.isEmpty();
		}
		const auto api = &peer->session().api();
		api->createPoll(result.poll, action, crl::guard(box, [=] {
			box->closeBox();
		}), crl::guard(box, [=](const RPCError &error) {
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
	using Flag = MTPDpeerSettings::Flag;
	const auto settings = peer->settings().value_or(Flag(0));
	const auto reportNeeded = v::is_null(suggestReport)
		? ((settings & Flag::f_report_spam) != 0)
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
			peer->session().api().blockPeer(peer);
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
	user->session().api().unblockPeer(user, [=] {
		if (user->isBot() && !user->isSupport()) {
			user->session().api().sendBotStart(user);
		}
	});
}

/*
QPointer<Ui::RpWidget> ShowForwardMessagesBox(
		not_null<Window::SessionNavigation*> navigation,
		MessageIdsList &&items,
		FnMut<void()> &&successCallback) {
	const auto weak = std::make_shared<QPointer<PeerListBox>>();
	auto callback = [
		ids = std::move(items),
		callback = std::move(successCallback),
		weak,
		navigation
	](not_null<PeerData*> peer) mutable {
		if (peer->isSelf()) {
			auto items = peer->owner().idsToItems(ids);
			if (!items.empty()) {
				const auto api = &peer->session().api();
				auto action = Api::SendAction(peer->owner().history(peer));
				action.clearDraft = false;
				action.generateLocal = false;
				api->forwardMessages(std::move(items), action, [] {
					Ui::Toast::Show(tr::lng_share_done(tr::now));
				});
			}
		} else if (!navigation->parentController()->content()->setForwardDraft(peer->id, std::move(ids))) {
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
			navigation,
			std::move(callback)),
		std::move(initBox)), Ui::LayerOption::KeepOther);
	return weak->data();
}
*/

QPointer<Ui::RpWidget> ShowForwardMessagesBox(
		not_null<Window::SessionNavigation*> navigation,
		MessageIdsList &&items,
		FnMut<void()> &&successCallback) {
	struct ShareData {
		ShareData(not_null<PeerData*> peer, MessageIdsList &&ids, FnMut<void()> &&callback)
		: peer(peer)
		, msgIds(std::move(ids))
		, submitCallback(std::move(callback)) {
		}
		not_null<PeerData*> peer;
		MessageIdsList msgIds;
		int requestsLeft = 0;
		FnMut<void()> submitCallback;
	};
	struct MsgIdsGroup {
		MsgIdsGroup() = default;
		MsgIdsGroup(not_null<HistoryItem*> item, MTPint fullId, bool isGrouped = false)
		: grouped(isGrouped) {
			add(item, fullId);
		}
		void add(not_null<HistoryItem*> item, MTPint fullId) {
			items.push_back(item);
			ids.push_back(fullId);
		}
		HistoryItemsList items;
		QVector<MTPint> ids;
		bool grouped = false;
	};
	const auto weak = std::make_shared<QPointer<ShareBox>>();
	const auto firstItem = navigation->session().data().message(items[0]);
	const auto history = firstItem->history();
	const auto owner = &history->owner();
	const auto session = &history->session();
	const auto isGame = firstItem->getMessageBot()
		&& firstItem->media()
		&& (firstItem->media()->game() != nullptr);
	const auto canCopyLink = items.size() == 1 && (firstItem->hasDirectLink() || isGame);
	auto hasMediaForGrouping = false;

	if (items.size() > 1) {
		auto grouppableMediaCount = 0;
		for (const auto item : history->owner().idsToItems(items)) {
			if (item->media() && item->media()->canBeGrouped()) {
				grouppableMediaCount++;
			} else {
				grouppableMediaCount = 0;
			}
			if (grouppableMediaCount > 1) {
				hasMediaForGrouping = true;
				break;
			}
		}
	}

	const auto data = std::make_shared<ShareData>(history->peer, std::move(items), std::move(successCallback));

	auto copyCallback = [=]() {
		if (const auto item = owner->message(data->msgIds[0])) {
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
			Api::SendOptions options) {
		if (data->requestsLeft > 0) {
			return; // Share clicked already.
		}
		auto items = history->owner().idsToItems(data->msgIds);
		if (items.empty() || result.empty()) {
			return;
		}

		const auto error = [&] {
			for (const auto peer : result) {
				const auto error = GetErrorTextForSending(
					peer,
					items,
					comment);
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
				Box<InformBox>(text),
				Ui::LayerOption::KeepOther);
			return;
		}

		const auto sendFlags = MTPmessages_ForwardMessages::Flag(0)
			| MTPmessages_ForwardMessages::Flag::f_with_my_score
			| (options.silent
				? MTPmessages_ForwardMessages::Flag::f_silent
				: MTPmessages_ForwardMessages::Flag(0))
			| (options.scheduled
				? MTPmessages_ForwardMessages::Flag::f_schedule_date
				: MTPmessages_ForwardMessages::Flag(0));

		// Regroup messages if needed
		auto groupedMsgIds = QVector<MsgIdsGroup>();
		for (const auto fullId : data->msgIds) {
			const auto item = navigation->session().data().message(fullId);
			const auto group = owner->groups().find(item);
			const auto canBeGrouped = hasMediaForGrouping && item->media() && item->media()->canBeGrouped();

			if (groupedMsgIds.size() > 0) {
				auto lastGroup = &groupedMsgIds.back();

				if (cForwardAlbumsAsIs()) {
					if (owner->groups().find(lastGroup->items.back()) == group) {
						lastGroup->add(item, MTP_int(fullId.msg));
						continue;
					}
				} else {
					if (lastGroup->grouped) {
						if (lastGroup->items.size() < 10 && canBeGrouped) {
							lastGroup->add(item, MTP_int(fullId.msg));
							continue;
						}
					} else {
						if (!canBeGrouped) {
							lastGroup->add(item, MTP_int(fullId.msg));
							continue;
						}
					}
				}
			}

			groupedMsgIds.push_back(MsgIdsGroup(
				item,
				MTP_int(fullId.msg),
				cForwardAlbumsAsIs()
					? (group != nullptr)
					: canBeGrouped
						? cForwardGrouped()
						: false));
		}

		const auto generateRandom = [&] (int size) {
			auto result = QVector<MTPlong>(size);
			for (auto &value : result) {
				value = rand_value<MTPlong>();
			}
			return result;
		};
		const auto checkAndClose = [=] {
			data->requestsLeft--;
			if (!data->requestsLeft) {
				Ui::Toast::Show(tr::lng_share_done(tr::now));
				Ui::hideLayer();
			}
		};
		auto &api = owner->session().api();
		auto &histories = owner->histories();
		const auto requestType = Data::Histories::RequestType::Send;

		const auto forwardQuoted = [&] (
			MsgIdsGroup &&group,
			not_null<History*> history,
			MTPmessages_ForwardMessages::Flags flags) {
			histories.sendRequest(history, requestType, [=](Fn<void()> finish) {
				auto &api = history->session().api();
				history->sendRequestId = api.request(MTPmessages_ForwardMessages(
						MTP_flags(flags),
						data->peer->input,
						MTP_vector<MTPint>(group.ids),
						MTP_vector<MTPlong>(generateRandom(group.ids.size())),
						history->peer->input,
						MTP_int(options.scheduled)
				)).done([=](const MTPUpdates &updates, mtpRequestId requestId) {
					history->session().api().applyUpdates(updates);
					checkAndClose();
					finish();
				}).fail([=](const RPCError &error) {
					finish();
				}).afterRequest(history->sendRequestId).send();
				return history->sendRequestId;
			});
			data->requestsLeft++;
		};

		const auto forwardAlbumUnquoted = [&] (MsgIdsGroup &&group, not_null<History*> history) {
			auto medias = QVector<MTPInputSingleMedia>();
			auto mediaRefs = QVector<QByteArray>();
			medias.reserve(group.items.size());
			mediaRefs.reserve(group.items.size());

			auto randomIds = generateRandom(group.items.size());

			for (const auto item : group.items) {
				const auto media = item->media();
				const auto inputMedia = media->photo()
					? MTP_inputMediaPhoto(MTP_flags(0), media->photo()->mtpInput(), MTPint())
					: MTP_inputMediaDocument(MTP_flags(0), media->document()->mtpInput(), MTPint());
				const auto caption = cForwardCaptioned()
						? item->originalText()
						: TextWithEntities();
				const auto sentEntities = Api::EntitiesToMTP(
					session,
					caption.entities,
					Api::ConvertOption::SkipLocal);

				const auto flags = !sentEntities.v.isEmpty()
						? MTPDinputSingleMedia::Flag::f_entities
						: MTPDinputSingleMedia::Flag(0);

				const auto randomId = randomIds.takeFirst();

				medias.push_back(MTP_inputSingleMedia(
					MTP_flags(flags),
					inputMedia,
					randomId,
					MTP_string(caption.text),
					sentEntities));
				mediaRefs.push_back(media->photo()
					? media->photo()->fileReference()
					: media->document()->fileReference());
			}

			const auto flags = MTPmessages_SendMultiMedia::Flags(0)
				| (options.silent
					? MTPmessages_SendMultiMedia::Flag::f_silent
					: MTPmessages_SendMultiMedia::Flag(0))
				| (options.scheduled
					? MTPmessages_SendMultiMedia::Flag::f_schedule_date
					: MTPmessages_SendMultiMedia::Flag(0));

			auto performRequest = [=, &mediaRefs, &histories](const auto &repeatRequest) -> void {
				mediaRefs.clear();
				for (const auto item : group.items) {
					const auto media = item->media();
					mediaRefs.push_back(media->photo()
						? media->photo()->fileReference()
						: media->document()->fileReference());
				}
				histories.sendRequest(history, requestType, [=](Fn<void()> finish) {
					auto &api = history->session().api();
					history->sendRequestId = api.request(MTPmessages_SendMultiMedia(
						MTP_flags(flags),
						history->peer->input,
						MTPint(),
						MTP_vector<MTPInputSingleMedia>(medias),
						MTP_int(options.scheduled)
					)).done([=](const MTPUpdates &updates, mtpRequestId requestId) {
						history->session().api().applyUpdates(updates);
						checkAndClose();
						finish();
					}).fail([=](const RPCError &error) {
						if (error.code() == 400
							&& error.type().startsWith(qstr("FILE_REFERENCE_"))) {
							auto refreshRequests = mediaRefs.size();
							auto index = 0;
							for (const auto item : group.items) {
								const auto media = item->media();
								const auto origin = media->document()
										? media->document()->stickerOrGifOrigin()
										: Data::FileOrigin();
								const auto usedFileReference = mediaRefs.value(index);
								
								history->session().api().refreshFileReference(origin, [=, &refreshRequests](const auto &result) {
									if (refreshRequests > 0) {
										refreshRequests--;
										return;
									}

									const auto currentMediaReference = media->photo()
											? media->photo()->fileReference()
											: media->document()->fileReference();

									if (currentMediaReference != usedFileReference) {
										repeatRequest(repeatRequest);
									} else {
										history->session().api().sendMessageFail(error, history->peer);
									}
								});
								index++;
							}
						} else {
							finish();
						}
					}).afterRequest(history->sendRequestId).send();
					return history->sendRequestId;
				});
			};
			performRequest(performRequest);
			data->requestsLeft++;
		};

		const auto forwardMediaUnquoted = [&] (not_null<HistoryItem *> item, not_null<History*> history) {
			const auto media = item->media();

			auto message = ApiWrap::MessageToSend(history);
			const auto caption = (cForwardCaptioned()
				&& !media->geoPoint()
				&& !media->sharedContact())
					? item->originalText()
					: TextWithEntities();

			message.textWithTags = TextWithTags{
				caption.text,
				TextUtilities::ConvertEntitiesToTextTags(caption.entities)
			};
			message.action.options = options;
			message.action.clearDraft = false;

			auto doneCallback = [=] () {
				checkAndClose();
			};

			auto &api = history->session().api();

			if (media->poll()) {
				const auto poll = *(media->poll());
				api.createPoll(
					poll,
					message.action,
					std::move(doneCallback),
					nullptr);
			} else if (media->geoPoint()) {
				const auto location = *(media->geoPoint());
				Api::SendLocationPoint(
					location,
					message.action,
					std::move(doneCallback),
					nullptr);
			} else if (media->sharedContact()) {
				const auto contact = media->sharedContact();
				api.shareContact(
					contact->phoneNumber,
					contact->firstName,
					contact->lastName,
					message.action);
			} else if (media->photo()) {
				Api::SendExistingPhoto(
					std::move(message),
					media->photo(),
					std::move(doneCallback),
					true); // forwarding
			} else if (media->document()) {
				Api::SendExistingDocument(
					std::move(message),
					media->document(),
					std::move(doneCallback),
					true); // forwarding
			} else {
				Unexpected("Media type in Window::ShowForwardMessagesBox.");
			}

			data->requestsLeft++;
		};

		const auto forwardMessageUnquoted = [&] (not_null<HistoryItem *> item, not_null<History*> history) {
			const auto media = item->media();

			const auto webPageId = (!media || !media->webpage())
				? CancelledWebPageId
				: media->webpage()->id;

			auto message = ApiWrap::MessageToSend(history);
			message.textWithTags = TextWithTags{
				item->originalText().text,
				TextUtilities::ConvertEntitiesToTextTags(item->originalText().entities)
			};
			message.action.options = options;
			message.action.clearDraft = false;
			message.webPageId = webPageId;

			history->session().api().sendMessage(
				std::move(message),
				[=] (const MTPUpdates &result, mtpRequestId requestId) {
					checkAndClose();
				},
				true); // forwarding

			data->requestsLeft++;
		};

		for (const auto peer : result) {
			const auto history = owner->history(peer);
			if (!comment.text.isEmpty()) {
				auto message = ApiWrap::MessageToSend(history);
				message.textWithTags = comment;
				message.action.options = options;
				message.action.clearDraft = false;
				api.sendMessage(std::move(message));
			}
			for (auto &group : groupedMsgIds) {
				if (cForwardQuoted()) {
					// Forward regrouped messages as is
					const auto flags = sendFlags;
					forwardQuoted(std::move(group), history, sendFlags);
				} else if (group.grouped) {
					// Sending albums without author
					forwardAlbumUnquoted(std::move(group), history);
				} else {
					for (const auto item : group.items) {
						const auto media = item->media();

						if (media && !media->webpage()) {
							if ((media->poll() && !history->peer->isUser())
								|| media->geoPoint()
								|| media->sharedContact()
								|| media->photo()
								|| media->document()) {
								// Send media messages without author
								forwardMediaUnquoted(item, history);
							} else {
								// Forward message if type doesn't support forwarding unquoted
								forwardQuoted(
									MsgIdsGroup(item, MTP_int(item->fullId().msg)),
									history,
									sendFlags);
							}
						} else {
							// Send messages without author
							forwardMessageUnquoted(item, history);
						}
					}
				}
			}
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
	auto goToChatCallback = [navigation, data](PeerData *peer) {
		navigation->parentController()->content()->setForwardDraft(peer->id, std::move(data->msgIds));
	};
	*weak = Ui::show(Box<ShareBox>(
		navigation,
		std::move(copyLinkCallback),
		std::move(submitCallback),
		std::move(filterCallback),
		std::move(goToChatCallback),
		hasMediaForGrouping));
	return weak->data();
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
		}).fail([=](const RPCError &error) {
			session->api().sendMessageFail(error, history->peer);
		}).send();
		if (callback) {
			callback();
		}
	};
	return Ui::show(
		Box<ConfirmBox>(text, tr::lng_send_button(tr::now), std::move(done)),
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
	api->requestChannelMembersForAdd(channel, crl::guard(navigation, [=](
			const MTPchannels_ChannelParticipants &result) {
		api->parseChannelParticipants(channel, result, [&](
				int availableCount,
				const QVector<MTPChannelParticipant> &list) {
			auto already = (
				list
			) | ranges::view::transform([](const MTPChannelParticipant &p) {
				return p.match([](const auto &data) {
					return data.vuser_id().v;
				});
			}) | ranges::view::transform([&](UserId userId) {
				return channel->owner().userLoaded(userId);
			}) | ranges::view::filter([](UserData *user) {
				return (user != nullptr);
			}) | ranges::to_vector;

			AddParticipantsBoxController::Start(
				navigation,
				channel,
				{ already.begin(), already.end() });
		});
	}));
}

void PeerMenuAddMuteAction(
		not_null<PeerData*> peer,
		const PeerMenuCallback &addAction) {
	peer->owner().requestNotifySettings(peer);
	const auto muteText = [](bool isUnmuted) {
		return isUnmuted
			? tr::lng_disable_notifications_from_tray(tr::now)
			: tr::lng_enable_notifications_from_tray(tr::now);
	};
	const auto muteAction = addAction(QString("-"), [=] {
		if (!peer->owner().notifyIsMuted(peer)) {
			Ui::show(Box<MuteSettingsBox>(peer));
		} else {
			peer->owner().updateNotifySettings(peer, 0);
		}
	});

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
		Ui::show(Box<ConfirmBox>(
			tr::lng_context_mark_read_all_sure(tr::now),
			std::move(boxCallback)));
	};
	addAction(
		tr::lng_context_mark_read_all(tr::now),
		std::move(callback));
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
			Ui::show(Box<ConfirmBox>(
				tr::lng_context_mark_read_sure(tr::now),
				std::move(boxCallback)));
		} else {
			MarkAsReadChatList(list());
		}
	};
	addAction(
		tr::lng_context_mark_read(tr::now),
		std::move(callback));
}
// #feed
//void PeerMenuUngroupFeed(not_null<Data::Feed*> feed) {
//	Ui::show(Box<ConfirmBox>(
//		tr::lng_feed_sure_ungroup_all(tr::now),
//		tr::lng_feed_ungroup_sure(tr::now),
//		[=] { Ui::hideLayer(); feed->session().api().ungroupAllFromFeed(feed); }));
//}
//
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

void FillPeerMenu(
		not_null<SessionController*> controller,
		not_null<PeerData*> peer,
		FilterId filterId,
		const PeerMenuCallback &callback,
		PeerMenuSource source) {
	Filler filler(controller, peer, filterId, callback, source);
	filler.fill();
}

void FillFolderMenu(
		not_null<SessionController*> controller,
		not_null<Data::Folder*> folder,
		const PeerMenuCallback &callback,
		PeerMenuSource source) {
	FolderFiller filler(controller, folder, callback, source);
	filler.fill();
}

} // namespace Window
