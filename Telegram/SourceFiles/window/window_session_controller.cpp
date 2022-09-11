/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_session_controller.h"

#include "kotato/kotato_settings.h"
#include "kotato/kotato_lang.h"
#include "boxes/add_contact_box.h"
#include "boxes/peers/add_bot_to_chat_box.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/delete_messages_box.h"
#include "window/window_adaptive.h"
#include "window/window_controller.h"
#include "window/main_window.h"
#include "window/window_filters_menu.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "inline_bots/bot_attach_web_view.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_replies_section.h"
#include "history/view/history_view_react_button.h"
#include "history/view/history_view_reactions.h"
#include "history/view/history_view_scheduled_section.h"
#include "media/player/media_player_instance.h"
#include "media/view/media_view_open_common.h"
#include "data/data_document_resolver.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_document_resolver.h"
#include "data/data_changes.h"
#include "data/data_group_call.h"
#include "data/data_chat_filters.h"
#include "data/data_peer_values.h"
#include "passport/passport_form_controller.h"
#include "chat_helpers/tabbed_selector.h"
#include "chat_helpers/emoji_interactions.h"
#include "core/shortcuts.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/click_handler_types.h"
#include "base/unixtime.h"
#include "base/random.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/text/format_values.h" // Ui::FormatPhone.
#include "ui/delayed_activation.h"
#include "ui/chat/attach/attach_bot_webview.h"
#include "ui/chat/message_bubble.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/message_sending_animation_controller.h"
#include "ui/style/style_palette_colorizer.h"
#include "ui/toast/toast.h"
#include "ui/toasts/common_toasts.h"
#include "calls/calls_instance.h" // Core::App().calls().inCall().
#include "calls/group/calls_group_call.h"
#include "ui/boxes/choose_date_time.h"
#include "ui/boxes/calendar_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/popup_menu.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_session_settings.h"
#include "apiwrap.h"
#include "api/api_chat_invite.h"
#include "api/api_global_privacy.h"
#include "api/api_blocked_peers.h"
#include "support/support_helper.h"
#include "storage/file_upload.h"
#include "facades.h"
#include "window/themes/window_theme.h"
#include "settings/settings_main.h"
#include "settings/settings_privacy_security.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h" // st::boxLabel
#include "styles/style_chat.h" // st::historyMessageRadius
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"

namespace Window {
namespace {

constexpr auto kCustomThemesInMemory = 5;
constexpr auto kMaxChatEntryHistorySize = 50;
constexpr auto kDayBaseFile = ":/gui/day-custom-base.tdesktop-theme"_cs;
constexpr auto kNightBaseFile = ":/gui/night-custom-base.tdesktop-theme"_cs;

[[nodiscard]] Fn<void(style::palette&)> PreparePaletteCallback(
	bool dark,
	std::optional<QColor> accent) {
	return [=](style::palette &palette) {
		using namespace Theme;
		const auto &embedded = EmbeddedThemes();
		const auto i = ranges::find(
			embedded,
			dark ? EmbeddedType::Night : EmbeddedType::Default,
			&EmbeddedScheme::type);
		Assert(i != end(embedded));
		const auto colorizer = accent
			? ColorizerFrom(*i, *accent)
			: style::colorizer();

		auto instance = Instance();
		const auto loaded = LoadFromFile(
			(dark ? kNightBaseFile : kDayBaseFile).utf16(),
			&instance,
			nullptr,
			nullptr,
			colorizer);
		Assert(loaded);
		palette.finalize();
		palette = instance.palette;
	};
}

[[nodiscard]] Ui::ChatThemeBubblesData PrepareBubblesData(
	const Data::CloudTheme &theme,
	Data::CloudThemeType type) {
	const auto i = theme.settings.find(type);
	return {
		.colors = (i != end(theme.settings)
			? i->second.outgoingMessagesColors
			: std::vector<QColor>()),
		.accent = (i != end(theme.settings)
			? i->second.outgoingAccentColor
			: std::optional<QColor>()),
	};
}

void ChooseJumpDateTimeBox(
	not_null<Ui::GenericBox*> box,
	QDateTime minDate,
	QDateTime maxDate,
	QDateTime highlighted,
	Fn<void(TimeId)> onDone,
	Fn<void()> onBegnning,
	Fn<void()> onCalendar,
	bool hasCalendar) {

	auto descriptor = Ui::ChooseDateTimeBox(box,
		Ui::ChooseDateTimeBoxArgs{
			.title = rktr("ktg_jump_to_date_title"),
			.submit = rktr("ktg_jump_to_date_button"),
			.done = std::move(onDone),
			.min = [=] { return base::unixtime::serialize(minDate); },
			.time = base::unixtime::serialize(highlighted),
			.max = [=] { return base::unixtime::serialize(maxDate); },
		});
	const auto topMenuButton = box->addTopButton(st::infoTopBarMenu);
	const auto menu = std::make_shared<base::unique_qptr<Ui::PopupMenu>>();
	topMenuButton.data()->setClickedCallback([=] {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			topMenuButton.data(),
			st::popupMenuWithIcons);
		(*menu)->addAction(
			ktr("ktg_jump_to_beginning"),
			std::move(onBegnning),
			&st::menuIconToBeginning);
		if (hasCalendar) {		
			(*menu)->addAction(
				ktr("ktg_show_calendar"),
				std::move(onCalendar),
				&st::menuIconSchedule);
		}

		(*menu)->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
		const auto buttonTopLeft = topMenuButton.data()->mapToGlobal(QPoint());
		const auto buttonRect = QRect(buttonTopLeft, topMenuButton.data()->size());
		const auto pos = QPoint(
			buttonRect.x() + buttonRect.width(),
			buttonRect.y() + buttonRect.height());
		(*menu)->popup(pos);
		return true;
	});
}

} // namespace

void ActivateWindow(not_null<SessionController*> controller) {
	const auto window = controller->widget();
	window->raise();
	window->activateWindow();
	Ui::ActivateWindowDelayed(window);
}

bool operator==(const PeerThemeOverride &a, const PeerThemeOverride &b) {
	return (a.peer == b.peer) && (a.theme == b.theme);
}

bool operator!=(const PeerThemeOverride &a, const PeerThemeOverride &b) {
	return !(a == b);
}

DateClickHandler::DateClickHandler(Dialogs::Key chat, QDateTime date)
	: _chat(chat)
	, _date(date) {
}

void DateClickHandler::setDate(QDateTime date) {
	_date = date;
}

void DateClickHandler::onClick(ClickContext context) const {
	const auto my = context.other.value<ClickHandlerContext>();
	if (const auto window = my.sessionWindow.get()) {
		window->showCalendar(_chat, _date);
	}
}

SessionNavigation::SessionNavigation(not_null<Main::Session*> session)
	: _session(session)
	, _api(&_session->mtp()) {
}

SessionNavigation::~SessionNavigation() = default;

Main::Session &SessionNavigation::session() const {
	return *_session;
}

void SessionNavigation::showPeerByLink(const PeerByLinkInfo &info) {
	Core::App().hideMediaView();
	if (!info.phone.isEmpty()) {
		resolvePhone(info.phone, [=](not_null<PeerData*> peer) {
			showPeerByLinkResolved(peer, info);
		});
	} else if (const auto name = std::get_if<QString>(&info.usernameOrId)) {
		resolveUsername(*name, [=](not_null<PeerData*> peer) {
			if (info.startAutoSubmit) {
				peer->session().api().blockedPeers().unblock(
					peer,
					[=] { showPeerByLinkResolved(peer, info); },
					true);
			} else {
				showPeerByLinkResolved(peer, info);
			}
		});
	} else if (const auto id = std::get_if<ChannelId>(&info.usernameOrId)) {
		resolveChannelById(*id, [=](not_null<ChannelData*> channel) {
			showPeerByLinkResolved(channel, info);
		});
	}
}

void SessionNavigation::resolvePhone(
		const QString &phone,
		Fn<void(not_null<PeerData*>)> done) {
	if (const auto peer = _session->data().userByPhone(phone)) {
		done(peer);
		return;
	}
	_api.request(base::take(_resolveRequestId)).cancel();
	_resolveRequestId = _api.request(MTPcontacts_ResolvePhone(
		MTP_string(phone)
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		resolveDone(result, done);
	}).fail([=](const MTP::Error &error) {
		_resolveRequestId = 0;
		if (error.code() == 400) {
			parentController()->show(
				Ui::MakeInformBox(tr::lng_username_by_phone_not_found(
					tr::now,
					lt_phone,
					Ui::FormatPhone(phone))),
				Ui::LayerOption::CloseOther);
		}
	}).send();
}

