/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_message.h"

#include "base/random.h"
#include "base/unixtime.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "api/api_text_entities.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_location_manager.h"
#include "history/history_service.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_context_menu.h" // CopyPostLink.
#include "history/view/history_view_spoiler_click_handler.h"
#include "history/view/media/history_view_media.h" // AddTimestampLinks.
#include "chat_helpers/stickers_emoji_pack.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "api/api_updates.h"
#include "boxes/share_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/toast/toast.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/text/format_values.h"
#include "ui/item_text_options.h"
#include "core/application.h"
#include "core/ui_integration.h"
#include "window/notifications_manager.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "storage/storage_shared_media.h"
#include "mtproto/mtproto_config.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_game.h"
#include "data/data_media_types.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_histories.h"
#include "data/data_web_page.h"
#include "data/data_sponsored_messages.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"
#include "styles/style_chat.h"
#include "styles/style_window.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace {

[[nodiscard]] MessageFlags NewForwardedFlags(
		not_null<PeerData*> peer,
		PeerId from,
		not_null<HistoryItem*> fwd) {
	auto result = NewMessageFlags(peer);
	if (from) {
		result |= MessageFlag::HasFromId;
	}
	if (const auto media = fwd->media()) {
		if ((!peer->isChannel() || peer->isMegagroup())
			&& media->forwardedBecomesUnread()) {
			result |= MessageFlag::MediaIsUnread;
		}
	}
	if (fwd->hasViews()) {
		result |= MessageFlag::HasViews;
	}
	return result;
}

[[nodiscard]] bool CopyMarkupToForward(not_null<const HistoryItem*> item) {
	auto mediaOriginal = item->media();
	if (mediaOriginal && mediaOriginal->game()) {
		// Copy inline keyboard when forwarding messages with a game.
		return true;
	}
	const auto markup = item->inlineReplyMarkup();
	if (!markup) {
		return false;
	}
	using Type = HistoryMessageMarkupButton::Type;
	for (const auto &row : markup->data.rows) {
		for (const auto &button : row) {
			const auto switchInline = (button.type == Type::SwitchInline)
				|| (button.type == Type::SwitchInlineSame);
			const auto url = (button.type == Type::Url)
				|| (button.type == Type::Auth);
			if ((!switchInline || !item->viaBot()) && !url) {
				return false;
			}
		}
	}
	return true;
}

[[nodiscard]] bool HasInlineItems(const HistoryItemsList &items) {
	for (const auto &item : items) {
		if (item->viaBot()) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] TextWithEntities EnsureNonEmpty(
		const TextWithEntities &text = TextWithEntities()) {
	if (!text.text.isEmpty()) {
		return text;
	}
	return { QString::fromUtf8(":-("), EntitiesInText() };
}

} // namespace

QString GetErrorTextForSending(
		not_null<PeerData*> peer,
		const HistoryItemsList &items,
		const TextWithTags &comment,
		bool ignoreSlowmodeCountdown,
		bool unquoted) {
	if (!peer->canWrite()) {
		return tr::lng_forward_cant(tr::now);
	}

	for (const auto &item : items) {
		if (const auto media = item->media()) {
			const auto error = media->errorTextForForward(peer, unquoted);
			if (!error.isEmpty() && error != qstr("skip")) {
				return error;
			}
		}
	}
	const auto error = Data::RestrictionError(
		peer,
		ChatRestriction::SendInline);
	if (error && HasInlineItems(items)) {
		return *error;
	}

	if (peer->slowmodeApplied()) {
		if (const auto history = peer->owner().historyLoaded(peer)) {
			if (!ignoreSlowmodeCountdown
				&& (history->latestSendingMessage() != nullptr)
				&& (!items.empty() || !comment.text.isEmpty())) {
				return tr::lng_slowmode_no_many(tr::now);
			}
		}
		if (comment.text.size() > MaxMessageSize) {
			return tr::lng_slowmode_too_long(tr::now);
		} else if (!items.empty() && !comment.text.isEmpty()) {
			return tr::lng_slowmode_no_many(tr::now);
		} else if (items.size() > 1) {
			const auto albumForward = [&] {
				if (const auto groupId = items.front()->groupId()) {
					for (const auto &item : items) {
						if (item->groupId() != groupId) {
							return false;
						}
					}
					return true;
				}
				return false;
			}();
			if (!albumForward) {
				return tr::lng_slowmode_no_many(tr::now);
			}
		}
	}
	if (const auto left = peer->slowmodeSecondsLeft()) {
		if (!ignoreSlowmodeCountdown) {
			return tr::lng_slowmode_enabled(
				tr::now,
				lt_left,
				Ui::FormatDurationWords(left));
		}
	}

	return QString();
}

void FastShareMessage(not_null<HistoryItem*> item) {
	Window::ShowForwardMessagesBox(
		App::wnd()->sessionController(),
		item->history()->owner().itemOrItsGroup(item));
	/*
	struct ShareData {
		ShareData(not_null<PeerData*> peer, MessageIdsList &&ids)
		: peer(peer)
		, msgIds(std::move(ids)) {
		}
		not_null<PeerData*> peer;
		MessageIdsList msgIds;
		base::flat_set<mtpRequestId> requests;
	};
	const auto history = item->history();
	const auto owner = &history->owner();
	const auto session = &history->session();
	const auto data = std::make_shared<ShareData>(
		history->peer,
		owner->itemOrItsGroup(item));
	const auto isGame = item->getMessageBot()
		&& item->media()
		&& (item->media()->game() != nullptr);
	const auto canCopyLink = item->hasDirectLink() || isGame;

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
		if (!data->requests.empty()) {
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
				Box<Ui::InformBox>(text),
				Ui::LayerOption::KeepOther);
			return;
		}

		const auto commonSendFlags = MTPmessages_ForwardMessages::Flag(0)
			| MTPmessages_ForwardMessages::Flag::f_with_my_score
			| (options.scheduled
				? MTPmessages_ForwardMessages::Flag::f_schedule_date
				: MTPmessages_ForwardMessages::Flag(0));
		auto msgIds = QVector<MTPint>();
		msgIds.reserve(data->msgIds.size());
		for (const auto &fullId : data->msgIds) {
			msgIds.push_back(MTP_int(fullId.msg));
		}
		const auto generateRandom = [&] {
			auto result = QVector<MTPlong>(data->msgIds.size());
			for (auto &value : result) {
				value = base::RandomValue<MTPlong>();
			}
			return result;
		};
		auto &api = owner->session().api();
		auto &histories = owner->histories();
		const auto requestType = Data::Histories::RequestType::Send;
		for (const auto peer : result) {
			const auto history = owner->history(peer);
			if (!comment.text.isEmpty()) {
				auto message = Api::MessageToSend(
					Api::SendAction(history, options));
				message.textWithTags = comment;
				message.action.clearDraft = false;
				api.sendMessage(std::move(message));
			}
			histories.sendRequest(history, requestType, [=](Fn<void()> finish) {
				auto &api = history->session().api();
				const auto sendFlags = commonSendFlags
					| (ShouldSendSilent(peer, options)
						? MTPmessages_ForwardMessages::Flag::f_silent
						: MTPmessages_ForwardMessages::Flag(0));
				history->sendRequestId = api.request(MTPmessages_ForwardMessages(
					MTP_flags(sendFlags),
					data->peer->input,
					MTP_vector<MTPint>(msgIds),
					MTP_vector<MTPlong>(generateRandom()),
					peer->input,
					MTP_int(options.scheduled),
					MTP_inputPeerEmpty() // send_as
				)).done([=](const MTPUpdates &updates, mtpRequestId requestId) {
					history->session().api().applyUpdates(updates);
					data->requests.remove(requestId);
					if (data->requests.empty()) {
						Ui::Toast::Show(tr::lng_share_done(tr::now));
						Ui::hideLayer();
					}
					finish();
				}).fail([=] {
					finish();
				}).afterRequest(history->sendRequestId).send();
				return history->sendRequestId;
			});
			data->requests.insert(history->sendRequestId);
		}
	};
	auto filterCallback = [isGame](PeerData *peer) {
		if (peer->canWrite()) {
			if (auto channel = peer->asChannel()) {
				return isGame ? (!channel->isBroadcast()) : true;
			}
			return true;
		}
		return false;
	};
	auto copyLinkCallback = canCopyLink
		? Fn<void()>(std::move(copyCallback))
		: Fn<void()>();
	Ui::show(Box<ShareBox>(ShareBox::Descriptor{
		.session = session,
		.copyCallback = std::move(copyLinkCallback),
		.submitCallback = std::move(submitCallback),
		.filterCallback = std::move(filterCallback),
		.navigation = App::wnd()->sessionController() }));
	*/
}

