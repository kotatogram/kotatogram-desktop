/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_scheduled_section.h"

#include "history/view/controls/history_view_compose_controls.h"
#include "history/view/history_view_empty_list_bubble.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_schedule_box.h"
#include "history/history.h"
#include "history/history_drag_area.h"
#include "history/history_item.h"
#include "chat_helpers/send_context_menu.h" // SendMenu::Type.
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/layers/generic_box.h"
#include "ui/item_text_options.h"
#include "ui/toast/toast.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "ui/special_buttons.h"
#include "ui/ui_utility.h"
#include "ui/text/text_utilities.h"
#include "ui/toasts/common_toasts.h"
#include "api/api_common.h"
#include "api/api_editing.h"
#include "api/api_sending.h"
#include "apiwrap.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/delete_messages_box.h"
#include "boxes/edit_caption_box.h"
#include "boxes/send_files_box.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "base/event_filter.h"
#include "base/call_delayed.h"
#include "core/file_utilities.h"
#include "main/main_session.h"
#include "data/data_chat_participant_status.h"
#include "data/data_session.h"
#include "data/data_scheduled_messages.h"
#include "data/data_user.h"
#include "storage/storage_media_prepare.h"
#include "storage/storage_account.h"
#include "inline_bots/inline_bot_result.h"
#include "lang/lang_keys.h"
#include "facades.h"
#include "styles/style_chat.h"
#include "styles/style_window.h"
#include "styles/style_info.h"
#include "styles/style_boxes.h"

#include <QtCore/QMimeData>

namespace HistoryView {
namespace {

bool CanSendFiles(not_null<const QMimeData*> data) {
	if (data->hasImage()) {
		return true;
	} else if (const auto urls = data->urls(); !urls.empty()) {
		if (ranges::all_of(urls, &QUrl::isLocalFile)) {
			return true;
		}
	}
	return false;
}

} // namespace

object_ptr<Window::SectionWidget> ScheduledMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third) {
		return nullptr;
	}
	auto result = object_ptr<ScheduledWidget>(parent, controller, _history);
	result->setInternalState(geometry, this);
	return result;
}

ScheduledWidget::ScheduledWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<History*> history)
: Window::SectionWidget(parent, controller, history->peer)
, _history(history)
, _scroll(
	this,
	controller->chatStyle()->value(lifetime(), st::historyScroll),
	false)
, _topBar(this, controller)
, _topBarShadow(this)
, _composeControls(std::make_unique<ComposeControls>(
	this,
	controller,
	ComposeControls::Mode::Scheduled,
	SendMenu::Type::PreviewOnly))
, _scrollDown(
		_scroll,
		controller->chatStyle()->value(lifetime(), st::historyToDown)) {
	controller->chatStyle()->paletteChanged(
	) | rpl::start_with_next([=] {
		_scroll->updateBars();
	}, _scroll->lifetime());

	Window::ChatThemeValueFromPeer(
		controller,
		history->peer
	) | rpl::start_with_next([=](std::shared_ptr<Ui::ChatTheme> &&theme) {
		_theme = std::move(theme);
		controller->setChatStyleTheme(_theme);
	}, lifetime());

	const auto state = Dialogs::EntryState{
		.key = _history,
		.section = Dialogs::EntryState::Section::Scheduled,
	};
	_topBar->setActiveChat(state, nullptr);
	_topBar->setCustomTitle(_history->peer->isSelf()
		? tr::lng_reminder_messages(tr::now)
		: tr::lng_scheduled_messages(tr::now));
	_composeControls->setCurrentDialogsEntryState(state);

	_topBar->move(0, 0);
	_topBar->resizeToWidth(width());
	_topBar->show();

	_topBar->sendNowSelectionRequest(
	) | rpl::start_with_next([=] {
		confirmSendNowSelected();
	}, _topBar->lifetime());
	_topBar->deleteSelectionRequest(
	) | rpl::start_with_next([=] {
		confirmDeleteSelected();
	}, _topBar->lifetime());
	_topBar->clearSelectionRequest(
	) | rpl::start_with_next([=] {
		clearSelected();
	}, _topBar->lifetime());

	_topBarShadow->raise();
	controller->adaptive().value(
	) | rpl::start_with_next([=] {
		updateAdaptiveLayout();
	}, lifetime());

	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		controller,
		static_cast<ListDelegate*>(this)));
	_scroll->move(0, _topBar->height());
	_scroll->show();
	_scroll->scrolls(
	) | rpl::start_with_next([=] {
		onScroll();
	}, lifetime());

	_inner->editMessageRequested(
	) | rpl::start_with_next([=](auto fullId) {
		if (const auto item = session().data().message(fullId)) {
			const auto media = item->media();
			if (media && !media->webpage()) {
				if (media->allowsEditCaption()) {
					controller->show(Box<EditCaptionBox>(controller, item));
				}
			} else {
				_composeControls->editMessage(fullId);
			}
		}
	}, _inner->lifetime());

	{
		auto emptyInfo = base::make_unique_q<EmptyListBubbleWidget>(
			_inner,
			controller->chatStyle(),
			st::msgServicePadding);
		const auto emptyText = Ui::Text::Semibold(
			tr::lng_scheduled_messages_empty(tr::now));
		emptyInfo->setText(emptyText);
		_inner->setEmptyInfoWidget(std::move(emptyInfo));
	}

	setupScrollDownButton();
	setupComposeControls();
}

