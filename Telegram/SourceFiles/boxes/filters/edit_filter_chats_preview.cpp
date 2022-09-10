/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/filters/edit_filter_chats_preview.h"

#include "kotato/kotato_lang.h"
#include "boxes/filters/edit_filter_chats_list.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "ui/text/text_options.h"
#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "styles/style_chat.h"
#include "styles/style_window.h"

namespace {

using Flag = Data::ChatFilter::Flag;

constexpr auto kAllTypes = {
	Flag::NewChats,
	Flag::ExistingChats,
	Flag::Contacts,
	Flag::NonContacts,
	Flag::Groups,
	Flag::Channels,
	Flag::Bots,
	Flag::NoMuted,
	Flag::NoRead,
	Flag::NoArchived,
};

} // namespace

FilterChatsPreview::FilterChatsPreview(
	not_null<QWidget*> parent,
	Flags flags,
	const base::flat_set<not_null<History*>> &peers)
: RpWidget(parent) {
	updateData(flags, peers);
}

void FilterChatsPreview::refresh() {
	resizeToWidth(width());
}

void FilterChatsPreview::updateData(
		Flags flags,
		const base::flat_set<not_null<History*>> &peers) {
	_removeFlag.clear();
	_removePeer.clear();
	const auto makeButton = [&](Fn<void()> handler) {
		auto result = base::make_unique_q<Ui::IconButton>(
			this,
			st::windowFilterSmallRemove);
		result->setClickedCallback(std::move(handler));
		result->show();
		return result;
	};
	for (const auto flag : kAllTypes) {
		if (flags & flag) {
			_removeFlag.push_back({
				flag,
				makeButton([=] { removeFlag(flag); }) });
		}
	}
	for (const auto &history : peers) {
		_removePeer.push_back(PeerButton{
			.history = history,
			.button = makeButton([=] { removePeer(history); })
		});
	}
	refresh();
}

int FilterChatsPreview::resizeGetHeight(int newWidth) {
	const auto right = st::windowFilterSmallRemoveRight;
	const auto add = (st::windowFilterSmallItem.height
		- st::windowFilterSmallRemove.height) / 2;
	auto top = 0;
	const auto moveNextButton = [&](not_null<Ui::IconButton*> button) {
		button->moveToRight(right, top + add, newWidth);
		top += st::windowFilterSmallItem.height;
	};
	for (const auto &[flag, button] : _removeFlag) {
		moveNextButton(button.get());
	}
	for (const auto &[history, userpic, name, button] : _removePeer) {
		moveNextButton(button.get());
	}
	return top;
}

