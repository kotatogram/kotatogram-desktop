/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/gifs_list_widget.h"

#include "api/api_toggling_media.h" // Api::ToggleSavedGif
#include "base/const_string.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_file_origin.h"
#include "data/data_photo_media.h"
#include "data/data_document_media.h"
#include "data/stickers/data_stickers.h"
#include "chat_helpers/send_context_menu.h" // SendMenu::FillSendMenu
#include "core/click_handler_types.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/popup_menu.h"
#include "ui/effects/ripple_animation.h"
#include "ui/image/image.h"
#include "boxes/stickers_box.h"
#include "inline_bots/inline_bot_result.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "layout/layout_position.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "history/view/history_view_cursor_state.h"
#include "storage/storage_account.h" // Account::writeSavedGifs
#include "styles/style_chat_helpers.h"

#include <QtWidgets/QApplication>

namespace ChatHelpers {
namespace {

constexpr auto kSearchRequestDelay = 400;
constexpr auto kInlineItemsMaxPerRow = 5;
constexpr auto kSearchBotUsername = "gif"_cs;

} // namespace

void AddGifAction(
		Fn<void(QString, Fn<void()> &&)> callback,
		not_null<DocumentData*> document) {
	if (!document->isGifv()) {
		return;
	}
	auto &data = document->owner();
	const auto index = data.stickers().savedGifs().indexOf(document);
	const auto saved = (index >= 0);
	const auto text = (saved
		? tr::lng_context_delete_gif
		: tr::lng_context_save_gif)(tr::now);
	callback(text, [=] {
		Api::ToggleSavedGif(
			document,
			Data::FileOriginSavedGifs(),
			!saved);

		auto &data = document->owner();
		if (saved) {
			data.stickers().savedGifsRef().remove(index);
			document->session().local().writeSavedGifs();
		}
		data.stickers().notifySavedGifsUpdated();
	});
}

class GifsListWidget::Footer : public TabbedSelector::InnerFooter {
public:
	Footer(not_null<GifsListWidget*> parent);

	void stealFocus();
	void returnFocus();
	void setLoading(bool loading) {
		_cancel->setLoadingAnimation(loading);
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void processPanelHideFinished() override;

private:
	not_null<GifsListWidget*> _pan;

	object_ptr<Ui::InputField> _field;
	object_ptr<Ui::CrossButton> _cancel;

	QPointer<QWidget> _focusTakenFrom;

};

GifsListWidget::Footer::Footer(not_null<GifsListWidget*> parent) : InnerFooter(parent)
, _pan(parent)
, _field(this, st::gifsSearchField, tr::lng_gifs_search())
, _cancel(this, st::gifsSearchCancel) {
	connect(_field, &Ui::InputField::submitted, [=] {
		_pan->sendInlineRequest();
	});
	connect(_field, &Ui::InputField::cancelled, [=] {
		if (_field->getLastText().isEmpty()) {
			_pan->cancelled();
		} else {
			_field->setText(QString());
		}
	});
	connect(_field, &Ui::InputField::changed, [=] {
		_cancel->toggle(
			!_field->getLastText().isEmpty(),
			anim::type::normal);
		_pan->searchForGifs(_field->getLastText());
	});
	_cancel->setClickedCallback([=] {
		_field->setText(QString());
	});
}

void GifsListWidget::Footer::stealFocus() {
	if (!_focusTakenFrom) {
		_focusTakenFrom = QApplication::focusWidget();
	}
	_field->setFocus();
}

void GifsListWidget::Footer::returnFocus() {
	if (_focusTakenFrom) {
		if (_field->hasFocus()) {
			_focusTakenFrom->setFocus();
		}
		_focusTakenFrom = nullptr;
	}
}

void GifsListWidget::Footer::paintEvent(QPaintEvent *e) {
	Painter p(this);
	st::gifsSearchIcon.paint(p, st::gifsSearchIconPosition.x(), st::gifsSearchIconPosition.y(), width());
}

void GifsListWidget::Footer::resizeEvent(QResizeEvent *e) {
	auto fieldWidth = width()
		- st::gifsSearchFieldPosition.x()
		- st::gifsSearchCancelPosition.x()
		- st::gifsSearchCancel.width;
	_field->resizeToWidth(fieldWidth);
	_field->moveToLeft(st::gifsSearchFieldPosition.x(), st::gifsSearchFieldPosition.y());
	_cancel->moveToRight(st::gifsSearchCancelPosition.x(), st::gifsSearchCancelPosition.y());
}

void GifsListWidget::Footer::processPanelHideFinished() {
	// Preserve panel state through visibility toggles.
	//_field->setText(QString());
}

GifsListWidget::GifsListWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Inner(parent, controller)
, _api(&controller->session().mtp())
, _section(Section::Gifs)
, _updateInlineItems([=] { updateInlineItems(); })
, _mosaic(st::emojiPanWidth - st::inlineResultsLeft)
, _previewTimer([=] { showPreview(); }) {
	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	_inlineRequestTimer.setSingleShot(true);
	connect(
		&_inlineRequestTimer,
		&QTimer::timeout,
		this,
		[=] { sendInlineRequest(); });

	controller->session().data().stickers().savedGifsUpdated(
	) | rpl::start_with_next([=] {
		refreshSavedGifs();
	}, lifetime());

	controller->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	controller->gifPauseLevelChanged(
	) | rpl::start_with_next([=] {
		if (!controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::SavedGifs)) {
			update();
		}
	}, lifetime());

	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		_mosaic.setFullWidth(s.width());
	}, lifetime());

	_mosaic.setOffset(
		st::inlineResultsLeft - st::roundRadiusSmall,
		st::stickerPanPadding);
	_mosaic.setRightSkip(st::inlineResultsSkip);
}