ScheduledWidget::~ScheduledWidget() = default;

void ScheduledWidget::setupComposeControls() {
	_composeControls->setHistory({ .history = _history.get() });

	_composeControls->height(
	) | rpl::start_with_next([=] {
		const auto wasMax = (_scroll->scrollTopMax() == _scroll->scrollTop());
		updateControlsGeometry();
		if (wasMax) {
			listScrollTo(_scroll->scrollTopMax());
		}
	}, lifetime());

	_composeControls->cancelRequests(
	) | rpl::start_with_next([=] {
		listCancelRequest();
	}, lifetime());

	_composeControls->sendRequests(
	) | rpl::start_with_next([=] {
		send();
	}, lifetime());

	_composeControls->sendVoiceRequests(
	) | rpl::start_with_next([=](ComposeControls::VoiceToSend &&data) {
		sendVoice(data.bytes, data.waveform, data.duration);
	}, lifetime());

	_composeControls->sendCommandRequests(
	) | rpl::start_with_next([=](const QString &command) {
		listSendBotCommand(command, FullMsgId());
	}, lifetime());

	const auto saveEditMsgRequestId = lifetime().make_state<mtpRequestId>(0);
	_composeControls->editRequests(
	) | rpl::start_with_next([=](auto data) {
		if (const auto item = session().data().message(data.fullId)) {
			if (item->isScheduled()) {
				edit(item, data.options, saveEditMsgRequestId);
			}
		}
	}, lifetime());

	_composeControls->attachRequests(
	) | rpl::filter([=] {
		return !_choosingAttach;
	}) | rpl::start_with_next([=] {
		_choosingAttach = true;
		base::call_delayed(
			st::historyAttach.ripple.hideDuration,
			this,
			[=] { _choosingAttach = false; chooseAttach(); });
	}, lifetime());

	using Selector = ChatHelpers::TabbedSelector;

	_composeControls->fileChosen(
	) | rpl::start_with_next([=](Selector::FileChosen chosen) {
		sendExistingDocument(chosen.document);
	}, lifetime());

	_composeControls->photoChosen(
	) | rpl::start_with_next([=](Selector::PhotoChosen chosen) {
		sendExistingPhoto(chosen.photo);
	}, lifetime());

	_composeControls->inlineResultChosen(
	) | rpl::start_with_next([=](Selector::InlineChosen chosen) {
		if (chosen.sendPreview) {
			const auto request = chosen.result->openRequest();
			if (const auto photo = request.photo()) {
				sendExistingPhoto(photo);
			} else if (const auto document = request.document()) {
				sendExistingDocument(document);
			}

			addRecentBot(chosen.bot);
			_composeControls->clear();
			_composeControls->hidePanelsAnimated();
			_composeControls->focus();
		} else {
			sendInlineResult(chosen.result, chosen.bot);
		}
	}, lifetime());

	_composeControls->scrollRequests(
	) | rpl::start_with_next([=](Data::MessagePosition pos) {
		showAtPosition(pos);
	}, lifetime());

	_composeControls->scrollKeyEvents(
	) | rpl::start_with_next([=](not_null<QKeyEvent*> e) {
		_scroll->keyPressEvent(e);
	}, lifetime());

	_composeControls->editLastMessageRequests(
	) | rpl::start_with_next([=](not_null<QKeyEvent*> e) {
		if (!_inner->lastMessageEditRequestNotify()) {
			_scroll->keyPressEvent(e);
		}
	}, lifetime());

	_composeControls->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			return CanSendFiles(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			return confirmSendingFiles(data, std::nullopt, data->text());
		}
		Unexpected("action in MimeData hook.");
	});

	_composeControls->lockShowStarts(
	) | rpl::start_with_next([=] {
		updateScrollDownVisibility();
	}, lifetime());

	_composeControls->viewportEvents(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		_scroll->viewportEvent(e);
	}, lifetime());
}

