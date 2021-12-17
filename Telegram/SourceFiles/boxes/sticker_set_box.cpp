/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/sticker_set_box.h"

#include "kotato/kotato_lang.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_document_media.h"
#include "data/stickers/data_stickers.h"
#include "chat_helpers/send_context_menu.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "core/application.h"
#include "mtproto/sender.h"
#include "storage/storage_account.h"
#include "dialogs/ui/dialogs_layout.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/image/image.h"
#include "ui/image/image_location_factory.h"
#include "ui/text/text_utilities.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/emoji_config.h"
#include "ui/toast/toast.h"
#include "ui/widgets/popup_menu.h"
#include "ui/cached_round_corners.h"
#include "lottie/lottie_multi_player.h"
#include "lottie/lottie_animation.h"
#include "chat_helpers/stickers_lottie.h"
#include "window/window_session_controller.h"
#include "base/unixtime.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "api/api_toggling_media.h"
#include "api/api_common.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"

#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>

namespace {

constexpr auto kStickersPanelPerRow = 5;

using Data::StickersSet;
using Data::StickersPack;
using Data::StickersByEmojiMap;
using SetFlag = Data::StickersSetFlag;

} // namespace

class StickerSetBox::Inner final : public Ui::RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		const StickerSetIdentifier &set);

	bool loaded() const;
	bool notInstalled() const;
	bool official() const;
	[[nodiscard]] rpl::producer<TextWithEntities> title() const;
	[[nodiscard]] QString shortName() const;

	void install();
	[[nodiscard]] rpl::producer<uint64> setInstalled() const;
	[[nodiscard]] rpl::producer<uint64> setArchived() const;
	[[nodiscard]] rpl::producer<> updateControls() const;

	[[nodiscard]] rpl::producer<Error> errors() const;

	void archiveStickers();

	bool isMasksSet() const {
		return (_setFlags & SetFlag::Masks);
	}

	~Inner();

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct Element {
		not_null<DocumentData*> document;
		std::shared_ptr<Data::DocumentMedia> documentMedia;
		Lottie::Animation *animated = nullptr;
		Ui::Animations::Simple overAnimation;
	};

	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;

	QSize boundingBoxSize() const;

	void paintSticker(Painter &p, int index, QPoint position) const;
	void setupLottie(int index);

	void updateSelected();
	void setSelected(int selected);
	void startOverAnimation(int index, float64 from, float64 to);
	int stickerFromGlobalPos(const QPoint &p) const;

	void gotSet(const MTPmessages_StickerSet &set);
	void installDone(const MTPmessages_StickerSetInstallResult &result);

	void send(not_null<DocumentData*> sticker, Api::SendOptions options);

	not_null<Lottie::MultiPlayer*> getLottiePlayer();

	void showPreview();

	not_null<Window::SessionController*> _controller;
	MTP::Sender _api;
	std::vector<Element> _elements;
	std::unique_ptr<Lottie::MultiPlayer> _lottiePlayer;
	StickersPack _pack;
	StickersByEmojiMap _emoji;
	bool _loaded = false;
	uint64 _setId = 0;
	uint64 _setAccessHash = 0;
	uint64 _setHash = 0;
	QString _setTitle, _setShortName;
	int _setCount = 0;
	Data::StickersSetFlags _setFlags;
	TimeId _setInstallDate = TimeId(0);
	ImageWithLocation _setThumbnail;

	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;

	StickerSetIdentifier _input;

	mtpRequestId _installRequest = 0;

	int _selected = -1;

	base::Timer _previewTimer;
	int _previewShown = -1;

	base::unique_qptr<Ui::PopupMenu> _menu;

	rpl::event_stream<uint64> _setInstalled;
	rpl::event_stream<uint64> _setArchived;
	rpl::event_stream<> _updateControls;
	rpl::event_stream<Error> _errors;

};

StickerSetBox::StickerSetBox(
	QWidget*,
	not_null<Window::SessionController*> controller,
	const StickerSetIdentifier &set)
: _controller(controller)
, _set(set) {
}

