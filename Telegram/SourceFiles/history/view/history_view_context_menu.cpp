/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_context_menu.h"

#include "api/api_attached_stickers.h"
#include "api/api_editing.h"
#include "api/api_polls.h"
#include "api/api_toggling_media.h" // Api::ToggleFavedSticker
#include "base/unixtime.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_message.h"
#include "history/history_item_text.h"
#include "history/view/history_view_schedule_box.h"
#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_web_page.h"
#include "ui/widgets/popup_menu.h"
#include "ui/image/image.h"
#include "ui/toast/toast.h"
#include "ui/controls/delete_message_context_action.h"
#include "ui/boxes/report_box.h"
#include "ui/ui_utility.h"
#include "chat_helpers/send_context_menu.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/delete_messages_box.h"
#include "boxes/sticker_set_box.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_groups.h"
#include "data/data_channel.h"
#include "data/data_file_click_handler.h"
#include "data/data_file_origin.h"
#include "data/data_scheduled_messages.h"
#include "core/file_utilities.h"
#include "base/platform/base_platform_info.h"
#include "window/window_peer_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "core/application.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "apiwrap.h"
#include "facades.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace HistoryView {
namespace {

constexpr auto kRescheduleLimit = 20;

bool HasEditMessageAction(
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto item = request.item;
	const auto context = list->elementContext();
	if (!item
		|| item->isSending()
		|| item->hasFailed()
		|| item->isEditingMedia()
		|| !request.selectedItems.empty()
		|| (context != Context::History && context != Context::Replies)) {
		return false;
	}
	const auto peer = item->history()->peer;
	if (const auto channel = peer->asChannel()) {
		if (!channel->isMegagroup() && !channel->canEditMessages()) {
			return false;
		}
	}
	return true;
}

void SavePhotoToFile(not_null<PhotoData*> photo) {
	const auto media = photo->activeMediaView();
	if (photo->isNull() || !media || !media->loaded()) {
		return;
	}

	const auto image = media->image(Data::PhotoSize::Large)->original();
	FileDialog::GetWritePath(
		Core::App().getFileDialogParent(),
		tr::lng_save_photo(tr::now),
		qsl("JPEG Image (*.jpg);;") + FileDialog::AllFilesFilter(),
		filedialogDefaultName(qsl("photo"), qsl(".jpg")),
		crl::guard(&photo->session(), [=](const QString &result) {
			if (!result.isEmpty()) {
				image.save(result, "JPG");
			}
		}));
}

void CopyImage(not_null<PhotoData*> photo) {
	const auto media = photo->activeMediaView();
	if (photo->isNull() || !media || !media->loaded()) {
		return;
	}

	const auto image = media->image(Data::PhotoSize::Large)->original();
	QGuiApplication::clipboard()->setImage(image);
}

void ShowStickerPackInfo(
		not_null<DocumentData*> document,
		not_null<ListWidget*> list) {
	StickerSetBox::Show(list->controller(), document);
}

void ToggleFavedSticker(
		not_null<DocumentData*> document,
		FullMsgId contextId) {
	Api::ToggleFavedSticker(document, contextId);
}

void AddPhotoActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<PhotoData*> photo,
		HistoryItem *item,
		not_null<ListWidget*> list) {
	const auto contextId = item ? item->fullId() : FullMsgId();
	if (!list->hasCopyRestriction(item)) {
		menu->addAction(
			tr::lng_context_save_image(tr::now),
			App::LambdaDelayed(
				st::defaultDropdownMenu.menu.ripple.hideDuration,
				&photo->session(),
				[=] { SavePhotoToFile(photo); }));
		menu->addAction(tr::lng_context_copy_image(tr::now), [=] {
			const auto item = photo->owner().message(contextId);
			if (!list->showCopyRestriction(item)) {
				CopyImage(photo);
			}
		});
	}
	if (photo->hasAttachedStickers()) {
		const auto controller = list->controller();
		auto callback = [=] {
			auto &attached = photo->session().api().attachedStickers();
			attached.requestAttachedStickerSets(controller, photo);
		};
		menu->addAction(
			tr::lng_context_attached_stickers(tr::now),
			std::move(callback));
	}
}