void RequestDependentMessageData(
		not_null<HistoryItem*> item,
		PeerId peerId,
		MsgId msgId) {
	const auto fullId = item->fullId();
	const auto history = item->history();
	const auto session = &history->session();
	const auto done = [=] {
		if (const auto item = session->data().message(fullId)) {
			item->updateDependencyItem();
		}
	};
	history->session().api().requestMessageData(
		(peerId ? history->owner().peer(peerId) : history->peer),
		msgId,
		done);
}

MessageFlags NewMessageFlags(not_null<PeerData*> peer) {
	return MessageFlag::BeingSent
		| (peer->isSelf() ? MessageFlag() : MessageFlag::Outgoing);
}

bool ShouldSendSilent(
		not_null<PeerData*> peer,
		const Api::SendOptions &options) {
	return options.silent
		|| (peer->isBroadcast() && peer->owner().notifySilentPosts(peer))
		|| (peer->session().supportMode()
			&& peer->session().settings().supportAllSilent());
}

MsgId LookupReplyToTop(not_null<History*> history, MsgId replyToId) {
	const auto &owner = history->owner();
	if (const auto item = owner.message(history->peer, replyToId)) {
		return item->replyToTop();
	}
	return 0;
}

MTPMessageReplyHeader NewMessageReplyHeader(const Api::SendAction &action) {
	if (const auto id = action.replyTo) {
		if (const auto replyToTop = LookupReplyToTop(action.history, id)) {
			return MTP_messageReplyHeader(
				MTP_flags(MTPDmessageReplyHeader::Flag::f_reply_to_top_id),
				MTP_int(id),
				MTPPeer(),
				MTP_int(replyToTop));
		}
		return MTP_messageReplyHeader(
			MTP_flags(0),
			MTP_int(id),
			MTPPeer(),
			MTPint());
	}
	return MTPMessageReplyHeader();
}

QString GetErrorTextForSending(
		not_null<PeerData*> peer,
		const HistoryItemsList &items,
		bool ignoreSlowmodeCountdown,
		bool unquoted) {
	return GetErrorTextForSending(peer, items, {}, ignoreSlowmodeCountdown, unquoted);
}

struct HistoryMessage::CreateConfig {
	PeerId replyToPeer = 0;
	MsgId replyTo = 0;
	MsgId replyToTop = 0;
	UserId viaBotId = 0;
	int viewsCount = -1;
	QString author;
	PeerId senderOriginal = 0;
	QString senderNameOriginal;
	QString forwardPsaType;
	MsgId originalId = 0;
	PeerId savedFromPeer = 0;
	MsgId savedFromMsgId = 0;
	QString authorOriginal;
	TimeId originalDate = 0;
	TimeId editDate = 0;
	HistoryMessageMarkupData markup;
	HistoryMessageRepliesData replies;
	bool imported = false;

	// For messages created from existing messages (forwarded).
	const HistoryMessageReplyMarkup *inlineMarkup = nullptr;
};

void HistoryMessage::FillForwardedInfo(
		CreateConfig &config,
		const MTPDmessageFwdHeader &data) {
	if (const auto fromId = data.vfrom_id()) {
		config.senderOriginal = peerFromMTP(*fromId);
	}
	config.originalDate = data.vdate().v;
	config.senderNameOriginal = qs(data.vfrom_name().value_or_empty());
	config.forwardPsaType = qs(data.vpsa_type().value_or_empty());
	config.originalId = data.vchannel_post().value_or_empty();
	config.authorOriginal = qs(data.vpost_author().value_or_empty());
	const auto savedFromPeer = data.vsaved_from_peer();
	const auto savedFromMsgId = data.vsaved_from_msg_id();
	if (savedFromPeer && savedFromMsgId) {
		config.savedFromPeer = peerFromMTP(*savedFromPeer);
		config.savedFromMsgId = savedFromMsgId->v;
	}
	config.imported = data.is_imported();
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	const MTPDmessage &data,
	MessageFlags localFlags)
: HistoryItem(
		history,
		id,
		FlagsFromMTP(id, data.vflags().v, localFlags),
		data.vdate().v,
		data.vfrom_id() ? peerFromMTP(*data.vfrom_id()) : PeerId(0)) {
	auto config = CreateConfig();
	if (const auto forwarded = data.vfwd_from()) {
		forwarded->match([&](const MTPDmessageFwdHeader &data) {
			FillForwardedInfo(config, data);
		});
	}
	if (const auto reply = data.vreply_to()) {
		reply->match([&](const MTPDmessageReplyHeader &data) {
			if (const auto peer = data.vreply_to_peer_id()) {
				config.replyToPeer = peerFromMTP(*peer);
				if (config.replyToPeer == history->peer->id) {
					config.replyToPeer = 0;
				}
			}
			config.replyTo = data.vreply_to_msg_id().v;
			config.replyToTop = data.vreply_to_top_id().value_or(
				data.vreply_to_msg_id().v);
		});
	}
	config.viaBotId = data.vvia_bot_id().value_or_empty();
	config.viewsCount = data.vviews().value_or(-1);
	config.replies = isScheduled()
		? HistoryMessageRepliesData()
		: HistoryMessageRepliesData(data.vreplies());
	config.markup = HistoryMessageMarkupData(data.vreply_markup());
	config.editDate = data.vedit_date().value_or_empty();
	config.author = qs(data.vpost_author().value_or_empty());
	createComponents(std::move(config));

	if (const auto media = data.vmedia()) {
		setMedia(*media);
	}
	const auto textWithEntities = TextWithEntities{
		TextUtilities::Clean(qs(data.vmessage())),
		Api::EntitiesFromMTP(
			&history->session(),
			data.ventities().value_or_empty())
	};
	setText(_media ? textWithEntities : EnsureNonEmpty(textWithEntities));
	if (const auto groupedId = data.vgrouped_id()) {
		setGroupId(
			MessageGroupId::FromRaw(history->peer->id, groupedId->v));
	}
	if (const auto reactions = data.vreactions()) {
		updateReactions(reactions);
	}

	applyTTL(data);
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	const MTPDmessageService &data,
	MessageFlags localFlags)
: HistoryItem(
		history,
		id,
		FlagsFromMTP(id, data.vflags().v, localFlags),
		data.vdate().v,
		data.vfrom_id() ? peerFromMTP(*data.vfrom_id()) : PeerId(0)) {
	auto config = CreateConfig();
	if (const auto reply = data.vreply_to()) {
		reply->match([&](const MTPDmessageReplyHeader &data) {
			const auto peer = data.vreply_to_peer_id()
				? peerFromMTP(*data.vreply_to_peer_id())
				: history->peer->id;
			if (!peer || peer == history->peer->id) {
				config.replyTo = data.vreply_to_msg_id().v;
				config.replyToTop = data.vreply_to_top_id().value_or(
					data.vreply_to_msg_id().v);
			}
		});
	}
	createComponents(std::move(config));

	data.vaction().match([&](const MTPDmessageActionPhoneCall &data) {
		_media = std::make_unique<Data::MediaCall>(
			this,
			Data::ComputeCallData(data));
		setEmptyText();
	}, [](const auto &) {
		Unexpected("Service message action type in HistoryMessage.");
	});

	applyTTL(data);
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MessageFlags flags,
	TimeId date,
	PeerId from,
	const QString &postAuthor,
	not_null<HistoryItem*> original)
