/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_sending.h"

#include "api/api_text_entities.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_location.h"
#include "data/data_channel.h" // ChannelData::addsSignature.
#include "data/data_user.h" // UserData::name
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_histories.h"
#include "data/data_changes.h"
#include "data/stickers/data_stickers.h"
#include "history/history.h"
#include "history/history_message.h" // NewMessageFlags.
#include "chat_helpers/message_field.h" // ConvertTextTagsToEntities.
#include "chat_helpers/stickers_dice_pack.h" // DicePacks::kDiceString.
#include "ui/text/text_entity.h" // TextWithEntities.
#include "ui/item_text_options.h" // Ui::ItemTextOptions.
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "storage/localimageloader.h"
#include "storage/file_upload.h"
#include "mainwidget.h"
#include "apiwrap.h"

namespace Api {
namespace {

void InnerFillMessagePostFlags(
		const SendOptions &options,
		not_null<PeerData*> peer,
		MessageFlags &flags) {
	const auto anonymousPost = peer->amAnonymous();
	if (!anonymousPost || options.sendAs) {
		flags |= MessageFlag::HasFromId;
		return;
	} else if (peer->asMegagroup()) {
		return;
	}
	flags |= MessageFlag::Post;
	// Don't display views and author of a new post when it's scheduled.
	if (options.scheduled) {
		return;
	}
	flags |= MessageFlag::HasViews;
	if (peer->asChannel()->addsSignature()) {
		flags |= MessageFlag::HasPostAuthor;
	}
}

template <typename MediaData>
void SendExistingMedia(
		MessageToSend &&message,
		not_null<MediaData*> media,
		Fn<MTPInputMedia()> inputMedia,
		Data::FileOrigin origin,
		std::optional<MsgId> localMessageId,
		Fn<void()> doneCallback = nullptr,
		bool forwarding = false) {
	const auto history = message.action.history;
	const auto peer = history->peer;
	const auto session = &history->session();
	const auto api = &session->api();

	message.action.clearDraft = false;
	message.action.generateLocal = true;
	api->sendAction(message.action);

	const auto newId = FullMsgId(
		peer->id,
		localMessageId
			? (*localMessageId)
			: session->data().nextLocalMessageId());
	const auto randomId = base::RandomValue<uint64>();

	auto flags = NewMessageFlags(peer);
	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (message.action.replyTo) {
		flags |= MessageFlag::HasReplyInfo;
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
	}
	const auto anonymousPost = peer->amAnonymous();
	const auto silentPost = ShouldSendSilent(peer, message.action.options);
	InnerFillMessagePostFlags(message.action.options, peer, flags);
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	const auto sendAs = message.action.options.sendAs;
	const auto messageFromId = sendAs
		? sendAs->id
		: anonymousPost
		? 0
		: session->userPeerId();
	if (sendAs) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_send_as;
	}
	const auto messagePostAuthor = peer->isBroadcast()
		? session->user()->name()
		: QString();

	auto caption = TextWithEntities{
		message.textWithTags.text,
		TextUtilities::ConvertTextTagsToEntities(message.textWithTags.tags)
	};
	TextUtilities::Trim(caption);
	auto sentEntities = EntitiesToMTP(
		session,
		caption.entities,
		ConvertOption::SkipLocal);
	if (!sentEntities.v.isEmpty()) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_entities;
	}
	const auto replyTo = message.action.replyTo;
	const auto captionText = caption.text;

	if (message.action.options.scheduled) {
		flags |= MessageFlag::IsOrWasScheduled;
		sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_date;
	}

	session->data().registerMessageRandomId(randomId, newId);

	const auto viaBotId = UserId();
	history->addNewLocalMessage(
		newId.msg,
		flags,
		viaBotId,
		replyTo,
		HistoryItem::NewMessageDate(message.action.options.scheduled),
		messageFromId,
		messagePostAuthor,
		media,
		caption,
		HistoryMessageMarkupData());