void SaveGif(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId) {
	if (const auto item = controller->session().data().message(itemId)) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				Api::ToggleSavedGif(document, item->fullId(), true);
			}
		}
	}
}

void OpenGif(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId) {
	if (const auto item = controller->session().data().message(itemId)) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				controller->openDocument(document, itemId, true);
			}
		}
	}
}

void ShowInFolder(not_null<DocumentData*> document) {
	const auto filepath = document->filepath(true);
	if (!filepath.isEmpty()) {
		File::ShowInFolder(filepath);
	}
}

void AddSaveDocumentAction(
		not_null<Ui::PopupMenu*> menu,
		HistoryItem *item,
		not_null<DocumentData*> document,
		not_null<ListWidget*> list) {
	if (list->hasCopyRestriction(item)) {
		return;
	}
	const auto origin = Data::FileOrigin(
		item ? item->fullId() : FullMsgId());
	const auto save = [=] {
		DocumentSaveClickHandler::Save(
			origin,
			document,
			DocumentSaveClickHandler::Mode::ToNewFile);
	};

	menu->addAction(
		(document->isVideoFile()
			? tr::lng_context_save_video(tr::now)
			: (document->isVoiceMessage()
				? tr::lng_context_save_audio(tr::now)
				: (document->isAudioFile()
					? tr::lng_context_save_audio_file(tr::now)
					: (document->sticker()
						? tr::lng_context_save_image(tr::now)
						: tr::lng_context_save_file(tr::now))))),
		App::LambdaDelayed(
			st::defaultDropdownMenu.menu.ripple.hideDuration,
			&document->session(),
			save));
}

void AddDocumentActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<DocumentData*> document,
		HistoryItem *item,
		not_null<ListWidget*> list) {
	if (document->loading()) {
		menu->addAction(tr::lng_context_cancel_download(tr::now), [=] {
			document->cancel();
		});
		return;
	}
	const auto contextId = item ? item->fullId() : FullMsgId();
	const auto session = &document->session();
	if (item) {
		const auto notAutoplayedGif = [&] {
			return document->isGifv()
				&& !Data::AutoDownload::ShouldAutoPlay(
					document->session().settings().autoDownload(),
					item->history()->peer,
					document);
		}();
		if (notAutoplayedGif) {
			menu->addAction(tr::lng_context_open_gif(tr::now), [=] {
				OpenGif(list->controller(), contextId);
			});
		}
		if (document->isGifv() && !list->hasCopyRestriction(item)) {
			menu->addAction(tr::lng_context_save_gif(tr::now), [=] {
				SaveGif(list->controller(), contextId);
			});
		}
	}
	if (document->sticker() && document->sticker()->set) {
		menu->addAction(
			(document->isStickerSetInstalled()
				? tr::lng_context_pack_info(tr::now)
				: tr::lng_context_pack_add(tr::now)),
			[=] { ShowStickerPackInfo(document, list); });
		menu->addAction(
			(document->owner().stickers().isFaved(document)
				? tr::lng_faved_stickers_remove(tr::now)
				: tr::lng_faved_stickers_add(tr::now)),
			[=] { ToggleFavedSticker(document, contextId); });
	}
	if (!document->filepath(true).isEmpty()) {
		menu->addAction(
			(Platform::IsMac()
				? tr::lng_context_show_in_finder(tr::now)
				: tr::lng_context_show_in_folder(tr::now)),
			[=] { ShowInFolder(document); });
	}
	if (document->hasAttachedStickers()) {
		const auto controller = list->controller();
		auto callback = [=] {
			auto &attached = session->api().attachedStickers();
			attached.requestAttachedStickerSets(controller, document);
		};
		menu->addAction(
			tr::lng_context_attached_stickers(tr::now),
			std::move(callback));
	}
	AddSaveDocumentAction(menu, item, document, list);
}

void AddPostLinkAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request) {
	const auto item = request.item;
	if (!item
		|| !item->hasDirectLink()
		|| request.pointState == PointState::Outside) {
		return;
	} else if (request.link
		&& !request.link->copyToClipboardContextItemText().isEmpty()) {
		return;
	}
	const auto session = &item->history()->session();
	const auto itemId = item->fullId();
	const auto context = request.view
		? request.view->context()
		: Context::History;
	menu->addAction(
		(item->history()->peer->isMegagroup()
			? tr::lng_context_copy_message_link
			: tr::lng_context_copy_post_link)(tr::now),
		[=] { CopyPostLink(session, itemId, context); });
}

MessageIdsList ExtractIdsList(const SelectedItems &items) {
	return ranges::views::all(
		items
	) | ranges::views::transform(
		&SelectedItem::msgId
	) | ranges::to_vector;
}

bool AddForwardSelectedAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	if (!request.overSelection || request.selectedItems.empty()) {
		return false;
	}
	if (!ranges::all_of(request.selectedItems, &SelectedItem::canForward)) {
		return false;
	}

	menu->addAction(tr::lng_context_forward_selected(tr::now), [=] {
		const auto weak = Ui::MakeWeak(list);
		const auto callback = [=] {
			if (const auto strong = weak.data()) {
				strong->cancelSelection();
			}
		};
		Window::ShowForwardMessagesBox(
			request.navigation,
			ExtractIdsList(request.selectedItems),
			callback);
	});
	return true;
}

bool AddForwardMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto item = request.item;
	if (!request.selectedItems.empty()) {
		return false;
	} else if (!item || !item->allowsForward()) {
		return false;
	}
	const auto owner = &item->history()->owner();
	const auto asGroup = (request.pointState != PointState::GroupPart);
	if (asGroup) {
		if (const auto group = owner->groups().find(item)) {
			if (!ranges::all_of(group->items, &HistoryItem::allowsForward)) {
				return false;
			}
		}
	}
	const auto itemId = item->fullId();
	menu->addAction(tr::lng_context_forward_msg(tr::now), [=] {
		if (const auto item = owner->message(itemId)) {
			Window::ShowForwardMessagesBox(
				request.navigation,
				(asGroup
					? owner->itemOrItsGroup(item)
					: MessageIdsList{ 1, itemId }));
		}
	});
	return true;
}

void AddForwardAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	AddForwardSelectedAction(menu, request, list);
	AddForwardMessageAction(menu, request, list);
}

bool AddSendNowSelectedAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	if (!request.overSelection || request.selectedItems.empty()) {
		return false;
	}
	if (!ranges::all_of(request.selectedItems, &SelectedItem::canSendNow)) {
		return false;
	}

	const auto session = &request.navigation->session();
	auto histories = ranges::views::all(
		request.selectedItems
	) | ranges::views::transform([&](const SelectedItem &item) {
		return session->data().message(item.msgId);
	}) | ranges::views::filter([](HistoryItem *item) {
		return item != nullptr;
	}) | ranges::views::transform(
		&HistoryItem::history
	);
	if (histories.begin() == histories.end()) {
		return false;
	}
	const auto history = *histories.begin();

	menu->addAction(tr::lng_context_send_now_selected(tr::now), [=] {
		const auto weak = Ui::MakeWeak(list);
		const auto callback = [=] {
			request.navigation->showBackFromStack();
		};
		Window::ShowSendNowMessagesBox(
			request.navigation,
			history,
			ExtractIdsList(request.selectedItems),
			callback);
	});
	return true;
}

bool AddSendNowMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto item = request.item;
	if (!request.selectedItems.empty()) {
		return false;
	} else if (!item || !item->allowsSendNow()) {
		return false;
	}
	const auto owner = &item->history()->owner();
	const auto asGroup = (request.pointState != PointState::GroupPart);
	if (asGroup) {
		if (const auto group = owner->groups().find(item)) {
			if (!ranges::all_of(group->items, &HistoryItem::allowsSendNow)) {
				return false;
			}
		}
	}
	const auto itemId = item->fullId();
	menu->addAction(tr::lng_context_send_now_msg(tr::now), [=] {
		if (const auto item = owner->message(itemId)) {
			Window::ShowSendNowMessagesBox(
				request.navigation,
				item->history(),
				(asGroup
					? owner->itemOrItsGroup(item)
					: MessageIdsList{ 1, itemId }));
		}
	});
	return true;
}

