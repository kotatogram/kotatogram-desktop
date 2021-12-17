/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/who_read_context_action.h"

#include "base/call_delayed.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/popup_menu.h"
#include "ui/effects/ripple_animation.h"
#include "ui/chat/group_call_userpics.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h"

namespace Ui {
namespace {

struct EntryData {
	QString text;
	QImage userpic;
	Fn<void()> callback;
};

class EntryAction final : public Menu::ItemBase {
public:
	EntryAction(
		not_null<RpWidget*> parent,
		const style::Menu &st,
		EntryData &&data);

	void setData(EntryData &&data);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

private:
	int contentHeight() const override;

	void paint(Painter &&p);

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const int _height = 0;

	Text::String _text;
	int _textWidth = 0;
	QImage _userpic;

};

class Action final : public Menu::ItemBase {
public:
	Action(
		not_null<PopupMenu*> parentMenu,
		rpl::producer<WhoReadContent> content,
		Fn<void(uint64)> participantChosen,
		int userpicsRadius);

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

	void handleKeyPress(not_null<QKeyEvent*> e) override;

protected:
	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	int contentHeight() const override;

private:
	void paint(Painter &p);

	void updateUserpicsFromContent();
	void resolveMinWidth();
	void refreshText();
	void refreshDimensions();
	void populateSubmenu();

	const not_null<PopupMenu*> _parentMenu;
	const not_null<QAction*> _dummyAction;
	const Fn<void(uint64)> _participantChosen;
	const std::unique_ptr<GroupCallUserpics> _userpics;
	const style::Menu &_st;

	std::vector<not_null<EntryAction*>> _submenuActions;

	Text::String _text;
	int _textWidth = 0;
	const int _height = 0;
	int _userpicsWidth = 0;
	bool _appeared = false;

	WhoReadContent _content;

};

TextParseOptions MenuTextOptions = {
	TextParseLinks | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

EntryAction::EntryAction(
	not_null<RpWidget*> parent,
	const style::Menu &st,
	EntryData &&data)
: ItemBase(parent, st)
, _dummyAction(CreateChild<QAction>(parent.get()))
, _st(st)
, _height(st::defaultWhoRead.photoSkip * 2 + st::defaultWhoRead.photoSize) {
	setAcceptBoth(true);

	initResizeHook(parent->sizeValue());
	setData(std::move(data));

	paintRequest(
	) | rpl::start_with_next([=] {
		paint(Painter(this));
	}, lifetime());

	enableMouseSelecting();
}

not_null<QAction*> EntryAction::action() const {
	return _dummyAction.get();
}

bool EntryAction::isEnabled() const {
	return true;
}

int EntryAction::contentHeight() const {
	return _height;
}

void EntryAction::setData(EntryData &&data) {
	setClickedCallback(std::move(data.callback));
	_userpic = std::move(data.userpic);
	_text.setMarkedText(_st.itemStyle, { data.text }, MenuTextOptions);
	const auto textWidth = _text.maxWidth();
	const auto &padding = _st.itemPadding;
	const auto goodWidth = st::defaultWhoRead.nameLeft
		+ textWidth
		+ padding.right();
	const auto w = std::clamp(goodWidth, _st.widthMin, _st.widthMax);
	_textWidth = w - (goodWidth - textWidth);
	setMinWidth(w);
	update();
}

void EntryAction::paint(Painter &&p) {
	const auto enabled = isEnabled();
	const auto selected = isSelected();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), _height, _st.itemBg);
	}
	p.fillRect(0, 0, width(), _height, selected ? _st.itemBgOver : _st.itemBg);
	if (enabled) {
		paintRipple(p, 0, 0);
	}
	const auto photoSize = st::defaultWhoRead.photoSize;
	const auto photoTop = (height() - photoSize) / 2;
	p.drawImage(st::defaultWhoRead.photoLeft, photoTop, _userpic);

	p.setPen(selected
		? _st.itemFgOver
		: enabled
		? _st.itemFg
		: _st.itemFgDisabled);
	_text.drawLeftElided(
		p,
		st::defaultWhoRead.nameLeft,
		(height() - _st.itemStyle.font->height) / 2,
		_textWidth,
		width());
}

Action::Action(
	not_null<PopupMenu*> parentMenu,
	rpl::producer<WhoReadContent> content,
	Fn<void(uint64)> participantChosen,
	int userpicsRadius)