	auto performRequest = [=](const auto &repeatRequest) -> void {
		auto &histories = history->owner().histories();
		const auto requestType = Data::Histories::RequestType::Send;
		histories.sendRequest(history, requestType, [=](Fn<void()> finish) {
			history->sendRequestId = api->request(MTPmessages_SendMedia(
				MTP_flags(sendFlags),
				peer->input,
				MTP_int(replyTo),
				inputMedia(),
				MTP_string(captionText),
				MTP_long(randomId),
				MTPReplyMarkup(),
				sentEntities,
				MTP_int(message.action.options.scheduled),
				(sendAs ? sendAs->input : MTP_inputPeerEmpty())
			)).done([=](const MTPUpdates &result, mtpRequestId requestId) {
				api->applyUpdates(result, randomId);
				if (doneCallback) {
					doneCallback();
				}
				finish();
			}).fail([=](const MTP::Error &error) {
				if (error.code() == 400
					&& error.type().startsWith(qstr("FILE_REFERENCE_"))) {
					const auto usedFileReference = media->fileReference();
					api->refreshFileReference(origin, [=](const auto &result) {
						if (media->fileReference() != usedFileReference) {
							repeatRequest(repeatRequest);
						} else {
							api->sendMessageFail(error, peer, randomId, newId);
						}
					});
				} else {
					api->sendMessageFail(error, peer, randomId, newId);
				}
				finish();
			}).afterRequest(history->sendRequestId
			).send();
			return history->sendRequestId;
		});
	};
	performRequest(performRequest);

	if (!forwarding) {
		api->finishForwarding(message.action);
	}
}

} // namespace

void SendWebDocument(
		Api::MessageToSend &&message,
		not_null<DocumentData*> document,
		std::optional<MsgId> localMessageId,
		Fn<void()> doneCallback,
		bool forwarding) {
	const auto inputMedia = [=] {
		return MTP_inputMediaDocumentExternal(
			MTP_flags(0),
			MTP_string(document->url()),
			MTPint()); // ttl_seconds
	};
	SendExistingMedia(
		std::move(message),
		document,
		inputMedia,
		document->stickerOrGifOrigin(),
		std::move(localMessageId),
		(doneCallback ? std::move(doneCallback) : nullptr),
		forwarding);
}

void SendExistingDocument(
		MessageToSend &&message,
		not_null<DocumentData*> document,
		std::optional<MsgId> localMessageId,
		Fn<void()> doneCallback,
		bool forwarding) {
	const auto inputMedia = [=] {
		return MTP_inputMediaDocument(
			MTP_flags(0),
			document->mtpInput(),
			MTPint(), // ttl_seconds
			MTPstring()); // query
	};
	SendExistingMedia(
		std::move(message),
		document,
		inputMedia,
		document->stickerOrGifOrigin(),
		std::move(localMessageId),
		(doneCallback ? std::move(doneCallback) : nullptr),
		forwarding);

	if (document->sticker()) {
		document->owner().stickers().incrementSticker(document);
	}
}

void SendExistingPhoto(
		MessageToSend &&message,
		not_null<PhotoData*> photo,
		std::optional<MsgId> localMessageId,
		Fn<void()> doneCallback,
		bool forwarding) {
	const auto inputMedia = [=] {
		return MTP_inputMediaPhoto(
			MTP_flags(0),
			photo->mtpInput(),
			MTPint());
	};
	SendExistingMedia(
		std::move(message),
		photo,
		inputMedia,
		Data::FileOrigin(),
		std::move(localMessageId),
		(doneCallback ? std::move(doneCallback) : nullptr),
		forwarding);
}