: HistoryItem(
		history,
		id,
		(NewForwardedFlags(history->peer, from, original) | flags),
		date,
		from) {
	const auto peer = history->peer;

	auto config = CreateConfig();

	const auto originalMedia = original->media();
	const auto dropForwardInfo = (originalMedia
		&& originalMedia->dropForwardedInfo())
		|| (original->history()->peer->isSelf()
			&& !history->peer->isSelf()
			&& !original->Has<HistoryMessageForwarded>()
			&& (!originalMedia || !originalMedia->forceForwardedInfo()));
	if (!dropForwardInfo) {
		config.originalDate = original->dateOriginal();
		if (const auto info = original->hiddenSenderInfo()) {
			config.senderNameOriginal = info->name;
		} else if (const auto senderOriginal = original->senderOriginal()) {
			config.senderOriginal = senderOriginal->id;
			if (senderOriginal->isChannel()) {
				config.originalId = original->idOriginal();
			}
		} else {
			Unexpected("Corrupt forwarded information in message.");
		}
		config.authorOriginal = original->authorOriginal();
	}
	if (peer->isSelf()) {
		//
		// iOS app sends you to the original post if we forward a forward from channel.
		// But server returns not the original post but the forward in saved_from_...
		//
		//if (config.originalId) {
		//	config.savedFromPeer = config.senderOriginal;
		//	config.savedFromMsgId = config.originalId;
		//} else {
			config.savedFromPeer = original->history()->peer->id;
			config.savedFromMsgId = original->id;
		//}
	}
	if (flags & MessageFlag::HasPostAuthor) {
		config.author = postAuthor;
	}
	if (const auto fwdViaBot = original->viaBot()) {
		config.viaBotId = peerToUser(fwdViaBot->id);
	} else if (originalMedia && originalMedia->game()) {
		if (const auto sender = original->senderOriginal()) {
			if (const auto user = sender->asUser()) {
				if (user->isBot()) {
					config.viaBotId = peerToUser(user->id);
				}
			}
		}
	}
	const auto fwdViewsCount = original->viewsCount();
	if (fwdViewsCount > 0) {
		config.viewsCount = fwdViewsCount;
	} else if ((isPost() && !isScheduled())
		|| (original->senderOriginal()
			&& original->senderOriginal()->isChannel())) {
		config.viewsCount = 1;
	}

	const auto mediaOriginal = original->media();
	if (CopyMarkupToForward(original)) {
		config.inlineMarkup = original->inlineReplyMarkup();
	}
	createComponents(std::move(config));

	const auto ignoreMedia = [&] {
		if (mediaOriginal && mediaOriginal->webpage()) {
			if (peer->amRestricted(ChatRestriction::EmbedLinks)) {
				return true;
			}
		}
		return false;
	};
	if (mediaOriginal && !ignoreMedia()) {
		_media = mediaOriginal->clone(this);
	}
	setText(original->originalText());
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MessageFlags flags,
	MsgId replyTo,
	UserId viaBotId,
	TimeId date,
	PeerId from,
	const QString &postAuthor,
	const TextWithEntities &textWithEntities,
	const MTPMessageMedia &media,
	HistoryMessageMarkupData &&markup,
	uint64 groupedId)
: HistoryItem(
		history,
		id,
		flags,
		date,
		(flags & MessageFlag::HasFromId) ? from : 0) {
	createComponentsHelper(
		flags,
		replyTo,
		viaBotId,
		postAuthor,
		std::move(markup));
	setMedia(media);
	setText(textWithEntities);
	if (groupedId) {
		setGroupId(MessageGroupId::FromRaw(history->peer->id, groupedId));
	}
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MessageFlags flags,
	MsgId replyTo,
	UserId viaBotId,
	TimeId date,
	PeerId from,
	const QString &postAuthor,
	not_null<DocumentData*> document,
	const TextWithEntities &caption,
	HistoryMessageMarkupData &&markup,
	uint64 newGroupId)
: HistoryItem(
		history,
		id,
		flags,
		date,
		(flags & MessageFlag::HasFromId) ? from : 0) {
	createComponentsHelper(
		flags,
		replyTo,
		viaBotId,
		postAuthor,
		std::move(markup));

	_media = std::make_unique<Data::MediaFile>(this, document);
	setText(caption);

	if (newGroupId) {
		setGroupId(
			MessageGroupId::FromRaw(history->peer->id, newGroupId));
	}	
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MessageFlags flags,
	MsgId replyTo,
	UserId viaBotId,
	TimeId date,
	PeerId from,
	const QString &postAuthor,
	not_null<PhotoData*> photo,
	const TextWithEntities &caption,
	HistoryMessageMarkupData &&markup,
	uint64 newGroupId)
: HistoryItem(
		history,
		id,
		flags,
		date,
		(flags & MessageFlag::HasFromId) ? from : 0) {
	createComponentsHelper(
		flags,
		replyTo,
		viaBotId,
		postAuthor,
		std::move(markup));

	_media = std::make_unique<Data::MediaPhoto>(this, photo);
	setText(caption);

	if (newGroupId) {
		setGroupId(
			MessageGroupId::FromRaw(history->peer->id, newGroupId));
	}
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	MessageFlags flags,
	MsgId replyTo,
	UserId viaBotId,
	TimeId date,
	PeerId from,
	const QString &postAuthor,
	not_null<GameData*> game,
	HistoryMessageMarkupData &&markup)
: HistoryItem(
		history,
		id,
		flags,
		date,
		(flags & MessageFlag::HasFromId) ? from : 0) {
	createComponentsHelper(
		flags,
		replyTo,
		viaBotId,
		postAuthor,
		std::move(markup));

	_media = std::make_unique<Data::MediaGame>(this, game);
	setEmptyText();
}

HistoryMessage::HistoryMessage(
	not_null<History*> history,
	MsgId id,
	Data::SponsoredFrom from,
	const TextWithEntities &textWithEntities)
: HistoryItem(
		history,
		id,
		((history->peer->isChannel() ? MessageFlag::Post : MessageFlag(0))
			//| (from.peer ? MessageFlag::HasFromId : MessageFlag(0))
			| MessageFlag::Local),
		HistoryItem::NewMessageDate(0),
		/*from.peer ? from.peer->id : */PeerId(0)) {
	createComponentsHelper(
		_flags,
		MsgId(0), // replyTo
		UserId(0), // viaBotId
		QString(), // postAuthor
		HistoryMessageMarkupData());
	setText(textWithEntities);
	setSponsoredFrom(from);
}

void HistoryMessage::createComponentsHelper(
		MessageFlags flags,
		MsgId replyTo,
		UserId viaBotId,
		const QString &postAuthor,
		HistoryMessageMarkupData &&markup) {
	auto config = CreateConfig();
	config.viaBotId = viaBotId;
	if (flags & MessageFlag::HasReplyInfo) {
		config.replyTo = replyTo;
		const auto replyToTop = LookupReplyToTop(history(), replyTo);
		config.replyToTop = replyToTop ? replyToTop : replyTo;
	}
	config.markup = std::move(markup);
	if (flags & MessageFlag::HasPostAuthor) config.author = postAuthor;
	if (flags & MessageFlag::HasViews) config.viewsCount = 1;

	createComponents(std::move(config));
}

int HistoryMessage::viewsCount() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		return std::max(views->views.count, 0);
	}
	return HistoryItem::viewsCount();
}

bool HistoryMessage::checkCommentsLinkedChat(ChannelId id) const {
	if (!id) {
		return true;
	} else if (const auto channel = history()->peer->asChannel()) {
		if (channel->linkedChatKnown()
			|| !(channel->flags() & ChannelDataFlag::HasLink)) {
			const auto linked = channel->linkedChat();
			if (!linked || peerToChannel(linked->id) != id) {
				return false;
			}
		}
		return true;
	}
	return false;
}

int HistoryMessage::repliesCount() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		if (!checkCommentsLinkedChat(views->commentsMegagroupId)) {
			return 0;
		}
		return std::max(views->replies.count, 0);
	}
	return HistoryItem::repliesCount();
}