void ScheduledWidget::chooseAttach() {
	if (const auto error = Data::RestrictionError(
			_history->peer,
			ChatRestriction::SendMedia)) {
		Ui::ShowMultilineToast({
			.text = { *error },
		});
		return;
	}

	const auto filter = FileDialog::AllOrImagesFilter();
	FileDialog::GetOpenPaths(this, tr::lng_choose_files(tr::now), filter, crl::guard(this, [=](
			FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		if (!result.remoteContent.isEmpty()) {
			auto read = Images::Read({
				.content = result.remoteContent,
			});
			if (!read.image.isNull() && !read.animated) {
				confirmSendingFiles(
					std::move(read.image),
					std::move(result.remoteContent));
			} else {
				uploadFile(result.remoteContent, SendMediaType::File);
			}
		} else {
			auto list = Storage::PrepareMediaList(
				result.paths,
				st::sendMediaPreviewSize);
			confirmSendingFiles(std::move(list));
		}
	}), nullptr);
}

bool ScheduledWidget::confirmSendingFiles(
		not_null<const QMimeData*> data,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel) {
	const auto hasImage = data->hasImage();

	if (const auto urls = data->urls(); !urls.empty()) {
		auto list = Storage::PrepareMediaList(
			urls,
			st::sendMediaPreviewSize);
		if (list.error != Ui::PreparedList::Error::NonLocalUrl) {
			if (list.error == Ui::PreparedList::Error::None
				|| !hasImage) {
				const auto emptyTextOnCancel = QString();
				list.overrideSendImagesAsPhotos = overrideSendImagesAsPhotos;
				confirmSendingFiles(std::move(list), emptyTextOnCancel);
				return true;
			}
		}
	}

	if (hasImage) {
		auto image = qvariant_cast<QImage>(data->imageData());
		if (!image.isNull()) {
			confirmSendingFiles(
				std::move(image),
				QByteArray(),
				overrideSendImagesAsPhotos,
				insertTextOnCancel);
			return true;
		}
	}
	return false;
}

bool ScheduledWidget::confirmSendingFiles(
		Ui::PreparedList &&list,
		const QString &insertTextOnCancel) {
	if (showSendingFilesError(list)) {
		return false;
	}

	using SendLimit = SendFilesBox::SendLimit;
	auto box = Box<SendFilesBox>(
		controller(),
		std::move(list),
		_composeControls->getTextWithAppliedMarkdown(),
		_history->peer->slowmodeApplied() ? SendLimit::One : SendLimit::Many,
		CanScheduleUntilOnline(_history->peer)
			? Api::SendType::ScheduledToUser
			: Api::SendType::Scheduled,
		SendMenu::Type::Disabled);

	box->setConfirmedCallback(crl::guard(this, [=](
			Ui::PreparedList &&list,
			Ui::SendFilesWay way,
			TextWithTags &&caption,
			Api::SendOptions options,
			bool ctrlShiftEnter) {
		sendingFilesConfirmed(
			std::move(list),
			way,
			std::move(caption),
			options,
			ctrlShiftEnter);
	}));
	box->setCancelledCallback(_composeControls->restoreTextCallback(
		insertTextOnCancel));

	//ActivateWindow(controller());
	const auto shown = controller()->show(std::move(box));
	shown->setCloseByOutsideClick(false);

	return true;
}

void ScheduledWidget::sendingFilesConfirmed(
		Ui::PreparedList &&list,
		Ui::SendFilesWay way,
		TextWithTags &&caption,
		Api::SendOptions options,
		bool ctrlShiftEnter) {
	Expects(list.filesToProcess.empty());

	if (showSendingFilesError(list)) {
		return;
	}
	auto groups = DivideByGroups(std::move(list), way, false);
	const auto type = way.sendImagesAsPhotos()
		? SendMediaType::Photo
		: SendMediaType::File;
	auto action = prepareSendAction(options);
	action.clearDraft = false;
	if ((groups.size() != 1 || !groups.front().sentWithCaption())
		&& !caption.text.isEmpty()) {
		auto message = Api::MessageToSend(action);
		message.textWithTags = base::take(caption);
		session().api().sendMessage(std::move(message));
	}
	for (auto &group : groups) {
		const auto album = (group.type != Ui::AlbumType::None)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		session().api().sendFiles(
			std::move(group.list),
			type,
			base::take(caption),
			album,
			action);
	}
}

bool ScheduledWidget::confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel) {
	if (image.isNull()) {
		return false;
	}

	auto list = Storage::PrepareMediaFromImage(
		std::move(image),
		std::move(content),
		st::sendMediaPreviewSize);
	list.overrideSendImagesAsPhotos = overrideSendImagesAsPhotos;
	return confirmSendingFiles(std::move(list), insertTextOnCancel);
}

