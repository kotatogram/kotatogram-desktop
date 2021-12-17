/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peer_list_box.h"

#include "kotato/kotato_lang.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/effects/round_checkbox.h"
#include "ui/effects/ripple_animation.h"
#include "ui/empty_userpic.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/text/text_options.h"
#include "lang/lang_keys.h"
#include "storage/file_download.h"
#include "data/data_peer_values.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "base/unixtime.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"
#include "styles/style_info.h"

#include <rpl/range.h>

PaintRoundImageCallback PaintUserpicCallback(
		not_null<PeerData*> peer,
		bool respectSavedMessagesChat) {
	if (respectSavedMessagesChat) {
		if (peer->isSelf()) {
			return [](Painter &p, int x, int y, int outerWidth, int size) {
				Ui::EmptyUserpic::PaintSavedMessages(p, x, y, outerWidth, size);
			};
		} else if (peer->isRepliesChat()) {
			return [](Painter &p, int x, int y, int outerWidth, int size) {
				Ui::EmptyUserpic::PaintRepliesMessages(p, x, y, outerWidth, size);
			};
		}
	}
	auto userpic = std::shared_ptr<Data::CloudImageView>();
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable {
		peer->paintUserpicLeft(p, userpic, x, y, outerWidth, size);
	};
}

PeerListBox::PeerListBox(
	QWidget*,
	std::unique_ptr<PeerListController> controller,
	Fn<void(not_null<PeerListBox*>)> init)
: _controller(std::move(controller))
, _init(std::move(init)) {
	Expects(_controller != nullptr);
}

void PeerListBox::createMultiSelect() {
	Expects(_select == nullptr);

	auto entity = object_ptr<Ui::MultiSelect>(
		this,
		(_controller->selectSt()
			? *_controller->selectSt()
			: st::defaultMultiSelect),
		tr::lng_participant_filter());
	_select.create(this, std::move(entity));
	_select->heightValue(
	) | rpl::start_with_next(
		[this] { updateScrollSkips(); },
		lifetime());
	_select->entity()->setSubmittedCallback([=](Qt::KeyboardModifiers) {
		content()->submitted();
	});
	_select->entity()->setQueryChangedCallback([=](const QString &query) {
		searchQueryChanged(query);
	});
	_select->entity()->setItemRemovedCallback([=](uint64 itemId) {
		if (_controller->handleDeselectForeignRow(itemId)) {
			return;
		}
		if (const auto peer = _controller->session().data().peerLoaded(PeerId(itemId))) {
			if (const auto row = peerListFindRow(itemId)) {
				content()->changeCheckState(row, false, anim::type::normal);
				update();
			}
			_controller->itemDeselectedHook(peer);
		}
	});
	_select->resizeToWidth(_controller->contentWidth());
	_select->moveToLeft(0, 0);
}

int PeerListBox::getTopScrollSkip() const {
	auto result = 0;
	if (_select && !_select->isHidden()) {
		result += _select->height();
	}
	return result;
}

void PeerListBox::updateScrollSkips() {
	// If we show / hide the search field scroll top is fixed.
	// If we resize search field by bubbles scroll bottom is fixed.
	setInnerTopSkip(getTopScrollSkip(), _scrollBottomFixed);
	if (!_select->animating()) {
		_scrollBottomFixed = true;
	}
}

void PeerListBox::prepare() {
	setContent(setInnerWidget(
		object_ptr<PeerListContent>(
			this,
			_controller.get()),
		st::boxScroll));
	content()->resizeToWidth(_controller->contentWidth());

	_controller->setDelegate(this);

	_controller->boxHeightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(_controller->contentWidth(), height);
	}, lifetime());

	if (_select) {
		_select->finishAnimating();
		Ui::SendPendingMoveResizeEvents(_select);
		_scrollBottomFixed = true;
		onScrollToY(0);
	}

	content()->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		onScrollToY(request.ymin, request.ymax);
	}, lifetime());

	if (_init) {
		_init(this);
	}
}

void PeerListBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Down) {
		content()->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		content()->selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		content()->selectSkipPage(height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		content()->selectSkipPage(height(), -1);
	} else if (e->key() == Qt::Key_Escape && _select && !_select->entity()->getQuery().isEmpty()) {
		_select->entity()->clearQuery();
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void PeerListBox::searchQueryChanged(const QString &query) {
	onScrollToY(0);
	content()->searchQueryChanged(query);
}

void PeerListBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	if (_select) {
		_select->resizeToWidth(width());
		_select->moveToLeft(0, 0);

		updateScrollSkips();
	}

	content()->resizeToWidth(width());
}

void PeerListBox::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto &bg = (_controller->listSt()
		? *_controller->listSt()
		: st::peerListBox).bg;
	for (const auto &rect : e->region()) {
		p.fillRect(rect, bg);
	}
}

void PeerListBox::setInnerFocus() {
	if (!_select || !_select->toggled()) {
		content()->setFocus();
	} else {
		_select->entity()->setInnerFocus();
	}
}

void PeerListBox::peerListSetRowChecked(
		not_null<PeerListRow*> row,
		bool checked) {
	if (checked) {
		addSelectItem(row, anim::type::normal);
		PeerListContentDelegate::peerListSetRowChecked(row, checked);
		peerListUpdateRow(row);

		// This call deletes row from _searchRows.
		//_select->entity()->clearQuery();
	} else {
		// The itemRemovedCallback will call changeCheckState() here.
		_select->entity()->removeItem(row->id());
		peerListUpdateRow(row);
	}
}

void PeerListBox::peerListSetForeignRowChecked(
		not_null<PeerListRow*> row,
		bool checked,
		anim::type animated) {
	if (checked) {
		addSelectItem(row, animated);

		// This call deletes row from _searchRows.
		//_select->entity()->clearQuery();
	} else {
		// The itemRemovedCallback will call changeCheckState() here.
		_select->entity()->removeItem(row->id());
	}
}

void PeerListBox::peerListScrollToTop() {
	onScrollToY(0);
}

void PeerListBox::peerListSetSearchMode(PeerListSearchMode mode) {
	PeerListContentDelegate::peerListSetSearchMode(mode);

	auto selectVisible = (mode != PeerListSearchMode::Disabled);
	if (selectVisible && !_select) {
		createMultiSelect();
		_select->toggle(!selectVisible, anim::type::instant);
	}
	if (_select) {
		_select->toggle(selectVisible, anim::type::normal);
		_scrollBottomFixed = false;
		setInnerFocus();
	}
}

PeerListController::PeerListController(std::unique_ptr<PeerListSearchController> searchController) : _searchController(std::move(searchController)) {
	if (_searchController) {
		_searchController->setDelegate(this);
	}
}

const style::PeerList &PeerListController::computeListSt() const {
	return _listSt ? *_listSt : st::peerListBox;
}

const style::MultiSelect &PeerListController::computeSelectSt() const {
	return _selectSt ? *_selectSt : st::defaultMultiSelect;
}

bool PeerListController::hasComplexSearch() const {
	return (_searchController != nullptr);
}

void PeerListController::search(const QString &query) {
	Expects(hasComplexSearch());

	_searchController->searchQuery(query);
}

void PeerListController::peerListSearchAddRow(not_null<PeerData*> peer) {
	if (auto row = delegate()->peerListFindRow(peer->id.value)) {
		Assert(row->id() == row->peer()->id.value);
		delegate()->peerListAppendFoundRow(row);
	} else if (auto row = createSearchRow(peer)) {
		Assert(row->id() == row->peer()->id.value);
		delegate()->peerListAppendSearchRow(std::move(row));
	}
}

void PeerListController::peerListSearchRefreshRows() {
	delegate()->peerListRefreshRows();
}

rpl::producer<int> PeerListController::onlineCountValue() const {
	return rpl::single(0);
}

void PeerListController::setDescriptionText(const QString &text) {
	if (text.isEmpty()) {
		setDescription(nullptr);
	} else {
		setDescription(object_ptr<Ui::FlatLabel>(nullptr, text, computeListSt().about));
	}
}

void PeerListController::setSearchLoadingText(const QString &text) {
	if (text.isEmpty()) {
		setSearchLoading(nullptr);
	} else {
		setSearchLoading(object_ptr<Ui::FlatLabel>(nullptr, text, st::membersAbout));
	}
}