bool HistoryMessage::repliesAreComments() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		return (views->commentsMegagroupId != 0)
			&& checkCommentsLinkedChat(views->commentsMegagroupId);
	}
	return HistoryItem::repliesAreComments();
}

bool HistoryMessage::externalReply() const {
	if (!history()->peer->isRepliesChat()) {
		return false;
	} else if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->savedFromPeer && forwarded->savedFromMsgId;
	}
	return false;
}

MsgId HistoryMessage::repliesInboxReadTill() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		return views->repliesInboxReadTillId;
	}
	return 0;
}

void HistoryMessage::setRepliesInboxReadTill(
		MsgId readTillId,
		std::optional<int> unreadCount) {
	if (const auto views = Get<HistoryMessageViews>()) {
		const auto newReadTillId = std::max(readTillId.bare, int64(1));
		const auto ignore = (newReadTillId < views->repliesInboxReadTillId);
		if (ignore) {
			return;
		}
		const auto changed = (newReadTillId > views->repliesInboxReadTillId);
		if (changed) {
			const auto wasUnread = repliesAreComments() && areRepliesUnread();
			views->repliesInboxReadTillId = newReadTillId;
			if (wasUnread && !areRepliesUnread()) {
				history()->owner().requestItemRepaint(this);
			}
		}
		const auto wasUnreadCount = (views->repliesUnreadCount >= 0)
			? std::make_optional(views->repliesUnreadCount)
			: std::nullopt;
		if (unreadCount != wasUnreadCount
			&& (changed || unreadCount.has_value())) {
			setUnreadRepliesCount(views, unreadCount.value_or(-1));
		}
	}
}

MsgId HistoryMessage::computeRepliesInboxReadTillFull() const {
	const auto views = Get<HistoryMessageViews>();
	if (!views) {
		return 0;
	}
	const auto local = views->repliesInboxReadTillId;
	const auto group = views->commentsMegagroupId
		? history()->owner().historyLoaded(
			peerFromChannel(views->commentsMegagroupId))
		: history().get();
	if (const auto megagroup = group->peer->asChannel()) {
		if (megagroup->amIn()) {
			return std::max(local, group->inboxReadTillId());
		}
	}
	return local;
}

MsgId HistoryMessage::repliesOutboxReadTill() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		return views->repliesOutboxReadTillId;
	}
	return 0;
}

void HistoryMessage::setRepliesOutboxReadTill(MsgId readTillId) {
	if (const auto views = Get<HistoryMessageViews>()) {
		const auto newReadTillId = std::max(readTillId.bare, int64(1));
		if (newReadTillId > views->repliesOutboxReadTillId) {
			views->repliesOutboxReadTillId = newReadTillId;
			if (!repliesAreComments()) {
				history()->session().changes().historyUpdated(
					history(),
					Data::HistoryUpdate::Flag::OutboxRead);
			}
		}
	}
}

MsgId HistoryMessage::computeRepliesOutboxReadTillFull() const {
	const auto views = Get<HistoryMessageViews>();
	if (!views) {
		return 0;
	}
	const auto local = views->repliesOutboxReadTillId;
	const auto group = views->commentsMegagroupId
		? history()->owner().historyLoaded(
			peerFromChannel(views->commentsMegagroupId))
		: history().get();
	if (const auto megagroup = group->peer->asChannel()) {
		if (megagroup->amIn()) {
			return std::max(local, group->outboxReadTillId());
		}
	}
	return local;
}

void HistoryMessage::setRepliesMaxId(MsgId maxId) {
	if (const auto views = Get<HistoryMessageViews>()) {
		if (views->repliesMaxId != maxId) {
			const auto comments = repliesAreComments();
			const auto wasUnread = comments && areRepliesUnread();
			views->repliesMaxId = maxId;
			if (comments && wasUnread != areRepliesUnread()) {
				history()->owner().requestItemRepaint(this);
			}
		}
	}
}

void HistoryMessage::setRepliesPossibleMaxId(MsgId possibleMaxId) {
	if (const auto views = Get<HistoryMessageViews>()) {
		if (views->repliesMaxId < possibleMaxId) {
			const auto comments = repliesAreComments();
			const auto wasUnread = comments && areRepliesUnread();
			views->repliesMaxId = possibleMaxId;
			if (comments && !wasUnread && areRepliesUnread()) {
				history()->owner().requestItemRepaint(this);
			}
		}
	}
}

bool HistoryMessage::areRepliesUnread() const {
	const auto views = Get<HistoryMessageViews>();
	if (!views) {
		return false;
	}
	const auto local = views->repliesInboxReadTillId;
	if (views->repliesInboxReadTillId < 2 || views->repliesMaxId <= local) {
		return false;
	}
	const auto group = views->commentsMegagroupId
		? history()->owner().historyLoaded(
			peerFromChannel(views->commentsMegagroupId))
		: history().get();
	return !group || (views->repliesMaxId > group->inboxReadTillId());
}

FullMsgId HistoryMessage::commentsItemId() const {
	if (const auto views = Get<HistoryMessageViews>()) {
		return FullMsgId(
			PeerId(views->commentsMegagroupId),
			views->commentsRootId);
	}
	return FullMsgId();
}

void HistoryMessage::setCommentsItemId(FullMsgId id) {
	if (id.peer == _history->peer->id) {
		if (id.msg != this->id) {
			if (const auto reply = Get<HistoryMessageReply>()) {
				reply->replyToMsgTop = id.msg;
			}
		}
	} else if (const auto views = Get<HistoryMessageViews>()) {
		if (const auto channelId = peerToChannel(id.peer)) {
			if (views->commentsMegagroupId != channelId) {
				views->commentsMegagroupId = channelId;
				history()->owner().requestItemResize(this);
			}
			views->commentsRootId = id.msg;
		}
	}
}

void HistoryMessage::hideSpoilers() {
	HistoryView::HideSpoilers(_text);
}

bool HistoryMessage::updateDependencyItem() {
	if (const auto reply = Get<HistoryMessageReply>()) {
		const auto documentId = reply->replyToDocumentId;
		const auto webpageId = reply->replyToWebPageId;
		const auto result = reply->updateData(this, true);
		const auto mediaIdChanged = (documentId != reply->replyToDocumentId)
			|| (webpageId != reply->replyToWebPageId);
		if (mediaIdChanged && generateLocalEntitiesByReply()) {
			reapplyText();
		}
		return result;
	}
	return true;
}

void HistoryMessage::applySentMessage(const MTPDmessage &data) {
	HistoryItem::applySentMessage(data);

	if (const auto period = data.vttl_period(); period && period->v > 0) {
		applyTTL(data.vdate().v + period->v);
	} else {
		applyTTL(0);
	}
}

void HistoryMessage::applySentMessage(
		const QString &text,
		const MTPDupdateShortSentMessage &data,
		bool wasAlready) {
	HistoryItem::applySentMessage(text, data, wasAlready);

	if (const auto period = data.vttl_period(); period && period->v > 0) {
		applyTTL(data.vdate().v + period->v);
	} else {
		applyTTL(0);
	}
}

bool HistoryMessage::allowsForward() const {
	return isRegular()
		&& !forbidsForward()
		&& history()->peer->allowsForwarding()
		&& (!_media || _media->allowsForward());
}

bool HistoryMessage::allowsSendNow() const {
	return isScheduled() && !isSending() && !hasFailed() && !isEditingMedia();
}

bool HistoryMessage::isTooOldForEdit(TimeId now) const {
	return !_history->peer->canEditMessagesIndefinitely()
		&& !isScheduled()
		&& (now - date() >= _history->session().serverConfig().editTimeLimit);
}

bool HistoryMessage::allowsEdit(TimeId now) const {
	return canBeEdited()
		&& !isTooOldForEdit(now)
		&& (!_media || _media->allowsEdit())
		&& !isLegacyMessage()
		&& !isEditingMedia();
}

