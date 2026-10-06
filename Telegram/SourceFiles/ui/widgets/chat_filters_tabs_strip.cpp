/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/chat_filters_tabs_strip.h"

#include "kotato/kotato_settings.h"
#include "kotato/kotato_lang.h"
#include "api/api_chat_filters_remove_manager.h"
#include "boxes/choose_filter_box.h"
#include "boxes/filters/edit_filter_box.h"
#include "boxes/premium_limits_box.h"
#include "core/application.h"
#include "core/shortcuts.h"
#include "core/ui_integration.h"
#include "data/data_chat_filters.h"
#include "data/data_peer_values.h" // Data::AmPremiumValue.
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_unread_value.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "menu/menu_mark_as_read.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "settings/sections/settings_folders.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/filter_icons.h"
#include "ui/power_saving.h"
#include "ui/ui_utility.h"
#include "ui/widgets/chat_filters_tabs_slider_reorder.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_dialogs.h" // dialogsSearchTabs
#include "styles/style_media_player.h" // mediaPlayerMenuCheck
#include "styles/style_menu_icons.h"

#include <QScrollBar>

namespace Ui {
namespace {

struct State final {
	Ui::Animations::Simple animation;
	std::optional<FilterId> lastFilterId = std::nullopt;
	rpl::lifetime rebuildLifetime;
	rpl::lifetime reorderLifetime;
	base::unique_qptr<Ui::PopupMenu> menu;

	Api::RemoveComplexChatFilter removeApi;
	bool waitingSuggested = false;

	std::unique_ptr<Ui::ChatsFiltersTabsReorder> reorder;
	bool ignoreRefresh = false;
	bool allChatsHidden = false;
};

void ShowMenu(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<State*> state,
		int index) {
	const auto session = &controller->session();

	auto id = FilterId(0);
	{
		const auto &list = session->data().chatsFilters().list();
		if (index < 0 || index >= list.size()) {
			return;
		}
		id = list[index].id();
	}
	const auto account = &session->account();
	const auto defaultFilterId = account->defaultFilterId();
	const auto setDefaultFilter = [=](FilterId id) {
		account->setDefaultFilterId(id);
	};
	state->menu = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto addAction = Ui::Menu::CreateAddActionCallback(
		state->menu.get());

	if (id) {
		addAction(
			tr::lng_filters_context_edit(tr::now),
			[=] { EditExistingFilter(controller, id); },
			&st::menuIconEdit);

		MarkAsReadMenu::AddChatListAction(
			controller,
			MarkAsReadMenu::ChatListKind::Folder,
			[=] { return session->data().chatsFilters().chatsList(id); },
			addAction);
		if (defaultFilterId != id) {
			addAction(
				ktr("ktg_filters_context_make_default"),
				[=] { setDefaultFilter(id); },
				&st::menuIconFave);
		} else {
			addAction(
				ktr("ktg_filters_context_reset_default"),
				[=] { setDefaultFilter(0); },
				&st::menuIconUnfave);
		}

		auto showRemoveBox = [=] {
			state->removeApi.request(base::make_weak(parent), controller, id);
		};
		addAction({
			.text = tr::lng_filters_context_remove(tr::now),
			.handler = std::move(showRemoveBox),
			.icon = &st::menuIconDeleteAttention,
			.isAttention = true,
		});
	} else {
		MarkAsReadMenu::AddChatListAction(
			controller,
			MarkAsReadMenu::ChatListKind::AllChats,
			[=] { return session->data().chatsList(); },
			addAction);
		if (defaultFilterId != id) {
			addAction(
				ktr("ktg_filters_context_make_default"),
				[=] { setDefaultFilter(0); },
				&st::menuIconFave);
		}

		auto openFiltersSettings = [=] {
			const auto filters = &session->data().chatsFilters();
			if (filters->suggestedLoaded()) {
				controller->showSettings(Settings::FoldersId());
			} else if (!state->waitingSuggested) {
				state->waitingSuggested = true;
				filters->requestSuggested();
				filters->suggestedUpdated(
				) | rpl::take(1) | rpl::on_next([=] {
					controller->showSettings(Settings::FoldersId());
				}, parent->lifetime());
			}
		};
		addAction(
			tr::lng_filters_setup_menu(tr::now),
			std::move(openFiltersSettings),
			&st::menuIconEdit);
	}
	if (state->menu->empty()) {
		state->menu = nullptr;
		return;
	}
	state->menu->popup(QCursor::pos());
}

void ShowFiltersListMenu(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Session*> session,
		not_null<State*> state,
		int active,
		Fn<void(int)> changeActive) {
	const auto &list = session->data().chatsFilters().list();

	state->menu = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);