void PeerListController::setSearchNoResultsText(const QString &text) {
	if (text.isEmpty()) {
		setSearchNoResults(nullptr);
	} else {
		setSearchNoResults(object_ptr<Ui::FlatLabel>(nullptr, text, st::membersAbout));
	}
}

base::unique_qptr<Ui::PopupMenu> PeerListController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	return nullptr;
}

std::unique_ptr<PeerListState> PeerListController::saveState() const {
	return delegate()->peerListSaveState();
}

void PeerListController::restoreState(
		std::unique_ptr<PeerListState> state) {
	delegate()->peerListRestoreState(std::move(state));
}

int PeerListController::contentWidth() const {
	return st::boxWideWidth;
}

rpl::producer<int> PeerListController::boxHeightValue() const {
	return rpl::single(st::boxMaxListHeight);
}

int PeerListController::descriptionTopSkipMin() const {
	return computeListSt().item.height;
}

void PeerListBox::addSelectItem(
		not_null<PeerData*> peer,
		anim::type animated) {
	const auto respect = _controller->respectSavedMessagesChat();
	const auto text = (respect && peer->isSelf())
		? tr::lng_saved_short(tr::now)
		: (respect && peer->isRepliesChat())
		? tr::lng_replies_messages(tr::now)
		: peer->shortName();
	addSelectItem(
		peer->id.value,
		text,
		PaintUserpicCallback(peer, respect),
		animated);
}

void PeerListBox::addSelectItem(
		not_null<PeerListRow*> row,
		anim::type animated) {
	addSelectItem(
		row->id(),
		row->generateShortName(),
		row->generatePaintUserpicCallback(),
		animated);
}

void PeerListBox::addSelectItem(
		uint64 itemId,
		const QString &text,
		Ui::MultiSelect::PaintRoundImage paintUserpic,
		anim::type animated) {
	if (!_select) {
		createMultiSelect();
		_select->hide(anim::type::instant);
	}
	const auto &activeBg = (_controller->selectSt()
		? *_controller->selectSt()
		: st::defaultMultiSelect).item.textActiveBg;
	if (animated == anim::type::instant) {
		_select->entity()->addItemInBunch(
			itemId,
			text,
			activeBg,
			std::move(paintUserpic));
	} else {
		_select->entity()->addItem(
			itemId,
			text,
			activeBg,
			std::move(paintUserpic));
	}
}

void PeerListBox::peerListFinishSelectedRowsBunch() {
	Expects(_select != nullptr);

	_select->entity()->finishItemsBunch();
}

bool PeerListBox::peerListIsRowChecked(not_null<PeerListRow*> row) {
	return _select ? _select->entity()->hasItem(row->id()) : false;
}

int PeerListBox::peerListSelectedRowsCount() {
	return _select ? _select->entity()->getItemsCount() : 0;
}

auto PeerListBox::collectSelectedRows()
-> std::vector<not_null<PeerData*>> {
	auto result = std::vector<not_null<PeerData*>>();
	auto items = _select
		? _select->entity()->getItems()
		: QVector<uint64>();
	if (!items.empty()) {
		result.reserve(items.size());
		for (const auto itemId : items) {
			if (!_controller->isForeignRow(itemId)) {
				result.push_back(_controller->session().data().peer(PeerId(itemId)));
			}
		}
	}
	return result;
}

PeerListRow::PeerListRow(not_null<PeerData*> peer)
: PeerListRow(peer, peer->id.value) {
}

PeerListRow::PeerListRow(not_null<PeerData*> peer, PeerListRowId id)
: _id(id)
, _peer(peer) {
}

PeerListRow::PeerListRow(PeerListRowId id)
: _id(id) {
}

PeerListRow::~PeerListRow() = default;

bool PeerListRow::checked() const {
	return _checkbox && _checkbox->checked();
}

void PeerListRow::setCustomStatus(const QString &status, bool active) {
	setStatusText(status);
	_statusType = active ? StatusType::CustomActive : StatusType::Custom;
	_statusValidTill = 0;
}

void PeerListRow::clearCustomStatus() {
	_statusType = StatusType::Online;
	refreshStatus();
}

void PeerListRow::refreshStatus() {
	if (!_initialized
		|| special()
		|| _statusType == StatusType::Custom
		|| _statusType == StatusType::CustomActive) {
		return;
	}
	_statusType = StatusType::LastSeen;
	_statusValidTill = 0;
	if (auto user = peer()->asUser()) {
		if (_isSavedMessagesChat) {
			setStatusText(tr::lng_saved_forward_here(tr::now));
		} else if (user->isInaccessible()) {
			setStatusText(ktr("ktg_user_status_unaccessible"));
		} else {
			auto time = base::unixtime::now();
			setStatusText(Data::OnlineText(user, time));
			if (Data::OnlineTextActive(user, time)) {
				_statusType = StatusType::Online;
			}
			_statusValidTill = crl::now()
				+ Data::OnlineChangeTimeout(user, time);
		}
	} else if (auto chat = peer()->asChat()) {
		if (!chat->amIn()) {
			setStatusText(tr::lng_chat_status_unaccessible(tr::now));
		} else if (chat->count > 0) {
			setStatusText(tr::lng_group_status(tr::now) + ", " + tr::lng_chat_status_members(tr::now, lt_count_decimal, chat->count));
		} else {
			setStatusText(tr::lng_group_status(tr::now));
		}
	} else if (peer()->isMegagroup()) {
		if (peer()->asChannel()->membersCountKnown()) {
			setStatusText(ktr("ktg_supergroup_status") + ", " + tr::lng_chat_status_members(tr::now, lt_count_decimal, peer()->asChannel()->membersCount()));
		} else {
			setStatusText(ktr("ktg_supergroup_status"));
		}
	} else if (peer()->isChannel()) {
		if (peer()->asChannel()->membersCountKnown()) {
			setStatusText(tr::lng_channel_status(tr::now) + ", " + tr::lng_chat_status_subscribers(tr::now, lt_count_decimal, peer()->asChannel()->membersCount()));
		} else {
			setStatusText(tr::lng_channel_status(tr::now));
		}
	}
}

crl::time PeerListRow::refreshStatusTime() const {
	return _statusValidTill;
}

void PeerListRow::refreshName(const style::PeerListItem &st) {
	if (!_initialized) {
		return;
	}
	const auto text = _isSavedMessagesChat
		? tr::lng_saved_messages(tr::now)
		: _isRepliesMessagesChat
		? tr::lng_replies_messages(tr::now)
		: generateName();
	_name.setText(st.nameStyle, text, Ui::NameTextOptions());
}

int PeerListRow::elementsCount() const {
	return 1;
}

QRect PeerListRow::elementGeometry(int element, int outerWidth) const {
	if (element != 1) {
		return QRect();
	}
	const auto size = rightActionSize();
	if (size.isEmpty()) {
		return QRect();
	}
	const auto margins = rightActionMargins();
	const auto right = margins.right();
	const auto top = margins.top();
	const auto left = outerWidth - right - size.width();
	return QRect(QPoint(left, top), size);
}

bool PeerListRow::elementDisabled(int element) const {
	return (element == 1) && rightActionDisabled();
}

bool PeerListRow::elementOnlySelect(int element) const {
	return false;
}

void PeerListRow::elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) {
	if (element == 1) {
		rightActionAddRipple(point, std::move(updateCallback));
	}
}

void PeerListRow::elementsStopLastRipple() {
	rightActionStopLastRipple();
}

void PeerListRow::elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) {
	const auto geometry = elementGeometry(1, outerWidth);
	if (!geometry.isEmpty()) {
		rightActionPaint(
			p,
			geometry.x(),
			geometry.y(),
			outerWidth,
			selected,
			(selectedElement == 1));
	}
}

QString PeerListRow::generateName() {
	return peer()->name;
}

QString PeerListRow::generateShortName() {
	return _isSavedMessagesChat
		? tr::lng_saved_short(tr::now)
		: _isRepliesMessagesChat
		? tr::lng_replies_messages(tr::now)
		: peer()->shortName();
}