rpl::producer<TabbedSelector::FileChosen> GifsListWidget::fileChosen() const {
	return _fileChosen.events();
}

auto GifsListWidget::photoChosen() const
-> rpl::producer<TabbedSelector::PhotoChosen> {
	return _photoChosen.events();
}

auto GifsListWidget::inlineResultChosen() const
-> rpl::producer<InlineChosen> {
	return _inlineResultChosen.events();
}

object_ptr<TabbedSelector::InnerFooter> GifsListWidget::createFooter() {
	Expects(_footer == nullptr);

	auto result = object_ptr<Footer>(this);
	_footer = result;
	return result;
}

void GifsListWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	auto top = getVisibleTop();
	Inner::visibleTopBottomUpdated(visibleTop, visibleBottom);
	if (top != getVisibleTop()) {
		_lastScrolled = crl::now();
	}
	checkLoadMore();
}

void GifsListWidget::checkLoadMore() {
	auto visibleHeight = (getVisibleBottom() - getVisibleTop());
	if (getVisibleBottom() + visibleHeight > height()) {
		sendInlineRequest();
	}
}

int GifsListWidget::countDesiredHeight(int newWidth) {
	return _mosaic.countDesiredHeight(newWidth) + st::stickerPanPadding * 2;
}

GifsListWidget::~GifsListWidget() {
	clearInlineRows(true);
	deleteUnusedGifLayouts();
	deleteUnusedInlineLayouts();
}

void GifsListWidget::cancelGifsSearch() {
	_footer->setLoading(false);
	if (_inlineRequestId) {
		_api.request(_inlineRequestId).cancel();
		_inlineRequestId = 0;
	}
	_inlineRequestTimer.stop();
	_inlineQuery = _inlineNextQuery = _inlineNextOffset = QString();
	_inlineCache.clear();
	refreshInlineRows(nullptr, true);
}

void GifsListWidget::inlineResultsDone(const MTPmessages_BotResults &result) {
	_footer->setLoading(false);
	_inlineRequestId = 0;

	auto it = _inlineCache.find(_inlineQuery);
	auto adding = (it != _inlineCache.cend());
	if (result.type() == mtpc_messages_botResults) {
		auto &d = result.c_messages_botResults();
		controller()->session().data().processUsers(d.vusers());

		auto &v = d.vresults().v;
		auto queryId = d.vquery_id().v;

		if (it == _inlineCache.cend()) {
			it = _inlineCache.emplace(
				_inlineQuery,
				std::make_unique<InlineCacheEntry>()).first;
		}
		const auto entry = it->second.get();
		entry->nextOffset = qs(d.vnext_offset().value_or_empty());
		if (const auto count = v.size()) {
			entry->results.reserve(entry->results.size() + count);
		}
		auto added = 0;
		for (const auto &res : v) {
			auto result = InlineBots::Result::Create(
				&controller()->session(),
				queryId,
				res);
			if (result) {
				++added;
				entry->results.push_back(std::move(result));
			}
		}

		if (!added) {
			entry->nextOffset = QString();
		}
	} else if (adding) {
		it->second->nextOffset = QString();
	}

	if (!showInlineRows(!adding)) {
		it->second->nextOffset = QString();
	}
	checkLoadMore();
}

void GifsListWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto clip = e->rect();
	p.fillRect(clip, st::emojiPanBg);

	paintInlineItems(p, clip);
}

void GifsListWidget::paintInlineItems(Painter &p, QRect clip) {
	if (_mosaic.empty()) {
		p.setFont(st::normalFont);
		p.setPen(st::noContactsColor);
		auto text = _inlineQuery.isEmpty()
			? tr::lng_gifs_no_saved(tr::now)
			: tr::lng_inline_bot_no_results(tr::now);
		p.drawText(QRect(0, 0, width(), (height() / 3) * 2 + st::normalFont->height), text, style::al_center);
		return;
	}
	const auto gifPaused = controller()->isGifPausedAtLeastFor(
		Window::GifPauseReason::SavedGifs);
	using namespace InlineBots::Layout;
	PaintContext context(crl::now(), false, gifPaused, false);

	auto paintItem = [&](not_null<const ItemBase*> item, QPoint point) {
		p.translate(point.x(), point.y());
		item->paint(
			p,
			clip.translated(-point),
			&context);
		p.translate(-point.x(), -point.y());
	};
	_mosaic.paint(std::move(paintItem), clip);
}

void GifsListWidget::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_lastMousePos = e->globalPos();
	updateSelected();

	_pressed = _selected;
	ClickHandler::pressed();
	_previewTimer.callOnce(QApplication::startDragTime());
}

void GifsListWidget::fillContextMenu(
		not_null<Ui::PopupMenu*> menu,
		SendMenu::Type type) {
	if (_selected < 0 || _pressed >= 0) {
		return;
	}

	const auto send = [=, selected = _selected](Api::SendOptions options) {
		selectInlineResult(selected, options, true);
	};
	SendMenu::FillSendMenu(
		menu,
		type,
		SendMenu::DefaultSilentCallback(send),
		SendMenu::DefaultScheduleCallback(this, type, send));

	if (const auto item = _mosaic.maybeItemAt(_selected)) {
		const auto document = item->getDocument()
			? item->getDocument() // Saved GIF.
			: item->getPreviewDocument(); // Searched GIF.
		if (document) {
			auto callback = [&](const QString &text, Fn<void()> &&done) {
				menu->addAction(text, std::move(done));
			};
			AddGifAction(std::move(callback), document);
		}
	};
}

void GifsListWidget::mouseReleaseEvent(QMouseEvent *e) {
	_previewTimer.cancel();

	auto pressed = std::exchange(_pressed, -1);
	auto activated = ClickHandler::unpressed();

	if (_previewShown) {
		_previewShown = false;
		return;
	}

	_lastMousePos = e->globalPos();
	updateSelected();

	if (_selected < 0 || _selected != pressed || !activated) {
		return;
	}

	if (dynamic_cast<InlineBots::Layout::SendClickHandler*>(activated.get())) {
		selectInlineResult(_selected, {});
	} else {
		ActivateClickHandler(window(), activated, {
			e->button(),
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(controller().get()),
			})
		});
	}
}

void GifsListWidget::selectInlineResult(
		int index,
		Api::SendOptions options,
		bool forceSend) {
	const auto item = _mosaic.maybeItemAt(index);
	if (!item) {
		return;
	}

	forceSend |= (QGuiApplication::keyboardModifiers()
		== Qt::ControlModifier);
	options.hideVia = true;
	if (const auto photo = item->getPhoto()) {
		using Data::PhotoSize;
		const auto media = photo->activeMediaView();
		if (forceSend
			|| (media && media->image(PhotoSize::Thumbnail))
			|| (media && media->image(PhotoSize::Large))) {
			_photoChosen.fire_copy({
				.photo = photo,
				.options = options });
		} else if (!photo->loading(PhotoSize::Thumbnail)) {
			photo->load(PhotoSize::Thumbnail, Data::FileOrigin());
		}
	} else if (const auto document = item->getDocument()) {
		const auto media = document->activeMediaView();
		const auto preview = Data::VideoPreviewState(media.get());
		if (forceSend || (media && preview.loaded())) {
			_fileChosen.fire_copy({
				.document = document,
				.options = options });
		} else if (!preview.usingThumbnail()) {
			if (preview.loading()) {
				document->cancel();
			} else {
				document->save(
					document->stickerOrGifOrigin(),
					QString());
			}
		}
	} else if (const auto inlineResult = item->getResult()) {
		if (inlineResult->onChoose(item)) {
			_inlineResultChosen.fire({ inlineResult, _searchBot, options });
		}
	}
}