bool AddRescheduleAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto owner = &request.navigation->session().data();

	const auto goodSingle = HasEditMessageAction(request, list)
		&& request.item->isScheduled();
	const auto goodMany = [&] {
		if (goodSingle) {
			return false;
		}
		const auto &items = request.selectedItems;
		if (!request.overSelection || items.empty()) {
			return false;
		}
		if (items.size() > kRescheduleLimit) {
			return false;
		}
		return ranges::all_of(items, &SelectedItem::canSendNow);
	}();
	if (!goodSingle && !goodMany) {
		return false;
	}
	auto ids = goodSingle
		? MessageIdsList{ request.item->fullId() }
		: ExtractIdsList(request.selectedItems);
	ranges::sort(ids, [&](const FullMsgId &a, const FullMsgId &b) {
		const auto itemA = owner->message(a);
		const auto itemB = owner->message(b);
		return (itemA && itemB) && (itemA->position() < itemB->position());
	});

	auto text = ((ids.size() == 1)
		? tr::lng_context_reschedule
		: tr::lng_context_reschedule_selected)(tr::now);

	menu->addAction(std::move(text), [=] {
		const auto firstItem = owner->message(ids.front());
		if (!firstItem) {
			return;
		}
		const auto callback = [=](Api::SendOptions options) {
			list->cancelSelection();
			for (const auto &id : ids) {
				const auto item = owner->message(id);
				if (!item || !item->isScheduled()) {
					continue;
				}
				if (!item->media() || !item->media()->webpage()) {
					options.removeWebPageId = true;
				}
				Api::RescheduleMessage(item, options);
				// Increase the scheduled date by 1s to keep the order.
				options.scheduled += 1;
			}
		};

		const auto peer = firstItem->history()->peer;
		const auto sendMenuType = !peer
			? SendMenu::Type::Disabled
			: peer->isSelf()
			? SendMenu::Type::Reminder
			: HistoryView::CanScheduleUntilOnline(peer)
			? SendMenu::Type::ScheduledToUser
			: SendMenu::Type::Scheduled;

		using S = Data::ScheduledMessages;
		const auto itemDate = firstItem->date();
		const auto date = (itemDate == S::kScheduledUntilOnlineTimestamp)
			? HistoryView::DefaultScheduleTime()
			: itemDate + 600;

		const auto box = request.navigation->parentController()->show(
			HistoryView::PrepareScheduleBox(
				&request.navigation->session(),
				sendMenuType,
				callback,
				date),
			Ui::LayerOption::KeepOther);

		owner->itemRemoved(
		) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
			if (ranges::contains(ids, item->fullId())) {
				box->closeBox();
			}
		}, box->lifetime());
	});
	return true;
}

bool AddReplyToMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto context = list->elementContext();
	const auto item = request.item;
	if (!item
		|| !item->isRegular()
		|| !item->history()->peer->canWrite()
		|| (context != Context::History && context != Context::Replies)) {
		return false;
	}
	const auto owner = &item->history()->owner();
	const auto itemId = item->fullId();
	menu->addAction(tr::lng_context_reply_msg(tr::now), [=] {
		const auto item = owner->message(itemId);
		if (!item) {
			return;
		}
		list->replyToMessageRequestNotify(item->fullId());
	});
	return true;
}