std::shared_ptr<Data::CloudImageView> &PeerListRow::ensureUserpicView() {
	if (!_userpic) {
		_userpic = peer()->createUserpicView();
	}
	return _userpic;
}

PaintRoundImageCallback PeerListRow::generatePaintUserpicCallback() {
	const auto saved = _isSavedMessagesChat;
	const auto replies = _isRepliesMessagesChat;
	const auto peer = this->peer();
	auto userpic = saved ? nullptr : ensureUserpicView();
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable {
		if (saved) {
			Ui::EmptyUserpic::PaintSavedMessages(p, x, y, outerWidth, size);
		} else if (replies) {
			Ui::EmptyUserpic::PaintRepliesMessages(p, x, y, outerWidth, size);
		} else {
			peer->paintUserpicLeft(p, userpic, x, y, outerWidth, size);
		}
	};
}

void PeerListRow::invalidatePixmapsCache() {
	if (_checkbox) {
		_checkbox->invalidateCache();
	}
}

int PeerListRow::nameIconWidth() const {
	if (special()) {
		return 0;
	}
	auto hasCreatorRights = false;
	auto hasAdminRights = false;
	if (const auto chat = _peer->asChat()) {
		if (chat->amCreator()) {
			hasCreatorRights = true;
			hasAdminRights = true;
		} else if (chat->hasAdminRights()) {
			hasAdminRights = true;
		}
	} else if (const auto channel = _peer->asChannel()) {
		if (channel->amCreator()) {
			hasCreatorRights = true;
			hasAdminRights = true;
		} else if (channel->hasAdminRights()) {
			hasAdminRights = true;
		}
	}

	return special()
		? 0
		: (_peer->isVerified()
			? st::dialogsVerifiedIcon.width()
			: 0)
		+ (hasCreatorRights
			? st::infoMembersCreatorIcon.width()
			: hasAdminRights
				? st::infoMembersAdminIcon.width()
				: 0);
}

void PeerListRow::paintNameIcon(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected) {
	if (special()) {
		return;
	}

	auto hasCreatorRights = false;
	auto hasAdminRights = false;
	if (const auto chat = _peer->asChat()) {
		if (chat->amCreator()) {
			hasCreatorRights = true;
			hasAdminRights = true;
		} else if (chat->hasAdminRights()) {
			hasAdminRights = true;
		}
	} else if (const auto channel = _peer->asChannel()) {
		if (channel->amCreator()) {
			hasCreatorRights = true;
			hasAdminRights = true;
		} else if (channel->hasAdminRights()) {
			hasAdminRights = true;
		}
	}

	auto icon = [&] {
		return hasCreatorRights
				? (selected
					? &st::infoMembersCreatorIconOver
					: &st::infoMembersCreatorIcon)
				: (selected
					? &st::infoMembersAdminIconOver
					: &st::infoMembersAdminIcon);
	}();
	if (_peer->isVerified()) { 
		st::dialogsVerifiedIcon.paint(p, x, y, outerWidth);
	}
	if (hasAdminRights) {
		icon->paint(p, x + (_peer->isVerified() ? st::dialogsVerifiedIcon.width() : 0 ), y, outerWidth);
	}
}

void PeerListRow::paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) {
	auto statusHasOnlineColor = (_statusType == PeerListRow::StatusType::Online)
		|| (_statusType == PeerListRow::StatusType::CustomActive);
	p.setFont(st::contactsStatusFont);
	p.setPen(statusHasOnlineColor ? st.statusFgActive : (selected ? st.statusFgOver : st.statusFg));
	_status.drawLeftElided(p, x, y, availableWidth, outerWidth);
}

bool PeerListRow::hasAction() {
	return true;
}

template <typename MaskGenerator, typename UpdateCallback>
void PeerListRow::addRipple(const style::PeerListItem &st, MaskGenerator &&maskGenerator, QPoint point, UpdateCallback &&updateCallback) {
	if (!_ripple) {
		auto mask = maskGenerator();
		if (mask.isNull()) {
			return;
		}
		_ripple = std::make_unique<Ui::RippleAnimation>(st.button.ripple, std::move(mask), std::forward<UpdateCallback>(updateCallback));
	}
	_ripple->add(point);
}

int PeerListRow::adminRankWidth() const {
	return 0;
}

void PeerListRow::paintAdminRank(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected) {
}

void PeerListRow::stopLastRipple() {
	if (_ripple) {
		_ripple->lastStop();
	}
}

void PeerListRow::paintRipple(Painter &p, int x, int y, int outerWidth) {
	if (_ripple) {
		_ripple->paint(p, x, y, outerWidth);
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}
}

void PeerListRow::paintUserpic(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int outerWidth) {
	if (_disabledState == State::DisabledChecked) {
		paintDisabledCheckUserpic(p, st, x, y, outerWidth);
	} else if (_checkbox) {
		_checkbox->paint(p, x, y, outerWidth);
	} else if (const auto callback = generatePaintUserpicCallback()) {
		callback(p, x, y, outerWidth, st.photoSize);
	}
}

// Emulates Ui::RoundImageCheckbox::paint() in a checked state.
void PeerListRow::paintDisabledCheckUserpic(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int outerWidth) const {
	auto userpicRadius = st.checkbox.imageSmallRadius;
	auto userpicShift = st.checkbox.imageRadius - userpicRadius;
	auto userpicDiameter = st.checkbox.imageRadius * 2;
	auto userpicLeft = x + userpicShift;
	auto userpicTop = y + userpicShift;
	auto userpicEllipse = style::rtlrect(x, y, userpicDiameter, userpicDiameter, outerWidth);
	auto userpicBorderPen = st.disabledCheckFg->p;
	userpicBorderPen.setWidth(st.checkbox.selectWidth);

	auto iconDiameter = st.checkbox.check.size;
	auto iconLeft = x + userpicDiameter + st.checkbox.selectWidth - iconDiameter;
	auto iconTop = y + userpicDiameter + st.checkbox.selectWidth - iconDiameter;
	auto iconEllipse = style::rtlrect(iconLeft, iconTop, iconDiameter, iconDiameter, outerWidth);
	auto iconBorderPen = st.checkbox.check.border->p;
	iconBorderPen.setWidth(st.checkbox.selectWidth);

	if (_isSavedMessagesChat) {
		Ui::EmptyUserpic::PaintSavedMessages(p, userpicLeft, userpicTop, outerWidth, userpicRadius * 2);
	} else if (_isRepliesMessagesChat) {
		Ui::EmptyUserpic::PaintRepliesMessages(p, userpicLeft, userpicTop, outerWidth, userpicRadius * 2);
	} else {
		peer()->paintUserpicLeft(p, _userpic, userpicLeft, userpicTop, outerWidth, userpicRadius * 2);
	}

	{
		PainterHighQualityEnabler hq(p);

		p.setPen(userpicBorderPen);
		p.setBrush(Qt::NoBrush);
		switch (cUserpicCornersType()) {
			case 0:
				p.drawRoundedRect(
					userpicEllipse,
					0, 0);
				break;

			case 1:
				p.drawRoundedRect(
					userpicEllipse,
					st::buttonRadius, st::buttonRadius);
				break;

			case 2:
				p.drawRoundedRect(
					userpicEllipse,
					st::dateRadius, st::dateRadius);
				break;

			default:
				p.drawEllipse(userpicEllipse);
		}

		p.setPen(iconBorderPen);
		p.setBrush(st.disabledCheckFg);
		p.drawEllipse(iconEllipse);
	}

	st.checkbox.check.check.paint(p, iconEllipse.topLeft(), outerWidth);
}

void PeerListRow::setStatusText(const QString &text) {
	_status.setText(st::defaultTextStyle, text, Ui::NameTextOptions());
}

float64 PeerListRow::checkedRatio() {
	return _checkbox ? _checkbox->checkedAnimationRatio() : 0.;
}

void PeerListRow::lazyInitialize(const style::PeerListItem &st) {
	if (_initialized) {
		return;
	}
	_initialized = true;
	refreshName(st);
	refreshStatus();
}

void PeerListRow::createCheckbox(
		const style::RoundImageCheckbox &st,
		Fn<void()> updateCallback) {
	_checkbox = std::make_unique<Ui::RoundImageCheckbox>(
		st,
		std::move(updateCallback),
		generatePaintUserpicCallback());
}