void HistoryMessage::createComponents(CreateConfig &&config) {
	uint64 mask = 0;
	if (config.replyTo) {
		mask |= HistoryMessageReply::Bit();
	}
	if (config.viaBotId) {
		mask |= HistoryMessageVia::Bit();
	}
	if (config.viewsCount >= 0 || !config.replies.isNull) {
		mask |= HistoryMessageViews::Bit();
	}
	if (!config.author.isEmpty()) {
		mask |= HistoryMessageSigned::Bit();
	} else if (_history->peer->isMegagroup() // Discussion posts signatures.
		&& config.savedFromPeer
		&& !config.authorOriginal.isEmpty()) {
		const auto savedFrom = _history->owner().peerLoaded(
			config.savedFromPeer);
		if (savedFrom && savedFrom->isChannel()) {
			mask |= HistoryMessageSigned::Bit();
		}
	} else if ((_history->peer->isSelf() || _history->peer->isRepliesChat())
		&& !config.authorOriginal.isEmpty()) {
		mask |= HistoryMessageSigned::Bit();
	}
	if (config.editDate != TimeId(0)) {
		mask |= HistoryMessageEdited::Bit();
	}
	if (config.originalDate != 0) {
		mask |= HistoryMessageForwarded::Bit();
	}
	if (!config.markup.isTrivial()) {
		mask |= HistoryMessageReplyMarkup::Bit();
	} else if (config.inlineMarkup) {
		mask |= HistoryMessageReplyMarkup::Bit();
	}

	UpdateComponents(mask);

	if (const auto reply = Get<HistoryMessageReply>()) {
		reply->replyToPeerId = config.replyToPeer;
		reply->replyToMsgId = config.replyTo;
		reply->replyToMsgTop = isScheduled() ? 0 : config.replyToTop;
		if (!reply->updateData(this)) {
			RequestDependentMessageData(
				this,
				reply->replyToPeerId,
				reply->replyToMsgId);
		}
	}
	if (const auto via = Get<HistoryMessageVia>()) {
		via->create(&history()->owner(), config.viaBotId);
	}
	if (const auto views = Get<HistoryMessageViews>()) {
		changeViewsCount(config.viewsCount);
		if (config.replies.isNull
			&& isSending()
			&& config.markup.isNull()) {
			if (const auto broadcast = history()->peer->asBroadcast()) {
				if (const auto linked = broadcast->linkedChat()) {
					config.replies.isNull = false;
					config.replies.channelId = peerToChannel(linked->id);
				}
			}
		}
		setReplies(std::move(config.replies));
	}
	if (const auto edited = Get<HistoryMessageEdited>()) {
		edited->date = config.editDate;
	}
	if (const auto msgsigned = Get<HistoryMessageSigned>()) {
		msgsigned->author = config.author.isEmpty()
			? config.authorOriginal
			: config.author;
		msgsigned->isAnonymousRank = !isDiscussionPost()
			&& author()->isMegagroup();
	}
	setupForwardedComponent(config);
	if (const auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (!config.markup.isTrivial()) {
			markup->updateData(std::move(config.markup));
		} else if (config.inlineMarkup) {
			markup->createForwarded(*config.inlineMarkup);
		}
		if (markup->data.flags & ReplyMarkupFlag::HasSwitchInlineButton) {
			_flags |= MessageFlag::HasSwitchInlineButton;
		}
	} else if (!config.markup.isNull()) {
		_flags |= MessageFlag::HasReplyMarkup;
	} else {
		_flags &= ~MessageFlag::HasReplyMarkup;
	}
	const auto from = displayFrom();
	_fromNameVersion = from ? from->nameVersion : 1;
}

bool HistoryMessage::checkRepliesPts(
		const HistoryMessageRepliesData &data) const {
	const auto channel = history()->peer->asChannel();
	const auto pts = channel
		? channel->pts()
		: history()->session().updates().pts();
	return (data.pts >= pts);
}

void HistoryMessage::setupForwardedComponent(const CreateConfig &config) {
	const auto forwarded = Get<HistoryMessageForwarded>();
	if (!forwarded) {
		return;
	}
	forwarded->originalDate = config.originalDate;
	const auto originalSender = config.senderOriginal
		? config.senderOriginal
		: !config.senderNameOriginal.isEmpty()
		? PeerId()
		: from()->id;
	forwarded->originalSender = originalSender
		? history()->owner().peer(originalSender).get()
		: nullptr;
	if (!forwarded->originalSender) {
		forwarded->hiddenSenderInfo = std::make_unique<HiddenSenderInfo>(
			config.senderNameOriginal,
			config.imported);
	}
	forwarded->originalId = config.originalId;
	forwarded->originalAuthor = config.authorOriginal;
	forwarded->psaType = config.forwardPsaType;
	forwarded->savedFromPeer = history()->owner().peerLoaded(
		config.savedFromPeer);
	forwarded->savedFromMsgId = config.savedFromMsgId;
	forwarded->imported = config.imported;
}

void HistoryMessage::refreshMedia(const MTPMessageMedia *media) {
	const auto was = (_media != nullptr);
	_media = nullptr;
	if (media) {
		setMedia(*media);
	}
	if (was || _media) {
		if (const auto views = Get<HistoryMessageViews>()) {
			refreshRepliesText(views);
		}
	}
}

void HistoryMessage::refreshSentMedia(const MTPMessageMedia *media) {
	const auto wasGrouped = history()->owner().groups().isGrouped(this);
	refreshMedia(media);
	if (wasGrouped) {
		history()->owner().groups().refreshMessage(this);
	} else {
		history()->owner().requestItemViewRefresh(this);
	}
}

void HistoryMessage::returnSavedMedia() {
	if (!isEditingMedia()) {
		return;
	}
	const auto wasGrouped = history()->owner().groups().isGrouped(this);
	_media = std::move(_savedLocalEditMediaData->media);
	setText(_savedLocalEditMediaData->text);
	clearSavedMedia();
	if (wasGrouped) {
		history()->owner().groups().refreshMessage(this, true);
	} else {
		history()->owner().requestItemViewRefresh(this);
		history()->owner().updateDependentMessages(this);
	}
}

void HistoryMessage::setMedia(const MTPMessageMedia &media) {
	_media = CreateMedia(this, media);
	checkBuyButton();
}

void HistoryMessage::checkBuyButton() {
	if (const auto invoice = _media ? _media->invoice() : nullptr) {
		if (invoice->receiptMsgId) {
			replaceBuyWithReceiptInMarkup();
		}
	}
}