void SessionNavigation::resolveUsername(
		const QString &username,
		Fn<void(not_null<PeerData*>)> done) {
	if (const auto peer = _session->data().peerByUsername(username)) {
		done(peer);
		return;
	}
	_api.request(base::take(_resolveRequestId)).cancel();
	_resolveRequestId = _api.request(MTPcontacts_ResolveUsername(
		MTP_string(username)
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		resolveDone(result, done);
	}).fail([=](const MTP::Error &error) {
		_resolveRequestId = 0;
		if (error.code() == 400) {
			parentController()->show(
				Ui::MakeInformBox(
					tr::lng_username_not_found(tr::now, lt_user, username)),
				Ui::LayerOption::CloseOther);
		}
	}).send();
}

void SessionNavigation::resolveDone(
		const MTPcontacts_ResolvedPeer &result,
		Fn<void(not_null<PeerData*>)> done) {
	_resolveRequestId = 0;
	parentController()->hideLayer();
	result.match([&](const MTPDcontacts_resolvedPeer &data) {
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());
		if (const auto peerId = peerFromMTP(data.vpeer())) {
			done(_session->data().peer(peerId));
		}
	});
}

void SessionNavigation::resolveChannelById(
		ChannelId channelId,
		Fn<void(not_null<ChannelData*>)> done) {
	if (const auto channel = _session->data().channelLoaded(channelId)) {
		done(channel);
		return;
	}
	const auto fail = [=] {
		Ui::ShowMultilineToast({
			.parentOverride = Window::Show(this).toastParent(),
			.text = { tr::lng_error_post_link_invalid(tr::now) }
		});
	};
	_api.request(base::take(_resolveRequestId)).cancel();
	_resolveRequestId = _api.request(MTPchannels_GetChannels(
		MTP_vector<MTPInputChannel>(
			1,
			MTP_inputChannel(MTP_long(channelId.bare), MTP_long(0)))
	)).done([=](const MTPmessages_Chats &result) {
		result.match([&](const auto &data) {
			const auto peer = _session->data().processChats(data.vchats());
			if (peer && peer->id == peerFromChannel(channelId)) {
				done(peer->asChannel());
			} else {
				fail();
			}
		});
	}).fail(fail).send();
}

void SessionNavigation::showPeerByLinkResolved(
		not_null<PeerData*> peer,
		const PeerByLinkInfo &info) {
	auto params = SectionShow{
		SectionShow::Way::Forward
	};
	params.origin = SectionShow::OriginMessage{
		info.clickFromMessageId
	};
	if (info.voicechatHash && peer->isChannel()) {
		// First show the channel itself.
		crl::on_main(this, [=] {
			showPeerHistory(peer->id, params, ShowAtUnreadMsgId);
		});

		// Then try to join the voice chat.
		joinVoiceChatFromLink(peer, info);
		return;
	}
	using Scope = AddBotToGroupBoxController::Scope;
	const auto user = peer->asUser();
	const auto bot = (user && user->isBot()) ? user : nullptr;
	const auto &replies = info.repliesInfo;
	const auto searchQuery = info.searchQuery;

	const auto applySearchQuery = [=] {
		parentController()->content()->searchMessages(
			searchQuery + ' ',
			(peer && !peer->isUser())
				? peer->owner().history(peer).get()
				: Dialogs::Key());
	};

	if (const auto threadId = std::get_if<ThreadId>(&replies)) {
		showRepliesForMessage(
			session().data().history(peer),
			threadId->id,
			info.messageId,
			params);
	} else if (const auto commentId = std::get_if<CommentId>(&replies)) {
		showRepliesForMessage(
			session().data().history(peer),
			info.messageId,
			commentId->id,
			params);
	} else if (bot
		&& (info.resolveType == ResolveType::AddToGroup
			|| info.resolveType == ResolveType::AddToChannel
			|| info.resolveType == ResolveType::ShareGame)) {
		const auto scope = (info.resolveType == ResolveType::ShareGame)
			? Scope::ShareGame
			: (info.resolveType == ResolveType::AddToGroup)
			? (info.startAdminRights ? Scope::GroupAdmin : Scope::All)
			: (info.resolveType == ResolveType::AddToChannel)
			? Scope::ChannelAdmin
			: Scope::None;
		Assert(scope != Scope::None);

		AddBotToGroupBoxController::Start(
			parentController(),
			bot,
			scope,
			info.startToken,
			info.startAdminRights);
	} else if (info.resolveType == ResolveType::Mention) {
		if (bot || peer->isChannel()) {
			crl::on_main(this, [=] {
				showPeerHistory(peer->id, params);
				if (!searchQuery.isEmpty()) {
					applySearchQuery();
				}
			});
		} else if (!searchQuery.isEmpty()) {
			crl::on_main(this, [=] {
				showPeerHistory(peer->id, params);
				applySearchQuery();
			});
		} else {
			showPeerInfo(peer, params);
		}
	} else {
		// Show specific posts only in channels / supergroups.
		const auto msgId = peer->isChannel()
			? info.messageId
			: info.startAutoSubmit
			? ShowAndStartBotMsgId
			: ShowAtUnreadMsgId;
		const auto attachBotUsername = info.attachBotUsername;
		if (bot && bot->botInfo->startToken != info.startToken) {
			bot->botInfo->startToken = info.startToken;
			bot->session().changes().peerUpdated(
				bot,
				Data::PeerUpdate::Flag::BotStartToken);
		}
		if (!attachBotUsername.isEmpty()) {
			crl::on_main(this, [=] {
				showPeerHistory(peer->id, params, msgId);
				if (searchQuery.isEmpty()) {
					peer->session().attachWebView().request(
						peer,
						attachBotUsername,
						info.attachBotToggleCommand.value_or(QString()));
				} else {
					applySearchQuery();
				}
			});
		} else if (bot && info.attachBotToggleCommand) {
			const auto itemId = info.clickFromMessageId;
			const auto item = _session->data().message(itemId);
			const auto contextPeer = item
				? item->history()->peer.get()
				: nullptr;
			const auto contextUser = contextPeer
				? contextPeer->asUser()
				: nullptr;
			bot->session().attachWebView().requestAddToMenu(
				contextUser,
				bot,
				*info.attachBotToggleCommand,
				parentController(),
				info.attachBotChooseTypes);
		} else {
			crl::on_main(this, [=] {
				showPeerHistory(peer->id, params, msgId);
				if (!searchQuery.isEmpty()) {
					applySearchQuery();
				}
			});
		}
	}
}

void SessionNavigation::joinVoiceChatFromLink(
		not_null<PeerData*> peer,
		const PeerByLinkInfo &info) {
	Expects(info.voicechatHash.has_value());

	const auto bad = [=] {
		Ui::ShowMultilineToast({
			.parentOverride = Window::Show(this).toastParent(),
			.text = { tr::lng_group_invite_bad_link(tr::now) }
		});
	};
	const auto hash = *info.voicechatHash;
	_api.request(base::take(_resolveRequestId)).cancel();
	_resolveRequestId = _api.request(
		MTPchannels_GetFullChannel(peer->asChannel()->inputChannel)
	).done([=](const MTPmessages_ChatFull &result) {
		_session->api().processFullPeer(peer, result);
		const auto call = peer->groupCall();
		if (!call) {
			bad();
			return;
		}
		const auto join = [=] {
			parentController()->startOrJoinGroupCall(
				peer,
				{ hash, Calls::StartGroupCallArgs::JoinConfirm::Always });
		};
		if (call->loaded()) {
			join();
			return;
		}
		const auto id = call->id();
		const auto limit = 5;
		_resolveRequestId = _api.request(
			MTPphone_GetGroupCall(call->input(), MTP_int(limit))
		).done([=](const MTPphone_GroupCall &result) {
			if (const auto now = peer->groupCall(); now && now->id() == id) {
				if (!now->loaded()) {
					now->processFullCall(result);
				}
				join();
			} else {
				bad();
			}
		}).fail(bad).send();
	}).send();
}