	const auto reorderAll = session->user()->isPremium();
	const auto maxLimit = (reorderAll ? 1 : 0)
		+ Data::PremiumLimits(session).dialogFiltersCurrent();
	const auto premiumFrom = (reorderAll ? 0 : 1) + maxLimit;

	for (auto i = 0; i < list.size(); ++i) {
		const auto title = list[i].title();
		const auto text = title.text.empty()
			? tr::lng_filters_all_short(tr::now)
			: title.text.text;
		const auto callback = [=] {
			if (i != active) {
				changeActive(i);
			}
		};
		const auto icon = (i == active)
			? &st::mediaPlayerMenuCheck
			: nullptr;
		const auto action = Ui::Menu::CreateAction(
			state->menu->menu(),
			text,
			callback);
		auto item = base::make_unique_q<Ui::Menu::Action>(
			state->menu->menu(),
			state->menu->st().menu,
			action,
			icon,
			icon);
		action->setEnabled(!Data::ChatFilterLocked(list, i, premiumFrom));
		if (!title.text.empty()) {
			const auto context = Core::TextContext({
				.session = session,
				.repaint = [raw = item.get()] { raw->update(); },
				.customEmojiLoopLimit = title.isStatic ? -1 : 0,
			});
			item->setMarkedText(title.text, QString(), context);
		}
		state->menu->addAction(std::move(item));
	}
	session->data().chatsFilters().changed() | rpl::on_next([=] {
		state->menu->hideMenu();
	}, state->menu->lifetime());

	if (state->menu->empty()) {
		state->menu = nullptr;
		return;
	}
	state->menu->popup(QCursor::pos());
}

} // namespace