bool SendDice(
		MessageToSend &message,
		Fn<void(const MTPUpdates &, mtpRequestId)> doneCallback,
		bool forwarding) {
	const auto full = QStringView(message.textWithTags.text).trimmed();
	auto length = 0;
	if (!Ui::Emoji::Find(full.data(), full.data() + full.size(), &length)
		|| length != full.size()) {
		return false;
	}
	auto &account = message.action.history->session().account();
	auto &config = account.appConfig();
	static const auto hardcoded = std::vector<QString>{
		Stickers::DicePacks::kDiceString,
		Stickers::DicePacks::kDartString,
		Stickers::DicePacks::kSlotString,
		Stickers::DicePacks::kFballString,
		Stickers::DicePacks::kFballString + QChar(0xFE0F),
		Stickers::DicePacks::kBballString,
	};
	const auto list = config.get<std::vector<QString>>(
		"emojies_send_dice",
		hardcoded);
	const auto emoji = full.toString();
	if (!ranges::contains(list, emoji)) {
		return false;
	}
	const auto history = message.action.history;
	const auto peer = history->peer;
	const auto session = &history->session();
	const auto api = &session->api();

	message.textWithTags = TextWithTags();
	message.action.clearDraft = false;
	message.action.generateLocal = true;
	api->sendAction(message.action);

	const auto newId = FullMsgId(
		peer->id,
		session->data().nextLocalMessageId());
	const auto randomId = base::RandomValue<uint64>();

	auto &histories = history->owner().histories();
	auto flags = NewMessageFlags(peer);
	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (message.action.replyTo) {
		flags |= MessageFlag::HasReplyInfo;
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
	}
	const auto replyHeader = NewMessageReplyHeader(message.action);
	const auto anonymousPost = peer->amAnonymous();
	const auto silentPost = ShouldSendSilent(peer, message.action.options);
	InnerFillMessagePostFlags(message.action.options, peer, flags);
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	const auto sendAs = message.action.options.sendAs;
	const auto messageFromId = sendAs
		? sendAs->id
		: anonymousPost
		? 0
		: session->userPeerId();
	if (sendAs) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_send_as;
	}
	const auto messagePostAuthor = peer->isBroadcast()
		? session->user()->name()
		: QString();
	const auto replyTo = message.action.replyTo;

	if (message.action.options.scheduled) {
		flags |= MessageFlag::IsOrWasScheduled;
		sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_date;
	}

	session->data().registerMessageRandomId(randomId, newId);

	const auto viaBotId = UserId();
	history->addNewLocalMessage(
		newId.msg,
		flags,
		viaBotId,
		message.action.replyTo,
		HistoryItem::NewMessageDate(message.action.options.scheduled),
		messageFromId,
		messagePostAuthor,
		TextWithEntities(),
		MTP_messageMediaDice(MTP_int(0), MTP_string(emoji)),
		HistoryMessageMarkupData());

	const auto requestType = Data::Histories::RequestType::Send;
	histories.sendRequest(history, requestType, [=](Fn<void()> finish) {
		history->sendRequestId = api->request(MTPmessages_SendMedia(
			MTP_flags(sendFlags),
			peer->input,
			MTP_int(replyTo),
			MTP_inputMediaDice(MTP_string(emoji)),
			MTP_string(),
			MTP_long(randomId),
			MTPReplyMarkup(),
			MTP_vector<MTPMessageEntity>(),
			MTP_int(message.action.options.scheduled),
			(sendAs ? sendAs->input : MTP_inputPeerEmpty())
		)).done([=](const MTPUpdates &result, mtpRequestId requestId) {
			api->applyUpdates(result, randomId);
			if (doneCallback) {
				doneCallback(result, requestId);
			}
			finish();
		}).fail([=](const MTP::Error &error) {
			api->sendMessageFail(error, peer, randomId, newId);
			finish();
		}).afterRequest(history->sendRequestId
		).send();
		return history->sendRequestId;
	});
	if (!forwarding) {
		api->finishForwarding(message.action);
	}
	return true;
}

void FillMessagePostFlags(
		const SendAction &action,
		not_null<PeerData*> peer,
		MessageFlags &flags) {
	InnerFillMessagePostFlags(action.options, peer, flags);
}