QPointer<Ui::BoxContent> StickerSetBox::Show(
		not_null<Window::SessionController*> controller,
		not_null<DocumentData*> document) {
	if (const auto sticker = document->sticker()) {
		if (sticker->set) {
			return controller->show(
				Box<StickerSetBox>(controller, sticker->set),
				Ui::LayerOption::KeepOther).data();
		}
	}
	return nullptr;
}

void StickerSetBox::prepare() {
	setTitle(tr::lng_contacts_loading());

	_inner = setInnerWidget(
		object_ptr<Inner>(this, _controller, _set),
		st::stickersScroll);
	_controller->session().data().stickers().updated(
	) | rpl::start_with_next([=] {
		updateButtons();
	}, lifetime());

	setDimensions(st::boxWideWidth, st::stickersMaxHeight);

	updateTitleAndButtons();

	_inner->updateControls(
	) | rpl::start_with_next([=] {
		updateTitleAndButtons();
	}, lifetime());

	_inner->setInstalled(
	) | rpl::start_with_next([=](uint64 setId) {
		if (_inner->isMasksSet()) {
			Ui::Toast::Show(tr::lng_masks_installed(tr::now));
		} else {
			auto &stickers = _controller->session().data().stickers();
			stickers.notifyStickerSetInstalled(setId);
		}
		closeBox();
	}, lifetime());

	_inner->errors(
	) | rpl::start_with_next([=](Error error) {
		handleError(error);
	}, lifetime());

	_inner->setArchived(
	) | rpl::start_with_next([=](uint64 setId) {
		const auto isMasks = _inner->isMasksSet();

		Ui::Toast::Show(isMasks
			? tr::lng_masks_has_been_archived(tr::now)
			: tr::lng_stickers_has_been_archived(tr::now));

		auto &order = isMasks
			? _controller->session().data().stickers().maskSetsOrderRef()
			: _controller->session().data().stickers().setsOrderRef();
		const auto index = order.indexOf(setId);
		if (index != -1) {
			order.removeAt(index);

			auto &local = _controller->session().local();
			if (isMasks) {
				local.writeInstalledMasks();
				local.writeArchivedMasks();
			} else {
				local.writeInstalledStickers();
				local.writeArchivedStickers();
			}
		}

		_controller->session().data().stickers().notifyUpdated();

		closeBox();
	}, lifetime());
}

void StickerSetBox::addStickers() {
	_inner->install();
}

void StickerSetBox::copyStickersLink() {
	const auto url = _controller->session().createInternalLinkFull(
		qsl("addstickers/") + _inner->shortName());
	QGuiApplication::clipboard()->setText(url);
}

void StickerSetBox::copyTitle() {
	_inner->title(
	) | rpl::start_with_next([](const TextWithEntities &value) {
		QGuiApplication::clipboard()->setText(value.text);
		Ui::show(Box<Ui::InformBox>(ktr("ktg_stickers_title_copied")));
	}, lifetime());
}

void StickerSetBox::handleError(Error error) {
	const auto guard = gsl::finally(crl::guard(this, [=] {
		closeBox();
	}));

	switch (error) {
	case Error::NotFound:
		_controller->show(
			Box<Ui::InformBox>(tr::lng_stickers_not_found(tr::now)));
		break;
	default: Unexpected("Error in StickerSetBox::handleError.");
	}
}

void StickerSetBox::updateTitleAndButtons() {
	setTitle(_inner->title());
	updateButtons();
}