void SessionNavigation::showRepliesForMessage(
		not_null<History*> history,
		MsgId rootId,
		MsgId commentId,
		const SectionShow &params) {
	if (_showingRepliesRequestId
		&& _showingRepliesHistory == history.get()
		&& _showingRepliesRootId == rootId) {
		return;
	} else if (!history->peer->asChannel()) {
		// HistoryView::RepliesWidget right now handles only channels.
		return;
	}
	_api.request(base::take(_showingRepliesRequestId)).cancel();

	const auto postPeer = history->peer;
	//const auto item = _session->data().message(postPeer, rootId);
	//if (!commentId && (!item || !item->repliesAreComments())) {
	//	showSection(std::make_shared<HistoryView::RepliesMemento>(history, rootId));
	//	return;
	//} else if (const auto id = item ? item->commentsItemId() : FullMsgId()) {
	//	if (const auto commentsItem = _session->data().message(id)) {
	//		showSection(
	//			std::make_shared<HistoryView::RepliesMemento>(commentsItem));
	//		return;
	//	}
	//}
	_showingRepliesHistory = history;
	_showingRepliesRootId = rootId;
	_showingRepliesRequestId = _api.request(
		MTPmessages_GetDiscussionMessage(
			history->peer->input,
			MTP_int(rootId))
	).done([=](const MTPmessages_DiscussionMessage &result) {
		_showingRepliesRequestId = 0;
		result.match([&](const MTPDmessages_discussionMessage &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			_session->data().processMessages(
				data.vmessages(),
				NewMessageType::Existing);
			const auto list = data.vmessages().v;
			if (list.isEmpty()) {
				return;
			}
			const auto id = IdFromMessage(list.front());
			const auto peer = PeerFromMessage(list.front());
			if (!peer || !id) {
				return;
			}
			auto item = _session->data().message(peer, id);
			if (const auto group = _session->data().groups().find(item)) {
				item = group->items.front();
			}
			if (item) {
				if (const auto maxId = data.vmax_id()) {
					item->setRepliesMaxId(maxId->v);
				}
				item->setRepliesInboxReadTill(
					data.vread_inbox_max_id().value_or_empty(),
					data.vunread_count().v);
				item->setRepliesOutboxReadTill(
					data.vread_outbox_max_id().value_or_empty());
				const auto post = _session->data().message(postPeer, rootId);
				if (post && item->history()->peer != postPeer) {
					post->setCommentsItemId(item->fullId());
					if (const auto maxId = data.vmax_id()) {
						post->setRepliesMaxId(maxId->v);
					}
					post->setRepliesInboxReadTill(
						data.vread_inbox_max_id().value_or_empty(),
						data.vunread_count().v);
					post->setRepliesOutboxReadTill(
						data.vread_outbox_max_id().value_or_empty());
				}
				showSection(std::make_shared<HistoryView::RepliesMemento>(
					item,
					commentId));
			}
		});
	}).fail([=](const MTP::Error &error) {
		_showingRepliesRequestId = 0;
		if (error.type() == u"CHANNEL_PRIVATE"_q
			|| error.type() == u"USER_BANNED_IN_CHANNEL"_q) {
			Ui::Toast::Show(
				Show(this).toastParent(),
				tr::lng_group_not_accessible(tr::now));
		}
	}).send();
}

void SessionNavigation::showPeerInfo(
		PeerId peerId,
		const SectionShow &params) {
	showPeerInfo(_session->data().peer(peerId), params);
}

void SessionNavigation::showPeerInfo(
		not_null<PeerData*> peer,
		const SectionShow &params) {
	//if (Adaptive::ThreeColumn()
	//	&& !Core::App().settings().thirdSectionInfoEnabled()) {
	//	Core::App().settings().setThirdSectionInfoEnabled(true);
	//	Core::App().saveSettingsDelayed();
	//}
	showSection(std::make_shared<Info::Memento>(peer), params);
}

void SessionNavigation::showPeerInfo(
		not_null<History*> history,
		const SectionShow &params) {
	showPeerInfo(history->peer->id, params);
}

void SessionNavigation::showPeerHistory(
		not_null<PeerData*> peer,
		const SectionShow &params,
		MsgId msgId) {
	showPeerHistory(peer->id, params, msgId);
}

void SessionNavigation::showPeerHistory(
		not_null<History*> history,
		const SectionShow &params,
		MsgId msgId) {
	showPeerHistory(history->peer->id, params, msgId);
}

void SessionNavigation::showSettings(
		Settings::Type type,
		const SectionShow &params) {
	showSection(
		std::make_shared<Info::Memento>(
			Info::Settings::Tag{ _session->user() },
			Info::Section(type)),
		params);
}

void SessionNavigation::showSettings(const SectionShow &params) {
	showSettings(Settings::Main::Id(), params);
}

void SessionNavigation::showPollResults(
		not_null<PollData*> poll,
		FullMsgId contextId,
		const SectionShow &params) {
	showSection(std::make_shared<Info::Memento>(poll, contextId), params);
}

struct SessionController::CachedTheme {
	std::weak_ptr<Ui::ChatTheme> theme;
	std::shared_ptr<Data::DocumentMedia> media;
	Data::WallPaper paper;
	bool caching = false;
	rpl::lifetime lifetime;
};

SessionController::SessionController(
	not_null<Main::Session*> session,
	not_null<Controller*> window)
: SessionNavigation(session)
, _window(window)
, _emojiInteractions(
	std::make_unique<ChatHelpers::EmojiInteractions>(session))
, _isPrimary(window->isPrimary())
, _sendingAnimation(
	std::make_unique<Ui::MessageSendingAnimationController>(this))
, _tabbedSelector(
	std::make_unique<ChatHelpers::TabbedSelector>(
		_window->widget(),
		this,
		GifPauseReason::TabbedPanel))
, _invitePeekTimer([=] { checkInvitePeek(); })
, _activeChatsFilter(session->data().chatsFilters().defaultId())
, _defaultChatTheme(std::make_shared<Ui::ChatTheme>())
, _chatStyle(std::make_unique<Ui::ChatStyle>())
, _cachedReactionIconFactory(std::make_unique<ReactionIconFactory>())
, _giftPremiumValidator(GiftPremiumValidator(this)) {
	init();

	_chatStyleTheme = _defaultChatTheme;
	_chatStyle->apply(_defaultChatTheme.get());

	pushDefaultChatBackground();
	Theme::Background()->updates(
	) | rpl::start_with_next([=](const Theme::BackgroundUpdate &update) {
		if (update.type == Theme::BackgroundUpdate::Type::New
			|| update.type == Theme::BackgroundUpdate::Type::Changed) {
			pushDefaultChatBackground();
		}
	}, _lifetime);

	if (Media::Player::instance()->pauseGifByRoundVideo()) {
		enableGifPauseReason(GifPauseReason::RoundPlaying);
	}

	session->changes().peerUpdates(
		Data::PeerUpdate::Flag::FullInfo
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		return (update.peer == _showEditPeer);
	}) | rpl::start_with_next([=] {
		show(Box<EditPeerInfoBox>(this, base::take(_showEditPeer)));
	}, lifetime());

	session->data().chatsListChanges(
	) | rpl::filter([=](Data::Folder *folder) {
		return (folder != nullptr)
			&& (folder == _openedFolder.current())
			&& folder->chatsList()->indexed()->empty();
	}) | rpl::start_with_next([=](Data::Folder *folder) {
		folder->updateChatListSortPosition();
		closeFolder();
	}, lifetime());

	session->data().chatsFilters().changed(
	) | rpl::start_with_next([=] {
		checkOpenedFilter();
		crl::on_main(this, [=] {
			refreshFiltersMenu();
		});
	}, lifetime());

	session->api().globalPrivacy().suggestArchiveAndMute(
	) | rpl::take(1) | rpl::start_with_next([=] {
		session->api().globalPrivacy().reload(crl::guard(this, [=] {
			if (!session->api().globalPrivacy().archiveAndMuteCurrent()) {
				suggestArchiveAndMute();
			}
		}));
	}, _lifetime);

	session->addWindow(this);

	crl::on_main(this, [=] {
		activateFirstChatsFilter();
		setupPremiumToast();
	});
}

void SessionController::suggestArchiveAndMute() {
	const auto weak = base::make_weak(this);
	_window->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_suggest_hide_new_title());
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_suggest_hide_new_about(Ui::Text::RichLangValue),
			st::boxLabel));
		box->addButton(tr::lng_suggest_hide_new_to_settings(), [=] {
			showSettings(Settings::PrivacySecurity::Id());
		});
		box->setCloseByOutsideClick(false);
		box->boxClosing(
		) | rpl::start_with_next([=] {
			crl::on_main(weak, [=] {
				auto &privacy = session().api().globalPrivacy();
				privacy.dismissArchiveAndMuteSuggestion();
			});
		}, box->lifetime());
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