void PeerListRow::setCheckedInternal(bool checked, anim::type animated) {
	Expects(_checkbox != nullptr);

	_checkbox->setChecked(checked, animated);
}

void PeerListRow::finishCheckedAnimation() {
	_checkbox->setChecked(_checkbox->checked(), anim::type::instant);
}

PeerListContent::PeerListContent(
	QWidget *parent,
	not_null<PeerListController*> controller)
: RpWidget(parent)
, _st(controller->computeListSt())
, _controller(controller)
, _rowHeight(_st.item.height) {
	_controller->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	using UpdateFlag = Data::PeerUpdate::Flag;
	_controller->session().changes().peerUpdates(
		UpdateFlag::Name | UpdateFlag::Photo
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		if (update.flags & UpdateFlag::Name) {
			handleNameChanged(update.peer);
		}
		if (update.flags & UpdateFlag::Photo) {
			this->update();
		}
	}, lifetime());

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		invalidatePixmapsCache();
	}, lifetime());

	_repaintByStatus.setCallback([this] { update(); });
}

void PeerListContent::setMode(Mode mode) {
	if (mode == Mode::Default && _mode == Mode::Default) {
		return;
	}
	_mode = mode;
	switch (_mode) {
	case Mode::Default:
		_rowHeight = _st.item.height;
		break;
	case Mode::Custom:
		_rowHeight = _controller->customRowHeight();
		break;
	}
	const auto wasMouseSelection = _mouseSelection;
	const auto wasLastMousePosition = _lastMousePosition;
	_contextMenu = nullptr;
	if (wasMouseSelection) {
		setSelected(Selected());
	}
	setPressed(Selected());
	refreshRows();
	if (wasMouseSelection && wasLastMousePosition) {
		selectByMouse(*wasLastMousePosition);
	}
}

void PeerListContent::appendRow(std::unique_ptr<PeerListRow> row) {
	Expects(row != nullptr);

	if (_rowsById.find(row->id()) == _rowsById.cend()) {
		row->setAbsoluteIndex(_rows.size());
		addRowEntry(row.get());
		if (!_hiddenRows.empty()) {
			Assert(!row->hidden());
			_filterResults.push_back(row.get());
		}
		_rows.push_back(std::move(row));
	}
}

void PeerListContent::appendSearchRow(std::unique_ptr<PeerListRow> row) {
	Expects(row != nullptr);
	Expects(showingSearch());

	if (_rowsById.find(row->id()) == _rowsById.cend()) {
		row->setAbsoluteIndex(_searchRows.size());
		row->setIsSearchResult(true);
		addRowEntry(row.get());
		_filterResults.push_back(row.get());
		_searchRows.push_back(std::move(row));
	}
}

void PeerListContent::appendFoundRow(not_null<PeerListRow*> row) {
	Expects(showingSearch());

	auto index = findRowIndex(row);
	if (index.value < 0) {
		_filterResults.push_back(row);
	}
}

void PeerListContent::changeCheckState(
		not_null<PeerListRow*> row,
		bool checked,
		anim::type animated) {
	row->setChecked(
		checked,
		_st.item.checkbox,
		animated,
		[=] { updateRow(row); });
}

void PeerListContent::setRowHidden(not_null<PeerListRow*> row, bool hidden) {
	Expects(!row->isSearchResult());

	row->setHidden(hidden);
	if (hidden) {
		_hiddenRows.emplace(row);
	} else {
		_hiddenRows.remove(row);
	}
}

void PeerListContent::addRowEntry(not_null<PeerListRow*> row) {
	if (_controller->respectSavedMessagesChat() && !row->special()) {
		if (row->peer()->isSelf()) {
			row->setIsSavedMessagesChat(true);
		} else if (row->peer()->isRepliesChat()) {
			row->setIsRepliesMessagesChat(true);
		}
	}
	_rowsById.emplace(row->id(), row);
	if (!row->special()) {
		_rowsByPeer[row->peer()].push_back(row);
	}
	if (addingToSearchIndex()) {
		addToSearchIndex(row);
	}
	if (_controller->isRowSelected(row)) {
		Assert(row->special() || row->id() == row->peer()->id.value);
		changeCheckState(row, true, anim::type::instant);
	}
}

void PeerListContent::invalidatePixmapsCache() {
	auto invalidate = [](auto &&row) { row->invalidatePixmapsCache(); };
	ranges::for_each(_rows, invalidate);
	ranges::for_each(_searchRows, invalidate);
}

bool PeerListContent::addingToSearchIndex() const {
	// If we started indexing already, we continue.
	return (_searchMode != PeerListSearchMode::Disabled) || !_searchIndex.empty();
}

void PeerListContent::addToSearchIndex(not_null<PeerListRow*> row) {
	if (row->isSearchResult() || row->special()) {
		return;
	}

	removeFromSearchIndex(row);
	row->setNameFirstLetters(row->peer()->nameFirstLetters());
	for (auto ch : row->nameFirstLetters()) {
		_searchIndex[ch].push_back(row);
	}
}

void PeerListContent::removeFromSearchIndex(not_null<PeerListRow*> row) {
	const auto &nameFirstLetters = row->nameFirstLetters();
	if (!nameFirstLetters.empty()) {
		for (auto ch : row->nameFirstLetters()) {
			auto it = _searchIndex.find(ch);
			if (it != _searchIndex.cend()) {
				auto &entry = it->second;
				entry.erase(ranges::remove(entry, row), end(entry));
				if (entry.empty()) {
					_searchIndex.erase(it);
				}
			}
		}
		row->setNameFirstLetters({});
	}
}

void PeerListContent::prependRow(std::unique_ptr<PeerListRow> row) {
	Expects(row != nullptr);

	if (_rowsById.find(row->id()) == _rowsById.cend()) {
		addRowEntry(row.get());
		if (!_hiddenRows.empty()) {
			Assert(!row->hidden());
			_filterResults.insert(_filterResults.begin(), row.get());
		}
		_rows.insert(_rows.begin(), std::move(row));
		refreshIndices();
	}
}

void PeerListContent::prependRowFromSearchResult(not_null<PeerListRow*> row) {
	if (!row->isSearchResult()) {
		return;
	}
	Assert(_rowsById.find(row->id()) != _rowsById.cend());
	auto index = row->absoluteIndex();
	Assert(index >= 0 && index < _searchRows.size());
	Assert(_searchRows[index].get() == row);

	row->setIsSearchResult(false);
	if (!_hiddenRows.empty()) {
		Assert(!row->hidden());
		_filterResults.insert(_filterResults.begin(), row);
	}
	_rows.insert(_rows.begin(), std::move(_searchRows[index]));
	refreshIndices();
	removeRowAtIndex(_searchRows, index);

	if (addingToSearchIndex()) {
		addToSearchIndex(row);
	}
}

void PeerListContent::refreshIndices() {
	auto index = 0;
	for (auto &row : _rows) {
		row->setAbsoluteIndex(index++);
	}
}

void PeerListContent::removeRowAtIndex(
		std::vector<std::unique_ptr<PeerListRow>> &from,
		int index) {
	from.erase(from.begin() + index);
	for (auto i = index, count = int(from.size()); i != count; ++i) {
		from[i]->setAbsoluteIndex(i);
	}
}

PeerListRow *PeerListContent::findRow(PeerListRowId id) {
	auto it = _rowsById.find(id);
	return (it == _rowsById.cend()) ? nullptr : it->second.get();
}

void PeerListContent::removeRow(not_null<PeerListRow*> row) {
	auto index = row->absoluteIndex();
	auto isSearchResult = row->isSearchResult();
	auto &eraseFrom = isSearchResult ? _searchRows : _rows;

	Assert(index >= 0 && index < eraseFrom.size());
	Assert(eraseFrom[index].get() == row);

	auto pressedData = saveSelectedData(_pressed);
	auto contextedData = saveSelectedData(_contexted);
	setSelected(Selected());
	setPressed(Selected());
	setContexted(Selected());

	_rowsById.erase(row->id());
	if (!row->special()) {
		auto &byPeer = _rowsByPeer[row->peer()];
		byPeer.erase(ranges::remove(byPeer, row), end(byPeer));
	}
	removeFromSearchIndex(row);
	_filterResults.erase(
		ranges::remove(_filterResults, row),
		end(_filterResults));
	_hiddenRows.remove(row);
	removeRowAtIndex(eraseFrom, index);

	restoreSelection();
	setPressed(restoreSelectedData(pressedData));
	setContexted(restoreSelectedData(contextedData));
}