void StickerSetBox::updateButtons() {
	clearButtons();
	if (_inner->loaded()) {
		const auto isMasks = _inner->isMasksSet();
		const auto moreButton = addTopButton(st::infoTopBarMenu);
		moreButton->setClickedCallback([=] { showMenu(moreButton.data()); });

		if (_inner->notInstalled()) {
			auto addText = isMasks
				? tr::lng_stickers_add_masks()
				: tr::lng_stickers_add_pack();
			addButton(std::move(addText), [=] { addStickers(); });
			addButton(tr::lng_cancel(), [=] { closeBox(); });

			/*
			if (!_inner->shortName().isEmpty()) {
				const auto top = addTopButton(st::infoTopBarMenu);
				const auto share = [=] {
					copyStickersLink();
					Ui::Toast::Show(tr::lng_stickers_copied(tr::now));
					closeBox();
				};
				const auto menu =
					std::make_shared<base::unique_qptr<Ui::PopupMenu>>();
				top->setClickedCallback([=] {
					*menu = base::make_unique_q<Ui::PopupMenu>(top);
					(*menu)->addAction(
						(isMasks
							? tr::lng_stickers_share_masks
							: tr::lng_stickers_share_pack)(tr::now),
						share);
					(*menu)->popup(QCursor::pos());
					return true;
				});
			}
			*/
		} else if (_inner->official()) {
			addButton(tr::lng_about_done(), [=] { closeBox(); });
		} else {
			auto share = [=] {
				copyStickersLink();
				Ui::Toast::Show(tr::lng_stickers_copied(tr::now));
			};
			auto shareText = isMasks
				? tr::lng_stickers_share_masks()
				: tr::lng_stickers_share_pack();
			addButton(std::move(shareText), std::move(share));
			addButton(tr::lng_cancel(), [=] { closeBox(); });

			/*
			if (!_inner->shortName().isEmpty()) {
				const auto top = addTopButton(st::infoTopBarMenu);
				const auto archive = [=] {
					_inner->archiveStickers();
				};
				const auto menu =
					std::make_shared<base::unique_qptr<Ui::PopupMenu>>();
				top->setClickedCallback([=] {
					*menu = base::make_unique_q<Ui::PopupMenu>(top);
					(*menu)->addAction(
						isMasks
							? tr::lng_masks_archive_pack(tr::now)
							: tr::lng_stickers_archive_pack(tr::now),
						archive);
					(*menu)->popup(QCursor::pos());
					return true;
				});
			}
			*/
		}
	} else {
		addButton(tr::lng_cancel(), [=] { closeBox(); });
	}
	update();
}

bool StickerSetBox::showMenu(not_null<Ui::IconButton*> button) {
	if (_menu) {
		_menu->hideAnimated(Ui::InnerDropdown::HideOption::IgnoreShow);
		return true;
	}

	_menu = base::make_unique_q<Ui::DropdownMenu>(window());
	const auto weak = _menu.get();
	_menu->setHiddenCallback([=] {
		weak->deleteLater();
		if (_menu == weak) {
			button->setForceRippled(false);
		}
	});
	_menu->setShowStartCallback([=] {
		if (_menu == weak) {
			button->setForceRippled(true);
		}
	});
	_menu->setHideStartCallback([=] {
		if (_menu == weak) {
			button->setForceRippled(false);
		}
	});
	button->installEventFilter(_menu);

	_menu->addAction(ktr("ktg_stickers_copy_title"), [=] { copyTitle(); });

	if (!_inner->shortName().isEmpty()) {
		_menu->addAction(tr::lng_stickers_share_pack(tr::now), [=] { copyStickersLink(); });
	}

	if (!_inner->notInstalled()) {
		const auto archive = [=] {
			_inner->archiveStickers();
			closeBox();
		};
		_menu->addAction(tr::lng_stickers_archive_pack(tr::now), archive);
	}

	const auto parentTopLeft = window()->mapToGlobal(QPoint());
	const auto buttonTopLeft = button->mapToGlobal(QPoint());
	const auto parentRect = QRect(parentTopLeft, window()->size());
	const auto buttonRect = QRect(buttonTopLeft, button->size());
	_menu->move(
		buttonRect.x() + buttonRect.width() - _menu->width() - parentRect.x(),
		buttonRect.y() + buttonRect.height() - parentRect.y() - style::ConvertScale(18));
	_menu->showAnimated(Ui::PanelAnimation::Origin::TopRight);

	return true;
}

void StickerSetBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
	_inner->resize(width(), _inner->height());
}