std::unique_ptr<Data::Media> HistoryMessage::CreateMedia(
		not_null<HistoryMessage*> item,
		const MTPMessageMedia &media) {
	using Result = std::unique_ptr<Data::Media>;
	return media.match([&](const MTPDmessageMediaContact &media) -> Result {
		return std::make_unique<Data::MediaContact>(
			item,
			media.vuser_id().v,
			qs(media.vfirst_name()),
			qs(media.vlast_name()),
			qs(media.vphone_number()));
	}, [&](const MTPDmessageMediaGeo &media) -> Result {
		return media.vgeo().match([&](const MTPDgeoPoint &point) -> Result {
			return std::make_unique<Data::MediaLocation>(
				item,
				Data::LocationPoint(point));
		}, [](const MTPDgeoPointEmpty &) -> Result {
			return nullptr;
		});
	}, [&](const MTPDmessageMediaGeoLive &media) -> Result {
		return media.vgeo().match([&](const MTPDgeoPoint &point) -> Result {
			return std::make_unique<Data::MediaLocation>(
				item,
				Data::LocationPoint(point));
		}, [](const MTPDgeoPointEmpty &) -> Result {
			return nullptr;
		});
	}, [&](const MTPDmessageMediaVenue &media) -> Result {
		return media.vgeo().match([&](const MTPDgeoPoint &point) -> Result {
			return std::make_unique<Data::MediaLocation>(
				item,
				Data::LocationPoint(point),
				qs(media.vtitle()),
				qs(media.vaddress()));
		}, [](const MTPDgeoPointEmpty &data) -> Result {
			return nullptr;
		});
	}, [&](const MTPDmessageMediaPhoto &media) -> Result {
		const auto photo = media.vphoto();
		if (media.vttl_seconds()) {
			LOG(("App Error: "
				"Unexpected MTPMessageMediaPhoto "
				"with ttl_seconds in HistoryMessage."));
			return nullptr;
		} else if (!photo) {
			LOG(("API Error: "
				"Got MTPMessageMediaPhoto "
				"without photo and without ttl_seconds."));
			return nullptr;
		}
		return photo->match([&](const MTPDphoto &photo) -> Result {
			return std::make_unique<Data::MediaPhoto>(
				item,
				item->history()->owner().processPhoto(photo));
		}, [](const MTPDphotoEmpty &) -> Result {
			return nullptr;
		});
	}, [&](const MTPDmessageMediaDocument &media) -> Result {
		const auto document = media.vdocument();
		if (media.vttl_seconds()) {
			LOG(("App Error: "
				"Unexpected MTPMessageMediaDocument "
				"with ttl_seconds in HistoryMessage."));
			return nullptr;
		} else if (!document) {
			LOG(("API Error: "
				"Got MTPMessageMediaDocument "
				"without document and without ttl_seconds."));
			return nullptr;
		}
		return document->match([&](const MTPDdocument &document) -> Result {
			return std::make_unique<Data::MediaFile>(
				item,
				item->history()->owner().processDocument(document));
		}, [](const MTPDdocumentEmpty &) -> Result {
			return nullptr;
		});
	}, [&](const MTPDmessageMediaWebPage &media) {
		return media.vwebpage().match([](const MTPDwebPageEmpty &) -> Result {
			return nullptr;
		}, [&](const MTPDwebPagePending &webpage) -> Result {
			return std::make_unique<Data::MediaWebPage>(
				item,
				item->history()->owner().processWebpage(webpage));
		}, [&](const MTPDwebPage &webpage) -> Result {
			return std::make_unique<Data::MediaWebPage>(
				item,
				item->history()->owner().processWebpage(webpage));
		}, [](const MTPDwebPageNotModified &) -> Result {
			LOG(("API Error: "
				"webPageNotModified is unexpected in message media."));
			return nullptr;
		});
	}, [&](const MTPDmessageMediaGame &media) -> Result {
		return media.vgame().match([&](const MTPDgame &game) {
			return std::make_unique<Data::MediaGame>(
				item,
				item->history()->owner().processGame(game));
		});
	}, [&](const MTPDmessageMediaInvoice &media) -> Result {
		return std::make_unique<Data::MediaInvoice>(
			item,
			Data::ComputeInvoiceData(item, media));
	}, [&](const MTPDmessageMediaPoll &media) -> Result {
		return std::make_unique<Data::MediaPoll>(
			item,
			item->history()->owner().processPoll(media));
	}, [&](const MTPDmessageMediaDice &media) -> Result {
		return std::make_unique<Data::MediaDice>(
			item,
			qs(media.vemoticon()),
			media.vvalue().v);
	}, [](const MTPDmessageMediaEmpty &) -> Result {
		return nullptr;
	}, [](const MTPDmessageMediaUnsupported &) -> Result {
		return nullptr;
	});
}

void HistoryMessage::replaceBuyWithReceiptInMarkup() {
	if (const auto markup = inlineReplyMarkup()) {
		for (auto &row : markup->data.rows) {
			for (auto &button : row) {
				if (button.type == HistoryMessageMarkupButton::Type::Buy) {
					const auto receipt = tr::lng_payments_receipt_button(tr::now);
					if (button.text != receipt) {
						button.text = receipt;
						if (markup->inlineKeyboard) {
							markup->inlineKeyboard = nullptr;
							history()->owner().requestItemResize(this);
						}
					}
				}
			}
		}
	}
}

void HistoryMessage::applyEdition(HistoryMessageEdition &&edition) {
	int keyboardTop = -1;
	//if (!pendingResize()) {// #TODO edit bot message
	//	if (auto keyboard = inlineReplyKeyboard()) {
	//		int h = st::msgBotKbButton.margin + keyboard->naturalHeight();
	//		keyboardTop = _height - h + st::msgBotKbButton.margin - marginBottom();
	//	}
	//}

	if (edition.isEditHide) {
		_flags |= MessageFlag::HideEdited;
	} else {
		_flags &= ~MessageFlag::HideEdited;
	}

	if (edition.editDate != -1) {
		//_flags |= MTPDmessage::Flag::f_edit_date;
		if (!Has<HistoryMessageEdited>()) {
			AddComponents(HistoryMessageEdited::Bit());
		}
		auto edited = Get<HistoryMessageEdited>();
		edited->date = edition.editDate;
	}

	if (!edition.useSameMarkup) {
		setReplyMarkup(base::take(edition.replyMarkup));
	}
	if (!isLocalUpdateMedia()) {
		refreshMedia(edition.mtpMedia);
	}
	if (!edition.useSameReactions) {
		updateReactions(edition.mtpReactions);
	}
	changeViewsCount(edition.views);
	setForwardsCount(edition.forwards);
	setText(_media
		? edition.textWithEntities
		: EnsureNonEmpty(edition.textWithEntities));
	if (!edition.useSameReplies) {
		if (!edition.replies.isNull) {
			if (checkRepliesPts(edition.replies)) {
				setReplies(base::take(edition.replies));
			}
		} else {
			clearReplies();
		}
	}

	applyTTL(edition.ttl);

	finishEdition(keyboardTop);
}

void HistoryMessage::applyEdition(const MTPDmessageService &message) {
	if (message.vaction().type() == mtpc_messageActionHistoryClear) {
		const auto wasGrouped = history()->owner().groups().isGrouped(this);
		setReplyMarkup({});
		refreshMedia(nullptr);
		setEmptyText();
		changeViewsCount(-1);
		setForwardsCount(-1);
		if (wasGrouped) {
			history()->owner().groups().unregisterMessage(this);
		}
		finishEditionToEmpty();
	}
}

void HistoryMessage::updateSentContent(
		const TextWithEntities &textWithEntities,
		const MTPMessageMedia *media) {
	const auto isolated = isolatedEmoji();
	setText(textWithEntities);
	if (_flags & MessageFlag::FromInlineBot) {
		if (!media || !_media || !_media->updateInlineResultMedia(*media)) {
			refreshSentMedia(media);
		}
		_flags &= ~MessageFlag::FromInlineBot;
	} else if (media || _media || !isolated || isolated != isolatedEmoji()) {
		if (!media || !_media || !_media->updateSentMedia(*media)) {
			refreshSentMedia(media);
		}
	}
	history()->owner().requestItemResize(this);
}

void HistoryMessage::updateForwardedInfo(const MTPMessageFwdHeader *fwd) {
	const auto forwarded = Get<HistoryMessageForwarded>();
	if (!fwd) {
		if (forwarded) {
			LOG(("API Error: Server removed forwarded information."));
		}
		return;
	} else if (!forwarded) {
		LOG(("API Error: Server added forwarded information."));
		return;
	}
	fwd->match([&](const MTPDmessageFwdHeader &data) {
		auto config = CreateConfig();
		FillForwardedInfo(config, data);
		setupForwardedComponent(config);
		history()->owner().requestItemResize(this);
	});
}

void HistoryMessage::updateReplyMarkup(HistoryMessageMarkupData &&markup) {
	setReplyMarkup(std::move(markup));
}

void HistoryMessage::contributeToSlowmode(TimeId realDate) {
	if (const auto channel = history()->peer->asChannel()) {
		if (out() && isRegular()) {
			channel->growSlowmodeLastMessage(realDate ? realDate : date());
		}
	}
}

void HistoryMessage::addToUnreadMentions(UnreadMentionType type) {
	if (isRegular() && isUnreadMention()) {
		if (history()->addToUnreadMentions(id, type)) {
			history()->session().changes().historyUpdated(
				history(),
				Data::HistoryUpdate::Flag::UnreadMentions);
		}
	}
}

void HistoryMessage::destroyHistoryEntry() {
	if (isUnreadMention()) {
		history()->eraseFromUnreadMentions(id);
	}
	if (const auto reply = Get<HistoryMessageReply>()) {
		changeReplyToTopCounter(reply, -1);
	}
}