: ItemBase(parentMenu->menu(), parentMenu->menu()->st())
, _parentMenu(parentMenu)
, _dummyAction(CreateChild<QAction>(parentMenu->menu().get()))
, _participantChosen(std::move(participantChosen))
, _userpics(std::make_unique<GroupCallUserpics>(
	st::defaultWhoRead.userpics,
	rpl::never<bool>(),
	[=] { update(); },
	userpicsRadius))
, _st(parentMenu->menu()->st())
, _height(st::defaultWhoRead.itemPadding.top()
		+ _st.itemStyle.font->height
		+ st::defaultWhoRead.itemPadding.bottom()) {
	const auto parent = parentMenu->menu();
	const auto checkAppeared = [=, now = crl::now()] {
		_appeared = (crl::now() - now) >= parentMenu->st().duration;
	};

	setAcceptBoth(true);
	initResizeHook(parent->sizeValue());
	resolveMinWidth();

	_userpics->widthValue(
	) | rpl::start_with_next([=](int width) {
		_userpicsWidth = width;
		refreshDimensions();
		update();
	}, lifetime());

	std::move(
		content
	) | rpl::start_with_next([=](WhoReadContent &&content) {
		checkAppeared();
		const auto changed = (_content.participants != content.participants)
			|| (_content.unknown != content.unknown);
		_content = content;
		if (changed) {
			PostponeCall(this, [=] { populateSubmenu(); });
		}
		updateUserpicsFromContent();
		refreshText();
		refreshDimensions();
		setPointerCursor(isEnabled());
		_dummyAction->setEnabled(isEnabled());
		if (!isEnabled()) {
			setSelected(false);
		}
		update();
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	clicks(
	) | rpl::start_with_next([=] {
		if (_content.participants.size() == 1) {
			if (const auto onstack = _participantChosen) {
				onstack(_content.participants.front().id);
			}
		}
	}, lifetime());

	enableMouseSelecting();

	base::call_delayed(parentMenu->st().duration, this, [=] {
		checkAppeared();
		updateUserpicsFromContent();
	});
}

void Action::resolveMinWidth() {
	const auto maxIconWidth = 0;
	const auto width = [&](const QString &text) {
		return _st.itemStyle.font->width(text);
	};
	const auto maxTextWidth = std::max({
		width(tr::lng_context_seen_text(tr::now, lt_count, 999)),
		width(tr::lng_context_seen_listened(tr::now, lt_count, 999)),
		width(tr::lng_context_seen_watched(tr::now, lt_count, 999)) });
	const auto maxWidth = st::defaultWhoRead.itemPadding.left()
		+ maxIconWidth
		+ maxTextWidth
		+ _userpics->maxWidth()
		+ st::defaultWhoRead.itemPadding.right();
	setMinWidth(maxWidth);
}

void Action::updateUserpicsFromContent() {
	if (!_appeared) {
		return;
	}
	auto users = std::vector<GroupCallUser>();
	if (!_content.participants.empty()) {
		const auto count = std::min(
			int(_content.participants.size()),
			WhoReadParticipant::kMaxSmallUserpics);
		const auto factor = style::DevicePixelRatio();
		users.reserve(count);
		for (auto i = 0; i != count; ++i) {
			auto &participant = _content.participants[i];
			participant.userpicSmall.setDevicePixelRatio(factor);
			users.push_back({
				.userpic = participant.userpicSmall,
				.userpicKey = participant.userpicKey,
				.id = participant.id,
			});
		}
	}
	_userpics->update(users, true);
}

void Action::populateSubmenu() {
	if (_content.participants.size() < 2) {
		_submenuActions.clear();
		_parentMenu->removeSubmenu(action());
		if (!isEnabled()) {
			setSelected(false);
		}
		return;
	}

	const auto submenu = _parentMenu->ensureSubmenu(action());
	if (_submenuActions.size() > _content.participants.size()) {
		_submenuActions.clear();
		submenu->clearActions();
	}
	auto index = 0;
	for (const auto &participant : _content.participants) {
		const auto chosen = [call = _participantChosen, id = participant.id] {
			call(id);
		};
		auto data = EntryData{
			.text = participant.name,
			.userpic = participant.userpicLarge,
			.callback = chosen,
		};
		if (index < _submenuActions.size()) {
			_submenuActions[index]->setData(std::move(data));
		} else {
			auto item = base::make_unique_q<EntryAction>(
				submenu->menu(),
				_st,
				std::move(data));
			_submenuActions.push_back(item.get());
			submenu->addAction(std::move(item));
		}
		++index;
	}
	_parentMenu->checkSubmenuShow();
}

void Action::paint(Painter &p) {
	const auto enabled = isEnabled();
	const auto selected = isSelected();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), _height, _st.itemBg);
	}
	p.fillRect(0, 0, width(), _height, selected ? _st.itemBgOver : _st.itemBg);
	if (enabled) {
		paintRipple(p, 0, 0);
	}
	const auto &icon = (_content.type == WhoReadType::Seen)
		? (!enabled
			? st::whoReadChecksDisabled
			: selected
			? st::whoReadChecksOver
			: st::whoReadChecks)
		: (!enabled
			? st::whoReadPlayedDisabled
			: selected
			? st::whoReadPlayedOver
			: st::whoReadPlayed);
	icon.paint(p, st::defaultWhoRead.iconPosition, width());
	p.setPen(!enabled
		? _st.itemFgDisabled
		: selected
		? _st.itemFgOver
		: _st.itemFg);
	_text.drawLeftElided(
		p,
		st::defaultWhoRead.itemPadding.left(),
		st::defaultWhoRead.itemPadding.top(),
		_textWidth,
		width());
	if (_appeared) {
		_userpics->paint(
			p,
			width() - st::defaultWhoRead.itemPadding.right(),
			(height() - st::defaultWhoRead.userpics.size) / 2,
			st::defaultWhoRead.userpics.size);
	}
}