StickerSetBox::Inner::Inner(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	const StickerSetIdentifier &set)
: RpWidget(parent)
, _controller(controller)
, _api(&_controller->session().mtp())
, _setId(set.id)
, _setAccessHash(set.accessHash)
, _setShortName(set.shortName)
, _pathGradient(std::make_unique<Ui::PathShiftGradient>(
	st::windowBgRipple,
	st::windowBgOver,
	[=] { update(); }))
, _input(set)
, _previewTimer([=] { showPreview(); }) {
	_api.request(MTPmessages_GetStickerSet(
		Data::InputStickerSet(_input),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		gotSet(result);
	}).fail([=] {
		_loaded = true;
		_errors.fire(Error::NotFound);
	}).send();

	_controller->session().api().updateStickers();

	_controller->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	setMouseTracking(true);
}

void StickerSetBox::Inner::gotSet(const MTPmessages_StickerSet &set) {
	_pack.clear();
	_emoji.clear();
	_elements.clear();
	_selected = -1;
	setCursor(style::cur_default);
	set.match([&](const MTPDmessages_stickerSet &data) {
		const auto &v = data.vdocuments().v;
		_pack.reserve(v.size());
		_elements.reserve(v.size());
		for (const auto &item : v) {
			const auto document = _controller->session().data().processDocument(item);
			const auto sticker = document->sticker();
			if (!sticker) {
				continue;
			}
			_pack.push_back(document);
			_elements.push_back({ document, document->createMediaView() });
		}
		for (const auto &pack : data.vpacks().v) {
			pack.match([&](const MTPDstickerPack &pack) {
				if (const auto emoji = Ui::Emoji::Find(qs(pack.vemoticon()))) {
					const auto original = emoji->original();
					auto &stickers = pack.vdocuments().v;

					auto p = StickersPack();
					p.reserve(stickers.size());
					for (auto j = 0, c = int(stickers.size()); j != c; ++j) {
						auto doc = _controller->session().data().document(stickers[j].v);
						if (!doc || !doc->sticker()) continue;

						p.push_back(doc);
					}
					_emoji.insert(original, p);
				}
			});
		}
		data.vset().match([&](const MTPDstickerSet &set) {
			_setTitle = _controller->session().data().stickers().getSetTitle(
				set);
			_setShortName = qs(set.vshort_name());
			_setId = set.vid().v;
			_setAccessHash = set.vaccess_hash().v;
			_setHash = set.vhash().v;
			_setCount = set.vcount().v;
			_setFlags = Data::ParseStickersSetFlags(set);
			_setInstallDate = set.vinstalled_date().value_or(0);
			_setThumbnail = [&] {
				if (const auto thumbs = set.vthumbs()) {
					for (const auto &thumb : thumbs->v) {
						const auto result = Images::FromPhotoSize(
							&_controller->session(),
							set,
							thumb);
						if (result.location.valid()) {
							return result;
						}
					}
				}
				return ImageWithLocation();
			}();
			const auto &sets = _controller->session().data().stickers().sets();
			const auto it = sets.find(_setId);
			if (it != sets.cend()) {
				const auto set = it->second.get();
				const auto clientFlags = set->flags
					& (SetFlag::Featured
						| SetFlag::NotLoaded
						| SetFlag::Unread
						| SetFlag::Special);
				_setFlags |= clientFlags;
				set->flags = _setFlags;
				set->installDate = _setInstallDate;
				set->stickers = _pack;
				set->emoji = _emoji;
				set->setThumbnail(_setThumbnail);
			}
		});
	}, [&](const MTPDmessages_stickerSetNotModified &data) {
		LOG(("API Error: Unexpected messages.stickerSetNotModified."));
	});

	if (_pack.isEmpty()) {
		_errors.fire(Error::NotFound);
		return;
	} else {
		int32 rows = _pack.size() / kStickersPanelPerRow + ((_pack.size() % kStickersPanelPerRow) ? 1 : 0);
		resize(st::stickersPadding.left() + kStickersPanelPerRow * st::stickersSize.width(), st::stickersPadding.top() + rows * st::stickersSize.height() + st::stickersPadding.bottom());
	}
	_loaded = true;

	updateSelected();
	_updateControls.fire({});
}