void SendConfirmedFile(
		not_null<Main::Session*> session,
		const std::shared_ptr<FileLoadResult> &file) {
	const auto isEditing = (file->type != SendMediaType::Audio)
		&& (file->to.replaceMediaOf != 0);
	const auto newId = FullMsgId(
		file->to.peer,
		isEditing
			? file->to.replaceMediaOf
			: session->data().nextLocalMessageId());
	const auto groupId = file->album ? file->album->groupId : uint64(0);
	if (file->album) {
		const auto proj = [](const SendingAlbum::Item &item) {
			return item.taskId;
		};
		const auto it = ranges::find(file->album->items, file->taskId, proj);
		Assert(it != file->album->items.end());

		it->msgId = newId;
	}
	session->uploader().upload(newId, file);

	const auto itemToEdit = isEditing
		? session->data().message(newId)
		: nullptr;

	const auto history = session->data().history(file->to.peer);
	const auto peer = history->peer;

	auto action = SendAction(history, file->to.options);
	action.clearDraft = false;
	action.replyTo = file->to.replyTo;
	action.generateLocal = true;
	session->api().sendAction(action);

	auto caption = TextWithEntities{
		file->caption.text,
		TextUtilities::ConvertTextTagsToEntities(file->caption.tags)
	};
	const auto prepareFlags = Ui::ItemTextOptions(
		history,
		session->user()).flags;
	TextUtilities::PrepareForSending(caption, prepareFlags);
	TextUtilities::Trim(caption);

	auto flags = isEditing ? MessageFlags() : NewMessageFlags(peer);
	if (file->to.replyTo) {
		flags |= MessageFlag::HasReplyInfo;
	}
	const auto replyHeader = NewMessageReplyHeader(action);
	const auto anonymousPost = peer->amAnonymous();
	const auto silentPost = ShouldSendSilent(peer, file->to.options);
	FillMessagePostFlags(action, peer, flags);
	if (silentPost) {
		flags |= MessageFlag::Silent;
	}
	if (file->to.options.scheduled) {
		flags |= MessageFlag::IsOrWasScheduled;

		// Scheduled messages have no the 'edited' badge.
		flags |= MessageFlag::HideEdited;
	}
	if (file->type == SendMediaType::Audio) {
		if (!peer->isChannel() || peer->isMegagroup()) {
			flags |= MessageFlag::MediaIsUnread;
		}
	}

	const auto messageFromId =
		file->to.options.sendAs
		? file->to.options.sendAs->id
		: anonymousPost
		? PeerId()
		: session->userPeerId();
	const auto messagePostAuthor = peer->isBroadcast()
		? session->user()->name()
		: QString();

	const auto media = MTPMessageMedia([&] {
		if (file->type == SendMediaType::Photo) {
			return MTP_messageMediaPhoto(
				MTP_flags(MTPDmessageMediaPhoto::Flag::f_photo),
				file->photo,
				MTPint());
		} else if (file->type == SendMediaType::File) {
			return MTP_messageMediaDocument(
				MTP_flags(MTPDmessageMediaDocument::Flag::f_document),
				file->document,
				MTPint());
		} else if (file->type == SendMediaType::Audio) {
			return MTP_messageMediaDocument(
				MTP_flags(MTPDmessageMediaDocument::Flag::f_document),
				file->document,
				MTPint());
		} else {
			Unexpected("Type in sendFilesConfirmed.");
		}
	}());

	if (itemToEdit) {
		itemToEdit->savePreviousMedia();
		auto edition = HistoryMessageEdition();
		edition.isEditHide = (flags & MessageFlag::HideEdited);
		edition.editDate = 0;
		edition.views = 0;
		edition.forwards = 0;
		edition.ttl = 0;
		edition.mtpMedia = &media;
		edition.textWithEntities = caption;
		edition.useSameMarkup = true;
		edition.useSameReplies = true;
		edition.useSameReactions = true;
		itemToEdit->applyEdition(std::move(edition));
	} else {
		const auto viaBotId = UserId();
		history->addNewLocalMessage(
			newId.msg,
			flags,
			viaBotId,
			file->to.replyTo,
			HistoryItem::NewMessageDate(file->to.options.scheduled),
			messageFromId,
			messagePostAuthor,
			caption,
			media,
			HistoryMessageMarkupData(),
			groupId);
	}

	if (isEditing) {
		return;
	}

	session->data().sendHistoryChangeNotifications();
	if (!itemToEdit) {
		session->changes().historyUpdated(
			history,
			(action.options.scheduled
				? Data::HistoryUpdate::Flag::ScheduledSent
				: Data::HistoryUpdate::Flag::MessageSent));
	}
}

void SendLocationPoint(
		const Data::LocationPoint &data,
		const SendAction &action,
		Fn<void()> done,
		Fn<void(const MTP::Error &error)> fail) {
	const auto history = action.history;
	const auto session = &history->session();
	const auto api = &session->api();
	const auto peer = history->peer;
	api->sendAction(action);

	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (action.replyTo) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
	}
	if (action.clearDraft) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_clear_draft;
		history->clearLocalDraft();
		history->clearCloudDraft();
	}
	const auto sendAs = action.options.sendAs;

	if (sendAs) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_send_as;
	}
	const auto silentPost = ShouldSendSilent(peer, action.options);
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	if (action.options.scheduled) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_date;
	}
	auto &histories = history->owner().histories();
	const auto requestType = Data::Histories::RequestType::Send;
	histories.sendRequest(history, requestType, [=](Fn<void()> finish) {
		const auto replyTo = action.replyTo;
		history->sendRequestId = api->request(MTPmessages_SendMedia(
			MTP_flags(sendFlags),
			peer->input,
			MTP_int(replyTo),
			MTP_inputMediaGeoPoint(
				MTP_inputGeoPoint(
					MTP_flags(0),
					MTP_double(data.lat()),
					MTP_double(data.lon()),
					MTP_int(0))),
			MTP_string(),
			MTP_long(base::RandomValue<uint64>()),
			MTPReplyMarkup(),
			MTPVector<MTPMessageEntity>(),
			MTP_int(action.options.scheduled),
			(sendAs ? sendAs->input : MTP_inputPeerEmpty())
		)).done([=](const MTPUpdates &result) mutable {
			api->applyUpdates(result);
			done();
			finish();
		}).fail([=](const MTP::Error &error) mutable {
			if (fail) {
				fail(error);
			}
			finish();
		}).afterRequest(history->sendRequestId
		).send();
		return history->sendRequestId;
	});
}

} // namespace Api