void GifsListWidget::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
}

void GifsListWidget::leaveEventHook(QEvent *e) {
	clearSelection();
}

void GifsListWidget::leaveToChildEvent(QEvent *e, QWidget *child) {
	clearSelection();
}

void GifsListWidget::enterFromChildEvent(QEvent *e, QWidget *child) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

void GifsListWidget::clearSelection() {
	if (_selected >= 0) {
		ClickHandler::clearActive(_mosaic.itemAt(_selected));
		setCursor(style::cur_default);
	}
	_selected = _pressed = -1;
	update();
}

TabbedSelector::InnerFooter *GifsListWidget::getFooter() const {
	return _footer;
}

void GifsListWidget::processHideFinished() {
	clearSelection();
	clearHeavyData();
}

void GifsListWidget::processPanelHideFinished() {
	clearHeavyData();
}

void GifsListWidget::clearHeavyData() {
	// Preserve panel state through visibility toggles.
	//clearInlineRows(false);
	for (const auto &[document, layout] : _gifLayouts) {
		layout->unloadHeavyPart();
	}
	for (const auto &[document, layout] : _inlineLayouts) {
		layout->unloadHeavyPart();
	}
}

void GifsListWidget::refreshSavedGifs() {
	if (_section == Section::Gifs) {
		clearInlineRows(false);

		const auto &saved = controller()->session().data().stickers().savedGifs();
		if (!saved.isEmpty()) {
			const auto layouts = ranges::views::all(
				saved
			) | ranges::views::transform([&](not_null<DocumentData*> gif) {
				return layoutPrepareSavedGif(gif);
			}) | ranges::views::filter([](const LayoutItem *item) {
				return item != nullptr;
			}) | ranges::to<std::vector<not_null<LayoutItem*>>>;

			_mosaic.addItems(layouts);
		}
		deleteUnusedGifLayouts();

		resizeToWidth(width());
		update();
	}

	if (isVisible()) {
		updateSelected();
	} else {
		preloadImages();
	}
}

void GifsListWidget::clearInlineRows(bool resultsDeleted) {
	if (resultsDeleted) {
		_selected = _pressed = -1;
	} else {
		clearSelection();
	}
	_mosaic.clearRows(resultsDeleted);
}

GifsListWidget::LayoutItem *GifsListWidget::layoutPrepareSavedGif(
		not_null<DocumentData*> document) {
	auto it = _gifLayouts.find(document);
	if (it == _gifLayouts.cend()) {
		if (auto layout = LayoutItem::createLayoutGif(this, document)) {
			it = _gifLayouts.emplace(document, std::move(layout)).first;
			it->second->initDimensions();
		} else {
			return nullptr;
		}
	}
	if (!it->second->maxWidth()) return nullptr;

	return it->second.get();
}

GifsListWidget::LayoutItem *GifsListWidget::layoutPrepareInlineResult(
		not_null<InlineResult*> result) {
	auto it = _inlineLayouts.find(result);
	if (it == _inlineLayouts.cend()) {
		if (auto layout = LayoutItem::createLayout(
				this,
				result,
				_inlineWithThumb)) {
			it = _inlineLayouts.emplace(result, std::move(layout)).first;
			it->second->initDimensions();
		} else {
			return nullptr;
		}
	}
	if (!it->second->maxWidth()) return nullptr;

	return it->second.get();
}

void GifsListWidget::deleteUnusedGifLayouts() {
	if (_mosaic.empty() || _section != Section::Gifs) { // delete all
		_gifLayouts.clear();
	} else {
		for (auto i = _gifLayouts.begin(); i != _gifLayouts.cend();) {
			if (i->second->position() < 0) {
				i = _gifLayouts.erase(i);
			} else {
				++i;
			}
		}
	}
}

void GifsListWidget::deleteUnusedInlineLayouts() {
	if (_mosaic.empty() || _section == Section::Gifs) { // delete all
		_inlineLayouts.clear();
	} else {
		for (auto i = _inlineLayouts.begin(); i != _inlineLayouts.cend();) {
			if (i->second->position() < 0) {
				i = _inlineLayouts.erase(i);
			} else {
				++i;
			}
		}
	}
}

void GifsListWidget::preloadImages() {
	_mosaic.forEach([](not_null<const LayoutItem*> item) {
		item->preload();
	});
}

void GifsListWidget::switchToSavedGifs() {
	clearInlineRows(false);
	_section = Section::Gifs;
	refreshSavedGifs();
	scrollTo(0);
}

