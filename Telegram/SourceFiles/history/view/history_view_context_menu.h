/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionNavigation;
} // namespace Main

namespace HistoryView {

enum class PointState : char;
class ListWidget;
class Element;
struct SelectedItem;
using SelectedItems = std::vector<SelectedItem>;

struct ContextMenuRequest {
	explicit ContextMenuRequest(
		not_null<Window::SessionNavigation*> navigation);

	const not_null<Window::SessionNavigation*> navigation;
	ClickHandlerPtr link;
	Element *view = nullptr;
	HistoryItem *item = nullptr;
	SelectedItems selectedItems;
	TextForMimeData selectedText;
	bool overSelection = false;
	PointState pointState = PointState();
};

base::unique_qptr<Ui::PopupMenu> FillContextMenu(
	not_null<ListWidget*> list,
	const ContextMenuRequest &request);

void CopyPostLink(FullMsgId itemId, bool forcePrivate = false);
void StopPoll(FullMsgId itemId);

} // namespace