bool AddViewRepliesAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto context = list->elementContext();
	const auto item = request.item;
	if (!item
		|| !item->isRegular()
		|| (context != Context::History && context != Context::Pinned)) {
		return false;
	}
	const auto repliesCount = item->repliesCount();
	const auto withReplies = (repliesCount > 0);
	if (!withReplies || !item->history()->peer->isMegagroup()) {
		return false;
	}
	const auto rootId = repliesCount ? item->id : item->replyToTop();
	const auto phrase = (repliesCount > 0)
		? tr::lng_replies_view(
			tr::now,
			lt_count,
			repliesCount)
		: tr::lng_replies_view_thread(tr::now);
	const auto controller = list->controller();
	const auto history = item->history();
	menu->addAction(phrase, crl::guard(controller, [=] {
		controller->showRepliesForMessage(history, rootId);
	}));
	return true;
}

bool AddEditMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	if (!HasEditMessageAction(request, list)) {
		return false;
	}
	const auto item = request.item;
	if (!item->allowsEdit(base::unixtime::now())) {
		return false;
	}
	const auto owner = &item->history()->owner();
	const auto itemId = item->fullId();
	menu->addAction(tr::lng_context_edit_msg(tr::now), [=] {
		const auto item = owner->message(itemId);
		if (!item) {
			return;
		}
		list->editMessageRequestNotify(item->fullId());
	});
	return true;
}

bool AddPinMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto context = list->elementContext();
	const auto item = request.item;
	if (!item
		|| !item->isRegular()
		|| (context != Context::History && context != Context::Pinned)) {
		return false;
	}
	const auto group = item->history()->owner().groups().find(item);
	const auto pinItem = ((item->canPin() && item->isPinned()) || !group)
		? item
		: group->items.front().get();
	if (!pinItem->canPin()) {
		return false;
	}
	const auto pinItemId = pinItem->fullId();
	const auto isPinned = pinItem->isPinned();
	const auto controller = list->controller();
	menu->addAction(isPinned ? tr::lng_context_unpin_msg(tr::now) : tr::lng_context_pin_msg(tr::now), crl::guard(controller, [=] {
		Window::ToggleMessagePinned(controller, pinItemId, !isPinned);
	}));
	return true;
}

bool AddGoToMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto context = list->elementContext();
	const auto view = request.view;
	if (!view
		|| !view->data()->isRegular()
		|| context != Context::Pinned
		|| !view->hasOutLayout()) {
		return false;
	}
	const auto itemId = view->data()->fullId();
	const auto controller = list->controller();
	menu->addAction(tr::lng_context_to_msg(tr::now), crl::guard(controller, [=] {
		const auto item = controller->session().data().message(itemId);
		if (item) {
			goToMessageClickHandler(item)->onClick(ClickContext{});
		}
	}));
	return true;
}

void AddSendNowAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	AddSendNowSelectedAction(menu, request, list);
	AddSendNowMessageAction(menu, request, list);
}

bool AddDeleteSelectedAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	if (!request.overSelection || request.selectedItems.empty()) {
		return false;
	}
	if (!ranges::all_of(request.selectedItems, &SelectedItem::canDelete)) {
		return false;
	}

	menu->addAction(tr::lng_context_delete_selected(tr::now), [=] {
		auto items = ExtractIdsList(request.selectedItems);
		auto box = Box<DeleteMessagesBox>(
			&request.navigation->session(),
			std::move(items));
		box->setDeleteConfirmedCallback(crl::guard(list, [=] {
			list->cancelSelection();
		}));
		request.navigation->parentController()->show(std::move(box));
	});
	return true;
}

bool AddDeleteMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto item = request.item;
	if (!request.selectedItems.empty()) {
		return false;
	} else if (!item || !item->canDelete()) {
		return false;
	}
	const auto owner = &item->history()->owner();
	const auto asGroup = (request.pointState != PointState::GroupPart);
	if (asGroup) {
		if (const auto group = owner->groups().find(item)) {
			if (ranges::any_of(group->items, [](auto item) {
				return item->isLocal() || !item->canDelete();
			})) {
				return false;
			}
		}
	}
	const auto controller = list->controller();
	const auto itemId = item->fullId();
	const auto callback = crl::guard(controller, [=] {
		if (const auto item = owner->message(itemId)) {
			if (asGroup) {
				if (const auto group = owner->groups().find(item)) {
					controller->show(Box<DeleteMessagesBox>(
						&owner->session(),
						owner->itemsToIds(group->items)));
					return;
				}
			}
			if (item->isUploading()) {
				controller->cancelUploadLayer(item);
				return;
			}
			const auto suggestModerateActions = true;
			controller->show(
				Box<DeleteMessagesBox>(item, suggestModerateActions));
		}
	});
	if (item->isUploading()) {
		menu->addAction(
			tr::lng_context_cancel_upload(tr::now),
			callback);
		return true;
	}
	menu->addAction(Ui::DeleteMessageContextAction(
		menu->menu(),
		callback,
		item->ttlDestroyAt(),
		[=] { delete menu; }));
	return true;
}

void AddDeleteAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	if (!AddDeleteSelectedAction(menu, request, list)) {
		AddDeleteMessageAction(menu, request, list);
	}
}

void AddReportAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto item = request.item;
	if (!request.selectedItems.empty()) {
		return;
	} else if (!item || !item->suggestReport()) {
		return;
	}
	const auto owner = &item->history()->owner();
	const auto controller = list->controller();
	const auto itemId = item->fullId();
	const auto callback = crl::guard(controller, [=] {
		if (const auto item = owner->message(itemId)) {
			const auto group = owner->groups().find(item);
			ShowReportItemsBox(
				item->history()->peer,
				(group
					? owner->itemsToIds(group->items)
					: MessageIdsList{ 1, itemId }));
		}
	});
	menu->addAction(tr::lng_context_report_msg(tr::now), callback);
}

bool AddClearSelectionAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	if (!request.overSelection || request.selectedItems.empty()) {
		return false;
	}
	menu->addAction(tr::lng_context_clear_selection(tr::now), [=] {
		list->cancelSelection();
	});
	return true;
}

bool AddSelectMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto item = request.item;
	if (request.overSelection && !request.selectedItems.empty()) {
		return false;
	} else if (!item
		|| item->isLocal()
		|| item->isService()
		|| list->hasSelectRestriction()) {
		return false;
	}
	const auto owner = &item->history()->owner();
	const auto itemId = item->fullId();
	const auto asGroup = (request.pointState != PointState::GroupPart);
	menu->addAction(tr::lng_context_select_msg(tr::now), [=] {
		if (const auto item = owner->message(itemId)) {
			if (asGroup) {
				list->selectItemAsGroup(item);
			} else {
				list->selectItem(item);
			}
		}
	});
	return true;
}

void AddSelectionAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	if (!AddClearSelectionAction(menu, request, list)) {
		AddSelectMessageAction(menu, request, list);
	}
}

void AddTopMessageActions(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	AddReplyToMessageAction(menu, request, list);
	AddGoToMessageAction(menu, request, list);
	AddViewRepliesAction(menu, request, list);
	AddEditMessageAction(menu, request, list);
	AddPinMessageAction(menu, request, list);
}

void AddMessageActions(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	AddPostLinkAction(menu, request);
	AddForwardAction(menu, request, list);
	AddSendNowAction(menu, request, list);
	AddDeleteAction(menu, request, list);
	AddReportAction(menu, request, list);
	AddSelectionAction(menu, request, list);
	AddRescheduleAction(menu, request, list);
}

void AddCopyLinkAction(
		not_null<Ui::PopupMenu*> menu,
		const ClickHandlerPtr &link) {
	if (!link) {
		return;
	}
	const auto action = link->copyToClipboardContextItemText();
	if (action.isEmpty()) {
		return;
	}
	const auto text = link->copyToClipboardText();
	menu->addAction(
		action,
		[=] { QGuiApplication::clipboard()->setText(text); });
}

} // namespace

ContextMenuRequest::ContextMenuRequest(
	not_null<Window::SessionNavigation*> navigation)
: navigation(navigation) {
}

