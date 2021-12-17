/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/section_widget.h"
#include "window/section_memento.h"
#include "history/admin_log/history_admin_log_item.h"
#include "mtproto/sender.h"

namespace Ui {
class ScrollArea;
class PlainShadow;
class FlatButton;
} // namespace Ui

namespace Profile {
class BackButton;
} // namespace Profile

namespace AdminLog {

class FixedBar;
class InnerWidget;
class SectionMemento;

struct FilterValue {
	enum class Flag : uint32 {
		Join = (1U << 0),
		Leave = (1U << 1),
		Invite = (1U << 2),
		Ban = (1U << 3),
		Unban = (1U << 4),
		Kick = (1U << 5),
		Unkick = (1U << 6),
		Promote = (1U << 7),
		Demote = (1U << 8),
		Info = (1U << 9),
		Settings = (1U << 10),
		Pinned = (1U << 11),
		Edit = (1U << 12),
		Delete = (1U << 13),
		GroupCall = (1U << 14),
		Invites = (1U << 15),

		MAX_FIELD = (1U << 15),
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr bool is_flag_type(Flag) { return true; };

	// Empty "flags" means all events.
	Flags flags = 0;
	std::vector<not_null<UserData*>> admins;
	bool allUsers = true;
};

inline bool operator==(const FilterValue &a, const FilterValue &b) {
	return (a.flags == b.flags && a.admins == b.admins && a.allUsers == b.allUsers);
}

inline bool operator!=(const FilterValue &a, const FilterValue &b) {
	return !(a == b);
}

class Widget final : public Window::SectionWidget {
public:
	Widget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<ChannelData*> channel);

	not_null<ChannelData*> channel() const;
	Dialogs::RowDescriptor activeChat() const override;

	bool hasTopBarShadow() const override {
		return true;
	}

	QPixmap grabForShowAnimation(const Window::SectionSlideParams &params) override;

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;

	void setInternalState(const QRect &geometry, not_null<SectionMemento*> memento);

	// Float player interface.
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;

	void applyFilter(FilterValue &&value);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void showAnimatedHook(
		const Window::SectionSlideParams &params) override;
	void showFinishedHook() override;
	void doSetInnerFocus() override;

private:
	void showFilter();
	void onScroll();
	void updateAdaptiveLayout();
	void saveState(not_null<SectionMemento*> memento);
	void restoreState(not_null<SectionMemento*> memento);
	void setupShortcuts();

	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<InnerWidget> _inner;
	object_ptr<FixedBar> _fixedBar;
	object_ptr<Ui::PlainShadow> _fixedBarShadow;
	object_ptr<Ui::FlatButton> _whatIsThis;

};

class SectionMemento : public Window::SectionMemento {
public:
	using Element = HistoryView::Element;

	SectionMemento(not_null<ChannelData*> channel) : _channel(channel) {
	}

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	not_null<ChannelData*> getChannel() const {
		return _channel;
	}
	void setScrollTop(int scrollTop) {
		_scrollTop = scrollTop;
	}
	int getScrollTop() const {
		return _scrollTop;
	}

	void setAdmins(std::vector<not_null<UserData*>> admins) {
		_admins = std::move(admins);
	}
	void setAdminsCanEdit(std::vector<not_null<UserData*>> admins) {
		_adminsCanEdit = std::move(admins);
	}
	std::vector<not_null<UserData*>> takeAdmins() {
		return std::move(_admins);
	}
	std::vector<not_null<UserData*>> takeAdminsCanEdit() {
		return std::move(_adminsCanEdit);
	}

	void setItems(
			std::vector<OwnedItem> &&items,
			std::set<uint64> &&eventIds,
			bool upLoaded,
			bool downLoaded) {
		_items = std::move(items);
		_eventIds = std::move(eventIds);
		_upLoaded = upLoaded;
		_downLoaded = downLoaded;
	}
	void setFilter(FilterValue &&filter) {
		_filter = std::move(filter);
	}
	void setSearchQuery(QString &&query) {
		_searchQuery = std::move(query);
	}
	std::vector<OwnedItem> takeItems() {
		return std::move(_items);
	}
	std::set<uint64> takeEventIds() {
		return std::move(_eventIds);
	}
	bool upLoaded() const {
		return _upLoaded;
	}
	bool downLoaded() const {
		return _downLoaded;
	}
	FilterValue takeFilter() {
		return std::move(_filter);
	}
	QString takeSearchQuery() {
		return std::move(_searchQuery);
	}

private:
	not_null<ChannelData*> _channel;
	int _scrollTop = 0;
	std::vector<not_null<UserData*>> _admins;
	std::vector<not_null<UserData*>> _adminsCanEdit;
	std::vector<OwnedItem> _items;
	std::set<uint64> _eventIds;
	bool _upLoaded = false;
	bool _downLoaded = true;
	FilterValue _filter;
	QString _searchQuery;

};

} // namespace AdminLog