void PeerListContent::clearAllContent() {
	setSelected(Selected());
	setPressed(Selected());
	setContexted(Selected());
	_mouseSelection = false;
	_lastMousePosition = std::nullopt;
	_rowsById.clear();
	_rowsByPeer.clear();
	_filterResults.clear();
	_searchIndex.clear();
	_rows.clear();
	_searchRows.clear();
	_searchQuery
		= _normalizedSearchQuery
		= _mentionHighlight
		= QString();
}

void PeerListContent::convertRowToSearchResult(not_null<PeerListRow*> row) {
	if (row->isSearchResult()) {
		return;
	} else if (!showingSearch() || !_controller->hasComplexSearch()) {
		return removeRow(row);
	}
	auto index = row->absoluteIndex();
	Assert(index >= 0 && index < _rows.size());
	Assert(_rows[index].get() == row);

	removeFromSearchIndex(row);
	row->setIsSearchResult(true);
	row->setHidden(false);
	row->setAbsoluteIndex(_searchRows.size());
	_hiddenRows.remove(row);
	_searchRows.push_back(std::move(_rows[index]));
	removeRowAtIndex(_rows, index);
}

int PeerListContent::fullRowsCount() const {
	return _rows.size();
}

not_null<PeerListRow*> PeerListContent::rowAt(int index) const {
	Expects(index >= 0 && index < _rows.size());

	return _rows[index].get();
}

void PeerListContent::setDescription(object_ptr<Ui::FlatLabel> description) {
	_description = std::move(description);
	if (_description) {
		_description->setParent(this);
	}
}

void PeerListContent::setSearchLoading(object_ptr<Ui::FlatLabel> loading) {
	_searchLoading = std::move(loading);
	if (_searchLoading) {
		_searchLoading->setParent(this);
	}
}

void PeerListContent::setSearchNoResults(object_ptr<Ui::FlatLabel> noResults) {
	_searchNoResults = std::move(noResults);
	if (_searchNoResults) {
		_searchNoResults->setParent(this);
	}
}

void PeerListContent::setAboveWidget(object_ptr<TWidget> widget) {
	_aboveWidget = std::move(widget);
	if (_aboveWidget) {
		_aboveWidget->setParent(this);
	}
}

void PeerListContent::setAboveSearchWidget(object_ptr<TWidget> widget) {
	_aboveSearchWidget = std::move(widget);
	if (_aboveSearchWidget) {
		_aboveSearchWidget->setParent(this);
	}
}

void PeerListContent::setHideEmpty(bool hide) {
	_hideEmpty = hide;
	resizeToWidth(width());
}

void PeerListContent::setBelowWidget(object_ptr<TWidget> widget) {
	_belowWidget = std::move(widget);
	if (_belowWidget) {
		_belowWidget->setParent(this);
	}
}

int PeerListContent::labelHeight() const {
	if (_hideEmpty && !shownRowsCount()) {
		return 0;
	}
	auto computeLabelHeight = [](auto &label) {
		if (!label) {
			return 0;
		}
		return st::membersAboutLimitPadding.top() + label->height() + st::membersAboutLimitPadding.bottom();
	};
	if (showingSearch()) {
		if (!_filterResults.empty()) {
			return 0;
		}
		if (_controller->isSearchLoading()) {
			return computeLabelHeight(_searchLoading);
		}
		return computeLabelHeight(_searchNoResults);
	}
	return computeLabelHeight(_description);
}

void PeerListContent::refreshRows() {
	if (!_hiddenRows.empty()) {
		_filterResults.clear();
		for (const auto &row : _rows) {
			if (!row->hidden()) {
				_filterResults.push_back(row.get());
			}
		}
	}
	resizeToWidth(width());
	if (_visibleBottom > 0) {
		checkScrollForPreload();
	}
	if (_mouseSelection) {
		selectByMouse(QCursor::pos());
	}
	update();
}

void PeerListContent::setSearchMode(PeerListSearchMode mode) {
	if (_searchMode != mode) {
		if (!addingToSearchIndex()) {
			for (const auto &row : _rows) {
				addToSearchIndex(row.get());
			}
		}
		_searchMode = mode;
		if (_controller->hasComplexSearch()) {
			if (!_searchLoading) {
				setSearchLoading(object_ptr<Ui::FlatLabel>(
					this,
					tr::lng_contacts_loading(tr::now),
					st::membersAbout));
			}
		} else {
			clearSearchRows();
		}
	}
}

void PeerListContent::clearSearchRows() {
	while (!_searchRows.empty()) {
		removeRow(_searchRows.back().get());
	}
}

void PeerListContent::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto clip = e->rect();
	if (_mode != Mode::Custom) {
		p.fillRect(clip, _st.item.button.textBg);
	}

	const auto repaintByStatusAfter = _repaintByStatus.remainingTime();
	auto repaintAfterMin = repaintByStatusAfter;

	const auto rowsTopCached = rowsTop();
	const auto now = crl::now();
	const auto yFrom = clip.y() - rowsTopCached;
	const auto yTo = clip.y() + clip.height() - rowsTopCached;
	p.translate(0, rowsTopCached);
	const auto count = shownRowsCount();
	if (count > 0) {
		const auto from = floorclamp(yFrom, _rowHeight, 0, count);
		const auto to = ceilclamp(yTo, _rowHeight, 0, count);
		p.translate(0, from * _rowHeight);
		for (auto index = from; index != to; ++index) {
			const auto repaintAfter = paintRow(p, now, RowIndex(index));
			if (repaintAfter > 0
				&& (repaintAfterMin < 0
					|| repaintAfterMin > repaintAfter)) {
				repaintAfterMin = repaintAfter;
			}
			p.translate(0, _rowHeight);
		}
	}
	if (repaintAfterMin != repaintByStatusAfter) {
		Assert(repaintAfterMin >= 0);
		_repaintByStatus.callOnce(repaintAfterMin);
	}
}

int PeerListContent::resizeGetHeight(int newWidth) {
	const auto rowsCount = shownRowsCount();
	const auto hideAll = !rowsCount && _hideEmpty;
	_aboveHeight = 0;
	if (_aboveWidget) {
		_aboveWidget->resizeToWidth(newWidth);
		_aboveWidget->moveToLeft(0, 0, newWidth);
		if (hideAll || showingSearch()) {
			_aboveWidget->hide();
		} else {
			_aboveWidget->show();
			_aboveHeight = _aboveWidget->height();
		}
	}
	if (_aboveSearchWidget) {
		_aboveSearchWidget->resizeToWidth(newWidth);
		_aboveSearchWidget->moveToLeft(0, 0, newWidth);
		if (hideAll || !showingSearch()) {
			_aboveSearchWidget->hide();
		} else {
			_aboveSearchWidget->show();
			_aboveHeight = _aboveSearchWidget->height();
		}
	}
	const auto labelTop = rowsTop()
		+ std::max(
			shownRowsCount() * _rowHeight,
			_controller->descriptionTopSkipMin());
	const auto labelWidth = newWidth - 2 * st::contactsPadding.left();
	if (_description) {
		_description->resizeToWidth(labelWidth);
		_description->moveToLeft(st::contactsPadding.left(), labelTop + st::membersAboutLimitPadding.top(), newWidth);
		_description->setVisible(!hideAll && !showingSearch());
	}
	if (_searchNoResults) {
		_searchNoResults->resizeToWidth(labelWidth);
		_searchNoResults->moveToLeft(st::contactsPadding.left(), labelTop + st::membersAboutLimitPadding.top(), newWidth);
		_searchNoResults->setVisible(!hideAll && showingSearch() && _filterResults.empty() && !_controller->isSearchLoading());
	}
	if (_searchLoading) {
		_searchLoading->resizeToWidth(labelWidth);
		_searchLoading->moveToLeft(st::contactsPadding.left(), labelTop + st::membersAboutLimitPadding.top(), newWidth);
		_searchLoading->setVisible(!hideAll && showingSearch() && _filterResults.empty() && _controller->isSearchLoading());
	}
	const auto label = labelHeight();
	const auto belowTop = (label > 0 || rowsCount > 0)
		? (labelTop + label + _st.padding.bottom())
		: _aboveHeight;
	_belowHeight = 0;
	if (_belowWidget) {
		_belowWidget->resizeToWidth(newWidth);
		_belowWidget->moveToLeft(0, belowTop, newWidth);
		if (hideAll || showingSearch()) {
			_belowWidget->hide();
		} else {
			_belowWidget->show();
			_belowHeight = _belowWidget->height();
		}
	}
	return belowTop + _belowHeight;
}