base::unique_qptr<Ui::PopupMenu> FillContextMenu(
		not_null<ListWidget*> list,
		const ContextMenuRequest &request) {
	auto result = base::make_unique_q<Ui::PopupMenu>(list);

	const auto link = request.link;
	const auto view = request.view;
	const auto item = request.item;
	const auto itemId = item ? item->fullId() : FullMsgId();
	const auto rawLink = link.get();
	const auto linkPhoto = dynamic_cast<PhotoClickHandler*>(rawLink);
	const auto linkDocument = dynamic_cast<DocumentClickHandler*>(rawLink);
	const auto photo = linkPhoto ? linkPhoto->photo().get() : nullptr;
	const auto document = linkDocument
		? linkDocument->document().get()
		: nullptr;
	const auto poll = item
		? (item->media() ? item->media()->poll() : nullptr)
		: nullptr;
	const auto hasSelection = !request.selectedItems.empty()
		|| !request.selectedText.empty();

	if (request.overSelection && !list->hasCopyRestrictionForSelected()) {
		const auto text = request.selectedItems.empty()
			? tr::lng_context_copy_selected(tr::now)
			: tr::lng_context_copy_selected_items(tr::now);
		result->addAction(text, [=] {
			if (!list->showCopyRestrictionForSelected()) {
				TextUtilities::SetClipboardText(list->getSelectedText());
			}
		});
	}

	AddTopMessageActions(result, request, list);
	if (linkPhoto) {
		AddPhotoActions(result, photo, item, list);
	} else if (linkDocument) {
		AddDocumentActions(result, document, item, list);
	} else if (poll) {
		AddPollActions(result, poll, item, list->elementContext());
	} else if (!request.overSelection && view && !hasSelection) {
		const auto owner = &view->data()->history()->owner();
		const auto media = view->media();
		const auto mediaHasTextForCopy = media && media->hasTextForCopy();
		if (const auto document = media ? media->getDocument() : nullptr) {
			AddDocumentActions(result, document, view->data(), list);
		}
		if (!link
			&& (view->hasVisibleText() || mediaHasTextForCopy)
			&& !list->hasCopyRestriction(view->data())) {
			const auto asGroup = (request.pointState != PointState::GroupPart);
			result->addAction(tr::lng_context_copy_text(tr::now), [=] {
				if (const auto item = owner->message(itemId)) {
					if (!list->showCopyRestriction(item)) {
						if (asGroup) {
							if (const auto group = owner->groups().find(item)) {
								TextUtilities::SetClipboardText(HistoryGroupText(group));
								return;
							}
						}
						TextUtilities::SetClipboardText(HistoryItemText(item));
					}
				}
			});
		}
	}

	AddCopyLinkAction(result, link);
	AddMessageActions(result, request, list);
	return result;
}

void CopyPostLink(
		not_null<Main::Session*> session,
		FullMsgId itemId,
		Context context) {
	const auto item = session->data().message(itemId);
	if (!item || !item->hasDirectLink()) {
		return;
	}
	const auto inRepliesContext = (context == Context::Replies);
	QGuiApplication::clipboard()->setText(
		item->history()->session().api().exportDirectMessageLink(
			item,
			inRepliesContext));

	const auto isPublicLink = [&] {
		const auto channel = item->history()->peer->asChannel();
		Assert(channel != nullptr);
		if (const auto rootId = item->replyToTop()) {
			const auto root = item->history()->owner().message(
				peerToChannel(channel->id),
				rootId);
			const auto sender = root
				? root->discussionPostOriginalSender()
				: nullptr;
			if (sender && sender->hasUsername()) {
				return true;
			}
		}
		return channel->hasUsername();
	}();

	Ui::Toast::Show(isPublicLink
		? tr::lng_channel_public_link_copied(tr::now)
		: tr::lng_context_about_private_link(tr::now));
}

void StopPoll(not_null<Main::Session*> session, FullMsgId itemId) {
	const auto stop = [=] {
		Ui::hideLayer();
		if (const auto item = session->data().message(itemId)) {
			session->api().polls().close(item);
		}
	};
	Ui::show(Box<Ui::ConfirmBox>(
		tr::lng_polls_stop_warning(tr::now),
		tr::lng_polls_stop_sure(tr::now),
		tr::lng_cancel(tr::now),
		stop));
}

void AddPollActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<PollData*> poll,
		not_null<HistoryItem*> item,
		Context context) {
	if ((context != Context::History)
		&& (context != Context::Replies)
		&& (context != Context::Pinned)) {
		return;
	}
	if (poll->closed()) {
		return;
	}
	const auto itemId = item->fullId();
	if (poll->voted() && !poll->quiz()) {
		menu->addAction(tr::lng_polls_retract(tr::now), [=] {
			poll->session().api().polls().sendVotes(itemId, {});
		});
	}
	if (item->canStopPoll()) {
		menu->addAction(tr::lng_polls_stop(tr::now), [=] {
			StopPoll(&poll->session(), itemId);
		});
	}
}

void ShowReportItemsBox(not_null<PeerData*> peer, MessageIdsList ids) {
	const auto chosen = [=](Ui::ReportReason reason) {
		Ui::show(Box(Ui::ReportDetailsBox, [=](const QString &text) {
			SendReport(peer, reason, text, ids);
			Ui::hideLayer();
		}));
	};
	Ui::show(Box(
		Ui::ReportReasonBox,
		Ui::ReportSource::Message,
		chosen));
}

void ShowReportPeerBox(
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer) {
	struct State {
		QPointer<Ui::GenericBox> reasonBox;
		QPointer<Ui::GenericBox> detailsBox;
		MessageIdsList ids;
	};
	const auto state = std::make_shared<State>();
	const auto chosen = [=](Ui::ReportReason reason) {
		const auto send = [=](const QString &text) {
			window->clearChooseReportMessages();
			SendReport(peer, reason, text, std::move(state->ids));
			if (const auto strong = state->reasonBox.data()) {
				strong->closeBox();
			}
			if (const auto strong = state->detailsBox.data()) {
				strong->closeBox();
			}
		};
		if (reason == Ui::ReportReason::Fake
			|| reason == Ui::ReportReason::Other) {
			state->ids = {};
			state->detailsBox = window->window().show(
				Box(Ui::ReportDetailsBox, send));
			return;
		}
		window->showChooseReportMessages(peer, reason, [=](
				MessageIdsList ids) {
			state->ids = std::move(ids);
			state->detailsBox = window->window().show(
				Box(Ui::ReportDetailsBox, send));
		});
	};
	state->reasonBox = window->window().show(Box(
		Ui::ReportReasonBox,
		(peer->isBroadcast()
			? Ui::ReportSource::Channel
			: peer->isUser()
			? Ui::ReportSource::Bot
			: Ui::ReportSource::Group),
		chosen));
}

void SendReport(
		not_null<PeerData*> peer,
		Ui::ReportReason reason,
		const QString &comment,
		MessageIdsList ids) {
	const auto apiReason = [&] {
		using Reason = Ui::ReportReason;
		switch (reason) {
		case Reason::Spam: return MTP_inputReportReasonSpam();
		case Reason::Fake: return MTP_inputReportReasonFake();
		case Reason::Violence: return MTP_inputReportReasonViolence();
		case Reason::ChildAbuse: return MTP_inputReportReasonChildAbuse();
		case Reason::Pornography: return MTP_inputReportReasonPornography();
		case Reason::Other: return MTP_inputReportReasonOther();
		}
		Unexpected("Bad reason group value.");
	}();
	if (ids.empty()) {
		peer->session().api().request(MTPaccount_ReportPeer(
			peer->input,
			apiReason,
			MTP_string(comment)
		)).done([=] {
			Ui::Toast::Show(tr::lng_report_thanks(tr::now));
		}).send();
	} else {
		auto apiIds = QVector<MTPint>();
		apiIds.reserve(ids.size());
		for (const auto &fullId : ids) {
			apiIds.push_back(MTP_int(fullId.msg));
		}
		peer->session().api().request(MTPmessages_Report(
			peer->input,
			MTP_vector<MTPint>(apiIds),
			apiReason,
			MTP_string(comment)
		)).done([=] {
			Ui::Toast::Show(tr::lng_report_thanks(tr::now));
		}).send();
	}
}

} // namespace HistoryView