PeerData *SessionController::singlePeer() const {
	return _window->singlePeer();
}

bool SessionController::isPrimary() const {
	return _isPrimary;
}

not_null<::MainWindow*> SessionController::widget() const {
	return _window->widget();
}

auto SessionController::sendingAnimation() const
-> Ui::MessageSendingAnimationController & {
	return *_sendingAnimation;
}

auto SessionController::tabbedSelector() const
-> not_null<ChatHelpers::TabbedSelector*> {
	return _tabbedSelector.get();
}

void SessionController::takeTabbedSelectorOwnershipFrom(
		not_null<QWidget*> parent) {
	if (_tabbedSelector->parent() == parent) {
		if (const auto chats = widget()->sessionContent()) {
			chats->returnTabbedSelector();
		}
		if (_tabbedSelector->parent() == parent) {
			_tabbedSelector->hide();
			_tabbedSelector->setParent(widget());
		}
	}
}

bool SessionController::hasTabbedSelectorOwnership() const {
	return (_tabbedSelector->parent() == widget());
}

void SessionController::showEditPeerBox(PeerData *peer) {
	_showEditPeer = peer;
	session().api().requestFullPeer(peer);
}

void SessionController::showGiftPremiumBox(UserData *user) {
	if (user) {
		_giftPremiumValidator.showBox(user);
	} else {
		_giftPremiumValidator.cancel();
	}
}

void SessionController::init() {
	if (session().supportMode()) {
		initSupportMode();
	}
}

void SessionController::initSupportMode() {
	session().supportHelper().registerWindow(this);

	Shortcuts::Requests(
	) | rpl::filter([=] {
		return (Core::App().activeWindow() == &window());
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using C = Shortcuts::Command;

		request->check(C::SupportHistoryBack) && request->handle([=] {
			return chatEntryHistoryMove(-1);
		});
		request->check(C::SupportHistoryForward) && request->handle([=] {
			return chatEntryHistoryMove(1);
		});
	}, lifetime());
}

void SessionController::toggleFiltersMenu(bool enabled) {
	if (!_isPrimary || (!enabled == !_filters)) {
		return;
	} else if (enabled) {
		_filters = std::make_unique<FiltersMenu>(
			widget()->bodyWidget(),
			this);
	} else {
		_filters = nullptr;
	}
	_filtersMenuChanged.fire({});
}

void SessionController::reloadFiltersMenu() {
	const auto enabled = !session().data().chatsFilters().list().empty();
	if (enabled) {
		auto previousFilter = activeChatsFilterCurrent();
		rpl::single(rpl::empty) | rpl::then(
			filtersMenuChanged()
		) | rpl::start_with_next([=] {
			toggleFiltersMenu(true);
			if (previousFilter) {
				if (activeChatsFilterCurrent() != previousFilter) {
					resetFakeUnreadWhileOpened();
				}
				_activeChatsFilter.force_assign(previousFilter);
				if (previousFilter) {
					closeFolder(true);
				}
			}
		}, lifetime());

		if (activeChatsFilterCurrent() != 0) {
			resetFakeUnreadWhileOpened();
		}
		_activeChatsFilter.force_assign(0);
		toggleFiltersMenu(false);
	}
}

void SessionController::refreshFiltersMenu() {
	toggleFiltersMenu(session().data().chatsFilters().has());
}

rpl::producer<> SessionController::filtersMenuChanged() const {
	return _filtersMenuChanged.events();
}

void SessionController::checkOpenedFilter() {
	activateFirstChatsFilter();
	if (const auto filterId = activeChatsFilterCurrent()) {
		const auto &list = session().data().chatsFilters().list();
		const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
		if (i == end(list)) {
			const auto defaultFilterId = session().account().defaultFilterId();
			const auto j = ranges::find(list, FilterId(defaultFilterId), &Data::ChatFilter::id);
			setActiveChatsFilter(j == end(list) ? 0 : defaultFilterId);
		}
	}
}

void SessionController::activateFirstChatsFilter() {
	if (_filtersActivated || !session().data().chatsFilters().loaded()) {
		return;
	}
	_filtersActivated = true;
	setActiveChatsFilter(session().data().chatsFilters().defaultId());
}

bool SessionController::uniqueChatsInSearchResults() const {
	return session().supportMode()
		&& !session().settings().supportAllSearchResults()
		&& !searchInChat.current();
}

void SessionController::openFolder(not_null<Data::Folder*> folder) {
	if (_openedFolder.current() != folder) {
		resetFakeUnreadWhileOpened();
	}
	setActiveChatsFilter(0);
	_openedFolder = folder.get();
}

void SessionController::closeFolder(bool force) {
	const auto defaultFilterId = session().account().defaultFilterId();
	if (defaultFilterId == 0 || force) {
		_openedFolder = nullptr;
	} else {
		setActiveChatsFilter(defaultFilterId);
		checkOpenedFilter();
	}
}

void SessionController::setupPremiumToast() {
	rpl::combine(
		Data::AmPremiumValue(&session()),
		session().changes().peerUpdates(
			Data::PeerUpdate::Flag::FullInfo
		)
	) | rpl::filter([=] {
		return session().user()->isFullLoaded();
	}) | rpl::map([=](bool premium, const auto&) {
		return premium;
	}) | rpl::distinct_until_changed() | rpl::skip(
		1
	) | rpl::filter([=](bool premium) {
		session().mtp().requestConfig();
		return premium;
	}) | rpl::start_with_next([=] {
		Ui::Toast::Show(
			Window::Show(this).toastParent(),
			{
				.text = { tr::lng_premium_success(tr::now) },
				.st = &st::defaultToast,
			});
	}, _lifetime);
}

const rpl::variable<Data::Folder*> &SessionController::openedFolder() const {
	return _openedFolder;
}

void SessionController::setActiveChatEntry(Dialogs::RowDescriptor row) {
	const auto was = _activeChatEntry.current().key.history();
	const auto now = row.key.history();
	if (was && was != now) {
		was->setFakeUnreadWhileOpened(false);
		_invitePeekTimer.cancel();
	}
	_activeChatEntry = row;
	if (now) {
		now->setFakeUnreadWhileOpened(true);
	}
	if (session().supportMode()) {
		pushToChatEntryHistory(row);
	}
	checkInvitePeek();
}

void SessionController::checkInvitePeek() {
	const auto history = activeChatCurrent().history();
	if (!history) {
		return;
	}
	const auto channel = history->peer->asChannel();
	if (!channel) {
		return;
	}
	const auto expires = channel->invitePeekExpires();
	if (!expires) {
		return;
	}
	const auto now = base::unixtime::now();
	if (expires > now) {
		_invitePeekTimer.callOnce((expires - now) * crl::time(1000));
		return;
	}
	const auto hash = channel->invitePeekHash();
	channel->clearInvitePeek();
	Api::CheckChatInvite(this, hash, channel);
}

void SessionController::resetFakeUnreadWhileOpened() {
	if (const auto history = _activeChatEntry.current().key.history()) {
		history->setFakeUnreadWhileOpened(false);
	}
}

bool SessionController::chatEntryHistoryMove(int steps) {
	if (_chatEntryHistory.empty()) {
		return false;
	}
	const auto position = _chatEntryHistoryPosition + steps;
	if (!base::in_range(position, 0, int(_chatEntryHistory.size()))) {
		return false;
	}
	_chatEntryHistoryPosition = position;
	return jumpToChatListEntry(_chatEntryHistory[position]);
}

bool SessionController::jumpToChatListEntry(Dialogs::RowDescriptor row) {
	if (const auto history = row.key.history()) {
		Ui::showPeerHistory(history, row.fullId.msg);
		return true;
	}
	return false;
}

Dialogs::RowDescriptor SessionController::resolveChatNext(
		Dialogs::RowDescriptor from) const {
	return content()->resolveChatNext(from);
}

Dialogs::RowDescriptor SessionController::resolveChatPrevious(
		Dialogs::RowDescriptor from) const {
	return content()->resolveChatPrevious(from);
}