void ScheduledWidget::uploadFile(
		const QByteArray &fileContent,
		SendMediaType type) {
	const auto callback = [=](Api::SendOptions options) {
		session().api().sendFile(
			fileContent,
			type,
			prepareSendAction(options));
	};
	controller()->show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		Ui::LayerOption::KeepOther);
}

bool ScheduledWidget::showSendingFilesError(
		const Ui::PreparedList &list) const {
	const auto text = [&] {
		const auto error = Data::RestrictionError(
			_history->peer,
			ChatRestriction::SendMedia);
		if (error) {
			return *error;
		}
		using Error = Ui::PreparedList::Error;
		switch (list.error) {
		case Error::None: return QString();
		case Error::EmptyFile:
		case Error::Directory:
		case Error::NonLocalUrl: return tr::lng_send_image_empty(
			tr::now,
			lt_name,
			list.errorData);
		case Error::TooLargeFile: return tr::lng_send_image_too_large(
			tr::now,
			lt_name,
			list.errorData);
		}
		return tr::lng_forward_send_files_cant(tr::now);
	}();
	if (text.isEmpty()) {
		return false;
	}

	Ui::ShowMultilineToast({
		.text = { text },
	});
	return true;
}

void ScheduledWidget::addRecentBot(not_null<UserData*> bot) {
	auto &bots = cRefRecentInlineBots();
	const auto index = bots.indexOf(bot);
	if (index) {
		if (index > 0) {
			bots.removeAt(index);
		} else if (bots.size() >= RecentInlineBotsLimit) {
			bots.resize(RecentInlineBotsLimit - 1);
		}
		bots.push_front(bot);
		bot->session().local().writeRecentHashtagsAndBots();
	}
}

Api::SendAction ScheduledWidget::prepareSendAction(
		Api::SendOptions options) const {
	auto result = Api::SendAction(_history, options);
	result.options.sendAs = _composeControls->sendAsPeer();
	return result;
}

void ScheduledWidget::send() {
	if (_composeControls->getTextWithAppliedMarkdown().text.isEmpty()) {
		return;
	}
	const auto callback = [=](Api::SendOptions options) { send(options); };
	controller()->show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		Ui::LayerOption::KeepOther);
}

void ScheduledWidget::send(Api::SendOptions options) {
	const auto webPageId = _composeControls->webPageId();

	auto message = ApiWrap::MessageToSend(prepareSendAction(options));
	message.textWithTags = _composeControls->getTextWithAppliedMarkdown();
	message.webPageId = webPageId;

	//const auto error = GetErrorTextForSending(
	//	_peer,
	//	_toForward,
	//	message.textWithTags);
	//if (!error.isEmpty()) {
	//	Ui::ShowMultilineToast({
	//		.text = { error },
	//	});
	//	return;
	//}

	session().api().sendMessage(std::move(message));

	_composeControls->clear();
	//_saveDraftText = true;
	//_saveDraftStart = crl::now();
	//onDraftSave();

	_composeControls->hidePanelsAnimated();

	//if (_previewData && _previewData->pendingTill) previewCancel();
	_composeControls->focus();
}

void ScheduledWidget::sendVoice(
		QByteArray bytes,
		VoiceWaveform waveform,
		int duration) {
	const auto callback = [=](Api::SendOptions options) {
		sendVoice(bytes, waveform, duration, options);
	};
	controller()->show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		Ui::LayerOption::KeepOther);
}

void ScheduledWidget::sendVoice(
		QByteArray bytes,
		VoiceWaveform waveform,
		int duration,
		Api::SendOptions options) {
	session().api().sendVoiceMessage(
		bytes,
		waveform,
		duration,
		prepareSendAction(options));
	_composeControls->clearListenState();
}