void PeerListContent::enterEventHook(QEnterEvent *e) {
	setMouseTracking(true);
}

void PeerListContent::leaveEventHook(QEvent *e) {
	setMouseTracking(false);
	mouseLeftGeometry();
}

void PeerListContent::mouseMoveEvent(QMouseEvent *e) {
	handleMouseMove(e->globalPos());
}

void PeerListContent::handleMouseMove(QPoint globalPosition) {
	if (!_lastMousePosition) {
		_lastMousePosition = globalPosition;
		return;
	} else if (!_mouseSelection
		&& *_lastMousePosition == globalPosition) {
		return;
	}
	selectByMouse(globalPosition);
}

void PeerListContent::mousePressEvent(QMouseEvent *e) {
	_pressButton = e->button();
	selectByMouse(e->globalPos());
	setPressed(_selected);
	if (auto row = getRow(_selected.index)) {
		auto updateCallback = [this, row, hint = _selected.index] {
			updateRow(row, hint);
		};
		if (_selected.element) {
			const auto elementRect = getElementRect(
				row,
				_selected.index,
				_selected.element);
			if (!elementRect.isEmpty()) {
				row->elementAddRipple(
					_selected.element,
					mapFromGlobal(QCursor::pos()) - elementRect.topLeft(),
					std::move(updateCallback));
			}
		} else {
			auto point = mapFromGlobal(QCursor::pos()) - QPoint(0, getRowTop(_selected.index));
			if (_mode == Mode::Custom) {
				row->addRipple(_st.item, _controller->customRowRippleMaskGenerator(), point, std::move(updateCallback));
			} else {
				const auto maskGenerator = [&] {
					return Ui::RippleAnimation::rectMask(
						QSize(width(), _rowHeight));
				};
				row->addRipple(_st.item, maskGenerator, point, std::move(updateCallback));
			}
		}
	}
	if (anim::Disabled() && !_selected.element) {
		mousePressReleased(e->button());
	}
}

void PeerListContent::mouseReleaseEvent(QMouseEvent *e) {
	mousePressReleased(e->button());
}

void PeerListContent::mousePressReleased(Qt::MouseButton button) {
	updateRow(_pressed.index);
	updateRow(_selected.index);

	auto pressed = _pressed;
	setPressed(Selected());
	if (button == Qt::LeftButton && pressed == _selected) {
		if (auto row = getRow(pressed.index)) {
			if (pressed.element) {
				_controller->rowElementClicked(row, pressed.element);
			} else {
				_controller->rowClicked(row);
			}
		}
	}
}

void PeerListContent::showRowMenu(
		not_null<PeerListRow*> row,
		bool highlightRow,
		Fn<void(not_null<Ui::PopupMenu*>)> destroyed) {
	const auto index = findRowIndex(row);
	showRowMenu(
		index,
		row,
		QCursor::pos(),
		highlightRow,
		std::move(destroyed));
}

bool PeerListContent::showRowMenu(
		RowIndex index,
		PeerListRow *row,
		QPoint globalPos,
		bool highlightRow,
		Fn<void(not_null<Ui::PopupMenu*>)> destroyed) {
	if (_contextMenu) {
		_contextMenu->setDestroyedCallback(nullptr);
		_contextMenu = nullptr;
	}
	setContexted(Selected());
	if (_pressButton != Qt::LeftButton) {
		mousePressReleased(_pressButton);
	}

	if (highlightRow) {
		row = getRow(index);
	}
	if (!row) {
		return false;
	}

	_contextMenu = _controller->rowContextMenu(this, row);
	const auto raw = _contextMenu.get();
	if (!raw) {
		return false;
	}

	if (highlightRow) {
		setContexted({ index, false });
	}
	raw->setDestroyedCallback(crl::guard(
		this,
		[=] {
			if (highlightRow) {
				setContexted(Selected());
			}
			handleMouseMove(QCursor::pos());
			if (destroyed) {
				destroyed(raw);
			}
		}));
	raw->popup(globalPos);
	return true;
}

void PeerListContent::contextMenuEvent(QContextMenuEvent *e) {
	if (e->reason() == QContextMenuEvent::Mouse) {
		handleMouseMove(e->globalPos());
	}
	if (showRowMenu(_selected.index, nullptr, e->globalPos(), true)) {
		e->accept();
	}
}

void PeerListContent::setPressed(Selected pressed) {
	if (_pressed == pressed) {
		return;
	} else if (const auto row = getRow(_pressed.index)) {
		row->stopLastRipple();
		row->elementsStopLastRipple();
	}
	_pressed = pressed;
}

crl::time PeerListContent::paintRow(
		Painter &p,
		crl::time now,
		RowIndex index) {
	const auto row = getRow(index);
	Assert(row != nullptr);

	row->lazyInitialize(_st.item);
	const auto outerWidth = width();

	auto refreshStatusAt = row->refreshStatusTime();
	if (refreshStatusAt > 0 && now >= refreshStatusAt) {
		row->refreshStatus();
		refreshStatusAt = row->refreshStatusTime();
	}
	const auto refreshStatusIn = (refreshStatusAt > 0)
		? std::max(refreshStatusAt - now, crl::time(1))
		: 0;

	const auto peer = row->special() ? nullptr : row->peer().get();
	const auto active = (_contexted.index.value >= 0)
		? _contexted
		: (_pressed.index.value >= 0)
		? _pressed
		: _selected;
	const auto selected = (active.index == index)
		&& (!active.element || !row->elementOnlySelect(active.element));

	if (_mode == Mode::Custom) {
		_controller->customRowPaint(p, now, row, selected);
		return refreshStatusIn;
	}

	const auto &bg = selected
		? _st.item.button.textBgOver
		: _st.item.button.textBg;
	p.fillRect(0, 0, outerWidth, _rowHeight, bg);
	row->paintRipple(p, 0, 0, outerWidth);
	row->paintUserpic(
		p,
		_st.item,
		_st.item.photoPosition.x(),
		_st.item.photoPosition.y(),
		outerWidth);

	p.setPen(st::contactsNameFg);

	auto skipRight = _st.item.photoPosition.x();
	auto rightActionSize = !row->rightActionSize().isEmpty()
						&& (row->placeholderSize().isEmpty() 
							|| selected)
						? row->rightActionSize()
						: row->placeholderSize();
	auto rightActionMargins = rightActionSize.isEmpty() ? QMargins() : row->rightActionMargins();
	auto &name = row->name();
	auto namex = _st.item.namePosition.x();
	auto namew = outerWidth - namex - skipRight;
	auto statusw = namew;
	if (!rightActionSize.isEmpty()) {
		statusw -= rightActionMargins.left()
			+ rightActionSize.width()
			+ rightActionMargins.right()
			- skipRight;
	}
	if (auto iconWidth = row->nameIconWidth()) {
		namew -= iconWidth;
		row->paintNameIcon(
			p,
			namex + qMin(name.maxWidth(), namew),
			_st.item.namePosition.y(),
			width(),
			selected);
	}
	if (auto adminRankWidth = row->adminRankWidth()) {
		namew -= adminRankWidth + skipRight;
		auto rankx = width() - adminRankWidth - skipRight;
		p.setFont(st::normalFont);
		row->paintAdminRank(
			p,
			rankx,
			_st.item.namePosition.y(),
			width(),
			selected);
	}
	auto nameCheckedRatio = row->disabled() ? 0. : row->checkedRatio();
	p.setPen(anim::pen(_st.item.nameFg, _st.item.nameFgChecked, nameCheckedRatio));
	name.drawLeftElided(p, namex, _st.item.namePosition.y(), namew, width());

	p.setFont(st::contactsStatusFont);
	if (row->isSearchResult()
		&& !_mentionHighlight.isEmpty()
		&& peer
		&& peer->userName().startsWith(
			_mentionHighlight,
			Qt::CaseInsensitive)) {
		const auto username = peer->userName();
		const auto availableWidth = statusw;
		auto highlightedPart = '@' + username.mid(0, _mentionHighlight.size());
		auto grayedPart = username.mid(_mentionHighlight.size());
		const auto highlightedWidth = st::contactsStatusFont->width(highlightedPart);
		if (highlightedWidth >= availableWidth || grayedPart.isEmpty()) {
			if (highlightedWidth > availableWidth) {
				highlightedPart = st::contactsStatusFont->elided(highlightedPart, availableWidth);
			}
			p.setPen(_st.item.statusFgActive);
			p.drawTextLeft(_st.item.statusPosition.x(), _st.item.statusPosition.y(), width(), highlightedPart);
		} else {
			grayedPart = st::contactsStatusFont->elided(grayedPart, availableWidth - highlightedWidth);
			p.setPen(_st.item.statusFgActive);
			p.drawTextLeft(_st.item.statusPosition.x(), _st.item.statusPosition.y(), width(), highlightedPart);
			p.setPen(selected ? _st.item.statusFgOver : _st.item.statusFg);
			p.drawTextLeft(_st.item.statusPosition.x() + highlightedWidth, _st.item.statusPosition.y(), width(), grayedPart);
		}
	} else {
		row->paintStatusText(p, _st.item, _st.item.statusPosition.x(), _st.item.statusPosition.y(), statusw, width(), selected);
	}

	row->elementsPaint(
		p,
		width(),
		selected,
		(active.index == index) ? active.element : 0);

	return refreshStatusIn;
}