rpl::producer<uint64> StickerSetBox::Inner::setInstalled() const {
	return _setInstalled.events();
}

rpl::producer<uint64> StickerSetBox::Inner::setArchived() const {
	return _setArchived.events();
}

rpl::producer<> StickerSetBox::Inner::updateControls() const {
	return _updateControls.events();
}

rpl::producer<StickerSetBox::Error> StickerSetBox::Inner::errors() const {
	return _errors.events();
}

void StickerSetBox::Inner::installDone(
		const MTPmessages_StickerSetInstallResult &result) {
	auto &stickers = _controller->session().data().stickers();
	auto &sets = stickers.setsRef();
	const auto isMasks = isMasksSet();

	const bool wasArchived = (_setFlags & SetFlag::Archived);
	if (wasArchived) {
		const auto index = (isMasks
			? stickers.archivedMaskSetsOrderRef()
			: stickers.archivedSetsOrderRef()).indexOf(_setId);
		if (index >= 0) {
			(isMasks
				? stickers.archivedMaskSetsOrderRef()
				: stickers.archivedSetsOrderRef()).removeAt(index);
		}
	}
	_setInstallDate = base::unixtime::now();
	_setFlags &= ~SetFlag::Archived;
	_setFlags |= SetFlag::Installed;
	auto it = sets.find(_setId);
	if (it == sets.cend()) {
		it = sets.emplace(
			_setId,
			std::make_unique<StickersSet>(
				&_controller->session().data(),
				_setId,
				_setAccessHash,
				_setHash,
				_setTitle,
				_setShortName,
				_setCount,
				_setFlags,
				_setInstallDate)).first;
	} else {
		it->second->flags = _setFlags;
		it->second->installDate = _setInstallDate;
	}
	const auto set = it->second.get();
	set->setThumbnail(_setThumbnail);
	set->stickers = _pack;
	set->emoji = _emoji;

	auto &order = isMasks
		? stickers.maskSetsOrderRef()
		: stickers.setsOrderRef();
	const auto insertAtIndex = 0, currentIndex = int(order.indexOf(_setId));
	if (currentIndex != insertAtIndex) {
		if (currentIndex > 0) {
			order.removeAt(currentIndex);
		}
		order.insert(insertAtIndex, _setId);
	}

	const auto customIt = sets.find(Data::Stickers::CustomSetId);
	if (customIt != sets.cend()) {
		const auto custom = customIt->second.get();
		for (const auto sticker : std::as_const(_pack)) {
			const int removeIndex = custom->stickers.indexOf(sticker);
			if (removeIndex >= 0) {
				custom->stickers.removeAt(removeIndex);
			}
		}
		if (custom->stickers.isEmpty()) {
			sets.erase(customIt);
		}
	}

	if (result.type() == mtpc_messages_stickerSetInstallResultArchive) {
		stickers.applyArchivedResult(
			result.c_messages_stickerSetInstallResultArchive());
	} else {
		auto &storage = _controller->session().local();
		if (wasArchived) {
			if (isMasks) {
				storage.writeArchivedMasks();
			} else {
				storage.writeArchivedStickers();
			}
		}
		if (isMasks) {
			storage.writeInstalledMasks();
		} else {
			storage.writeInstalledStickers();
		}
		stickers.notifyUpdated();
	}
	_setInstalled.fire_copy(_setId);
}

void StickerSetBox::Inner::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto index = stickerFromGlobalPos(e->globalPos());
	if (index < 0 || index >= _pack.size()) {
		return;
	}
	_previewTimer.callOnce(QApplication::startDragTime());
}

void StickerSetBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	updateSelected();
	if (_previewShown >= 0) {
		int index = stickerFromGlobalPos(e->globalPos());
		if (index >= 0 && index < _pack.size() && index != _previewShown) {
			_previewShown = index;
			_controller->widget()->showMediaPreview(
				Data::FileOriginStickerSet(_setId, _setAccessHash),
				_pack[_previewShown]);
		}
	}
}

void StickerSetBox::Inner::leaveEventHook(QEvent *e) {
	setSelected(-1);
}

void StickerSetBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	if (_previewShown >= 0) {
		_previewShown = -1;
		return;
	}
	if (!_previewTimer.isActive()) {
		return;
	}
	_previewTimer.cancel();
	const auto index = stickerFromGlobalPos(e->globalPos());
	if (index < 0 || index >= _pack.size() || isMasksSet()) {
		return;
	}
	send(_pack[index], {});
}

void StickerSetBox::Inner::send(
		not_null<DocumentData*> sticker,
		Api::SendOptions options) {
	const auto controller = _controller;
	Ui::PostponeCall(controller, [=] {
		if (controller->content()->sendExistingDocument(sticker, options)) {
			Ui::hideSettingsAndLayer();
		}
	});
}

void StickerSetBox::Inner::contextMenuEvent(QContextMenuEvent *e) {
	const auto index = stickerFromGlobalPos(e->globalPos());
	if (index < 0 || index >= _pack.size()) {
		return;
	}
	const auto type = _controller->content()->sendMenuType();
	if (type == SendMenu::Type::Disabled) {
		return;
	}
	_previewTimer.cancel();
	_menu = base::make_unique_q<Ui::PopupMenu>(this);

	const auto document = _pack[index];
	const auto sendSelected = [=](Api::SendOptions options) {
		send(document, options);
	};
	SendMenu::FillSendMenu(
		_menu.get(),
		type,
		SendMenu::DefaultSilentCallback(sendSelected),
		SendMenu::DefaultScheduleCallback(this, type, sendSelected));

	const auto toggleFavedSticker = [=] {
		Api::ToggleFavedSticker(
			document,
			Data::FileOriginStickerSet(Data::Stickers::FavedSetId, 0));
	};
	_menu->addAction(
		(document->owner().stickers().isFaved(document)
			? tr::lng_faved_stickers_remove
			: tr::lng_faved_stickers_add)(tr::now),
		toggleFavedSticker);

	_menu->popup(QCursor::pos());
}

void StickerSetBox::Inner::updateSelected() {
	auto selected = stickerFromGlobalPos(QCursor::pos());
	setSelected(isMasksSet() ? -1 : selected);
}

void StickerSetBox::Inner::setSelected(int selected) {
	if (_selected != selected) {
		startOverAnimation(_selected, 1., 0.);
		_selected = selected;
		startOverAnimation(_selected, 0., 1.);
		setCursor(_selected >= 0 ? style::cur_pointer : style::cur_default);
	}
}

void StickerSetBox::Inner::startOverAnimation(int index, float64 from, float64 to) {
	if (index < 0 || index >= _elements.size()) {
		return;
	}
	_elements[index].overAnimation.start([=] {
		const auto row = index / kStickersPanelPerRow;
		const auto column = index % kStickersPanelPerRow;
		const auto left = st::stickersPadding.left() + column * st::stickersSize.width();
		const auto top = st::stickersPadding.top() + row * st::stickersSize.height();
		rtlupdate(left, top, st::stickersSize.width(), st::stickersSize.height());
	}, from, to, st::emojiPanDuration);
}

void StickerSetBox::Inner::showPreview() {
	int index = stickerFromGlobalPos(QCursor::pos());
	if (index >= 0 && index < _pack.size()) {
		_previewShown = index;
		_controller->widget()->showMediaPreview(
			Data::FileOriginStickerSet(_setId, _setAccessHash),
			_pack[_previewShown]);
	}
}

not_null<Lottie::MultiPlayer*> StickerSetBox::Inner::getLottiePlayer() {
	if (!_lottiePlayer) {
		_lottiePlayer = std::make_unique<Lottie::MultiPlayer>(
			Lottie::Quality::Default,
			Lottie::MakeFrameRenderer());
		_lottiePlayer->updates(
		) | rpl::start_with_next([=] {
			update();
		}, lifetime());
	}
	return _lottiePlayer.get();
}