void SessionController::pushToChatEntryHistory(Dialogs::RowDescriptor row) {
	if (!_chatEntryHistory.empty()
		&& _chatEntryHistory[_chatEntryHistoryPosition] == row) {
		return;
	}
	_chatEntryHistory.resize(++_chatEntryHistoryPosition);
	_chatEntryHistory.push_back(row);
	if (_chatEntryHistory.size() > kMaxChatEntryHistorySize) {
		_chatEntryHistory.pop_front();
		--_chatEntryHistoryPosition;
	}
}

void SessionController::setActiveChatEntry(Dialogs::Key key) {
	setActiveChatEntry({ key, FullMsgId() });
}

Dialogs::RowDescriptor SessionController::activeChatEntryCurrent() const {
	return _activeChatEntry.current();
}

Dialogs::Key SessionController::activeChatCurrent() const {
	return activeChatEntryCurrent().key;
}

auto SessionController::activeChatEntryChanges() const
-> rpl::producer<Dialogs::RowDescriptor> {
	return _activeChatEntry.changes();
}

rpl::producer<Dialogs::Key> SessionController::activeChatChanges() const {
	return activeChatEntryChanges(
	) | rpl::map([](const Dialogs::RowDescriptor &value) {
		return value.key;
	}) | rpl::distinct_until_changed();
}

auto SessionController::activeChatEntryValue() const
-> rpl::producer<Dialogs::RowDescriptor> {
	return _activeChatEntry.value();
}

rpl::producer<Dialogs::Key> SessionController::activeChatValue() const {
	return activeChatEntryValue(
	) | rpl::map([](const Dialogs::RowDescriptor &value) {
		return value.key;
	}) | rpl::distinct_until_changed();
}

void SessionController::enableGifPauseReason(GifPauseReason reason) {
	if (!(_gifPauseReasons & reason)) {
		auto notify = (static_cast<int>(_gifPauseReasons) < static_cast<int>(reason));
		_gifPauseReasons |= reason;
		if (notify) {
			_gifPauseLevelChanged.fire({});
		}
	}
}

void SessionController::disableGifPauseReason(GifPauseReason reason) {
	if (_gifPauseReasons & reason) {
		_gifPauseReasons &= ~reason;
		if (_gifPauseReasons < reason) {
			_gifPauseLevelChanged.fire({});
		}
	}
}

bool SessionController::isGifPausedAtLeastFor(GifPauseReason reason) const {
	if (reason == GifPauseReason::Any) {
		return (_gifPauseReasons != 0) || !widget()->isActive();
	}
	return (static_cast<int>(_gifPauseReasons) >= 2 * static_cast<int>(reason)) || !widget()->isActive();
}

void SessionController::floatPlayerAreaUpdated() {
	if (const auto main = widget()->sessionContent()) {
		main->floatPlayerAreaUpdated();
	}
}

int SessionController::dialogsSmallColumnWidth() const {
	return st::dialogsPadding.x() + (::Kotato::JsonSettings::GetInt("chat_list_lines") == 1 ? st::dialogsUnreadHeight : st::dialogsPhotoSize) + st::dialogsPadding.x();
}

int SessionController::minimalThreeColumnWidth() const {
	return (_isPrimary ? st::columnMinimalWidthLeft : 0)
		+ st::columnMinimalWidthMain
		+ st::columnMinimalWidthThird;
}

bool SessionController::forceWideDialogs() const {
	if (dialogsListDisplayForced().value()) {
		return true;
	} else if (dialogsListFocused().value()) {
		return true;
	}
	return !content()->isMainSectionShown();
}

auto SessionController::computeColumnLayout() const -> ColumnLayout {
	auto layout = Adaptive::WindowLayout::OneColumn;

	auto bodyWidth = widget()->bodyWidget()->width() - filtersWidth();
	auto dialogsWidth = 0, chatWidth = 0, thirdWidth = 0;

	auto useOneColumnLayout = [&] {
		auto minimalNormal = st::columnMinimalWidthLeft
			+ st::columnMinimalWidthMain;
		if (_isPrimary && bodyWidth < minimalNormal) {
			return true;
		}
		return false;
	};

	auto useNormalLayout = [&] {
		// Used if useSmallColumnLayout() == false.
		if (bodyWidth < minimalThreeColumnWidth()) {
			return true;
		}
		if (!Core::App().settings().tabbedSelectorSectionEnabled()
			&& !Core::App().settings().thirdSectionInfoEnabled()) {
			return true;
		}
		return false;
	};

	if (useOneColumnLayout()) {
		dialogsWidth = chatWidth = bodyWidth;
	} else if (useNormalLayout()) {
		layout = Adaptive::WindowLayout::Normal;
		dialogsWidth = countDialogsWidthFromRatio(bodyWidth);
		accumulate_min(dialogsWidth, bodyWidth - st::columnMinimalWidthMain);
		chatWidth = bodyWidth - dialogsWidth;
	} else {
		layout = Adaptive::WindowLayout::ThreeColumn;
		dialogsWidth = countDialogsWidthFromRatio(bodyWidth);
		thirdWidth = countThirdColumnWidthFromRatio(bodyWidth);
		auto shrink = shrinkDialogsAndThirdColumns(
			dialogsWidth,
			thirdWidth,
			bodyWidth);
		dialogsWidth = shrink.dialogsWidth;
		thirdWidth = shrink.thirdWidth;

		chatWidth = bodyWidth - dialogsWidth - thirdWidth;
	}
	return { bodyWidth, dialogsWidth, chatWidth, thirdWidth, layout };
}

int SessionController::countDialogsWidthFromRatio(int bodyWidth) const {
	if (!_isPrimary) {
		return 0;
	}
	auto result = qRound(bodyWidth * Core::App().settings().dialogsWidthRatio());
	accumulate_max(result, st::columnMinimalWidthLeft);
//	accumulate_min(result, st::columnMaximalWidthLeft);
	return result;
}

int SessionController::countThirdColumnWidthFromRatio(int bodyWidth) const {
	auto result = Core::App().settings().thirdColumnWidth();
	accumulate_max(result, st::columnMinimalWidthThird);
	accumulate_min(result, st::columnMaximalWidthThird);
	return result;
}

SessionController::ShrinkResult SessionController::shrinkDialogsAndThirdColumns(
		int dialogsWidth,
		int thirdWidth,
		int bodyWidth) const {
	auto chatWidth = st::columnMinimalWidthMain;
	if (dialogsWidth + thirdWidth + chatWidth <= bodyWidth) {
		return { dialogsWidth, thirdWidth };
	}
	auto thirdWidthNew = ((bodyWidth - chatWidth) * thirdWidth)
		/ (dialogsWidth + thirdWidth);
	auto dialogsWidthNew = ((bodyWidth - chatWidth) * dialogsWidth)
		/ (dialogsWidth + thirdWidth);
	if (thirdWidthNew < st::columnMinimalWidthThird) {
		thirdWidthNew = st::columnMinimalWidthThird;
		dialogsWidthNew = bodyWidth - thirdWidthNew - chatWidth;
		Assert(!_isPrimary || dialogsWidthNew >= st::columnMinimalWidthLeft);
	} else if (_isPrimary && dialogsWidthNew < st::columnMinimalWidthLeft) {
		dialogsWidthNew = st::columnMinimalWidthLeft;
		thirdWidthNew = bodyWidth - dialogsWidthNew - chatWidth;
		Assert(thirdWidthNew >= st::columnMinimalWidthThird);
	}
	return { dialogsWidthNew, thirdWidthNew };
}

bool SessionController::canShowThirdSection() const {
	auto currentLayout = computeColumnLayout();
	auto minimalExtendBy = minimalThreeColumnWidth()
		- currentLayout.bodyWidth;
	return (minimalExtendBy <= widget()->maximalExtendBy());
}

bool SessionController::canShowThirdSectionWithoutResize() const {
	auto currentWidth = computeColumnLayout().bodyWidth;
	return currentWidth >= minimalThreeColumnWidth();
}

bool SessionController::takeThirdSectionFromLayer() {
	return widget()->takeThirdSectionFromLayer();
}

