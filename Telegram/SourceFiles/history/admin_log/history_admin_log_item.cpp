/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/admin_log/history_admin_log_item.h"

#include "kotato/kotato_lang.h"
#include "history/admin_log/history_admin_log_inner.h"
#include "history/view/history_view_element.h"
#include "history/history_location_manager.h"
#include "history/history_service.h"
#include "history/history_message.h"
#include "history/history.h"
#include "api/api_chat_participants.h"
#include "api/api_text_entities.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"
#include "ui/basic_click_handlers.h"
#include "boxes/sticker_set_box.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "facades.h"

namespace AdminLog {
namespace {

TextWithEntities PrepareText(
		const QString &value,
		const QString &emptyValue) {
	auto result = TextWithEntities { TextUtilities::Clean(value) };
	if (result.text.isEmpty()) {
		result.text = emptyValue;
		if (!emptyValue.isEmpty()) {
			result.entities.push_back({
				EntityType::Italic,
				0,
				int(emptyValue.size()) });
		}
	} else {
		TextUtilities::ParseEntities(
			result,
			TextParseLinks
				| TextParseMentions
				| TextParseHashtags
				| TextParseBotCommands);
	}
	return result;
}

TimeId ExtractSentDate(const MTPMessage &message) {
	return message.match([&](const MTPDmessageEmpty &) {
		return 0;
	}, [&](const MTPDmessageService &data) {
		return data.vdate().v;
	}, [&](const MTPDmessage &data) {
		return data.vdate().v;
	});
}

MTPMessage PrepareLogMessage(const MTPMessage &message, TimeId newDate) {
	return message.match([&](const MTPDmessageEmpty &data) {
		return MTP_messageEmpty(
			data.vflags(),
			data.vid(),
			data.vpeer_id() ? *data.vpeer_id() : MTPPeer());
	}, [&](const MTPDmessageService &data) {
		const auto removeFlags = MTPDmessageService::Flag::f_out
			| MTPDmessageService::Flag::f_post
			| MTPDmessageService::Flag::f_reply_to
			| MTPDmessageService::Flag::f_ttl_period;
		return MTP_messageService(
			MTP_flags(data.vflags().v & ~removeFlags),
			data.vid(),
			data.vfrom_id() ? *data.vfrom_id() : MTPPeer(),
			data.vpeer_id(),
			MTPMessageReplyHeader(),
			MTP_int(newDate),
			data.vaction(),
			MTPint()); // ttl_period
	}, [&](const MTPDmessage &data) {
		const auto removeFlags = MTPDmessage::Flag::f_out
			| MTPDmessage::Flag::f_post
			| MTPDmessage::Flag::f_reply_to
			| MTPDmessage::Flag::f_replies
			| MTPDmessage::Flag::f_edit_date
			| MTPDmessage::Flag::f_grouped_id
			| MTPDmessage::Flag::f_views
			| MTPDmessage::Flag::f_forwards
			//| MTPDmessage::Flag::f_reactions
			| MTPDmessage::Flag::f_restriction_reason
			| MTPDmessage::Flag::f_ttl_period;
		return MTP_message(
			MTP_flags(data.vflags().v & ~removeFlags),
			data.vid(),
			data.vfrom_id() ? *data.vfrom_id() : MTPPeer(),
			data.vpeer_id(),
			data.vfwd_from() ? *data.vfwd_from() : MTPMessageFwdHeader(),
			MTP_long(data.vvia_bot_id().value_or_empty()),
			MTPMessageReplyHeader(),
			MTP_int(newDate),
			data.vmessage(),
			data.vmedia() ? *data.vmedia() : MTPMessageMedia(),
			data.vreply_markup() ? *data.vreply_markup() : MTPReplyMarkup(),
			(data.ventities()
				? *data.ventities()
				: MTPVector<MTPMessageEntity>()),
			MTP_int(data.vviews().value_or_empty()),
			MTP_int(data.vforwards().value_or_empty()),
			MTPMessageReplies(),
			MTPint(), // edit_date
			MTP_string(),
			MTP_long(0), // grouped_id
			//MTPMessageReactions(),
			MTPVector<MTPRestrictionReason>(),
			MTPint()); // ttl_period
	});
}

bool MediaCanHaveCaption(const MTPMessage &message) {
	if (message.type() != mtpc_message) {
		return false;
	}
	const auto &data = message.c_message();
	const auto media = data.vmedia();
	const auto mediaType = media ? media->type() : mtpc_messageMediaEmpty;
	return (mediaType == mtpc_messageMediaDocument)
		|| (mediaType == mtpc_messageMediaPhoto);
}

TextWithEntities ExtractEditedText(
		not_null<Main::Session*> session,
		const MTPMessage &message) {
	if (message.type() != mtpc_message) {
		return TextWithEntities();
	}
	const auto &data = message.c_message();
	return {
		TextUtilities::Clean(qs(data.vmessage())),
		Api::EntitiesFromMTP(session, data.ventities().value_or_empty())
	};
}

const auto CollectChanges = [](
		auto &phraseMap,
		auto plusFlags,
		auto minusFlags) {
	auto withPrefix = [&phraseMap](auto flags, QChar prefix) {
		auto result = QString();
		for (auto &phrase : phraseMap) {
			if (flags & phrase.first) {
				result.append('\n' + (prefix + phrase.second(tr::now)));
			}
		}
		return result;
	};
	const auto kMinus = QChar(0x2212);
	return withPrefix(plusFlags & ~minusFlags, '+')
		+ withPrefix(minusFlags & ~plusFlags, kMinus);
};

const auto CollectChangesKtg = [](auto &phraseMap, auto &ktgPhraseMap, auto plusFlags, auto minusFlags) {
	auto withPrefix = [&ktgPhraseMap, &phraseMap](auto flags, QChar prefix) {
		auto result = QString();
		for (auto &phrase : ktgPhraseMap) {
			if (flags & phrase.first) {
				result.append('\n' + (prefix + ktr(phrase.second)));
			}
		}

		for (auto &phrase : phraseMap) {
			if (flags & phrase.first) {
				result.append('\n' + (prefix + phrase.second(tr::now)));
			}
		}
		return result;
	};
	const auto kMinus = QChar(0x2212);
	return withPrefix(plusFlags & ~minusFlags, '+') + withPrefix(minusFlags & ~plusFlags, kMinus);
};

TextWithEntities GenerateAdminChangeText(
		not_null<ChannelData*> channel,
		const TextWithEntities &user,
		ChatAdminRightsInfo newRights,
		ChatAdminRightsInfo prevRights) {
	using Flag = ChatAdminRight;
	using Flags = ChatAdminRights;

	auto result = tr::lng_admin_log_promoted(
		tr::now,
		lt_user,
		user,
		Ui::Text::WithEntities);

	const auto useInviteLinkPhrase = channel->isMegagroup()
		&& channel->anyoneCanAddMembers();
	const auto invitePhrase = useInviteLinkPhrase
		? tr::lng_admin_log_admin_invite_link
		: tr::lng_admin_log_admin_invite_users;
	const auto callPhrase = channel->isBroadcast()
		? tr::lng_admin_log_admin_manage_calls_channel
		: tr::lng_admin_log_admin_manage_calls;
	static auto phraseMap = std::map<Flags, tr::phrase<>> {
		{ Flag::ChangeInfo, tr::lng_admin_log_admin_change_info },
		{ Flag::PostMessages, tr::lng_admin_log_admin_post_messages },
		{ Flag::EditMessages, tr::lng_admin_log_admin_edit_messages },
		{ Flag::DeleteMessages, tr::lng_admin_log_admin_delete_messages },
		{ Flag::BanUsers, tr::lng_admin_log_admin_ban_users },
		{ Flag::InviteUsers, invitePhrase },
		{ Flag::PinMessages, tr::lng_admin_log_admin_pin_messages },
		{ Flag::ManageCall, tr::lng_admin_log_admin_manage_calls },
		{ Flag::AddAdmins, tr::lng_admin_log_admin_add_admins },
	};
	phraseMap[Flag::InviteUsers] = invitePhrase;
	phraseMap[Flag::ManageCall] = callPhrase;

	if (!channel->isMegagroup()) {
		// Don't display "Ban users" changes in channels.
		newRights.flags &= ~Flag::BanUsers;
		prevRights.flags &= ~Flag::BanUsers;
	}

	const auto changes = CollectChanges(
		phraseMap,
		newRights.flags,
		prevRights.flags);
	if (!changes.isEmpty()) {
		result.text.append('\n' + changes);
	}

	return result;
};

QString GenerateBannedChangeText(
		ChatRestrictionsInfo newRights,
		ChatRestrictionsInfo prevRights) {
	using Flag = ChatRestriction;
	using Flags = ChatRestrictions;

	static auto phraseMap = std::map<Flags, tr::phrase<>>{
		{ Flag::ViewMessages, tr::lng_admin_log_banned_view_messages },
		{ Flag::SendMessages, tr::lng_admin_log_banned_send_messages },
		{ Flag::SendMedia, tr::lng_admin_log_banned_send_media },
		{ Flag::EmbedLinks, tr::lng_admin_log_banned_embed_links },
		{ Flag::SendPolls, tr::lng_admin_log_banned_send_polls },
		{ Flag::ChangeInfo, tr::lng_admin_log_admin_change_info },
		{ Flag::InviteUsers, tr::lng_admin_log_admin_invite_users },
		{ Flag::PinMessages, tr::lng_admin_log_admin_pin_messages },
	};
	static auto ktgPhraseMap = std::map<Flags, QString>{
		{ Flag::SendStickers, "ktg_admin_log_banned_send_stickers" },
		{ Flag::SendGifs, "ktg_admin_log_banned_send_gif" },
		{ Flag::SendInline, "ktg_admin_log_banned_use_inline" },
		{ Flag::SendGames, "ktg_admin_log_banned_send_games" },
	};
	return CollectChangesKtg(phraseMap, ktgPhraseMap, prevRights.flags, newRights.flags);
}

TextWithEntities GenerateBannedChangeText(
		PeerId participantId,
		const TextWithEntities &user,
		ChatRestrictionsInfo newRights,
		ChatRestrictionsInfo prevRights) {
	using Flag = ChatRestriction;

	const auto newFlags = newRights.flags;
	const auto newUntil = newRights.until;
	const auto prevFlags = prevRights.flags;
	const auto indefinitely = ChannelData::IsRestrictedForever(newUntil);
	if (newFlags & Flag::ViewMessages) {
		return tr::lng_admin_log_banned(
			tr::now,
			lt_user,
			user,
			Ui::Text::WithEntities);
	} else if (newFlags == 0
		&& (prevFlags & Flag::ViewMessages)
		&& !peerIsUser(participantId)) {
		return tr::lng_admin_log_unbanned(
			tr::now,
			lt_user,
			user,
			Ui::Text::WithEntities);
	}
	const auto untilText = indefinitely
		? tr::lng_admin_log_restricted_forever(tr::now)
		: tr::lng_admin_log_restricted_until(
			tr::now,
			lt_date,
			langDateTime(base::unixtime::parse(newUntil)));
	auto result = tr::lng_admin_log_restricted(
		tr::now,
		lt_user,
		user,
		lt_until,
		TextWithEntities { untilText },
		Ui::Text::WithEntities);
	const auto changes = GenerateBannedChangeText(newRights, prevRights);
	if (!changes.isEmpty()) {
		result.text.append('\n' + changes);
	}
	return result;
}

QString ExtractInviteLink(const MTPExportedChatInvite &data) {
	return data.match([&](const MTPDchatInviteExported &data) {
		return qs(data.vlink());
	});
}

QString ExtractInviteLinkLabel(const MTPExportedChatInvite &data) {
	return data.match([&](const MTPDchatInviteExported &data) {
		return qs(data.vtitle().value_or_empty());
	});
}

QString InternalInviteLinkUrl(const MTPExportedChatInvite &data) {
	const auto base64 = ExtractInviteLink(data).toUtf8().toBase64();
	return "internal:show_invite_link/?link=" + QString::fromLatin1(base64);
}

QString GenerateInviteLinkText(const MTPExportedChatInvite &data) {
	const auto label = ExtractInviteLinkLabel(data);
	return label.isEmpty() ? ExtractInviteLink(data).replace(
		qstr("https://"),
		QString()
	).replace(
		qstr("t.me/+"),
		QString()
	).replace(
		qstr("t.me/joinchat/"),
		QString()
	) : label;
}

QString GenerateInviteLinkLink(const MTPExportedChatInvite &data) {
	const auto text = GenerateInviteLinkText(data);
	return text.endsWith("...")
		? text
		: textcmdLink(InternalInviteLinkUrl(data), text);
}

TextWithEntities GenerateInviteLinkChangeText(
		const MTPExportedChatInvite &newLink,
		const MTPExportedChatInvite &prevLink) {
	auto link = TextWithEntities{ GenerateInviteLinkText(newLink) };
	if (!link.text.endsWith("...")) {
		link.entities.push_back({
			EntityType::CustomUrl,
			0,
			int(link.text.size()),
			InternalInviteLinkUrl(newLink) });
	}
	auto result = tr::lng_admin_log_edited_invite_link(
		tr::now,
		lt_link,
		link,
		Ui::Text::WithEntities);
	result.text.append('\n');

	const auto label = [](const MTPExportedChatInvite &link) {
		return link.match([](const MTPDchatInviteExported &data) {
			return qs(data.vtitle().value_or_empty());
		});
	};
	const auto expireDate = [](const MTPExportedChatInvite &link) {
		return link.match([](const MTPDchatInviteExported &data) {
			return data.vexpire_date().value_or_empty();
		});
	};
	const auto usageLimit = [](const MTPExportedChatInvite &link) {
		return link.match([](const MTPDchatInviteExported &data) {
			return data.vusage_limit().value_or_empty();
		});
	};
	const auto requestApproval = [](const MTPExportedChatInvite &link) {
		return link.match([](const MTPDchatInviteExported &data) {
			return data.is_request_needed();
		});
	};
	const auto wrapDate = [](TimeId date) {
		return date
			? langDateTime(base::unixtime::parse(date))
			: tr::lng_group_invite_expire_never(tr::now);
	};
	const auto wrapUsage = [](int count) {
		return count
			? QString::number(count)
			: tr::lng_group_invite_usage_any(tr::now);
	};
	const auto wasLabel = label(prevLink);
	const auto nowLabel = label(newLink);
	const auto wasExpireDate = expireDate(prevLink);
	const auto nowExpireDate = expireDate(newLink);
	const auto wasUsageLimit = usageLimit(prevLink);
	const auto nowUsageLimit = usageLimit(newLink);
	const auto wasRequestApproval = requestApproval(prevLink);
	const auto nowRequestApproval = requestApproval(newLink);
	if (wasLabel != nowLabel) {
		result.text.append('\n').append(
			tr::lng_admin_log_invite_link_label(
				tr::now,
				lt_previous,
				wasLabel,
				lt_limit,
				nowLabel));
	}
	if (wasExpireDate != nowExpireDate) {
		result.text.append('\n').append(
			tr::lng_admin_log_invite_link_expire_date(
				tr::now,
				lt_previous,
				wrapDate(wasExpireDate),
				lt_limit,
				wrapDate(nowExpireDate)));
	}
	if (wasUsageLimit != nowUsageLimit) {
		result.text.append('\n').append(
			tr::lng_admin_log_invite_link_usage_limit(
				tr::now,
				lt_previous,
				wrapUsage(wasUsageLimit),
				lt_limit,
				wrapUsage(nowUsageLimit)));
	}
	if (wasRequestApproval != nowRequestApproval) {
		result.text.append('\n').append(
			nowRequestApproval
				? tr::lng_admin_log_invite_link_request_needed(tr::now)
				: tr::lng_admin_log_invite_link_request_not_needed(tr::now));
	}

	result.entities.push_front(
		EntityInText(EntityType::Italic, 0, result.text.size()));
	return result;
};

auto GenerateParticipantString(
		not_null<Main::Session*> session,
		PeerId participantId) {
	// User name in "User name (@username)" format with entities.
	const auto peer = session->data().peer(participantId);
	auto name = TextWithEntities { peer->name };
	if (const auto user = peer->asUser()) {
		auto entityData = QString::number(user->id.value)
			+ '.'
			+ QString::number(user->accessHash());
		name.entities.push_back({
			EntityType::MentionName,
			0,
			int(name.text.size()),
			entityData });
	}
	const auto username = peer->userName();
	if (username.isEmpty()) {
		return name;
	}
	auto mention = TextWithEntities { '@' + username };
	mention.entities.push_back({
		EntityType::Mention,
		0,
		int(mention.text.size()) });
	return tr::lng_admin_log_user_with_username(
		tr::now,
		lt_name,
		name,
		lt_mention,
		mention,
		Ui::Text::WithEntities);
}

auto GenerateParticipantChangeText(
		not_null<ChannelData*> channel,
		const Api::ChatParticipant &participant,
		std::optional<Api::ChatParticipant> oldParticipant = std::nullopt) {
	using Type = Api::ChatParticipant::Type;
	const auto oldRights = oldParticipant
		? oldParticipant->rights()
		: ChatAdminRightsInfo();
	const auto oldRestrictions = oldParticipant
		? oldParticipant->restrictions()
		: ChatRestrictionsInfo();

	const auto generateOther = [&](PeerId participantId) {
		auto user = GenerateParticipantString(
			&channel->session(),
			participantId);
		if (oldParticipant && oldParticipant->type() == Type::Admin) {
			return GenerateAdminChangeText(
				channel,
				user,
				ChatAdminRightsInfo(),
				oldRights);
		} else if (oldParticipant && oldParticipant->type() == Type::Banned) {
			return GenerateBannedChangeText(
				participantId,
				user,
				ChatRestrictionsInfo(),
				oldRestrictions);
		}
		return tr::lng_admin_log_invited(
			tr::now,
			lt_user,
			user,
			Ui::Text::WithEntities);
	};

	auto result = [&] {
		const auto &peerId = participant.id();
		switch (participant.type()) {
		case Api::ChatParticipant::Type::Creator: {
			// No valid string here :(
			return tr::lng_admin_log_transferred(
				tr::now,
				lt_user,
				GenerateParticipantString(&channel->session(), peerId),
				Ui::Text::WithEntities);
		}
		case Api::ChatParticipant::Type::Admin: {
			const auto user = GenerateParticipantString(
				&channel->session(),
				peerId);
			return GenerateAdminChangeText(
				channel,
				user,
				participant.rights(),
				oldRights);
		}
		case Api::ChatParticipant::Type::Restricted:
		case Api::ChatParticipant::Type::Banned: {
			const auto user = GenerateParticipantString(
				&channel->session(),
				peerId);
			return GenerateBannedChangeText(
				peerId,
				user,
				participant.restrictions(),
				oldRestrictions);
		}
		case Api::ChatParticipant::Type::Left:
		case Api::ChatParticipant::Type::Member:
			return generateOther(peerId);
		};
		Unexpected("Participant type in GenerateParticipantChangeText.");
	}();

	result.entities.push_front(
		EntityInText(EntityType::Italic, 0, result.text.size()));
	return result;
}

TextWithEntities GenerateParticipantChangeText(
		not_null<ChannelData*> channel,
		const MTPChannelParticipant &participant,
		std::optional<MTPChannelParticipant>oldParticipant = std::nullopt) {
	return GenerateParticipantChangeText(
		channel,
		Api::ChatParticipant(participant, channel),
		oldParticipant
			? std::make_optional(Api::ChatParticipant(
				*oldParticipant,
				channel))
			: std::nullopt);
}

TextWithEntities GenerateDefaultBannedRightsChangeText(
		not_null<ChannelData*> channel,
		ChatRestrictionsInfo rights,
		ChatRestrictionsInfo oldRights) {
	auto result = TextWithEntities{
		tr::lng_admin_log_changed_default_permissions(tr::now)
	};
	const auto changes = GenerateBannedChangeText(rights, oldRights);
	if (!changes.isEmpty()) {
		result.text.append('\n' + changes);
	}
	result.entities.push_front(
		EntityInText(EntityType::Italic, 0, result.text.size()));
	return result;
}

} // namespace

OwnedItem::OwnedItem(std::nullptr_t) {
}

OwnedItem::OwnedItem(
	not_null<HistoryView::ElementDelegate*> delegate,
	not_null<HistoryItem*> data)
: _data(data)
, _view(_data->createView(delegate)) {
}

OwnedItem::OwnedItem(OwnedItem &&other)
: _data(base::take(other._data))
, _view(base::take(other._view)) {
}

OwnedItem &OwnedItem::operator=(OwnedItem &&other) {
	_data = base::take(other._data);
	_view = base::take(other._view);
	return *this;
}

OwnedItem::~OwnedItem() {
	clearView();
	if (_data) {
		_data->destroy();
	}
}

void OwnedItem::refreshView(
		not_null<HistoryView::ElementDelegate*> delegate) {
	_view = _data->createView(delegate);
}

void OwnedItem::clearView() {
	_view = nullptr;
}

void GenerateItems(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history,
		const MTPDchannelAdminLogEvent &event,
		Fn<void(OwnedItem item, TimeId sentDate)> callback) {
	Expects(history->peer->isChannel());

	using LogTitle = MTPDchannelAdminLogEventActionChangeTitle;
	using LogAbout = MTPDchannelAdminLogEventActionChangeAbout;
	using LogUsername = MTPDchannelAdminLogEventActionChangeUsername;
	using LogPhoto = MTPDchannelAdminLogEventActionChangePhoto;
	using LogInvites = MTPDchannelAdminLogEventActionToggleInvites;
	using LogSign = MTPDchannelAdminLogEventActionToggleSignatures;
	using LogPin = MTPDchannelAdminLogEventActionUpdatePinned;
	using LogEdit = MTPDchannelAdminLogEventActionEditMessage;
	using LogDelete = MTPDchannelAdminLogEventActionDeleteMessage;
	using LogJoin = MTPDchannelAdminLogEventActionParticipantJoin;
	using LogLeave = MTPDchannelAdminLogEventActionParticipantLeave;
	using LogInvite = MTPDchannelAdminLogEventActionParticipantInvite;
	using LogBan = MTPDchannelAdminLogEventActionParticipantToggleBan;
	using LogPromote = MTPDchannelAdminLogEventActionParticipantToggleAdmin;
	using LogSticker = MTPDchannelAdminLogEventActionChangeStickerSet;
	using LogPreHistory =
		MTPDchannelAdminLogEventActionTogglePreHistoryHidden;
	using LogPermissions = MTPDchannelAdminLogEventActionDefaultBannedRights;
	using LogPoll = MTPDchannelAdminLogEventActionStopPoll;
	using LogDiscussion = MTPDchannelAdminLogEventActionChangeLinkedChat;
	using LogLocation = MTPDchannelAdminLogEventActionChangeLocation;
	using LogSlowMode = MTPDchannelAdminLogEventActionToggleSlowMode;
	using LogStartCall = MTPDchannelAdminLogEventActionStartGroupCall;
	using LogDiscardCall = MTPDchannelAdminLogEventActionDiscardGroupCall;
	using LogMute = MTPDchannelAdminLogEventActionParticipantMute;
	using LogUnmute = MTPDchannelAdminLogEventActionParticipantUnmute;
	using LogCallSetting =
		MTPDchannelAdminLogEventActionToggleGroupCallSetting;
	using LogJoinByInvite =
		MTPDchannelAdminLogEventActionParticipantJoinByInvite;
	using LogInviteDelete =
		MTPDchannelAdminLogEventActionExportedInviteDelete;
	using LogInviteRevoke =
		MTPDchannelAdminLogEventActionExportedInviteRevoke;
	using LogInviteEdit = MTPDchannelAdminLogEventActionExportedInviteEdit;
	using LogVolume = MTPDchannelAdminLogEventActionParticipantVolume;
	using LogTTL = MTPDchannelAdminLogEventActionChangeHistoryTTL;
	using LogJoinByRequest =
		MTPDchannelAdminLogEventActionParticipantJoinByRequest;
	using LogNoForwards = MTPDchannelAdminLogEventActionToggleNoForwards;
	using LogActionSendMessage = MTPDchannelAdminLogEventActionSendMessage;

	const auto session = &history->session();
	const auto id = event.vid().v;
	const auto from = history->owner().user(event.vuser_id().v);
	const auto channel = history->peer->asChannel();
	const auto broadcast = channel->isBroadcast();
	const auto &action = event.vaction();
	const auto date = event.vdate().v;
	const auto addPart = [&](
			not_null<HistoryItem*> item,
			TimeId sentDate = 0) {
		return callback(OwnedItem(delegate, item), sentDate);
	};

	const auto fromName = from->name;
	const auto fromLink = from->createOpenLink();
	const auto fromLinkText = textcmdLink(1, fromName);

	auto addSimpleServiceMessage = [&](
			const QString &text,
			PhotoData *photo = nullptr,
			bool showTime = true) {
		auto message = HistoryService::PreparedText { text };
		message.links.push_back(fromLink);
		addPart(history->makeServiceMessage(
			history->nextNonHistoryEntryId(),
			MessageFlag::AdminLogEntry,
			date,
			message,
			peerToUser(from->id),
			photo,
			showTime));
	};

	const auto createChangeTitle = [&](const LogTitle &action) {
		auto text = (channel->isMegagroup()
			? tr::lng_action_changed_title
			: tr::lng_admin_log_changed_title_channel)(
				tr::now,
				lt_from,
				fromLinkText,
				lt_title,
				qs(action.vnew_value()));
		addSimpleServiceMessage(text);
	};

	const auto makeSimpleTextMessage = [&](TextWithEntities &&text) {
		const auto bodyFlags = MessageFlag::HasFromId
			| MessageFlag::AdminLogEntry;
		const auto bodyReplyTo = MsgId();
		const auto bodyViaBotId = UserId();
		const auto bodyGroupedId = uint64();
		return history->makeMessage(
			history->nextNonHistoryEntryId(),
			bodyFlags,
			bodyReplyTo,
			bodyViaBotId,
			date,
			peerToUser(from->id),
			QString(),
			std::move(text),
			MTP_messageMediaEmpty(),
			HistoryMessageMarkupData(),
			bodyGroupedId);
	};

	const auto addSimpleTextMessage = [&](TextWithEntities &&text) {
		addPart(makeSimpleTextMessage(std::move(text)));
	};

	const auto createChangeAbout = [&](const LogAbout &action) {
		const auto newValue = qs(action.vnew_value());
		const auto oldValue = qs(action.vprev_value());
		const auto text = (channel->isMegagroup()
			? (newValue.isEmpty()
				? tr::lng_admin_log_removed_description_group
				: tr::lng_admin_log_changed_description_group)
			: (newValue.isEmpty()
				? tr::lng_admin_log_removed_description_channel
				: tr::lng_admin_log_changed_description_channel)
			)(tr::now, lt_from, fromLinkText);
		addSimpleServiceMessage(text, nullptr, false);

		const auto body = makeSimpleTextMessage(
			PrepareText(newValue, QString()));
		if (!oldValue.isEmpty()) {
			const auto oldDescription = PrepareText(oldValue, QString());
			body->addLogEntryOriginal(
				id,
				tr::lng_admin_log_previous_description(tr::now),
				oldDescription);
		}
		addPart(body);
	};

	const auto createChangeUsername = [&](const LogUsername &action) {
		const auto newValue = qs(action.vnew_value());
		const auto oldValue = qs(action.vprev_value());
		const auto text = (channel->isMegagroup()
			? (newValue.isEmpty()
				? tr::lng_admin_log_removed_link_group
				: tr::lng_admin_log_changed_link_group)
			: (newValue.isEmpty()
				? tr::lng_admin_log_removed_link_channel
				: tr::lng_admin_log_changed_link_channel)
			)(tr::now, lt_from, fromLinkText);
		addSimpleServiceMessage(text, nullptr, false);

		const auto body = makeSimpleTextMessage(newValue.isEmpty()
			? TextWithEntities()
			: PrepareText(
				history->session().createInternalLinkFull(newValue),
				QString()));
		if (!oldValue.isEmpty()) {
			const auto oldLink = PrepareText(
				history->session().createInternalLinkFull(oldValue),
				QString());
			body->addLogEntryOriginal(
				id,
				tr::lng_admin_log_previous_link(tr::now),
				oldLink);
		}
		addPart(body);
	};

	const auto createChangePhoto = [&](const LogPhoto &action) {
		action.vnew_photo().match([&](const MTPDphoto &data) {
			const auto photo = history->owner().processPhoto(data);
			const auto text = (channel->isMegagroup()
				? tr::lng_admin_log_changed_photo_group
				: tr::lng_admin_log_changed_photo_channel)(
					tr::now,
					lt_from,
					fromLinkText);
			addSimpleServiceMessage(text, photo);
		}, [&](const MTPDphotoEmpty &data) {
			const auto text = (channel->isMegagroup()
				? tr::lng_admin_log_removed_photo_group
				: tr::lng_admin_log_removed_photo_channel)(
					tr::now,
					lt_from,
					fromLinkText);
			addSimpleServiceMessage(text);
		});
	};

	const auto createToggleInvites = [&](const LogInvites &action) {
		const auto enabled = (action.vnew_value().type() == mtpc_boolTrue);
		const auto text = (enabled
			? tr::lng_admin_log_invites_enabled
			: tr::lng_admin_log_invites_disabled);
		addSimpleServiceMessage(text(tr::now, lt_from, fromLinkText));
	};

	const auto createToggleSignatures = [&](const LogSign &action) {
		const auto enabled = (action.vnew_value().type() == mtpc_boolTrue);
		const auto text = (enabled
			? tr::lng_admin_log_signatures_enabled
			: tr::lng_admin_log_signatures_disabled);
		addSimpleServiceMessage(text(tr::now, lt_from, fromLinkText));
	};

	const auto createUpdatePinned = [&](const LogPin &action) {
		action.vmessage().match([&](const MTPDmessage &data) {
			const auto pinned = data.is_pinned();
			const auto text = (pinned
				? tr::lng_admin_log_pinned_message
				: tr::lng_admin_log_unpinned_message)(
					tr::now,
					lt_from,
					fromLinkText);
			addSimpleServiceMessage(text, nullptr, false);

			const auto detachExistingItem = false;
			addPart(
				history->createItem(
					history->nextNonHistoryEntryId(),
					PrepareLogMessage(action.vmessage(), date),
					MessageFlag::AdminLogEntry,
					detachExistingItem),
				ExtractSentDate(action.vmessage()));
		}, [&](const auto &) {
			const auto text = tr::lng_admin_log_unpinned_message(
				tr::now,
				lt_from,
				fromLinkText);
			addSimpleServiceMessage(text);
		});
	};

	const auto createEditMessage = [&](const LogEdit &action) {
		const auto newValue = ExtractEditedText(
			session,
			action.vnew_message());
		const auto canHaveCaption = MediaCanHaveCaption(
			action.vnew_message());
		const auto text = (!canHaveCaption
			? tr::lng_admin_log_edited_message
			: newValue.text.isEmpty()
			? tr::lng_admin_log_removed_caption
			: tr::lng_admin_log_edited_caption)(
				tr::now,
				lt_from,
				fromLinkText);
		addSimpleServiceMessage(text, nullptr, false);

		auto oldValue = ExtractEditedText(
			session,
			action.vprev_message());
		const auto detachExistingItem = false;
		const auto body = history->createItem(
			history->nextNonHistoryEntryId(),
			PrepareLogMessage(action.vnew_message(), date),
			MessageFlag::AdminLogEntry,
			detachExistingItem);
		if (oldValue.text.isEmpty()) {
			oldValue = PrepareText(
				QString(),
				tr::lng_admin_log_empty_text(tr::now));
		}

		body->addLogEntryOriginal(
			id,
			(canHaveCaption
				? tr::lng_admin_log_previous_caption
				: tr::lng_admin_log_previous_message)(tr::now),
			oldValue);
		addPart(body);
	};

	const auto createDeleteMessage = [&](const LogDelete &action) {
		const auto text = tr::lng_admin_log_deleted_message(
			tr::now,
			lt_from,
			fromLinkText);
		addSimpleServiceMessage(text, nullptr, false);

		const auto detachExistingItem = false;
		addPart(
			history->createItem(
				history->nextNonHistoryEntryId(),
				PrepareLogMessage(action.vmessage(), date),
				MessageFlag::AdminLogEntry,
				detachExistingItem),
			ExtractSentDate(action.vmessage()));
	};

	const auto createParticipantJoin = [&]() {
		const auto text = (channel->isMegagroup()
			? tr::lng_admin_log_participant_joined
			: tr::lng_admin_log_participant_joined_channel);
		addSimpleServiceMessage(text(tr::now, lt_from, fromLinkText));
	};

	const auto createParticipantLeave = [&]() {
		const auto text = (channel->isMegagroup()
			? tr::lng_admin_log_participant_left
			: tr::lng_admin_log_participant_left_channel);
		addSimpleServiceMessage(text(tr::now, lt_from, fromLinkText));
	};

	const auto createParticipantInvite = [&](const LogInvite &action) {
		addSimpleTextMessage(
			GenerateParticipantChangeText(channel, action.vparticipant()));
	};

	const auto createParticipantToggleBan = [&](const LogBan &action) {
		addSimpleTextMessage(
			GenerateParticipantChangeText(
				channel,
				action.vnew_participant(),
				action.vprev_participant()));
	};

	const auto createParticipantToggleAdmin = [&](const LogPromote &action) {
		if ((action.vnew_participant().type() == mtpc_channelParticipantAdmin)
			&& (action.vprev_participant().type()
				== mtpc_channelParticipantCreator)) {
			// In case of ownership transfer we show that message in
			// the "User > Creator" part and skip the "Creator > Admin" part.
			return;
		}
		addSimpleTextMessage(
			GenerateParticipantChangeText(
				channel,
				action.vnew_participant(),
				action.vprev_participant()));
	};

	const auto createChangeStickerSet = [&](const LogSticker &action) {
		const auto set = action.vnew_stickerset();
		const auto removed = (set.type() == mtpc_inputStickerSetEmpty);
		if (removed) {
			const auto text = tr::lng_admin_log_removed_stickers_group(
				tr::now,
				lt_from,
				fromLinkText);
			addSimpleServiceMessage(text);
		} else {
			const auto text = tr::lng_admin_log_changed_stickers_group(
				tr::now,
				lt_from,
				fromLinkText,
				lt_sticker_set,
				textcmdLink(
					2,
					tr::lng_admin_log_changed_stickers_set(tr::now)));
			const auto setLink = std::make_shared<LambdaClickHandler>([=](
					ClickContext context) {
				const auto my = context.other.value<ClickHandlerContext>();
				if (const auto controller = my.sessionWindow.get()) {
					controller->show(
						Box<StickerSetBox>(
							controller,
							Data::FromInputSet(set)),
						Ui::LayerOption::CloseOther);
				}
			});
			auto message = HistoryService::PreparedText { text };
			message.links.push_back(fromLink);
			message.links.push_back(setLink);
			addPart(history->makeServiceMessage(
				history->nextNonHistoryEntryId(),
				MessageFlag::AdminLogEntry,
				date,
				message,
				peerToUser(from->id)));
		}
	};

	const auto createTogglePreHistoryHidden = [&](
			const LogPreHistory &action) {
		const auto hidden = (action.vnew_value().type() == mtpc_boolTrue);
		const auto text = (hidden
			? tr::lng_admin_log_history_made_hidden
			: tr::lng_admin_log_history_made_visible);
		addSimpleServiceMessage(text(tr::now, lt_from, fromLinkText));
	};

	const auto createDefaultBannedRights = [&](
			const LogPermissions &action) {
		addSimpleTextMessage(
			GenerateDefaultBannedRightsChangeText(
				channel,
				ChatRestrictionsInfo(action.vnew_banned_rights()),
				ChatRestrictionsInfo(action.vprev_banned_rights())));
	};

	const auto createStopPoll = [&](const LogPoll &action) {
		const auto text = tr::lng_admin_log_stopped_poll(
			tr::now,
			lt_from,
			fromLinkText);
		addSimpleServiceMessage(text, nullptr, false);

		const auto detachExistingItem = false;
		addPart(
			history->createItem(
				history->nextNonHistoryEntryId(),
				PrepareLogMessage(action.vmessage(), date),
				MessageFlag::AdminLogEntry,
				detachExistingItem),
			ExtractSentDate(action.vmessage()));
	};

	const auto createChangeLinkedChat = [&](const LogDiscussion &action) {
		const auto now = history->owner().channelLoaded(
			action.vnew_value().v);
		if (!now) {
			const auto text = (broadcast
				? tr::lng_admin_log_removed_linked_chat
				: tr::lng_admin_log_removed_linked_channel)(
					tr::now,
					lt_from,
					fromLinkText);
			addSimpleServiceMessage(text);
		} else {
			const auto text = (broadcast
				? tr::lng_admin_log_changed_linked_chat
				: tr::lng_admin_log_changed_linked_channel)(
					tr::now,
					lt_from,
					fromLinkText,
					lt_chat,
					textcmdLink(2, now->name));
			const auto chatLink = std::make_shared<LambdaClickHandler>([=] {
				Ui::showPeerHistory(now, ShowAtUnreadMsgId);
			});
			auto message = HistoryService::PreparedText{ text };
			message.links.push_back(fromLink);
			message.links.push_back(chatLink);
			addPart(history->makeServiceMessage(
				history->nextNonHistoryEntryId(),
				MessageFlag::AdminLogEntry,
				date,
				message,
				peerToUser(from->id)));
		}
	};

	const auto createChangeLocation = [&](const LogLocation &action) {
		action.vnew_value().match([&](const MTPDchannelLocation &data) {
			const auto address = qs(data.vaddress());
			const auto link = data.vgeo_point().match([&](
					const MTPDgeoPoint &data) {
				return textcmdLink(
					LocationClickHandler::Url(Data::LocationPoint(data)),
					address);
			}, [&](const MTPDgeoPointEmpty &) {
				return address;
			});
			const auto text = tr::lng_admin_log_changed_location_chat(
				tr::now,
				lt_from,
				fromLinkText,
				lt_address,
				link);
			addSimpleServiceMessage(text);
		}, [&](const MTPDchannelLocationEmpty &) {
			const auto text = tr::lng_admin_log_removed_location_chat(
				tr::now,
				lt_from,
				fromLinkText);
			addSimpleServiceMessage(text);
		});
	};

	const auto createToggleSlowMode = [&](const LogSlowMode &action) {
		if (const auto seconds = action.vnew_value().v) {
			const auto duration = (seconds >= 60)
				? tr::lng_admin_log_slow_mode_minutes(
					tr::now,
					lt_count,
					seconds / 60)
				: tr::lng_admin_log_slow_mode_seconds(
					tr::now,
					lt_count,
					seconds);
			const auto text = tr::lng_admin_log_changed_slow_mode(
				tr::now,
				lt_from,
				fromLinkText,
				lt_duration,
				duration);
			addSimpleServiceMessage(text);
		} else {
			const auto text = tr::lng_admin_log_removed_slow_mode(
				tr::now,
				lt_from,
				fromLinkText);
			addSimpleServiceMessage(text);
		}
	};

	const auto createStartGroupCall = [&](const LogStartCall &data) {
		const auto text = (broadcast
			? tr::lng_admin_log_started_group_call_channel
			: tr::lng_admin_log_started_group_call)(
				tr::now,
				lt_from,
				fromLinkText);
		addSimpleServiceMessage(text);
	};

	const auto createDiscardGroupCall = [&](const LogDiscardCall &data) {
		const auto text = (broadcast
			? tr::lng_admin_log_discarded_group_call_channel
			: tr::lng_admin_log_discarded_group_call)(
				tr::now,
				lt_from,
				fromLinkText);
		addSimpleServiceMessage(text);
	};

	const auto groupCallParticipantPeer = [&](
			const MTPGroupCallParticipant &data) {
		return data.match([&](const MTPDgroupCallParticipant &data) {
			return history->owner().peer(peerFromMTP(data.vpeer()));
		});
	};

	const auto addServiceMessageWithLink = [&](
			const QString &text,
			const ClickHandlerPtr &link) {
		auto message = HistoryService::PreparedText{ text };
		message.links.push_back(fromLink);
		message.links.push_back(link);
		addPart(history->makeServiceMessage(
			history->nextNonHistoryEntryId(),
			MessageFlag::AdminLogEntry,
			date,
			message,
			peerToUser(from->id)));
	};

	const auto createParticipantMute = [&](const LogMute &data) {
		const auto participantPeer = groupCallParticipantPeer(
			data.vparticipant());
		const auto participantPeerLink = participantPeer->createOpenLink();
		const auto participantPeerLinkText = textcmdLink(
			2,
			participantPeer->name);
		const auto text = (broadcast
			? tr::lng_admin_log_muted_participant_channel
			: tr::lng_admin_log_muted_participant)(
			tr::now,
			lt_from,
			fromLinkText,
			lt_user,
			participantPeerLinkText);
		addServiceMessageWithLink(text, participantPeerLink);
	};

	const auto createParticipantUnmute = [&](const LogUnmute &data) {
		const auto participantPeer = groupCallParticipantPeer(
			data.vparticipant());
		const auto participantPeerLink = participantPeer->createOpenLink();
		const auto participantPeerLinkText = textcmdLink(
			2,
			participantPeer->name);
		const auto text = (broadcast
			? tr::lng_admin_log_unmuted_participant_channel
			: tr::lng_admin_log_unmuted_participant)(
			tr::now,
			lt_from,
			fromLinkText,
			lt_user,
			participantPeerLinkText);
		addServiceMessageWithLink(text, participantPeerLink);
	};

	const auto createToggleGroupCallSetting = [&](
			const LogCallSetting &data) {
		const auto text = (mtpIsTrue(data.vjoin_muted())
			? (broadcast
				? tr::lng_admin_log_disallowed_unmute_self_channel
				: tr::lng_admin_log_disallowed_unmute_self)
			: (broadcast
				? tr::lng_admin_log_allowed_unmute_self_channel
				: tr::lng_admin_log_allowed_unmute_self))(
			tr::now,
			lt_from,
			fromLinkText);
		addSimpleServiceMessage(text);
	};

	const auto addInviteLinkServiceMessage = [&](
			const QString &text,
			const MTPExportedChatInvite &data,
			ClickHandlerPtr additional = nullptr) {
		auto message = HistoryService::PreparedText{ text };
		message.links.push_back(fromLink);
		if (!ExtractInviteLink(data).endsWith("...")) {
			message.links.push_back(std::make_shared<UrlClickHandler>(
				InternalInviteLinkUrl(data)));
		}
		if (additional) {
			message.links.push_back(std::move(additional));
		}
		addPart(history->makeServiceMessage(
			history->nextNonHistoryEntryId(),
			MessageFlag::AdminLogEntry,
			date,
			message,
			peerToUser(from->id),
			nullptr));
	};

	const auto createParticipantJoinByInvite = [&](
			const LogJoinByInvite &data) {
		const auto text = (channel->isMegagroup()
			? tr::lng_admin_log_participant_joined_by_link
			: tr::lng_admin_log_participant_joined_by_link_channel);
		addInviteLinkServiceMessage(
			text(
				tr::now,
				lt_from,
				fromLinkText,
				lt_link,
				GenerateInviteLinkLink(data.vinvite())),
			data.vinvite());
	};

	const auto createExportedInviteDelete = [&](const LogInviteDelete &data) {
		addInviteLinkServiceMessage(
			tr::lng_admin_log_delete_invite_link(
				tr::now,
				lt_from,
				fromLinkText,
				lt_link,
				GenerateInviteLinkLink(data.vinvite())),
			data.vinvite());
	};

	const auto createExportedInviteRevoke = [&](const LogInviteRevoke &data) {
		addInviteLinkServiceMessage(
			tr::lng_admin_log_revoke_invite_link(
				tr::now,
				lt_from,
				fromLinkText,
				lt_link,
				GenerateInviteLinkLink(data.vinvite())),
			data.vinvite());
	};

	const auto createExportedInviteEdit = [&](const LogInviteEdit &data) {
		addSimpleTextMessage(
			GenerateInviteLinkChangeText(
				data.vnew_invite(),
				data.vprev_invite()));
	};

	const auto createParticipantVolume = [&](const LogVolume &data) {
		const auto participantPeer = groupCallParticipantPeer(
			data.vparticipant());
		const auto participantPeerLink = participantPeer->createOpenLink();
		const auto participantPeerLinkText = textcmdLink(
			2,
			participantPeer->name);
		const auto volume = data.vparticipant().match([&](
				const MTPDgroupCallParticipant &data) {
			return data.vvolume().value_or(10000);
		});
		const auto volumeText = QString::number(volume / 100) + '%';
		auto text = (broadcast
			? tr::lng_admin_log_participant_volume_channel
			: tr::lng_admin_log_participant_volume)(
			tr::now,
			lt_from,
			fromLinkText,
			lt_user,
			participantPeerLinkText,
			lt_percent,
			volumeText);
		addServiceMessageWithLink(text, participantPeerLink);
	};

	const auto createChangeHistoryTTL = [&](const LogTTL &data) {
		const auto was = data.vprev_value().v;
		const auto now = data.vnew_value().v;
		const auto wrap = [](int duration) {
			return (duration == 5)
				? u"5 seconds"_q
				: (duration < 2 * 86400)
				? tr::lng_manage_messages_ttl_after1(tr::now)
				: (duration < 8 * 86400)
				? tr::lng_manage_messages_ttl_after2(tr::now)
				: tr::lng_manage_messages_ttl_after3(tr::now);
		};
		const auto text = !was
			? tr::lng_admin_log_messages_ttl_set(
				tr::now,
				lt_from,
				fromLinkText,
				lt_duration,
				wrap(now))
			: !now
			? tr::lng_admin_log_messages_ttl_removed(
				tr::now,
				lt_from,
				fromLinkText,
				lt_duration,
				wrap(was))
			: tr::lng_admin_log_messages_ttl_changed(
				tr::now,
				lt_from,
				fromLinkText,
				lt_previous,
				wrap(was),
				lt_duration,
				wrap(now));
		addSimpleServiceMessage(text);
	};

	const auto createParticipantJoinByRequest = [&](
			const LogJoinByRequest &data) {
		const auto user = channel->owner().user(UserId(data.vapproved_by()));
		const auto text = (channel->isMegagroup()
			? tr::lng_admin_log_participant_approved_by_link
			: tr::lng_admin_log_participant_approved_by_link_channel);
		const auto linkText = GenerateInviteLinkLink(data.vinvite());
		const auto adminIndex = linkText.endsWith("...") ? 2 : 3;
		addInviteLinkServiceMessage(
			text(
				tr::now,
				lt_from,
				fromLinkText,
				lt_link,
				linkText,
				lt_user,
				textcmdLink(adminIndex, user->name)),
			data.vinvite(),
			user->createOpenLink());
	};

	const auto createToggleNoForwards = [&](const LogNoForwards &data) {
		const auto disabled = (data.vnew_value().type() == mtpc_boolTrue);
		const auto text = (disabled
			? tr::lng_admin_log_forwards_disabled
			: tr::lng_admin_log_forwards_enabled);
		addSimpleServiceMessage(text(tr::now, lt_from, fromLinkText));
	};

	const auto createSendMessage = [&](const LogActionSendMessage &data) {
		const auto text = tr::lng_admin_log_sent_message(
			tr::now,
			lt_from,
			fromLinkText);
		addSimpleServiceMessage(text);

		const auto detachExistingItem = false;
		addPart(
			history->createItem(
				history->nextNonHistoryEntryId(),
				PrepareLogMessage(data.vmessage(), date),
				MessageFlag::AdminLogEntry,
				detachExistingItem),
			ExtractSentDate(data.vmessage()));
	};

	action.match([&](const LogTitle &data) {
		createChangeTitle(data);
	}, [&](const LogAbout &data) {
		createChangeAbout(data);
	}, [&](const LogUsername &data) {
		createChangeUsername(data);
	}, [&](const LogPhoto &data) {
		createChangePhoto(data);
	}, [&](const LogInvites &data) {
		createToggleInvites(data);
	}, [&](const LogSign &data) {
		createToggleSignatures(data);
	}, [&](const LogPin &data) {
		createUpdatePinned(data);
	}, [&](const LogEdit &data) {
		createEditMessage(data);
	}, [&](const LogDelete &data) {
		createDeleteMessage(data);
	}, [&](const LogJoin &) {
		createParticipantJoin();
	}, [&](const LogLeave &) {
		createParticipantLeave();
	}, [&](const LogInvite &data) {
		createParticipantInvite(data);
	}, [&](const LogBan &data) {
		createParticipantToggleBan(data);
	}, [&](const LogPromote &data) {
		createParticipantToggleAdmin(data);
	}, [&](const LogSticker &data) {
		createChangeStickerSet(data);
	}, [&](const LogPreHistory &data) {
		createTogglePreHistoryHidden(data);
	}, [&](const LogPermissions &data) {
		createDefaultBannedRights(data);
	}, [&](const LogPoll &data) {
		createStopPoll(data);
	}, [&](const LogDiscussion &data) {
		createChangeLinkedChat(data);
	}, [&](const LogLocation &data) {
		createChangeLocation(data);
	}, [&](const LogSlowMode &data) {
		createToggleSlowMode(data);
	}, [&](const LogStartCall &data) {
		createStartGroupCall(data);
	}, [&](const LogDiscardCall &data) {
		createDiscardGroupCall(data);
	}, [&](const LogMute &data) {
		createParticipantMute(data);
	}, [&](const LogUnmute &data) {
		createParticipantUnmute(data);
	}, [&](const LogCallSetting &data) {
		createToggleGroupCallSetting(data);
	}, [&](const LogJoinByInvite &data) {
		createParticipantJoinByInvite(data);
	}, [&](const LogInviteDelete &data) {
		createExportedInviteDelete(data);
	}, [&](const LogInviteRevoke &data) {
		createExportedInviteRevoke(data);
	}, [&](const LogInviteEdit &data) {
		createExportedInviteEdit(data);
	}, [&](const LogVolume &data) {
		createParticipantVolume(data);
	}, [&](const LogTTL &data) {
		createChangeHistoryTTL(data);
	}, [&](const LogJoinByRequest &data) {
		createParticipantJoinByRequest(data);
	}, [&](const LogNoForwards &data) {
		createToggleNoForwards(data);
	}, [&](const LogActionSendMessage &data) {
		createSendMessage(data);
	});
}

} // namespace AdminLog