void Action::refreshText() {
	const auto count = int(_content.participants.size());
	_text.setMarkedText(
		_st.itemStyle,
		{ (_content.unknown
			? tr::lng_context_seen_loading(tr::now)
			: (count == 1)
			? _content.participants.front().name
			: (_content.type == WhoReadType::Watched)
			? (count
				? tr::lng_context_seen_watched(tr::now, lt_count, count)
				: tr::lng_context_seen_watched_none(tr::now))
			: (_content.type == WhoReadType::Listened)
			? (count
				? tr::lng_context_seen_listened(tr::now, lt_count, count)
				: tr::lng_context_seen_listened_none(tr::now))
			: (count
				? tr::lng_context_seen_text(tr::now, lt_count, count)
				: tr::lng_context_seen_text_none(tr::now))) },
		MenuTextOptions);
}

void Action::refreshDimensions() {
	const auto textWidth = _text.maxWidth();
	const auto &padding = st::defaultWhoRead.itemPadding;

	const auto goodWidth = padding.left()
		+ textWidth
		+ (_userpicsWidth ? (_st.itemStyle.font->spacew + _userpicsWidth) : 0)
		+ padding.right();

	const auto w = std::clamp(
		goodWidth,
		_st.widthMin,
		minWidth());
	_textWidth = w - (goodWidth - textWidth);
}

bool Action::isEnabled() const {
	return !_content.participants.empty();
}

not_null<QAction*> Action::action() const {
	return _dummyAction;
}

QPoint Action::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QImage Action::prepareRippleMask() const {
	return Ui::RippleAnimation::rectMask(size());
}

int Action::contentHeight() const {
	return _height;
}

void Action::handleKeyPress(not_null<QKeyEvent*> e) {
	if (!isSelected()) {
		return;
	}
	const auto key = e->key();
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		setClicked(Menu::TriggeredSource::Keyboard);
	}
}

} // namespace

bool operator==(const WhoReadParticipant &a, const WhoReadParticipant &b) {
	return (a.id == b.id)
		&& (a.name == b.name)
		&& (a.userpicKey == b.userpicKey);
}

bool operator!=(const WhoReadParticipant &a, const WhoReadParticipant &b) {
	return !(a == b);
}

base::unique_qptr<Menu::ItemBase> WhoReadContextAction(
		not_null<PopupMenu*> menu,
		rpl::producer<WhoReadContent> content,
		Fn<void(uint64)> participantChosen,
		int userpicsRadius) {
	return base::make_unique_q<Action>(
		menu,
		std::move(content),
		std::move(participantChosen),
		userpicsRadius);
}

} // namespace Ui