void ScheduledWidget::edit(
		not_null<HistoryItem*> item,
		Api::SendOptions options,
		mtpRequestId *const saveEditMsgRequestId) {
	if (*saveEditMsgRequestId) {
		return;
	}
	const auto textWithTags = _composeControls->getTextWithAppliedMarkdown();
	const auto prepareFlags = Ui::ItemTextOptions(
		_history,
		session().user()).flags;
	auto sending = TextWithEntities();
	auto left = TextWithEntities {
		textWithTags.text,
		TextUtilities::ConvertTextTagsToEntities(textWithTags.tags) };
	TextUtilities::PrepareForSending(left, prepareFlags);

	if (!TextUtilities::CutPart(sending, left, MaxMessageSize)) {
		if (item) {
			controller()->show(Box<DeleteMessagesBox>(item, false));
		} else {
			_composeControls->focus();
		}
		return;
	} else if (!left.text.isEmpty()) {
		controller()->show(Box<Ui::InformBox>(
			tr::lng_edit_too_long(tr::now)));
		return;
	}

	lifetime().add([=] {
		if (!*saveEditMsgRequestId) {
			return;
		}
		session().api().request(base::take(*saveEditMsgRequestId)).cancel();
	});

	const auto done = [=](const MTPUpdates &result, mtpRequestId requestId) {
		if (requestId == *saveEditMsgRequestId) {
			*saveEditMsgRequestId = 0;
			_composeControls->cancelEditMessage();
		}
	};

	const auto fail = [=](const MTP::Error &error, mtpRequestId requestId) {
		if (requestId == *saveEditMsgRequestId) {
			*saveEditMsgRequestId = 0;
		}

		const auto &err = error.type();
		if (ranges::contains(Api::kDefaultEditMessagesErrors, err)) {
			controller()->show(Box<Ui::InformBox>(
				tr::lng_edit_error(tr::now)));
		} else if (err == u"MESSAGE_NOT_MODIFIED"_q) {
			_composeControls->cancelEditMessage();
		} else if (err == u"MESSAGE_EMPTY"_q) {
			_composeControls->focus();
		} else {
			controller()->show(Box<Ui::InformBox>(
				tr::lng_edit_error(tr::now)));
		}
		update();
		return true;
	};

	*saveEditMsgRequestId = Api::EditTextMessage(
		item,
		sending,
		options,
		crl::guard(this, done),
		crl::guard(this, fail));

	_composeControls->hidePanelsAnimated();
	_composeControls->focus();
}

void ScheduledWidget::sendExistingDocument(
		not_null<DocumentData*> document) {
	const auto callback = [=](Api::SendOptions options) {
		sendExistingDocument(document, options);
	};
	controller()->show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		Ui::LayerOption::KeepOther);
}

bool ScheduledWidget::sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options) {
	const auto error = Data::RestrictionError(
		_history->peer,
		ChatRestriction::SendStickers);
	if (error) {
		controller()->show(
			Box<Ui::InformBox>(*error),
			Ui::LayerOption::KeepOther);
		return false;
	}

	Api::SendExistingDocument(
		Api::MessageToSend(prepareSendAction(options)),
		document);

	_composeControls->hidePanelsAnimated();
	_composeControls->focus();
	return true;
}

void ScheduledWidget::sendExistingPhoto(not_null<PhotoData*> photo) {
	const auto callback = [=](Api::SendOptions options) {
		sendExistingPhoto(photo, options);
	};
	controller()->show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		Ui::LayerOption::KeepOther);
}

bool ScheduledWidget::sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options) {
	const auto error = Data::RestrictionError(
		_history->peer,
		ChatRestriction::SendMedia);
	if (error) {
		controller()->show(
			Box<Ui::InformBox>(*error),
			Ui::LayerOption::KeepOther);
		return false;
	}

	Api::SendExistingPhoto(
		Api::MessageToSend(prepareSendAction(options)),
		photo);

	_composeControls->hidePanelsAnimated();
	_composeControls->focus();
	return true;
}

void ScheduledWidget::sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot) {
	const auto errorText = result->getErrorOnSend(_history);
	if (!errorText.isEmpty()) {
		controller()->show(Box<Ui::InformBox>(errorText));
		return;
	}
	const auto callback = [=](Api::SendOptions options) {
		sendInlineResult(result, bot, options);
	};
	controller()->show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		Ui::LayerOption::KeepOther);
}

void ScheduledWidget::sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot,
		Api::SendOptions options) {
	auto action = prepareSendAction(options);
	action.generateLocal = true;
	session().api().sendInlineResult(bot, result, action);

	_composeControls->clear();
	//_saveDraftText = true;
	//_saveDraftStart = crl::now();
	//onDraftSave();

	addRecentBot(bot);

	_composeControls->hidePanelsAnimated();
	_composeControls->focus();
}

SendMenu::Type ScheduledWidget::sendMenuType() const {
	return _history->peer->isSelf()
		? SendMenu::Type::Reminder
		: HistoryView::CanScheduleUntilOnline(_history->peer)
		? SendMenu::Type::ScheduledToUser
		: SendMenu::Type::Scheduled;
}

void ScheduledWidget::setupScrollDownButton() {
	_scrollDown->setClickedCallback([=] {
		scrollDownClicked();
	});
	base::install_event_filter(_scrollDown, [=](not_null<QEvent*> event) {
		if (event->type() != QEvent::Wheel) {
			return base::EventFilterResult::Continue;
		}
		return _scroll->viewportEvent(event)
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	});
	updateScrollDownVisibility();
}