PeerListContent::SkipResult PeerListContent::selectSkip(int direction) {
	if (hasPressed()) {
		return { _selected.index.value, _selected.index.value };
	}
	_mouseSelection = false;
	_lastMousePosition = std::nullopt;

	auto newSelectedIndex = _selected.index.value + direction;

	auto result = SkipResult();
	result.shouldMoveTo = newSelectedIndex;

	auto rowsCount = shownRowsCount();
	auto index = 0;
	auto firstEnabled = -1, lastEnabled = -1;
	enumerateShownRows([&firstEnabled, &lastEnabled, &index](not_null<PeerListRow*> row) {
		if (!row->disabled()) {
			if (firstEnabled < 0) {
				firstEnabled = index;
			}
			lastEnabled = index;
		}
		++index;
		return true;
	});
	if (firstEnabled < 0) {
		firstEnabled = rowsCount;
		lastEnabled = firstEnabled - 1;
	}

	Assert(lastEnabled < rowsCount);
	Assert(firstEnabled - 1 <= lastEnabled);

	// Always pass through the first enabled item when changing from / to none selected.
	if ((_selected.index.value > firstEnabled && newSelectedIndex < firstEnabled)
		|| (_selected.index.value < firstEnabled && newSelectedIndex > firstEnabled)) {
		newSelectedIndex = firstEnabled;
	}

	// Snap the index.
	newSelectedIndex = std::clamp(
		newSelectedIndex,
		firstEnabled - 1,
		lastEnabled);

	// Skip the disabled rows.
	if (newSelectedIndex < firstEnabled) {
		newSelectedIndex = -1;
	} else if (newSelectedIndex > lastEnabled) {
		newSelectedIndex = lastEnabled;
	} else if (getRow(RowIndex(newSelectedIndex))->disabled()) {
		auto delta = (direction > 0) ? 1 : -1;
		for (newSelectedIndex += delta; ; newSelectedIndex += delta) {
			// We must find an enabled row, firstEnabled <= us <= lastEnabled.
			Assert(newSelectedIndex >= 0 && newSelectedIndex < rowsCount);
			if (!getRow(RowIndex(newSelectedIndex))->disabled()) {
				break;
			}
		}
	}

	_selected.index.value = newSelectedIndex;
	_selected.element = 0;
	if (newSelectedIndex >= 0) {
		auto top = (newSelectedIndex > 0) ? getRowTop(RowIndex(newSelectedIndex)) : 0;
		auto bottom = (newSelectedIndex + 1 < rowsCount) ? getRowTop(RowIndex(newSelectedIndex + 1)) : height();
		_scrollToRequests.fire({ top, bottom });
	}

	update();

	_selectedIndex = _selected.index.value;
	result.reallyMovedTo = _selected.index.value;
	return result;
}

void PeerListContent::selectSkipPage(int height, int direction) {
	auto rowsToSkip = height / _rowHeight;
	if (!rowsToSkip) {
		return;
	}
	selectSkip(rowsToSkip * direction);
}

rpl::producer<int> PeerListContent::selectedIndexValue() const {
	return _selectedIndex.value();
}

bool PeerListContent::hasSelection() const {
	return _selected.index.value >= 0;
}

bool PeerListContent::hasPressed() const {
	return _pressed.index.value >= 0;
}

void PeerListContent::clearSelection() {
	setSelected(Selected());
}

void PeerListContent::mouseLeftGeometry() {
	if (_mouseSelection) {
		setSelected(Selected());
		_mouseSelection = false;
		_lastMousePosition = std::nullopt;
	}
}

void PeerListContent::loadProfilePhotos() {
	if (_visibleTop >= _visibleBottom) return;

	auto yFrom = _visibleTop;
	auto yTo = _visibleBottom + (_visibleBottom - _visibleTop) * PreloadHeightsCount;

	if (yTo < 0) return;
	if (yFrom < 0) yFrom = 0;

	auto rowsCount = shownRowsCount();
	if (rowsCount > 0) {
		auto from = yFrom / _rowHeight;
		if (from < 0) from = 0;
		if (from < rowsCount) {
			auto to = (yTo / _rowHeight) + 1;
			if (to > rowsCount) to = rowsCount;

			for (auto index = from; index != to; ++index) {
				const auto row = getRow(RowIndex(index));
				if (!row->special()) {
					row->peer()->loadUserpic();
				}
			}
		}
	}
}

void PeerListContent::checkScrollForPreload() {
	if (_visibleBottom + PreloadHeightsCount * (_visibleBottom - _visibleTop) >= height()) {
		_controller->loadMoreRows();
	}
}

void PeerListContent::searchQueryChanged(QString query) {
	const auto searchWordsList = TextUtilities::PrepareSearchWords(query);
	const auto normalizedQuery = searchWordsList.join(' ');
	if (_normalizedSearchQuery != normalizedQuery) {
		setSearchQuery(query, normalizedQuery);
		if (_controller->searchInLocal() && !searchWordsList.isEmpty()) {
			Assert(_hiddenRows.empty());

			auto minimalList = (const std::vector<not_null<PeerListRow*>>*)nullptr;
			for (const auto &searchWord : searchWordsList) {
				auto searchWordStart = searchWord[0].toLower();
				auto it = _searchIndex.find(searchWordStart);
				if (it == _searchIndex.cend()) {
					// Some word can't be found in any row.
					minimalList = nullptr;
					break;
				} else if (!minimalList || minimalList->size() > it->second.size()) {
					minimalList = &it->second;
				}
			}
			if (minimalList) {
				auto searchWordInNames = [](
						not_null<PeerData*> peer,
						const QString &searchWord) {
					for (auto &nameWord : peer->nameWords()) {
						if (nameWord.startsWith(searchWord)) {
							return true;
						}
					}
					return false;
				};
				auto allSearchWordsInNames = [&](
						not_null<PeerData*> peer) {
					for (const auto &searchWord : searchWordsList) {
						if (!searchWordInNames(peer, searchWord)) {
							return false;
						}
					}
					return true;
				};

				_filterResults.reserve(minimalList->size());
				for (const auto &row : *minimalList) {
					if (!row->special() && allSearchWordsInNames(row->peer())) {
						_filterResults.push_back(row);
					}
				}
			}
		}
		if (_controller->hasComplexSearch()) {
			_controller->search(_searchQuery);
		}
		refreshRows();
	}
}