void SessionController::resizeForThirdSection() {
	if (adaptive().isThreeColumn()) {
		return;
	}

	auto &settings = Core::App().settings();
	auto layout = computeColumnLayout();
	auto tabbedSelectorSectionEnabled =
		settings.tabbedSelectorSectionEnabled();
	auto thirdSectionInfoEnabled =
		settings.thirdSectionInfoEnabled();
	settings.setTabbedSelectorSectionEnabled(false);
	settings.setThirdSectionInfoEnabled(false);

	auto wanted = countThirdColumnWidthFromRatio(layout.bodyWidth);
	auto minimal = st::columnMinimalWidthThird;
	auto extendBy = wanted;
	auto extendedBy = [&] {
		// Best - extend by third column without moving the window.
		// Next - extend by minimal third column without moving.
		// Next - show third column inside the window without moving.
		// Last - extend with moving.
		if (widget()->canExtendNoMove(wanted)) {
			return widget()->tryToExtendWidthBy(wanted);
		} else if (widget()->canExtendNoMove(minimal)) {
			extendBy = minimal;
			return widget()->tryToExtendWidthBy(minimal);
		} else if (layout.bodyWidth >= minimalThreeColumnWidth()) {
			return 0;
		}
		return widget()->tryToExtendWidthBy(minimal);
	}();
	if (extendedBy) {
		if (extendBy != settings.thirdColumnWidth()) {
			settings.setThirdColumnWidth(extendBy);
		}
		auto newBodyWidth = layout.bodyWidth + extendedBy;
		auto currentRatio = settings.dialogsWidthRatio();
		settings.setDialogsWidthRatio(
			(currentRatio * layout.bodyWidth) / newBodyWidth);
	}
	auto savedValue = (extendedBy == extendBy) ? -1 : extendedBy;
	settings.setThirdSectionExtendedBy(savedValue);

	settings.setTabbedSelectorSectionEnabled(
		tabbedSelectorSectionEnabled);
	settings.setThirdSectionInfoEnabled(
		thirdSectionInfoEnabled);
}

void SessionController::closeThirdSection() {
	auto &settings = Core::App().settings();
	auto newWindowSize = widget()->size();
	auto layout = computeColumnLayout();
	if (layout.windowLayout == Adaptive::WindowLayout::ThreeColumn) {
		auto noResize = widget()->isFullScreen()
			|| widget()->isMaximized();
		auto savedValue = settings.thirdSectionExtendedBy();
		auto extendedBy = (savedValue == -1)
			? layout.thirdWidth
			: savedValue;
		auto newBodyWidth = noResize
			? layout.bodyWidth
			: (layout.bodyWidth - extendedBy);
		auto currentRatio = settings.dialogsWidthRatio();
		settings.setDialogsWidthRatio(
			(currentRatio * layout.bodyWidth) / newBodyWidth);
		newWindowSize = QSize(
			widget()->width() + (newBodyWidth - layout.bodyWidth),
			widget()->height());
	}
	settings.setTabbedSelectorSectionEnabled(false);
	settings.setThirdSectionInfoEnabled(false);
	Core::App().saveSettingsDelayed();
	if (widget()->size() != newWindowSize) {
		widget()->resize(newWindowSize);
	} else {
		updateColumnLayout();
	}
}

bool SessionController::canShowSeparateWindow(
		not_null<PeerData*> peer) const {
	return peer->computeUnavailableReason().isEmpty();
}

void SessionController::showPeer(not_null<PeerData*> peer, MsgId msgId) {
	const auto currentPeer = activeChatCurrent().peer();
	if (peer && peer->isChannel() && currentPeer != peer) {
		const auto clickedChannel = peer->asChannel();
		if (!clickedChannel->isPublic()
			&& !clickedChannel->amIn()
			&& (!currentPeer->isChannel()
				|| currentPeer->asChannel()->linkedChat()
					!= clickedChannel)) {
			Ui::ShowMultilineToast({
				.parentOverride = Window::Show(this).toastParent(),
				.text = {
					.text = peer->isMegagroup()
						? tr::lng_group_not_accessible(tr::now)
						: tr::lng_channel_not_accessible(tr::now)
				},
			});
		} else {
			showPeerHistory(peer->id, SectionShow(), msgId);
		}
	} else {
		showPeerInfo(peer, SectionShow());
	}
}

void SessionController::startOrJoinGroupCall(
		not_null<PeerData*> peer,
		Calls::StartGroupCallArgs args) {
	Core::App().calls().startOrJoinGroupCall(
		std::make_shared<Show>(this),
		peer,
		args);
}

void SessionController::showCalendar(Dialogs::Key chat, QDateTime requestedDateTime) {
	const auto history = chat.history();
	if (!history) {
		return;
	}
	const auto currentPeerDate = [&] {
		if (history->scrollTopItem) {
			return history->scrollTopItem->dateTime();
		} else if (history->loadedAtTop()
			&& !history->isEmpty()
			&& history->peer->migrateFrom()) {
			if (const auto migrated = history->owner().historyLoaded(history->peer->migrateFrom())) {
				if (migrated->scrollTopItem) {
					// We're up in the migrated history.
					// So current date is the date of first message here.
					return history->blocks.front()->messages.front()->dateTime();
				}
			}
		} else if (const auto item = history->lastMessage()) {
			return base::unixtime::parse(item->date());
		}
		return QDateTime();
	}();
	const auto maxPeerDate = [&] {
		const auto check = history->peer->migrateTo()
			? history->owner().historyLoaded(history->peer->migrateTo())
			: history;
		if (const auto item = check ? check->lastMessage() : nullptr) {
			return base::unixtime::parse(item->date());
		}
		return QDateTime();
	}();
	const auto minPeerDate = [&] {
		const auto startDate = [] {
			// Telegram was launched in August 2013 :)
			return QDate(2013, 8, 1).startOfDay();
		};
		if (const auto chat = history->peer->migrateFrom()) {
			if (const auto history = chat->owner().historyLoaded(chat)) {
				if (history->loadedAtTop()) {
					if (!history->isEmpty()) {
						return history->blocks.front()->messages.front()->dateTime();
					}
				} else {
					return startDate();
				}
			}
		}
		if (history->loadedAtTop()) {
			if (!history->isEmpty()) {
				return history->blocks.front()->messages.front()->dateTime();
			}
			return QDateTime::currentDateTime();
		}
		return startDate();
	}();
	const auto highlighted = !requestedDateTime.isNull()
		? requestedDateTime
		: !currentPeerDate.isNull()
		? currentPeerDate
		: QDateTime::currentDateTime();

	struct ButtonState {
		enum class Type {
			None,
			Disabled,
			Active,
		};
		Type type = Type::None;
		style::complex_color disabledFg = style::complex_color([] {
			auto result = st::attentionBoxButton.textFg->c;
			result.setAlpha(result.alpha() / 2);
			return result;
		});
		style::RoundButton disabled = st::attentionBoxButton;
	};
	const auto buttonState = std::make_shared<ButtonState>();
	buttonState->disabled.textFg
		= buttonState->disabled.textFgOver
		= buttonState->disabledFg.color();
	buttonState->disabled.ripple.color
		= buttonState->disabled.textBgOver
		= buttonState->disabled.textBg;
	const auto selectionChanged = [=](
			not_null<Ui::CalendarBox*> box,
			std::optional<int> selected) {
		if (!selected.has_value()) {
			buttonState->type = ButtonState::Type::None;
			return;
		}
		const auto type = (*selected > 0)
			? ButtonState::Type::Active
			: ButtonState::Type::Disabled;
		if (buttonState->type == type) {
			return;
		}
		buttonState->type = type;
		box->clearButtons();
		box->addButton(tr::lng_cancel(), [=] {
			box->toggleSelectionMode(false);
		});
		auto text = tr::lng_profile_clear_history();
		const auto button = box->addLeftButton(std::move(text), [=] {
			const auto firstDate = box->selectedFirstDate();
			const auto lastDate = box->selectedLastDate();
			if (!firstDate.isNull()) {
				auto confirm = Box<DeleteMessagesBox>(
					history->peer,
					firstDate,
					lastDate);
				confirm->setDeleteConfirmedCallback(crl::guard(box, [=] {
					box->closeBox();
				}));
				box->getDelegate()->show(std::move(confirm));
			}
		}, (*selected > 0) ? st::attentionBoxButton : buttonState->disabled);
		if (!*selected) {
			button->setPointerCursor(false);
		}
	};

	const auto weak = base::make_weak(this);
	const auto jump = [=](const QDateTime &date) {
		const auto open = [=](not_null<PeerData*> peer, MsgId id) {
			if (const auto strong = weak.get()) {
				strong->showPeerHistory(peer, SectionShow::Way::Forward, id);
			}
		};
		session().api().resolveJumpToDate(chat, date, open);
	};
	const auto showCalendarCallback = [=] {
		show(Box<Ui::CalendarBox>(Ui::CalendarBoxArgs{
			.month = highlighted.date(),
			.highlighted = highlighted.date(),
			.callback = [=](const QDate &date) { jump(date.startOfDay()); },
			.minDate = minPeerDate.date(),
			.maxDate = maxPeerDate.date(),
			.allowsSelection = history->peer->isUser(),
			.selectionChanged = selectionChanged,
		}), Ui::LayerOption::CloseOther);
	};

	show(Box(ChooseJumpDateTimeBox,
			minPeerDate,
			maxPeerDate,
			highlighted,
			[=](TimeId result) {
				jump(base::unixtime::parse(result));
			},
			[=] {
				jump(minPeerDate);
			},
			std::move(showCalendarCallback),
			history->peer->isUser()),
		Ui::LayerOption::KeepOther);
}