void FilterChatsPreview::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto top = 0;
	const auto &st = st::windowFilterSmallItem;
	const auto iconLeft = st.photoPosition.x();
	const auto iconTop = st.photoPosition.y();
	const auto nameLeft = st.namePosition.x();
	p.setFont(st::windowFilterSmallItem.nameStyle.font);
	const auto nameTop = st.namePosition.y();
	const auto chatNameTop = st.chatNamePosition.y();
	const auto chatDescTop = st.chatDescPosition.y();
	for (const auto &[flag, button] : _removeFlag) {
		PaintFilterChatsTypeIcon(
			p,
			flag,
			iconLeft,
			top + iconTop,
			width(),
			st.photoSize);

		p.setPen(st::contactsNameFg);
		p.drawTextLeft(
			nameLeft,
			top + nameTop,
			width(),
			FilterChatsTypeName(flag));
		top += st.height;
	}

	QStringList statuses;

	for (auto &[history, userpic, name, button] : _removePeer) {
		const auto savedMessages = history->peer->isSelf();
		const auto repliesMessages = history->peer->isRepliesChat();
		if (savedMessages || repliesMessages) {
			if (savedMessages) {
				Ui::EmptyUserpic::PaintSavedMessages(
					p,
					iconLeft,
					top + iconTop,
					width(),
					st.photoSize);
			} else {
				Ui::EmptyUserpic::PaintRepliesMessages(
					p,
					iconLeft,
					top + iconTop,
					width(),
					st.photoSize);
			}
			p.setPen(st::contactsNameFg);
			p.drawTextLeft(
				nameLeft,
				top + nameTop,
				width(),
				(savedMessages
					? tr::lng_saved_messages(tr::now)
					: tr::lng_replies_messages(tr::now)));
		} else {
			history->peer->paintUserpicLeft(
				p,
				userpic,
				iconLeft,
				top + iconTop,
				width(),
				st.photoSize);

			if (history->peer->isUser()) {
				const auto user = history->peer->asUser();
				const auto flags = user->flags();

				if (user->isInaccessible()) {
					statuses << ktr("ktg_user_status_unaccessible");
				} else {
					if (user->isSupport()) {
						statuses << tr::lng_status_support(tr::now);
					}
					if (user->isBot()) {
						statuses << tr::lng_status_bot(tr::now);
					} else if (flags & UserDataFlag::MutualContact) {
						statuses << ktr("ktg_status_mutual_contact");
					} else if (flags & UserDataFlag::Contact) {
						statuses << ktr("ktg_status_contact");
					} else {
						statuses << ktr("ktg_status_non_contact");
					}
				}
			} else if (history->peer->isChat()) {
				statuses << tr::lng_group_status(tr::now);

				const auto chat = history->peer->asChat();
				if (!chat->amIn()) {
					statuses << ktr("ktg_group_status_not_in");
				} else if (chat->amCreator()) {
					statuses << ktr("ktg_group_status_owner");
				} else if (chat->hasAdminRights()) {
					statuses << ktr("ktg_group_status_admin");
				}

			} else if (history->peer->isChannel()) {
				if (history->peer->isMegagroup()) {
					statuses << ktr("ktg_supergroup_status");
				} else {
					statuses << tr::lng_channel_status(tr::now);
				}

				const auto channel = history->peer->asChannel();
				if (!channel->amIn()) {
					statuses << (channel->isMegagroup()
						? ktr("ktg_group_status_not_in")
						: ktr("ktg_channel_status_not_in"));
				} else if (channel->amCreator()) {
					statuses << ktr("ktg_group_status_owner");
				} else if (channel->hasAdminRights()) {
					statuses << ktr("ktg_group_status_admin");
				}
			}

			p.setPen(st::contactsNameFg);
			if (name.isEmpty()) {
				name.setText(
					st::msgNameStyle,
					history->peer->name(),
					Ui::NameTextOptions());
			}			
			if (statuses.empty()) {
				p.setFont(st::windowFilterSmallItem.nameStyle.font);
				name.drawLeftElided(
					p,
					nameLeft,
					top + nameTop,
					button->x() - nameLeft,
					width());
			} else {
				auto nameStr = Ui::Text::String{
					st::windowFilterChatNameStyle,
					name.toString(),
					Ui::NameTextOptions() };

				nameStr.drawLeftElided(
					p,
					nameLeft,
					top + chatNameTop,
					button->x() - nameLeft,
					width());

				p.setPen(st::windowSubTextFg);
				p.setFont(st::windowFilterChatDescStyle.font);
				p.drawTextLeft(
					nameLeft,
					top + chatDescTop,
					width(),
					statuses.join(", "));

				statuses.clear();
			}
		}
		top += st.height;
	}
}

void FilterChatsPreview::removeFlag(Flag flag) {
	const auto i = ranges::find(_removeFlag, flag, &FlagButton::flag);
	Assert(i != end(_removeFlag));
	_removeFlag.erase(i);
	refresh();
	_flagRemoved.fire_copy(flag);
}

void FilterChatsPreview::removePeer(not_null<History*> history) {
	const auto i = ranges::find(_removePeer, history, &PeerButton::history);
	Assert(i != end(_removePeer));
	_removePeer.erase(i);
	refresh();
	_peerRemoved.fire_copy(history);
}

rpl::producer<Flag> FilterChatsPreview::flagRemoved() const {
	return _flagRemoved.events();
}

rpl::producer<not_null<History*>> FilterChatsPreview::peerRemoved() const {
	return _peerRemoved.events();
}