Storage::SharedMediaTypesMask HistoryMessage::sharedMediaTypes() const {
	auto result = Storage::SharedMediaTypesMask {};
	if (const auto media = this->media()) {
		result.set(media->sharedMediaTypes());
	}
	if (hasTextLinks()) {
		result.set(Storage::SharedMediaType::Link);
	}
	if (isPinned()) {
		result.set(Storage::SharedMediaType::Pinned);
	}
	return result;
}

bool HistoryMessage::generateLocalEntitiesByReply() const {
	using namespace HistoryView;
	if (!_media) {
		return true;
	} else if (const auto document = _media->document()) {
		return !DurationForTimestampLinks(document);
	} else if (const auto webpage = _media->webpage()) {
		return (webpage->type != WebPageType::Video)
			&& !DurationForTimestampLinks(webpage);
	}
	return true;
}

TextWithEntities HistoryMessage::withLocalEntities(
		const TextWithEntities &textWithEntities) const {
	using namespace HistoryView;
	if (!generateLocalEntitiesByReply()) {
		if (!_media) {
		} else if (const auto document = _media->document()) {
			if (const auto duration = DurationForTimestampLinks(document)) {
				return AddTimestampLinks(
					textWithEntities,
					duration,
					TimestampLinkBase(document, fullId()));
			}
		} else if (const auto webpage = _media->webpage()) {
			if (const auto duration = DurationForTimestampLinks(webpage)) {
				return AddTimestampLinks(
					textWithEntities,
					duration,
					TimestampLinkBase(webpage, fullId()));
			}
		}
		return textWithEntities;
	}
	if (const auto reply = Get<HistoryMessageReply>()) {
		const auto document = reply->replyToDocumentId
			? history()->owner().document(reply->replyToDocumentId).get()
			: nullptr;
		const auto webpage = reply->replyToWebPageId
			? history()->owner().webpage(reply->replyToWebPageId).get()
			: nullptr;
		if (document) {
			if (const auto duration = DurationForTimestampLinks(document)) {
				const auto context = reply->replyToMsg->fullId();
				return AddTimestampLinks(
					textWithEntities,
					duration,
					TimestampLinkBase(document, context));
			}
		} else if (webpage) {
			if (const auto duration = DurationForTimestampLinks(webpage)) {
				const auto context = reply->replyToMsg->fullId();
				return AddTimestampLinks(
					textWithEntities,
					duration,
					TimestampLinkBase(webpage, context));
			}
		}
	}
	return textWithEntities;
}

void HistoryMessage::setText(const TextWithEntities &textWithEntities) {
	for (const auto &entity : textWithEntities.entities) {
		auto type = entity.type();
		if (type == EntityType::Url
			|| type == EntityType::CustomUrl
			|| type == EntityType::Email) {
			_flags |= MessageFlag::HasTextLinks;
			break;
		}
	}

	if (_media && _media->consumeMessageText(textWithEntities)) {
		setEmptyText();
		return;
	}

	clearIsolatedEmoji();
	const auto context = Core::MarkedTextContext{
		.session = &history()->session()
	};
	_text.setMarkedText(
		st::messageTextStyle,
		withLocalEntities(textWithEntities),
		Ui::ItemTextOptions(this),
		context);
	HistoryView::FillTextWithAnimatedSpoilers(_text);
	if (!textWithEntities.text.isEmpty() && _text.isEmpty()) {
		// If server has allowed some text that we've trim-ed entirely,
		// just replace it with something so that UI won't look buggy.
		_text.setMarkedText(
			st::messageTextStyle,
			EnsureNonEmpty(),
			Ui::ItemTextOptions(this));
	} else if (!_media) {
		checkIsolatedEmoji();
	}

	_textWidth = -1;
	_textHeight = 0;
}

void HistoryMessage::reapplyText() {
	setText(originalText());
	history()->owner().requestItemResize(this);
}

void HistoryMessage::setEmptyText() {
	clearIsolatedEmoji();
	_text.setMarkedText(
		st::messageTextStyle,
		{ QString(), EntitiesInText() },
		Ui::ItemTextOptions(this));

	_textWidth = -1;
	_textHeight = 0;
}

void HistoryMessage::clearIsolatedEmoji() {
	if (!(_flags & MessageFlag::IsolatedEmoji)) {
		return;
	}
	history()->session().emojiStickersPack().remove(this);
	_flags &= ~MessageFlag::IsolatedEmoji;
}

void HistoryMessage::checkIsolatedEmoji() {
	if (history()->session().emojiStickersPack().add(this)) {
		_flags |= MessageFlag::IsolatedEmoji;
	}
}

void HistoryMessage::setReplyMarkup(HistoryMessageMarkupData &&markup) {
	const auto requestUpdate = [&] {
		history()->owner().requestItemResize(this);
		history()->session().changes().messageUpdated(
			this,
			Data::MessageUpdate::Flag::ReplyMarkup);
	};
	if (markup.isNull()) {
		if (_flags & MessageFlag::HasReplyMarkup) {
			_flags &= ~MessageFlag::HasReplyMarkup;
			if (Has<HistoryMessageReplyMarkup>()) {
				RemoveComponents(HistoryMessageReplyMarkup::Bit());
			}
			requestUpdate();
		}
		return;
	}

	// optimization: don't create markup component for the case
	// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
	if (markup.isTrivial()) {
		bool changed = false;
		if (Has<HistoryMessageReplyMarkup>()) {
			RemoveComponents(HistoryMessageReplyMarkup::Bit());
			changed = true;
		}
		if (!(_flags & MessageFlag::HasReplyMarkup)) {
			_flags |= MessageFlag::HasReplyMarkup;
			changed = true;
		}
		if (changed) {
			requestUpdate();
		}
	} else {
		if (!(_flags & MessageFlag::HasReplyMarkup)) {
			_flags |= MessageFlag::HasReplyMarkup;
		}
		if (!Has<HistoryMessageReplyMarkup>()) {
			AddComponents(HistoryMessageReplyMarkup::Bit());
		}
		Get<HistoryMessageReplyMarkup>()->updateData(std::move(markup));
		requestUpdate();
	}
}

Ui::Text::IsolatedEmoji HistoryMessage::isolatedEmoji() const {
	return _text.toIsolatedEmoji();
}

TextWithEntities HistoryMessage::originalText() const {
	if (emptyText()) {
		return { QString(), EntitiesInText() };
	}
	return _text.toTextWithEntities();
}

TextWithEntities HistoryMessage::originalTextWithLocalEntities() const {
	return withLocalEntities(originalText());
}

TextForMimeData HistoryMessage::clipboardText() const {
	if (emptyText()) {
		return TextForMimeData();
	}
	return _text.toTextForMimeData();
}

bool HistoryMessage::textHasLinks() const {
	return emptyText() ? false : _text.hasLinks();
}

bool HistoryMessage::changeViewsCount(int count) {
	const auto views = Get<HistoryMessageViews>();
	if (!views
		|| views->views.count == count
		|| (count >= 0 && views->views.count > count)) {
		return false;
	}

	views->views.count = count;
	return true;
}

void HistoryMessage::setForwardsCount(int count) {
}

void HistoryMessage::setPostAuthor(const QString &author) {
	auto msgsigned = Get<HistoryMessageSigned>();
	if (author.isEmpty()) {
		if (!msgsigned) {
			return;
		}
		RemoveComponents(HistoryMessageSigned::Bit());
		history()->owner().requestItemResize(this);
		return;
	}
	if (!msgsigned) {
		AddComponents(HistoryMessageSigned::Bit());
		msgsigned = Get<HistoryMessageSigned>();
	} else if (msgsigned->author == author) {
		return;
	}
	msgsigned->author = author;
	msgsigned->isAnonymousRank = !isDiscussionPost()
		&& this->author()->isMegagroup();
	history()->owner().requestItemResize(this);
}