void ScheduledWidget::scrollDownClicked() {
	showAtPosition(Data::MaxMessagePosition);
}

void ScheduledWidget::showAtPosition(Data::MessagePosition position) {
	if (showAtPositionNow(position)) {
		if (const auto highlight = base::take(_highlightMessageId)) {
			_inner->highlightMessage(highlight);
		}
	} else {
		_nextAnimatedScrollPosition = position;
		_nextAnimatedScrollDelta = _inner->isBelowPosition(position)
			? -_scroll->height()
			: _inner->isAbovePosition(position)
			? _scroll->height()
			: 0;
		auto memento = HistoryView::ListMemento(position);
		_inner->restoreState(&memento);
	}
}

bool ScheduledWidget::showAtPositionNow(Data::MessagePosition position) {
	if (const auto scrollTop = _inner->scrollTopForPosition(position)) {
		const auto currentScrollTop = _scroll->scrollTop();
		const auto wanted = std::clamp(
			*scrollTop,
			0,
			_scroll->scrollTopMax());
		const auto fullDelta = (wanted - currentScrollTop);
		const auto limit = _scroll->height();
		const auto scrollDelta = std::clamp(fullDelta, -limit, limit);
		_inner->scrollTo(
			wanted,
			position,
			scrollDelta,
			(std::abs(fullDelta) > limit
				? HistoryView::ListWidget::AnimatedScroll::Part
				: HistoryView::ListWidget::AnimatedScroll::Full));
		return true;
	}
	return false;
}

void ScheduledWidget::updateScrollDownVisibility() {
	if (animating()) {
		return;
	}

	const auto scrollDownIsVisible = [&]() -> std::optional<bool> {
		if (_composeControls->isLockPresent()) {
			return false;
		}
		const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
		if (top < _scroll->scrollTopMax()) {
			return true;
		}
		if (_inner->loadedAtBottomKnown()) {
			return !_inner->loadedAtBottom();
		}
		return std::nullopt;
	};
	const auto scrollDownIsShown = scrollDownIsVisible();
	if (!scrollDownIsShown) {
		return;
	}
	if (_scrollDownIsShown != *scrollDownIsShown) {
		_scrollDownIsShown = *scrollDownIsShown;
		_scrollDownShown.start(
			[=] { updateScrollDownPosition(); },
			_scrollDownIsShown ? 0. : 1.,
			_scrollDownIsShown ? 1. : 0.,
			st::historyToDownDuration);
	}
}

void ScheduledWidget::updateScrollDownPosition() {
	// _scrollDown is a child widget of _scroll, not me.
	auto top = anim::interpolate(
		0,
		_scrollDown->height() + st::historyToDownPosition.y(),
		_scrollDownShown.value(_scrollDownIsShown ? 1. : 0.));
	_scrollDown->moveToRight(
		st::historyToDownPosition.x(),
		_scroll->height() - top);
	auto shouldBeHidden = !_scrollDownIsShown && !_scrollDownShown.animating();
	if (shouldBeHidden != _scrollDown->isHidden()) {
		_scrollDown->setVisible(!shouldBeHidden);
	}
}

void ScheduledWidget::scrollDownAnimationFinish() {
	_scrollDownShown.stop();
	updateScrollDownPosition();
}

void ScheduledWidget::updateAdaptiveLayout() {
	_topBarShadow->moveToLeft(
		controller()->adaptive().isOneColumn() ? 0 : st::lineWidth,
		_topBar->height());
}

not_null<History*> ScheduledWidget::history() const {
	return _history;
}

Dialogs::RowDescriptor ScheduledWidget::activeChat() const {
	return {
		_history,
		FullMsgId(_history->channelId(), ShowAtUnreadMsgId)
	};
}

bool ScheduledWidget::preventsClose(Fn<void()> &&continueCallback) const {
	return _composeControls->preventsClose(std::move(continueCallback));
}

QPixmap ScheduledWidget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	_topBar->updateControlsVisibility();
	if (params.withTopBarShadow) _topBarShadow->hide();
	_composeControls->showForGrab();
	auto result = Ui::GrabWidget(this);
	if (params.withTopBarShadow) _topBarShadow->show();
	return result;
}

void ScheduledWidget::doSetInnerFocus() {
	_composeControls->focus();
}

bool ScheduledWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (auto logMemento = dynamic_cast<ScheduledMemento*>(memento.get())) {
		if (logMemento->getHistory() == history()) {
			restoreState(logMemento);
			return true;
		}
	}
	return false;
}