void SessionController::showPassportForm(const Passport::FormRequest &request) {
	_passportForm = std::make_unique<Passport::FormController>(
		this,
		request);
	_passportForm->show();
}

void SessionController::clearPassportForm() {
	_passportForm = nullptr;
}

void SessionController::showChooseReportMessages(
		not_null<PeerData*> peer,
		Ui::ReportReason reason,
		Fn<void(MessageIdsList)> done) {
	content()->showChooseReportMessages(peer, reason, std::move(done));
}

void SessionController::clearChooseReportMessages() {
	content()->clearChooseReportMessages();
}

void SessionController::toggleChooseChatTheme(not_null<PeerData*> peer) {
	content()->toggleChooseChatTheme(peer);
}

void SessionController::updateColumnLayout() {
	content()->updateColumnLayout();
}

void SessionController::showPeerHistory(
		PeerId peerId,
		const SectionShow &params,
		MsgId msgId) {
	content()->ui_showPeerHistory(peerId, params, msgId);
}

void SessionController::showPeerHistoryAtItem(
		not_null<const HistoryItem*> item) {
	_window->invokeForSessionController(
		&item->history()->peer->session().account(),
		item->history()->peer,
		[&](not_null<SessionController*> controller) {
			if (item->isScheduled()) {
				controller->showSection(
					std::make_shared<HistoryView::ScheduledMemento>(
						item->history()));
			} else {
				controller->showPeerHistory(
					item->history()->peer,
					SectionShow::Way::ClearStack,
					item->id);
			}
		});
}

void SessionController::cancelUploadLayer(not_null<HistoryItem*> item) {
	const auto itemId = item->fullId();
	session().uploader().pause(itemId);
	const auto stopUpload = [=](Fn<void()> close) {
		auto &data = session().data();
		if (const auto item = data.message(itemId)) {
			if (!item->isEditingMedia()) {
				const auto history = item->history();
				item->destroy();
				history->requestChatListMessage();
			} else {
				item->returnSavedMedia();
				session().uploader().cancel(item->fullId());
			}
			data.sendHistoryChangeNotifications();
		}
		session().uploader().unpause();
		close();
	};
	const auto continueUpload = [=](Fn<void()> close) {
		session().uploader().unpause();
		close();
	};

	show(Ui::MakeConfirmBox({
		.text = tr::lng_selected_cancel_sure_this(),
		.confirmed = stopUpload,
		.cancelled = continueUpload,
		.confirmText = tr::lng_box_yes(),
		.cancelText = tr::lng_box_no(),
	}));
}

void SessionController::showSection(
		std::shared_ptr<SectionMemento> memento,
		const SectionShow &params) {
	if (!params.thirdColumn && widget()->showSectionInExistingLayer(
			memento.get(),
			params)) {
		return;
	}
	content()->showSection(std::move(memento), params);
}

void SessionController::showBackFromStack(const SectionShow &params) {
	content()->showBackFromStack(params);
}

void SessionController::showSpecialLayer(
		object_ptr<Ui::LayerWidget> &&layer,
		anim::type animated) {
	widget()->showSpecialLayer(std::move(layer), animated);
}

void SessionController::showLayer(
		std::unique_ptr<Ui::LayerWidget> &&layer,
		Ui::LayerOptions options,
		anim::type animated) {
	_window->showLayer(std::move(layer), options, animated);
}

void SessionController::removeLayerBlackout() {
	widget()->ui_removeLayerBlackout();
}

not_null<MainWidget*> SessionController::content() const {
	return widget()->sessionContent();
}

int SessionController::filtersWidth() const {
	return _filters
			? (::Kotato::JsonSettings::GetBool("folders/hide_names")
				? st::windowFiltersWidthNoText
				: st::windowFiltersWidth)
			: 0;
}

rpl::producer<FilterId> SessionController::activeChatsFilter() const {
	return _activeChatsFilter.value();
}

FilterId SessionController::activeChatsFilterCurrent() const {
	return _activeChatsFilter.current();
}

void SessionController::setActiveChatsFilter(FilterId id) {
	if (activeChatsFilterCurrent() != id) {
		resetFakeUnreadWhileOpened();
	}
	_activeChatsFilter.force_assign(id);
	if (id) {
		closeFolder(true);
	}
	if (adaptive().isOneColumn()) {
		Ui::showChatsList(&session());
	}
}

void SessionController::showAddContact() {
	_window->show(
		Box<AddContactBox>(&session()),
		Ui::LayerOption::KeepOther);
}

void SessionController::showNewGroup() {
	_window->show(
		Box<GroupInfoBox>(this, GroupInfoBox::Type::Group),
		Ui::LayerOption::KeepOther);
}

void SessionController::showNewChannel() {
	_window->show(
		Box<GroupInfoBox>(this, GroupInfoBox::Type::Channel),
		Ui::LayerOption::KeepOther);
}

Window::Adaptive &SessionController::adaptive() const {
	return _window->adaptive();
}

void SessionController::setConnectingBottomSkip(int skip) {
	_connectingBottomSkip = skip;
}

rpl::producer<int> SessionController::connectingBottomSkipValue() const {
	return _connectingBottomSkip.value();
}

QPointer<Ui::BoxContent> SessionController::show(
		object_ptr<Ui::BoxContent> content,
		Ui::LayerOptions options,
		anim::type animated) {
	return _window->show(std::move(content), options, animated);
}

void SessionController::hideLayer(anim::type animated) {
	_window->hideLayer(animated);
}

void SessionController::openPhoto(
		not_null<PhotoData*> photo,
		FullMsgId contextId) {
	_window->openInMediaView(Media::View::OpenRequest(
		this,
		photo,
		session().data().message(contextId)));
}

void SessionController::openPhoto(
		not_null<PhotoData*> photo,
		not_null<PeerData*> peer) {
	_window->openInMediaView(Media::View::OpenRequest(this, photo, peer));
}

void SessionController::openDocument(
		not_null<DocumentData*> document,
		FullMsgId contextId,
		bool showInMediaView) {
	if (showInMediaView) {
		_window->openInMediaView(Media::View::OpenRequest(
			this,
			document,
			session().data().message(contextId)));
		return;
	}
	Data::ResolveDocument(
		this,
		document,
		session().data().message(contextId));
}

auto SessionController::cachedChatThemeValue(
	const Data::CloudTheme &data,
	Data::CloudThemeType type)
-> rpl::producer<std::shared_ptr<Ui::ChatTheme>> {
	const auto key = Ui::ChatThemeKey{
		data.id,
		(type == Data::CloudThemeType::Dark),
	};
	const auto settings = data.settings.find(type);
	if (!key
		|| (settings == end(data.settings))
		|| !settings->second.paper
		|| settings->second.paper->backgroundColors().empty()) {
		return rpl::single(_defaultChatTheme);
	}
	const auto i = _customChatThemes.find(key);
	if (i != end(_customChatThemes)) {
		if (auto strong = i->second.theme.lock()) {
			pushLastUsedChatTheme(strong);
			return rpl::single(std::move(strong));
		}
	}
	if (i == end(_customChatThemes) || !i->second.caching) {
		cacheChatTheme(data, type);
	}
	const auto limit = Data::CloudThemes::TestingColors() ? (1 << 20) : 1;
	using namespace rpl::mappers;
	return rpl::single(
		_defaultChatTheme
	) | rpl::then(_cachedThemesStream.events(
	) | rpl::filter([=](const std::shared_ptr<Ui::ChatTheme> &theme) {
		if (theme->key() != key) {
			return false;
		}
		pushLastUsedChatTheme(theme);
		return true;
	}) | rpl::take(limit));
}