not_null<Ui::RpWidget*> AddChatFiltersTabsStrip(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Session*> session,
		Fn<void(FilterId)> choose,
		ChatHelpers::PauseReason pauseLevel,
		Window::SessionController *controller,
		bool trackActiveFilterAndUnreadAndReorder,
		bool handleKeyboardSwitch) {

	const auto wrap = Ui::CreateChild<Ui::SlideWrap<Ui::RpWidget>>(
		parent,
		object_ptr<Ui::RpWidget>(parent));
	if (!controller) {
		const auto window = Core::App().findWindow(parent);
		controller = window ? window->sessionController() : nullptr;
		if (!controller) {
			return wrap;
		}
	}
	const auto container = wrap->entity();
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		container,
		st::dialogsTabsScroll,
		true);
	const auto slider = scroll->setOwnedWidget(
		object_ptr<Ui::ChatsFiltersTabs>(
			parent,
			trackActiveFilterAndUnreadAndReorder
				? st::dialogsSearchTabs
				: st::chatsFiltersTabs));
	const auto state = wrap->lifetime().make_state<State>();
	// "All chats" hidden by the option has no tab.
	const auto hiddenAll = [=] {
		const auto &list = session->data().chatsFilters().list();
		const auto i = ranges::find(list, FilterId(), &Data::ChatFilter::id);
		return state->allChatsHidden ? int(i - begin(list)) : -1;
	};
	const auto listIndex = [=](int section) {
		const auto hidden = hiddenAll();
		return section + ((hidden >= 0 && section >= hidden) ? 1 : 0);
	};
	const auto sectionIndex = [=](int index) {
		const auto hidden = hiddenAll();
		return index - ((hidden >= 0 && index > hidden) ? 1 : 0);
	};
	const auto reassignUnreadValue = [=] {
		state->reorderLifetime.destroy();
		const auto &list = session->data().chatsFilters().list();
		auto includeMuted = Data::IncludeMutedCounterFoldersValue();
		const auto hidden = hiddenAll();
		for (auto i = 0; i < list.size(); i++) {
			if (i == hidden) {
				continue;
			}
			const auto section = sectionIndex(i);
			rpl::combine(
				Data::UnreadStateValue(session, list[i].id()),
				rpl::duplicate(includeMuted)
			) | rpl::on_next([=](
					const Dialogs::UnreadState &state,
					bool includeMuted) {
				const auto chats = state.chats;
				const auto chatsMuted = state.chatsMuted;
				const auto muted = (chatsMuted + state.marksMuted);
				const auto count = (chats + state.marks)
					- (includeMuted ? 0 : muted);
				const auto isMuted = includeMuted && (count == muted);
				slider->setUnreadCount(section, count, isMuted);
				slider->fitWidthToSections();
			}, state->reorderLifetime);
		}
	};
	if (trackActiveFilterAndUnreadAndReorder) {
		using Reorder = Ui::ChatsFiltersTabsReorder;
		state->reorder = std::make_unique<Reorder>(slider, scroll);
		const auto applyReorder = [=](
				int oldPosition,
				int newPosition) {
			if (newPosition == oldPosition) {
				return;
			}

			const auto filters = &session->data().chatsFilters();
			const auto &list = filters->list();
			if (!session->user()->isPremium()) {
				if (list[0].id() != FilterId()) {
					filters->moveAllToFront();
				}
			}
			oldPosition = listIndex(oldPosition);
			newPosition = listIndex(newPosition);
			Assert(oldPosition >= 0 && oldPosition < list.size());
			Assert(newPosition >= 0 && newPosition < list.size());

			auto order = ranges::views::all(
				list
			) | ranges::views::transform(
				&Data::ChatFilter::id
			) | ranges::to_vector;
			base::reorder(order, oldPosition, newPosition);

			state->ignoreRefresh = true;
			filters->saveOrder(order);
			state->ignoreRefresh = false;
		};

		state->reorder->updates(
		) | rpl::on_next([=](const Reorder::Single &data) {
			if (data.state == Reorder::State::Started) {
				slider->setReordering(slider->reordering() + 1);
			} else {
				Ui::PostponeCall(slider, [=] {
					slider->setReordering(slider->reordering() - 1);
				});
				if (data.state == Reorder::State::Applied) {
					applyReorder(data.oldPosition, data.newPosition);
					reassignUnreadValue();
				}
			}
		}, slider->lifetime());

		SetupFilterDragAndDrop(
			slider,
			session,
			[=](QPoint pos) -> std::optional<FilterId> {
				const auto local = slider->mapFromGlobal(pos);
				const auto x = local.x();
				const auto count = slider->sectionsCount();
				for (auto i = 0; i < count; ++i) {
					const auto left = slider->lookupSectionLeft(i);
					const auto right = (i + 1 < count)
						? slider->lookupSectionLeft(i + 1)
						: slider->width();
					if (x >= left && x < right) {
						const auto &list
							= session->data().chatsFilters().list();
						const auto index = listIndex(i);
						return (index < list.size())
							? list[index].id()
							: FilterId();
					}
				}
				return std::nullopt;
			},
			[=] { return state->lastFilterId.value_or(FilterId()); },
			[=](FilterId id) {
				const auto &list = session->data().chatsFilters().list();
				const auto hidden = hiddenAll();
				for (auto i = 0; i < list.size(); i++) {
					if (list[i].id() == id && i != hidden) {
						slider->selectSection(sectionIndex(i));
						return;
					}
				}
				slider->selectSection(-1);
			});
	}
	wrap->toggle(false, anim::type::instant);
	scroll->setCustomWheelProcess([=](not_null<QWheelEvent*> e) {
		const auto pixelDelta = e->pixelDelta();
		const auto angleDelta = e->angleDelta();
		if (std::abs(pixelDelta.x()) + std::abs(angleDelta.x())) {
			return false;
		}
		const auto bar = scroll->horizontalScrollBar();
		const auto y = pixelDelta.y() ? pixelDelta.y() : angleDelta.y();
		bar->setValue(bar->value() - y);
		return true;
	});

	const auto scrollToIndex = [=](int index, anim::type type) {
		const auto to = index
			? (slider->centerOfSection(index) - scroll->width() / 2)
			: 0;
		const auto bar = scroll->horizontalScrollBar();
		state->animation.stop();
		if (type == anim::type::instant) {
			bar->setValue(to);
		} else {
			state->animation.start(
				[=](float64 v) { bar->setValue(v); },
				bar->value(),
				std::min(to, bar->maximum()),
				st::defaultTabsSlider.duration);
		}
	};

	const auto applyFilter = [=](const Data::ChatFilter &filter) {
		if (slider->reordering()) {
			return;
		}
		choose(filter.id());
	};

	const auto filterByIndex = [=](int index) -> const Data::ChatFilter& {
		const auto &list = session->data().chatsFilters().list();
		index = listIndex(index);
		Assert(index >= 0 && index < list.size());
		return list[index];
	};

	const auto rebuild = [=] {
		const auto &list = session->data().chatsFilters().list();
		if ((list.size() <= 1 && !slider->width()) || state->ignoreRefresh) {
			return;
		}
		state->allChatsHidden = trackActiveFilterAndUnreadAndReorder
			&& (controller->hiddenAllChatsIndex() >= 0);
		const auto hidden = hiddenAll();
		const auto shown = [=](const Data::ChatFilter &filter) {
			return filter.id() || (hidden < 0);
		};
		const auto context = Core::TextContext({ .session = session });
		const auto paused = [=] {
			return On(PowerSaving::kEmojiChat)
				|| controller->isGifPausedAtLeastFor(pauseLevel);
		};
		const auto sectionsChanged = slider->setSectionsAndCheckChanged(
			ranges::views::all(
				list
			) | ranges::views::filter(
				shown
			) | ranges::views::transform([](const Data::ChatFilter &filter) {
				auto title = filter.title();
				return title.text.empty()
					? TextWithEntities{ tr::lng_filters_all_short(tr::now) }
					: title.isStatic
					? Data::ForceCustomEmojiStatic(title.text)
					: title.text;
			}) | ranges::to_vector, context, paused);
		slider->setSectionIcons(ranges::views::all(
			list
		) | ranges::views::filter(
			shown
		) | ranges::views::transform([](const Data::ChatFilter &filter) {
			return LookupFilterIcon(filter.id()
				? ComputeFilterIcon(filter)
				: FilterIcon::All).tabs.get();
		}) | ranges::to_vector);
		if (!sectionsChanged) {
			return;
		}
		state->rebuildLifetime.destroy();
		slider->fitWidthToSections();
		{
			const auto reorderAll = session->user()->isPremium();
			const auto maxLimit = (reorderAll ? 1 : 0)
				+ Data::PremiumLimits(session).dialogFiltersCurrent();
			const auto premiumFrom = (reorderAll ? 0 : 1) + maxLimit;
			auto lockedFrom = 0;
			auto locked = std::vector<int>();
			auto unlocked = base::flat_set<int>();
			for (auto i = 0; i != int(list.size()); ++i) {
				if (i == hidden) {
					continue;
				} else if (Data::ChatFilterLocked(list, i, premiumFrom)) {
					if (!lockedFrom) {
						lockedFrom = sectionIndex(i);
					}
					locked.push_back(sectionIndex(i));
				} else if (lockedFrom) {
					unlocked.emplace(sectionIndex(i));
				}
			}
			slider->setLockedFrom(lockedFrom, std::move(unlocked));
			slider->lockedClicked() | rpl::on_next([=] {
				controller->show(Box(FiltersLimitBox, session, std::nullopt));
			}, state->rebuildLifetime);
			if (state->reorder) {
				state->reorder->cancel();
				state->reorder->clearPinnedIntervals();
				if (!reorderAll && hidden != 0) {
					state->reorder->addPinnedInterval(0, 1);
				}
				for (const auto section : locked) {
					state->reorder->addPinnedInterval(section, 1);
				}
			}
		}
		if (trackActiveFilterAndUnreadAndReorder) {
			reassignUnreadValue();
		}
		[&] {
			const auto lookingId = state->lastFilterId.value_or(
				trackActiveFilterAndUnreadAndReorder
					? controller->activeChatsFilterCurrent()
					: list[0].id());
			for (auto i = 0; i < list.size(); i++) {
				const auto &filter = list[i];
				if (filter.id() == lookingId && i != hidden) {
					const auto wasLast = !!state->lastFilterId;
					const auto section = sectionIndex(i);
					state->lastFilterId = filter.id();
					slider->setActiveSectionFast(section);
					scrollToIndex(
						section,
						wasLast ? anim::type::normal : anim::type::instant);
					if (wasLast || !trackActiveFilterAndUnreadAndReorder) {
						applyFilter(filter);
					}
					return;
				}
			}
			if (list.size()) {
				const auto index = 0;
				const auto &filter = filterByIndex(index);
				state->lastFilterId = filter.id();
				slider->setActiveSectionFast(index);
				scrollToIndex(index, anim::type::instant);
				applyFilter(filter);
			}
		}();
		if (trackActiveFilterAndUnreadAndReorder) {
			controller->activeChatsFilter(
			) | rpl::on_next([=](FilterId id) {
				const auto &list = session->data().chatsFilters().list();
				const auto hidden = hiddenAll();
				for (auto i = 0; i < list.size(); ++i) {
					if (list[i].id() == id && i != hidden) {
						slider->setActiveSection(sectionIndex(i));
						scrollToIndex(sectionIndex(i), anim::type::normal);
						break;
					}
				}
				state->reorder->finishReordering();
			}, state->rebuildLifetime);
		}
		rpl::single(-1) | rpl::then(
			slider->sectionActivated()
		) | rpl::combine_previous(
		) | rpl::on_next([=](int was, int index) {
			if (slider->reordering()) {
				return;
			}
			const auto &filter = filterByIndex(index);
			if (was != index) {
				state->lastFilterId = filter.id();
				scrollToIndex(index, anim::type::normal);
			}
			applyFilter(filter);
		}, state->rebuildLifetime);
		slider->contextMenuRequested() | rpl::on_next([=](int index) {
			if (trackActiveFilterAndUnreadAndReorder) {
				ShowMenu(wrap, controller, state, listIndex(index));
			} else {
				ShowFiltersListMenu(
					wrap,
					session,
					state,
					slider->activeSection(),
					[=](int i) { slider->setActiveSection(i); });
			}
		}, state->rebuildLifetime);
		wrap->toggle((list.size() > 1), anim::type::instant);

		if (state->reorder) {
			state->reorder->start();
		}
	};
	rpl::combine(
		session->data().chatsFilters().changed(),
		Data::AmPremiumValue(session) | rpl::to_empty
	) | rpl::on_next(rebuild, wrap->lifetime());
	if (trackActiveFilterAndUnreadAndReorder) {
		::Kotato::JsonSettings::Events(
			"folders/hide_all_chats"
		) | rpl::to_empty | rpl::on_next(rebuild, wrap->lifetime());
	}
	Core::App().settings().chatFiltersTabsModeValue(
	) | rpl::on_next([=](ChatsFiltersTabsMode mode) {
		slider->setTabsMode(HorizontalChatsFiltersTabsMode(mode));
		scrollToIndex(slider->activeSection(), anim::type::instant);
	}, wrap->lifetime());
	rebuild();

	session->data().chatsFilters().isChatlistChanged(
	) | rpl::on_next([=](FilterId id) {
		if (!id || !state->lastFilterId || (id != state->lastFilterId)) {
			return;
		}
		for (const auto &filter : session->data().chatsFilters().list()) {
			if (filter.id() == id) {
				applyFilter(filter);
				return;
			}
		}
	}, wrap->lifetime());

	rpl::combine(
		parent->widthValue() | rpl::filter(rpl::mappers::_1 > 0),
		slider->heightValue() | rpl::filter(rpl::mappers::_1 > 0)
	) | rpl::on_next([=](int w, int h) {
		scroll->resize(w, h);
		container->resize(w, h);
		wrap->resize(w, h);
	}, wrap->lifetime());

	if (handleKeyboardSwitch) {
		Shortcuts::ChatSwitchRequests(
		) | rpl::filter([=](const Shortcuts::ChatSwitchRequest &request) {
			return wrap->toggled()
				&& ((request.action == Qt::Key_Tab)
					|| (request.action == Qt::Key_Backtab));
		}) | rpl::on_next([=](const Shortcuts::ChatSwitchRequest &request) {
			const auto count = slider->sectionsCount();
			if (count <= 1) {
				return;
			}
			const auto back = (request.action == Qt::Key_Backtab);
			const auto current = slider->activeSection();
			auto next = current;
			for (auto i = 0; i != count; ++i) {
				next = (next + (back ? -1 : 1) + count) % count;
				if (!slider->isLocked(next)) {
					break;
				}
			}
			if (next != current) {
				slider->setActiveSection(next);
			}
		}, wrap->lifetime());
	}

	return wrap;
}

} // namespace Ui