void ScheduledWidget::setInternalState(
		const QRect &geometry,
		not_null<ScheduledMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

bool ScheduledWidget::pushTabbedSelectorToThirdSection(
		not_null<PeerData*> peer,
		const Window::SectionShow &params) {
	return _composeControls->pushTabbedSelectorToThirdSection(peer, params);
}

bool ScheduledWidget::returnTabbedSelector() {
	return _composeControls->returnTabbedSelector();
}

std::shared_ptr<Window::SectionMemento> ScheduledWidget::createMemento() {
	auto result = std::make_shared<ScheduledMemento>(history());
	saveState(result.get());
	return result;
}

void ScheduledWidget::saveState(not_null<ScheduledMemento*> memento) {
	_inner->saveState(memento->list());
}

void ScheduledWidget::restoreState(not_null<ScheduledMemento*> memento) {
	_inner->restoreState(memento->list());
}

void ScheduledWidget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	_composeControls->resizeToWidth(width());
	updateControlsGeometry();
}

void ScheduledWidget::updateControlsGeometry() {
	const auto contentWidth = width();

	const auto newScrollTop = _scroll->isHidden()
		? std::nullopt
		: base::make_optional(_scroll->scrollTop() + topDelta());
	_topBar->resizeToWidth(contentWidth);
	_topBarShadow->resize(contentWidth, st::lineWidth);

	const auto bottom = height();
	const auto controlsHeight = _composeControls->heightCurrent();
	const auto scrollHeight = bottom - _topBar->height() - controlsHeight;
	const auto scrollSize = QSize(contentWidth, scrollHeight);
	if (_scroll->size() != scrollSize) {
		_skipScrollEvent = true;
		_scroll->resize(scrollSize);
		_inner->resizeToWidth(scrollSize.width(), _scroll->height());
		_skipScrollEvent = false;
	}
	if (!_scroll->isHidden()) {
		if (newScrollTop) {
			_scroll->scrollToY(*newScrollTop);
		}
		updateInnerVisibleArea();
	}
	_composeControls->move(0, bottom - controlsHeight);
	_composeControls->setAutocompleteBoundingRect(_scroll->geometry());

	updateScrollDownPosition();
}

void ScheduledWidget::paintEvent(QPaintEvent *e) {
	if (animating()) {
		SectionWidget::paintEvent(e);
		return;
	}
	if (Ui::skipPaintEvent(this, e)) {
		return;
	}
	//if (hasPendingResizedItems()) {
	//	updateListSize();
	//}

	//auto ms = crl::now();
	//_historyDownShown.step(ms);

	const auto clip = e->rect();
	SectionWidget::PaintBackground(controller(), _theme.get(), this, clip);
}

void ScheduledWidget::onScroll() {
	if (_skipScrollEvent) {
		return;
	}
	updateInnerVisibleArea();
}

void ScheduledWidget::updateInnerVisibleArea() {
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	updateScrollDownVisibility();
}

void ScheduledWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_topBar->setAnimatingMode(true);
	if (params.withTopBarShadow) {
		_topBarShadow->show();
	}
	_composeControls->showStarted();
}

void ScheduledWidget::showFinishedHook() {
	_topBar->setAnimatingMode(false);
	_composeControls->showFinished();

	// We should setup the drag area only after
	// the section animation is finished,
	// because after that the method showChildren() is called.
	setupDragArea();
}

bool ScheduledWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect ScheduledWidget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

Context ScheduledWidget::listContext() {
	return Context::History;
}

void ScheduledWidget::listScrollTo(int top) {
	if (_scroll->scrollTop() != top) {
		_scroll->scrollToY(top);
	} else {
		updateInnerVisibleArea();
	}
}

void ScheduledWidget::listCancelRequest() {
	if (_inner && !_inner->getSelectedItems().empty()) {
		clearSelected();
		return;
	} else if (_composeControls->handleCancelRequest()) {
		return;
	}
	controller()->showBackFromStack();
}

void ScheduledWidget::listDeleteRequest() {
	confirmDeleteSelected();
}

rpl::producer<Data::MessagesSlice> ScheduledWidget::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto data = &controller()->session().data();
	return rpl::single(
		rpl::empty_value()
	) | rpl::then(
		data->scheduledMessages().updates(_history)
	) | rpl::map([=] {
		return data->scheduledMessages().list(_history);
	}) | rpl::after_next([=](const Data::MessagesSlice &slice) {
		highlightSingleNewMessage(slice);
	});
}