std::unique_ptr<PeerListState> PeerListContent::saveState() const {
	Expects(_hiddenRows.empty());

	auto result = std::make_unique<PeerListState>();
	result->controllerState
		= std::make_unique<PeerListController::SavedStateBase>();
	result->list.reserve(_rows.size());
	for (const auto &row : _rows) {
		result->list.push_back(row->peer());
	}
	result->filterResults.reserve(_filterResults.size());
	for (const auto &row : _filterResults) {
		result->filterResults.push_back(row->peer());
	}
	result->searchQuery = _searchQuery;
	return result;
}

void PeerListContent::restoreState(
		std::unique_ptr<PeerListState> state) {
	if (!state || !state->controllerState) {
		return;
	}

	clearAllContent();

	for (auto peer : state->list) {
		if (auto row = _controller->createRestoredRow(peer)) {
			appendRow(std::move(row));
		}
	}
	auto query = state->searchQuery;
	auto searchWords = TextUtilities::PrepareSearchWords(query);
	setSearchQuery(query, searchWords.join(' '));
	for (auto peer : state->filterResults) {
		if (auto existingRow = findRow(peer->id.value)) {
			_filterResults.push_back(existingRow);
		} else if (auto row = _controller->createSearchRow(peer)) {
			appendSearchRow(std::move(row));
		}
	}
	refreshRows();
}

void PeerListContent::setSearchQuery(
		const QString &query,
		const QString &normalizedQuery) {
	setSelected(Selected());
	setPressed(Selected());
	setContexted(Selected());
	_mouseSelection = false;
	_lastMousePosition = std::nullopt;
	_searchQuery = query;
	_normalizedSearchQuery = normalizedQuery;
	_mentionHighlight = _searchQuery.startsWith('@')
		? _searchQuery.mid(1)
		: _searchQuery;
	_filterResults.clear();
	clearSearchRows();
}

bool PeerListContent::submitted() {
	if (const auto row = getRow(_selected.index)) {
		_controller->rowClicked(row);
		return true;
	} else if (showingSearch()) {
		if (const auto row = getRow(RowIndex(0))) {
			_controller->rowClicked(row);
			return true;
		}
	}
	return false;
}

void PeerListContent::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	loadProfilePhotos();
	checkScrollForPreload();
}

void PeerListContent::setSelected(Selected selected) {
	updateRow(_selected.index);
	if (_selected == selected) {
		return;
	}
	_selected = selected;
	updateRow(_selected.index);
	setCursor(_selected.element ? style::cur_pointer : style::cur_default);

	_selectedIndex = _selected.index.value;
}

void PeerListContent::setContexted(Selected contexted) {
	updateRow(_contexted.index);
	if (_contexted != contexted) {
		_contexted = contexted;
		updateRow(_contexted.index);
	}
}

void PeerListContent::restoreSelection() {
	if (_mouseSelection) {
		selectByMouse(QCursor::pos());
	}
}

auto PeerListContent::saveSelectedData(Selected from)
-> SelectedSaved {
	if (auto row = getRow(from.index)) {
		return { row->id(), from };
	}
	return { PeerListRowId(0), from };
}

auto PeerListContent::restoreSelectedData(SelectedSaved from)
-> Selected {
	auto result = from.old;
	if (auto row = findRow(from.id)) {
		result.index = findRowIndex(row, result.index);
	} else {
		result.index.value = -1;
	}
	return result;
}

void PeerListContent::selectByMouse(QPoint globalPosition) {
	_mouseSelection = true;
	_lastMousePosition = globalPosition;
	const auto point = mapFromGlobal(globalPosition);
	const auto customMode = (_mode == Mode::Custom);
	auto in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(globalPosition));
	auto selected = Selected();
	auto rowsPointY = point.y() - rowsTop();
	selected.index.value = (in
		&& rowsPointY >= 0
		&& rowsPointY < shownRowsCount() * _rowHeight)
		? (rowsPointY / _rowHeight)
		: -1;
	if (selected.index.value >= 0) {
		const auto row = getRow(selected.index);
		if (row->disabled()
			|| (customMode
				&& !_controller->customRowSelectionPoint(
					row,
					point.x(),
					rowsPointY - (selected.index.value * _rowHeight)))) {
			selected = Selected();
		} else if (!customMode) {
			for (auto i = 0, count = row->elementsCount(); i != count; ++i) {
				const auto rect = getElementRect(row, selected.index, i + 1);
				if (rect.contains(point)) {
					selected.element = i + 1;
					break;
				}
			}
		}
	}
	setSelected(selected);
}

QRect PeerListContent::getElementRect(
		not_null<PeerListRow*> row,
		RowIndex index,
		int element) const {
	if (row->elementDisabled(element)) {
		return QRect();
	}
	const auto geometry = row->elementGeometry(element, width());
	if (geometry.isEmpty()) {
		return QRect();
	}
	return geometry.translated(0, getRowTop(index));
}

int PeerListContent::rowsTop() const {
	return _aboveHeight + _st.padding.top();
}

int PeerListContent::getRowTop(RowIndex index) const {
	if (index.value >= 0) {
		return rowsTop() + index.value * _rowHeight;
	}
	return -1;
}

void PeerListContent::updateRow(not_null<PeerListRow*> row, RowIndex hint) {
	updateRow(findRowIndex(row, hint));
}

void PeerListContent::updateRow(RowIndex index) {
	if (index.value < 0) {
		return;
	}
	if (const auto row = getRow(index); row && row->disabled()) {
		if (index == _selected.index) {
			setSelected(Selected());
		}
		if (index == _pressed.index) {
			setPressed(Selected());
		}
		if (index == _contexted.index) {
			setContexted(Selected());
		}
	}
	update(0, getRowTop(index), width(), _rowHeight);
}

template <typename Callback>
bool PeerListContent::enumerateShownRows(Callback callback) {
	return enumerateShownRows(0, shownRowsCount(), std::move(callback));
}

template <typename Callback>
bool PeerListContent::enumerateShownRows(int from, int to, Callback callback) {
	Assert(0 <= from);
	Assert(from <= to);
	if (showingSearch()) {
		Assert(to <= _filterResults.size());
		for (auto i = from; i != to; ++i) {
			if (!callback(_filterResults[i])) {
				return false;
			}
		}
	} else {
		Assert(to <= _rows.size());
		for (auto i = from; i != to; ++i) {
			if (!callback(_rows[i].get())) {
				return false;
			}
		}
	}
	return true;
}

PeerListRow *PeerListContent::getRow(RowIndex index) {
	if (index.value >= 0) {
		if (showingSearch()) {
			if (index.value < _filterResults.size()) {
				return _filterResults[index.value];
			}
		} else if (index.value < _rows.size()) {
			return _rows[index.value].get();
		}
	}
	return nullptr;
}

PeerListContent::RowIndex PeerListContent::findRowIndex(
		not_null<PeerListRow*> row,
		RowIndex hint) {
	if (!showingSearch()) {
		Assert(!row->isSearchResult());
		return RowIndex(row->absoluteIndex());
	}

	auto result = hint;
	if (getRow(result) == row) {
		return result;
	}

	auto count = shownRowsCount();
	for (result.value = 0; result.value != count; ++result.value) {
		if (getRow(result) == row) {
			return result;
		}
	}
	result.value = -1;
	return result;
}

void PeerListContent::handleNameChanged(not_null<PeerData*> peer) {
	auto byPeer = _rowsByPeer.find(peer);
	if (byPeer != _rowsByPeer.cend()) {
		for (auto row : byPeer->second) {
			if (addingToSearchIndex()) {
				addToSearchIndex(row);
			}
			row->refreshName(_st.item);
			updateRow(row);
		}
	}
}

PeerListContent::~PeerListContent() {
	if (_contextMenu) {
		_contextMenu->setDestroyedCallback(nullptr);
	}
}

void PeerListContentDelegate::peerListShowRowMenu(
		not_null<PeerListRow*> row,
		bool highlightRow,
		Fn<void(not_null<Ui::PopupMenu *>)> destroyed) {
	_content->showRowMenu(row, highlightRow, std::move(destroyed));
}