void SessionController::pushLastUsedChatTheme(
		const std::shared_ptr<Ui::ChatTheme> &theme) {
	const auto i = ranges::find(_lastUsedCustomChatThemes, theme);
	if (i == end(_lastUsedCustomChatThemes)) {
		if (_lastUsedCustomChatThemes.size() >= kCustomThemesInMemory) {
			_lastUsedCustomChatThemes.pop_back();
		}
		_lastUsedCustomChatThemes.push_front(theme);
	} else if (i != begin(_lastUsedCustomChatThemes)) {
		std::rotate(begin(_lastUsedCustomChatThemes), i, i + 1);
	}
}

not_null<Ui::ChatTheme*> SessionController::currentChatTheme() const {
	if (const auto custom = content()->customChatTheme()) {
		return custom;
	}
	return defaultChatTheme().get();
}

void SessionController::setChatStyleTheme(
		const std::shared_ptr<Ui::ChatTheme> &theme) {
	if (_chatStyleTheme.lock() == theme) {
		return;
	}
	_chatStyleTheme = theme;
	_chatStyle->apply(theme.get());
}

void SessionController::clearCachedChatThemes() {
	_customChatThemes.clear();
}

void SessionController::overridePeerTheme(
		not_null<PeerData*> peer,
		std::shared_ptr<Ui::ChatTheme> theme) {
	_peerThemeOverride = PeerThemeOverride{
		peer,
		theme ? theme : _defaultChatTheme,
	};
}

void SessionController::clearPeerThemeOverride(not_null<PeerData*> peer) {
	if (_peerThemeOverride.current().peer == peer.get()) {
		_peerThemeOverride = PeerThemeOverride();
	}
}

void SessionController::pushDefaultChatBackground() {
	const auto background = Theme::Background();
	const auto &paper = background->paper();
	_defaultChatTheme->setBackground({
		.prepared = background->prepared(),
		.preparedForTiled = background->preparedForTiled(),
		.gradientForFill = background->gradientForFill(),
		.colorForFill = background->colorForFill(),
		.colors = paper.backgroundColors(),
		.patternOpacity = paper.patternOpacity(),
		.gradientRotation = paper.gradientRotation(),
		.isPattern = paper.isPattern(),
		.tile = background->tile(),
	});
}

void SessionController::cacheChatTheme(
		const Data::CloudTheme &data,
		Data::CloudThemeType type) {
	Expects(data.id != 0);

	const auto dark = (type == Data::CloudThemeType::Dark);
	const auto key = Ui::ChatThemeKey{ data.id, dark };
	const auto i = data.settings.find(type);
	Assert(i != end(data.settings));
	const auto &paper = i->second.paper;
	Assert(paper.has_value());
	Assert(!paper->backgroundColors().empty());
	const auto document = paper->document();
	const auto media = document ? document->createMediaView() : nullptr;
	paper->loadDocument();
	auto &theme = [&]() -> CachedTheme& {
		const auto i = _customChatThemes.find(key);
		if (i != end(_customChatThemes)) {
			i->second.media = media;
			i->second.paper = *paper;
			i->second.caching = true;
			return i->second;
		}
		return _customChatThemes.emplace(
			key,
			CachedTheme{
				.media = media,
				.paper = *paper,
				.caching = true,
			}).first->second;
	}();
	auto descriptor = Ui::ChatThemeDescriptor{
		.key = key,
		.preparePalette = PreparePaletteCallback(
			dark,
			i->second.accentColor),
		.backgroundData = backgroundData(theme),
		.bubblesData = PrepareBubblesData(data, type),
		.basedOnDark = dark,
	};
	crl::async([
		this,
		descriptor = std::move(descriptor),
		weak = base::make_weak(this)
	]() mutable {
		crl::on_main(weak,[
			this,
			result = std::make_shared<Ui::ChatTheme>(std::move(descriptor))
		]() mutable {
			result->finishCreateOnMain();
			cacheChatThemeDone(std::move(result));
		});
	});
	if (media && media->loaded(true)) {
		theme.media = nullptr;
	}
}

void SessionController::cacheChatThemeDone(
		std::shared_ptr<Ui::ChatTheme> result) {
	Expects(result != nullptr);

	const auto key = result->key();
	const auto i = _customChatThemes.find(key);
	if (i == end(_customChatThemes)) {
		return;
	}
	i->second.caching = false;
	i->second.theme = result;
	if (i->second.media) {
		if (i->second.media->loaded(true)) {
			updateCustomThemeBackground(i->second);
		} else {
			session().downloaderTaskFinished(
			) | rpl::filter([=] {
				const auto i = _customChatThemes.find(key);
				Assert(i != end(_customChatThemes));
				return !i->second.media || i->second.media->loaded(true);
			}) | rpl::start_with_next([=] {
				const auto i = _customChatThemes.find(key);
				Assert(i != end(_customChatThemes));
				updateCustomThemeBackground(i->second);
			}, i->second.lifetime);
		}
	}
	_cachedThemesStream.fire(std::move(result));
}

void SessionController::updateCustomThemeBackground(CachedTheme &theme) {
	const auto guard = gsl::finally([&] {
		theme.lifetime.destroy();
		theme.media = nullptr;
	});
	const auto strong = theme.theme.lock();
	if (!theme.media || !strong || !theme.media->loaded(true)) {
		return;
	}
	const auto key = strong->key();
	const auto weak = base::make_weak(this);
	crl::async([=, data = backgroundData(theme, false)] {
		crl::on_main(weak, [
			=,
			result = Ui::PrepareBackgroundImage(data)
		]() mutable {
			const auto i = _customChatThemes.find(key);
			if (i != end(_customChatThemes)) {
				if (const auto strong = i->second.theme.lock()) {
					strong->updateBackgroundImageFrom(std::move(result));
				}
			}
		});
	});
}

Ui::ChatThemeBackgroundData SessionController::backgroundData(
		CachedTheme &theme,
		bool generateGradient) const {
	const auto &paper = theme.paper;
	const auto &media = theme.media;
	const auto paperPath = media ? media->owner()->filepath() : QString();
	const auto paperBytes = media ? media->bytes() : QByteArray();
	const auto gzipSvg = media && media->owner()->isPatternWallPaperSVG();
	const auto &colors = paper.backgroundColors();
	const auto isPattern = paper.isPattern();
	const auto patternOpacity = paper.patternOpacity();
	const auto isBlurred = paper.isBlurred();
	const auto gradientRotation = paper.gradientRotation();
	return {
		.path = paperPath,
		.bytes = paperBytes,
		.gzipSvg = gzipSvg,
		.colors = colors,
		.isPattern = isPattern,
		.patternOpacity = patternOpacity,
		.isBlurred = isBlurred,
		.generateGradient = generateGradient,
		.gradientRotation = gradientRotation,
	};
}

HistoryView::PaintContext SessionController::preparePaintContext(
		PaintContextArgs &&args) {
	const auto visibleAreaTopLocal = content()->mapFromGlobal(
		QPoint(0, args.visibleAreaTopGlobal)).y();
	const auto viewport = QRect(
		0,
		args.visibleAreaTop - visibleAreaTopLocal,
		args.visibleAreaWidth,
		content()->height());
	return args.theme->preparePaintContext(
		_chatStyle.get(),
		viewport,
		args.clip);
}

void SessionController::setPremiumRef(const QString &ref) {
	_premiumRef = ref;
}

QString SessionController::premiumRef() const {
	return _premiumRef;
}

SessionController::~SessionController() {
	resetFakeUnreadWhileOpened();
}

Show::Show(not_null<SessionNavigation*> navigation)
: Show(&navigation->parentController()->window()) {
}

Show::Show(Controller *window)
: _window(base::make_weak(window)) {
}

Show::~Show() = default;

void Show::showBox(
		object_ptr<Ui::BoxContent> content,
		Ui::LayerOptions options) const {
	if (const auto window = _window.get()) {
		window->show(std::move(content), options);
	}
}

void Show::hideLayer() const {
	if (const auto window = _window.get()) {
		window->show(
			object_ptr<Ui::BoxContent>{ nullptr },
			Ui::LayerOption::CloseOther);
	}
}

not_null<QWidget*> Show::toastParent() const {
	const auto window = _window.get();
	Assert(window != nullptr);
	return window->widget()->bodyWidget();
}

bool Show::valid() const {
	return !_window.empty();
}

Show::operator bool() const {
	return valid();
}

} // namespace Window