int32 StickerSetBox::Inner::stickerFromGlobalPos(const QPoint &p) const {
	QPoint l(mapFromGlobal(p));
	if (rtl()) l.setX(width() - l.x());
	int32 row = (l.y() >= st::stickersPadding.top()) ? qFloor((l.y() - st::stickersPadding.top()) / st::stickersSize.height()) : -1;
	int32 col = (l.x() >= st::stickersPadding.left()) ? qFloor((l.x() - st::stickersPadding.left()) / st::stickersSize.width()) : -1;
	if (row >= 0 && col >= 0 && col < kStickersPanelPerRow) {
		int32 result = row * kStickersPanelPerRow + col;
		return (result < _pack.size()) ? result : -1;
	}
	return -1;
}

void StickerSetBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (_elements.empty()) {
		return;
	}

	int32 from = qFloor(e->rect().top() / st::stickersSize.height()), to = qFloor(e->rect().bottom() / st::stickersSize.height()) + 1;

	_pathGradient->startFrame(0, width(), width() / 2);

	for (int32 i = from; i < to; ++i) {
		for (int32 j = 0; j < kStickersPanelPerRow; ++j) {
			int32 index = i * kStickersPanelPerRow + j;
			if (index >= _elements.size()) {
				break;
			}
			const auto pos = QPoint(st::stickersPadding.left() + j * st::stickersSize.width(), st::stickersPadding.top() + i * st::stickersSize.height());
			paintSticker(p, index, pos);
		}
	}

	if (_lottiePlayer) {
		const auto paused = _controller->isGifPausedAtLeastFor(
			Window::GifPauseReason::Layer);
		if (!paused) {
			_lottiePlayer->markFrameShown();
		}
	}
}

QSize StickerSetBox::Inner::boundingBoxSize() const {
	return QSize(
		st::stickersSize.width() - st::roundRadiusSmall * 2,
		st::stickersSize.height() - st::roundRadiusSmall * 2);
}

void StickerSetBox::Inner::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	const auto pauseInRows = [&](int fromRow, int tillRow) {
		Expects(fromRow <= tillRow);

		for (auto i = fromRow; i != tillRow; ++i) {
			for (auto j = 0; j != kStickersPanelPerRow; ++j) {
				const auto index = i * kStickersPanelPerRow + j;
				if (index >= _elements.size()) {
					break;
				}
				if (const auto animated = _elements[index].animated) {
					_lottiePlayer->pause(animated);
				}
			}
		}
	};
	const auto count = int(_elements.size());
	const auto rowsCount = (count / kStickersPanelPerRow)
		+ ((count % kStickersPanelPerRow) ? 1 : 0);
	const auto rowsTop = st::stickersPadding.top();
	const auto singleHeight = st::stickersSize.height();
	const auto rowsBottom = rowsTop + rowsCount * singleHeight;
	if (visibleTop >= rowsTop + singleHeight && visibleTop < rowsBottom) {
		const auto pauseHeight = (visibleTop - rowsTop);
		const auto pauseRows = std::min(
			pauseHeight / singleHeight,
			rowsCount);
		pauseInRows(0, pauseRows);
	}
	if (visibleBottom > rowsTop
		&& visibleBottom + singleHeight <= rowsBottom) {
		const auto pauseHeight = (rowsBottom - visibleBottom);
		const auto pauseRows = std::min(
			pauseHeight / singleHeight,
			rowsCount);
		pauseInRows(rowsCount - pauseRows, rowsCount);
	}
}

void StickerSetBox::Inner::setupLottie(int index) {
	auto &element = _elements[index];

	element.animated = ChatHelpers::LottieAnimationFromDocument(
		getLottiePlayer(),
		element.documentMedia.get(),
		ChatHelpers::StickerLottieSize::StickerSet,
		boundingBoxSize() * cIntRetinaFactor());
}