void HistoryMessage::setReplies(HistoryMessageRepliesData &&data) {
	if (data.isNull) {
		return;
	}
	auto views = Get<HistoryMessageViews>();
	if (!views) {
		AddComponents(HistoryMessageViews::Bit());
		views = Get<HistoryMessageViews>();
	}
	const auto &repliers = data.recentRepliers;
	const auto count = data.repliesCount;
	const auto channelId = data.channelId;
	const auto readTillId = data.readMaxId
		? std::max({
			views->repliesInboxReadTillId.bare,
			data.readMaxId.bare,
			int64(1),
		})
		: views->repliesInboxReadTillId;
	const auto maxId = data.maxId ? data.maxId : views->repliesMaxId;
	const auto countsChanged = (views->replies.count != count)
		|| (views->repliesInboxReadTillId != readTillId)
		|| (views->repliesMaxId != maxId);
	const auto megagroupChanged = (views->commentsMegagroupId != channelId);
	const auto recentChanged = (views->recentRepliers != repliers);
	if (!countsChanged && !megagroupChanged && !recentChanged) {
		return;
	}
	views->replies.count = count;
	if (recentChanged) {
		views->recentRepliers = repliers;
	}
	views->commentsMegagroupId = channelId;
	const auto wasUnread = channelId && areRepliesUnread();
	views->repliesInboxReadTillId = readTillId;
	views->repliesMaxId = maxId;
	if (channelId && wasUnread != areRepliesUnread()) {
		history()->owner().requestItemRepaint(this);
	}
	refreshRepliesText(views, megagroupChanged);
}

void HistoryMessage::clearReplies() {
	auto views = Get<HistoryMessageViews>();
	if (!views) {
		return;
	}
	const auto viewsPart = views->views;
	if (viewsPart.count < 0) {
		RemoveComponents(HistoryMessageViews::Bit());
	} else {
		*views = HistoryMessageViews();
		views->views = viewsPart;
	}
	history()->owner().requestItemResize(this);
}

void HistoryMessage::refreshRepliesText(
		not_null<HistoryMessageViews*> views,
		bool forceResize) {
	if (views->commentsMegagroupId) {
		views->replies.text = (views->replies.count > 0)
			? tr::lng_comments_open_count(
				tr::now,
				lt_count_short,
				views->replies.count)
			: tr::lng_comments_open_none(tr::now);
		views->replies.textWidth = st::semiboldFont->width(
			views->replies.text);
		views->repliesSmall.text = (views->replies.count > 0)
			? Lang::FormatCountToShort(views->replies.count).string
			: QString();
		views->repliesSmall.textWidth = st::semiboldFont->width(
			views->repliesSmall.text);
	}
	if (forceResize) {
		history()->owner().requestItemResize(this);
	} else {
		history()->owner().requestItemRepaint(this);
	}
}

void HistoryMessage::changeRepliesCount(
		int delta,
		PeerId replier,
		std::optional<bool> unread) {
	const auto views = Get<HistoryMessageViews>();
	const auto limit = HistoryMessageViews::kMaxRecentRepliers;
	if (!views) {
		return;
	}

	// Update unread count.
	if (!unread) {
		setUnreadRepliesCount(views, -1);
	} else if (views->repliesUnreadCount >= 0 && *unread) {
		setUnreadRepliesCount(
			views,
			std::max(views->repliesUnreadCount + delta, 0));
	}

	// Update full count.
	if (views->replies.count < 0) {
		return;
	}
	views->replies.count = std::max(views->replies.count + delta, 0);
	if (replier && views->commentsMegagroupId) {
		if (delta < 0) {
			views->recentRepliers.erase(
				ranges::remove(views->recentRepliers, replier),
				end(views->recentRepliers));
		} else if (!ranges::contains(views->recentRepliers, replier)) {
			views->recentRepliers.insert(views->recentRepliers.begin(), replier);
			while (views->recentRepliers.size() > limit) {
				views->recentRepliers.pop_back();
			}
		}
	}
	refreshRepliesText(views);
	history()->owner().notifyItemDataChange(this);
}

void HistoryMessage::setUnreadRepliesCount(
		not_null<HistoryMessageViews*> views,
		int count) {
	// Track unread count in discussion forwards, not in the channel posts.
	if (views->repliesUnreadCount == count || views->commentsMegagroupId) {
		return;
	}
	views->repliesUnreadCount = count;
	history()->session().changes().messageUpdated(
		this,
		Data::MessageUpdate::Flag::RepliesUnreadCount);
}

void HistoryMessage::setSponsoredFrom(const Data::SponsoredFrom &from) {
	AddComponents(HistoryMessageSponsored::Bit());
	const auto sponsored = Get<HistoryMessageSponsored>();
	sponsored->sender = std::make_unique<HiddenSenderInfo>(
		from.title,
		false);

	using Type = HistoryMessageSponsored::Type;
	sponsored->type = from.isExactPost
		? Type::Post
		: from.isBot
		? Type::Bot
		: from.isBroadcast
		? Type::Broadcast
		: (from.peer && from.peer->isUser())
		? Type::User
		: Type::Group;
}

void HistoryMessage::setReplyToTop(MsgId replyToTop) {
	const auto reply = Get<HistoryMessageReply>();
	if (!reply
		|| (reply->replyToMsgTop == replyToTop)
		|| (reply->replyToMsgTop != 0)
		|| isScheduled()) {
		return;
	}
	reply->replyToMsgTop = replyToTop;
	changeReplyToTopCounter(reply, 1);
}

void HistoryMessage::setRealId(MsgId newId) {
	HistoryItem::setRealId(newId);

	history()->owner().groups().refreshMessage(this);
	history()->owner().requestItemResize(this);
	if (const auto reply = Get<HistoryMessageReply>()) {
		if (reply->replyToLink()) {
			reply->setReplyToLinkFrom(this);
		}
		changeReplyToTopCounter(reply, 1);
	}
}

void HistoryMessage::incrementReplyToTopCounter() {
	if (const auto reply = Get<HistoryMessageReply>()) {
		changeReplyToTopCounter(reply, 1);
	}
}

void HistoryMessage::changeReplyToTopCounter(
		not_null<HistoryMessageReply*> reply,
		int delta) {
	if (!isRegular() || !reply->replyToTop()) {
		return;
	}
	const auto peerId = _history->peer->id;
	if (!peerIsChannel(peerId)) {
		return;
	}
	const auto top = _history->owner().message(peerId, reply->replyToTop());
	if (!top) {
		return;
	}
	auto unread = out() ? std::make_optional(false) : std::nullopt;
	if (const auto views = top->Get<HistoryMessageViews>()) {
		if (views->commentsMegagroupId) {
			// This is a post in channel, we don't track its replies.
			return;
		}
		if (views->repliesInboxReadTillId > 0) {
			unread = !out() && (id > views->repliesInboxReadTillId);
		}
	}
	const auto changeFor = [&](not_null<HistoryItem*> item) {
		if (const auto from = displayFrom()) {
			item->changeRepliesCount(delta, from->id, unread);
		} else {
			item->changeRepliesCount(delta, PeerId(), unread);
		}
	};
	changeFor(top);
	if (const auto original = top->lookupDiscussionPostOriginal()) {
		changeFor(original);
	}
}

void HistoryMessage::dependencyItemRemoved(HistoryItem *dependency) {
	if (const auto reply = Get<HistoryMessageReply>()) {
		const auto documentId = reply->replyToDocumentId;
		reply->itemRemoved(this, dependency);
		if (documentId != reply->replyToDocumentId
			&& generateLocalEntitiesByReply()) {
			reapplyText();
		}
	}
}

QString HistoryMessage::notificationHeader() const {
	if (out() && isFromScheduled() && !_history->peer->isSelf()) {
		return tr::lng_from_you(tr::now);
	} else if (!_history->peer->isUser() && !isPost()) {
		return from()->name;
	}
	return QString();
}

std::unique_ptr<HistoryView::Element> HistoryMessage::createView(
		not_null<HistoryView::ElementDelegate*> delegate,
		HistoryView::Element *replacing) {
	return delegate->elementCreate(this, replacing);
}

HistoryMessage::~HistoryMessage() {
	_media.reset();
	clearSavedMedia();
	if (auto reply = Get<HistoryMessageReply>()) {
		reply->clearData(this);
	}
}