void ScheduledWidget::highlightSingleNewMessage(
		const Data::MessagesSlice &slice) {
	const auto guard = gsl::finally([&] { _lastSlice = slice; });
	if (_lastSlice.ids.empty()
		|| (slice.ids.size() != _lastSlice.ids.size() + 1)) {
		return;
	}
	auto firstDifferent = 0;
	while (firstDifferent != _lastSlice.ids.size()) {
		if (slice.ids[firstDifferent] != _lastSlice.ids[firstDifferent]) {
			break;
		}
		++firstDifferent;
	}
	auto lastDifferent = slice.ids.size() - 1;
	while (lastDifferent != firstDifferent) {
		if (slice.ids[lastDifferent] != _lastSlice.ids[lastDifferent - 1]) {
			break;
		}
		--lastDifferent;
	}
	if (firstDifferent != lastDifferent) {
		return;
	}
	const auto newId = slice.ids[firstDifferent];
	if (const auto item = session().data().message(newId)) {
	//	_highlightMessageId = newId;
		showAtPosition(item->position());
	}
}

bool ScheduledWidget::listAllowsMultiSelect() {
	return true;
}

bool ScheduledWidget::listIsItemGoodForSelection(
		not_null<HistoryItem*> item) {
	return !item->isSending() && !item->hasFailed();
}

bool ScheduledWidget::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return first->position() < second->position();
}

void ScheduledWidget::listSelectionChanged(SelectedItems &&items) {
	HistoryView::TopBarWidget::SelectedState state;
	state.count = items.size();
	for (const auto &item : items) {
		if (item.canDelete) {
			++state.canDeleteCount;
		}
		if (item.canSendNow) {
			++state.canSendNowCount;
		}
	}
	_topBar->showSelected(state);
}

void ScheduledWidget::listVisibleItemsChanged(HistoryItemsList &&items) {
}

MessagesBarData ScheduledWidget::listMessagesBar(
		const std::vector<not_null<Element*>> &elements) {
	return MessagesBarData();
}

void ScheduledWidget::listContentRefreshed() {
}

ClickHandlerPtr ScheduledWidget::listDateLink(not_null<Element*> view) {
	return nullptr;
}

bool ScheduledWidget::listElementHideReply(not_null<const Element*> view) {
	return false;
}

bool ScheduledWidget::listElementShownUnread(not_null<const Element*> view) {
	return true;
}

bool ScheduledWidget::listIsGoodForAroundPosition(
		not_null<const Element*> view) {
	return true;
}

Window::SectionActionResult ScheduledWidget::sendBotCommand(
		Bot::SendCommandRequest request) {
	if (request.peer != _history->peer) {
		return Window::SectionActionResult::Ignore;
	}
	listSendBotCommand(request.command, request.context);
	return Window::SectionActionResult::Handle;
}

void ScheduledWidget::listSendBotCommand(
		const QString &command,
		const FullMsgId &context) {
	const auto callback = [=](Api::SendOptions options) {
		const auto text = Bot::WrapCommandInChat(
			_history->peer,
			command,
			context);
		auto message = ApiWrap::MessageToSend(prepareSendAction(options));
		message.textWithTags = { text };
		session().api().sendMessage(std::move(message));
	};
	controller()->show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		Ui::LayerOption::KeepOther);
}

void ScheduledWidget::listHandleViaClick(not_null<UserData*> bot) {
	_composeControls->setText({ '@' + bot->username + ' ' });
}

not_null<Ui::ChatTheme*> ScheduledWidget::listChatTheme() {
	return _theme.get();
}

CopyRestrictionType ScheduledWidget::listCopyRestrictionType(
		HistoryItem *item) {
	return CopyRestrictionType::None;
}

CopyRestrictionType ScheduledWidget::listSelectRestrictionType() {
	return CopyRestrictionType::None;
}

void ScheduledWidget::confirmSendNowSelected() {
	ConfirmSendNowSelectedItems(_inner);
}

void ScheduledWidget::confirmDeleteSelected() {
	ConfirmDeleteSelectedItems(_inner);
}

void ScheduledWidget::clearSelected() {
	_inner->cancelSelection();
}

void ScheduledWidget::setupDragArea() {
	const auto areas = DragArea::SetupDragAreaToContainer(
		this,
		[=](auto d) { return _history && !_composeControls->isRecording(); },
		nullptr,
		[=] { updateControlsGeometry(); });

	const auto droppedCallback = [=](bool overrideSendImagesAsPhotos) {
		return [=](const QMimeData *data) {
			confirmSendingFiles(data, overrideSendImagesAsPhotos);
			Window::ActivateWindow(controller());
		};
	};
	areas.document->setDroppedCallback(droppedCallback(false));
	areas.photo->setDroppedCallback(droppedCallback(true));
}

} // namespace HistoryView