int GifsListWidget::refreshInlineRows(const InlineCacheEntry *entry, bool resultsDeleted) {
	if (!entry) {
		if (resultsDeleted) {
			clearInlineRows(true);
			deleteUnusedInlineLayouts();
		}
		switchToSavedGifs();
		return 0;
	}

	clearSelection();

	_section = Section::Inlines;
	const auto count = int(entry->results.size());
	const auto from = validateExistingInlineRows(entry->results);
	auto added = 0;
	if (count) {
		const auto resultLayouts = entry->results | ranges::views::slice(
			from,
			count
		) | ranges::views::transform([&](
				const std::unique_ptr<InlineBots::Result> &r) {
			return layoutPrepareInlineResult(r.get());
		}) | ranges::views::filter([](const LayoutItem *item) {
			return item != nullptr;
		}) | ranges::to<std::vector<not_null<LayoutItem*>>>;

		_mosaic.addItems(resultLayouts);
		added = resultLayouts.size();
		preloadImages();
	}

	resizeToWidth(width());
	update();

	_lastMousePos = QCursor::pos();
	updateSelected();

	return added;
}

int GifsListWidget::validateExistingInlineRows(const InlineResults &results) {
	const auto until = _mosaic.validateExistingRows([&](
			not_null<const LayoutItem*> item,
			int untilIndex) {
		return item->getResult() != results[untilIndex].get();
	}, results.size());

	if (_mosaic.empty()) {
		_inlineWithThumb = false;
		for (int i = until; i < results.size(); ++i) {
			if (results.at(i)->hasThumbDisplay()) {
				_inlineWithThumb = true;
				break;
			}
		}
	}
	return until;
}

void GifsListWidget::inlineItemLayoutChanged(const InlineBots::Layout::ItemBase *layout) {
	if (_selected < 0 || !isVisible()) {
		return;
	}

	if (const auto item = _mosaic.maybeItemAt(_selected)) {
		if (layout == item) {
			updateSelected();
		}
	}
}

void GifsListWidget::inlineItemRepaint(const InlineBots::Layout::ItemBase *layout) {
	auto ms = crl::now();
	if (_lastScrolled + 100 <= ms) {
		update();
	} else {
		_updateInlineItems.callOnce(_lastScrolled + 100 - ms);
	}
}

bool GifsListWidget::inlineItemVisible(const InlineBots::Layout::ItemBase *layout) {
	auto position = layout->position();
	if (position < 0 || !isVisible()) {
		return false;
	}

	const auto &[row, column] = Layout::IndexToPosition(position);
	auto top = 0;
	for (auto i = 0; i != row; ++i) {
		top += _mosaic.rowHeightAt(i);
	}

	return (top < getVisibleBottom())
		&& (top + _mosaic.itemAt(row, column)->height() > getVisibleTop());
}

Data::FileOrigin GifsListWidget::inlineItemFileOrigin() {
	return _inlineQuery.isEmpty()
		? Data::FileOriginSavedGifs()
		: Data::FileOrigin();
}

void GifsListWidget::afterShown() {
	if (_footer) {
		_footer->stealFocus();
	}
}

void GifsListWidget::beforeHiding() {
	if (_footer) {
		_footer->returnFocus();
	}
}

bool GifsListWidget::refreshInlineRows(int32 *added) {
	auto it = _inlineCache.find(_inlineQuery);
	const InlineCacheEntry *entry = nullptr;
	if (it != _inlineCache.cend()) {
		entry = it->second.get();
		_inlineNextOffset = it->second->nextOffset;
	}
	auto result = refreshInlineRows(entry, false);
	if (added) *added = result;
	return (entry != nullptr);
}

int32 GifsListWidget::showInlineRows(bool newResults) {
	auto added = 0;
	refreshInlineRows(&added);
	if (newResults) {
		scrollTo(0);
	}
	return added;
}