void StickerSetBox::Inner::paintSticker(
		Painter &p,
		int index,
		QPoint position) const {
	if (const auto over = _elements[index].overAnimation.value((index == _selected) ? 1. : 0.)) {
		p.setOpacity(over);
		auto tl = position;
		if (rtl()) tl.setX(width() - tl.x() - st::stickersSize.width());
		Ui::FillRoundRect(p, QRect(tl, st::stickersSize), st::emojiPanHover, Ui::StickerHoverCorners);
		p.setOpacity(1);
	}

	const auto &element = _elements[index];
	const auto document = element.document;
	const auto &media = element.documentMedia;
	media->checkStickerSmall();

	const auto isAnimated = document->sticker()->animated;
	if (isAnimated
		&& !element.animated
		&& media->loaded()) {
		const_cast<Inner*>(this)->setupLottie(index);
	}

	auto w = 1;
	auto h = 1;
	if (isAnimated && !document->dimensions.isEmpty()) {
		const auto request = Lottie::FrameRequest{ boundingBoxSize() * cIntRetinaFactor() };
		const auto size = request.size(document->dimensions, true) / cIntRetinaFactor();
		w = std::max(size.width(), 1);
		h = std::max(size.height(), 1);
	} else {
		auto coef = qMin((st::stickersSize.width() - st::roundRadiusSmall * 2) / float64(document->dimensions.width()), (st::stickersSize.height() - st::roundRadiusSmall * 2) / float64(document->dimensions.height()));
		if (coef > 1) coef = 1;
		w = std::max(qRound(coef * document->dimensions.width()), 1);
		h = std::max(qRound(coef * document->dimensions.height()), 1);
	}
	QPoint ppos = position + QPoint((st::stickersSize.width() - w) / 2, (st::stickersSize.height() - h) / 2);

	if (element.animated && element.animated->ready()) {
		const auto frame = element.animated->frame();
		p.drawImage(
			QRect(ppos, frame.size() / cIntRetinaFactor()),
			frame);

		_lottiePlayer->unpause(element.animated);
	} else if (const auto image = media->getStickerSmall()) {
		p.drawPixmapLeft(
			ppos,
			width(),
			image->pix(w, h));
	} else {
		ChatHelpers::PaintStickerThumbnailPath(
			p,
			media.get(),
			QRect(ppos, QSize(w, h)),
			_pathGradient.get());
	}
}

bool StickerSetBox::Inner::loaded() const {
	return _loaded && !_pack.isEmpty();
}

bool StickerSetBox::Inner::notInstalled() const {
	if (!_loaded) {
		return false;
	}
	const auto &sets = _controller->session().data().stickers().sets();
	const auto it = sets.find(_setId);
	if ((it == sets.cend())
		|| !(it->second->flags & SetFlag::Installed)
		|| (it->second->flags & SetFlag::Archived)) {
		return !_pack.empty();
	}
	return false;
}

bool StickerSetBox::Inner::official() const {
	return _loaded && _setShortName.isEmpty();
}

rpl::producer<TextWithEntities> StickerSetBox::Inner::title() const {
	if (!_loaded) {
		return tr::lng_contacts_loading() | Ui::Text::ToWithEntities();
	} else if (_pack.isEmpty()) {
		return tr::lng_attach_failed() | Ui::Text::ToWithEntities();
	}
	auto text = TextWithEntities{ _setTitle };
	TextUtilities::ParseEntities(text, TextParseMentions);
	return rpl::single(text);
}

QString StickerSetBox::Inner::shortName() const {
	return _setShortName;
}

void StickerSetBox::Inner::install() {
	if (_installRequest) {
		return;
	}
	_installRequest = _api.request(MTPmessages_InstallStickerSet(
		Data::InputStickerSet(_input),
		MTP_bool(false)
	)).done([=](const MTPmessages_StickerSetInstallResult &result) {
		installDone(result);
	}).fail([=] {
		_errors.fire(Error::NotFound);
	}).send();
}

void StickerSetBox::Inner::archiveStickers() {
	_api.request(MTPmessages_InstallStickerSet(
		Data::InputStickerSet(_input),
		MTP_boolTrue()
	)).done([=](const MTPmessages_StickerSetInstallResult &result) {
		if (result.type() == mtpc_messages_stickerSetInstallResultSuccess) {
			_setArchived.fire_copy(_setId);
		}
	}).fail([] {
		Ui::Toast::Show(Lang::Hard::ServerError());
	}).send();
}

StickerSetBox::Inner::~Inner() = default;