void GifsListWidget::searchForGifs(const QString &query) {
	if (query.isEmpty()) {
		cancelGifsSearch();
		return;
	}

	if (_inlineQuery != query) {
		_footer->setLoading(false);
		if (_inlineRequestId) {
			_api.request(_inlineRequestId).cancel();
			_inlineRequestId = 0;
		}
		if (_inlineCache.find(query) != _inlineCache.cend()) {
			_inlineRequestTimer.stop();
			_inlineQuery = _inlineNextQuery = query;
			showInlineRows(true);
		} else {
			_inlineNextQuery = query;
			_inlineRequestTimer.start(kSearchRequestDelay);
		}
	}

	if (!_searchBot && !_searchBotRequestId) {
		auto username = kSearchBotUsername.utf16();
		_searchBotRequestId = _api.request(MTPcontacts_ResolveUsername(
			MTP_string(username)
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			Expects(result.type() == mtpc_contacts_resolvedPeer);

			auto &data = result.c_contacts_resolvedPeer();
			controller()->session().data().processUsers(data.vusers());
			controller()->session().data().processChats(data.vchats());
			const auto peer = controller()->session().data().peerLoaded(
				peerFromMTP(data.vpeer()));
			if (const auto user = peer ? peer->asUser() : nullptr) {
				_searchBot = user;
			}
		}).send();
	}
}

void GifsListWidget::cancelled() {
	_cancelled.fire({});
}

rpl::producer<> GifsListWidget::cancelRequests() const {
	return _cancelled.events();
}

void GifsListWidget::sendInlineRequest() {
	if (_inlineRequestId || !_inlineQueryPeer || _inlineNextQuery.isEmpty()) {
		return;
	}

	if (!_searchBot) {
		// Wait for the bot being resolved.
		_footer->setLoading(true);
		_inlineRequestTimer.start(kSearchRequestDelay);
		return;
	}
	_inlineRequestTimer.stop();
	_inlineQuery = _inlineNextQuery;

	auto nextOffset = QString();
	auto it = _inlineCache.find(_inlineQuery);
	if (it != _inlineCache.cend()) {
		nextOffset = it->second->nextOffset;
		if (nextOffset.isEmpty()) {
			_footer->setLoading(false);
			return;
		}
	}

	_footer->setLoading(true);
	_inlineRequestId = _api.request(MTPmessages_GetInlineBotResults(
		MTP_flags(0),
		_searchBot->inputUser,
		_inlineQueryPeer->input,
		MTPInputGeoPoint(),
		MTP_string(_inlineQuery),
		MTP_string(nextOffset)
	)).done([this](const MTPmessages_BotResults &result) {
		inlineResultsDone(result);
	}).fail([this] {
		// show error?
		_footer->setLoading(false);
		_inlineRequestId = 0;
	}).handleAllErrors().send();
}

void GifsListWidget::refreshRecent() {
	if (_section == Section::Gifs) {
		refreshSavedGifs();
	}
}

void GifsListWidget::updateSelected() {
	if (_pressed >= 0 && !_previewShown) {
		return;
	}

	const auto p = mapFromGlobal(_lastMousePos);
	const auto sx = rtl() ? (width() - p.x()) : p.x();
	const auto sy = p.y();
	const auto &[index, exact, relative] = _mosaic.findByPoint({ sx, sy });
	const auto selected = exact ? index : -1;
	const auto item = exact ? _mosaic.itemAt(selected).get() : nullptr;
	const auto link = exact ? item->getState(relative, {}).link : nullptr;

	if (_selected != selected) {
		if (const auto s = _mosaic.maybeItemAt(_selected)) {
			s->update();
		}
		_selected = selected;
		if (item) {
			item->update();
		}
		if (_previewShown && _selected >= 0 && _pressed != _selected) {
			_pressed = _selected;
			if (item) {
				if (const auto preview = item->getPreviewDocument()) {
					controller()->widget()->showMediaPreview(
						Data::FileOriginSavedGifs(),
						preview);
				} else if (const auto preview = item->getPreviewPhoto()) {
					controller()->widget()->showMediaPreview(
						Data::FileOrigin(),
						preview);
				}
			}
		}
	}
	if (ClickHandler::setActive(link, item)) {
		setCursor(link ? style::cur_pointer : style::cur_default);
	}
}

void GifsListWidget::showPreview() {
	if (_pressed < 0) {
		return;
	}
	if (const auto layout = _mosaic.maybeItemAt(_pressed)) {
		if (const auto previewDocument = layout->getPreviewDocument()) {
			_previewShown = controller()->widget()->showMediaPreview(
				Data::FileOriginSavedGifs(),
				previewDocument);
		} else if (const auto previewPhoto = layout->getPreviewPhoto()) {
			_previewShown = controller()->widget()->showMediaPreview(
				Data::FileOrigin(),
				previewPhoto);
		}
	}
}

void GifsListWidget::updateInlineItems() {
	auto ms = crl::now();
	if (_lastScrolled + 100 <= ms) {
		update();
	} else {
		_updateInlineItems.callOnce(_lastScrolled + 100 - ms);
	}
}

} // namespace ChatHelpers
