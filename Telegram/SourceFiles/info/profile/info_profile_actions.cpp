/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_actions.h"

#include "kotato/kotato_lang.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/boxes/report_box.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/text/text_utilities.h" // Ui::Text::ToUpper
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "history/history_location_manager.h" // LocationClickHandler.
#include "history/view/history_view_context_menu.h" // HistoryView::ShowReportPeerBox
#include "history/admin_log/history_admin_log_section.h"
#include "boxes/abstract_box.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/add_contact_box.h"
#include "boxes/peers/edit_contact_box.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "boxes/peers/edit_peer_invite_links.h"
#include "boxes/peers/edit_participants_box.h"
#include "lang/lang_keys.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/info_profile_text.h"
#include "support/support_helper.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h" // Window::Controller::show.
#include "window/window_peer_menu.h"
#include "mainwidget.h"
#include "mainwindow.h" // MainWindow::controller.
#include "main/main_session.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "apiwrap.h"
#include "api/api_blocked_peers.h"
#include "facades.h"
#include "styles/style_info.h"
#include "styles/style_boxes.h"
#include "app.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace Info {
namespace Profile {
namespace {

object_ptr<Ui::RpWidget> CreateSkipWidget(
		not_null<Ui::RpWidget*> parent) {
	return Ui::CreateSkipWidget(parent, st::infoProfileSkip);
}

object_ptr<Ui::SlideWrap<>> CreateSlideSkipWidget(
		not_null<Ui::RpWidget*> parent) {
	auto result = Ui::CreateSlideSkipWidget(
		parent,
		st::infoProfileSkip);
	result->setDuration(st::infoSlideDuration);
	return result;
}

template <typename Text, typename ToggleOn, typename Callback>
auto AddActionButton(
		not_null<Ui::VerticalLayout*> parent,
		Text &&text,
		ToggleOn &&toggleOn,
		Callback &&callback,
		const style::SettingsButton &st
			= st::infoSharedMediaButton) {
	auto result = parent->add(object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
		parent,
		object_ptr<Ui::SettingsButton>(
			parent,
			std::move(text),
			st))
	);
	result->setDuration(
		st::infoSlideDuration
	)->toggleOn(
		std::move(toggleOn)
	)->entity()->addClickHandler(std::move(callback));
	result->finishAnimating();
	return result;
};

template <typename Text, typename ToggleOn, typename Callback>
auto AddMainButton(
		not_null<Ui::VerticalLayout*> parent,
		Text &&text,
		ToggleOn &&toggleOn,
		Callback &&callback,
		Ui::MultiSlideTracker &tracker) {
	tracker.track(AddActionButton(
		parent,
		std::move(text) | Ui::Text::ToUpper(),
		std::move(toggleOn),
		std::move(callback),
		st::infoMainButton));
}

class DetailsFiller {
public:
	DetailsFiller(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer);

	object_ptr<Ui::RpWidget> fill();

private:
	object_ptr<Ui::RpWidget> setupInfo();
	object_ptr<Ui::RpWidget> setupMuteToggle();
	void setupMainButtons();
	Ui::MultiSlideTracker fillUserButtons(
		not_null<UserData*> user);
	Ui::MultiSlideTracker fillChannelButtons(
		not_null<ChannelData*> channel);

	template <
		typename Widget,
		typename = std::enable_if_t<
		std::is_base_of_v<Ui::RpWidget, Widget>>>
	Widget *add(
			object_ptr<Widget> &&child,
			const style::margins &margin = style::margins()) {
		return _wrap->add(
			std::move(child),
			margin);
	}

	not_null<Controller*> _controller;
	not_null<Ui::RpWidget*> _parent;
	not_null<PeerData*> _peer;
	object_ptr<Ui::VerticalLayout> _wrap;

};

class ActionsFiller {
public:
	ActionsFiller(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer);

	object_ptr<Ui::RpWidget> fill();

private:
	void addInviteToGroupAction(not_null<UserData*> user);
	void addShareContactAction(not_null<UserData*> user);
	void addEditContactAction(not_null<UserData*> user);
	void addDeleteContactAction(not_null<UserData*> user);
	void addClearHistoryAction(not_null<UserData*> user);
	void addDeleteConversationAction(not_null<UserData*> user);
	void addBotCommandActions(not_null<UserData*> user);
	void addReportAction();
	void addBlockAction(not_null<UserData*> user);
	void addLeaveChannelAction(not_null<ChannelData*> channel);
	void addJoinChannelAction(not_null<ChannelData*> channel);
	void fillUserActions(not_null<UserData*> user);
	void fillChannelActions(not_null<ChannelData*> channel);

	not_null<Controller*> _controller;
	not_null<Ui::RpWidget*> _parent;
	not_null<PeerData*> _peer;
	object_ptr<Ui::VerticalLayout> _wrap = { nullptr };

};

class ManageFiller {
public:
	ManageFiller(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer);

	object_ptr<Ui::RpWidget> fill();

private:
	void addPeerPermissions(not_null<PeerData*> peer);
	void addPeerAdmins(not_null<PeerData*> peer);
	void addPeerInviteLinks(not_null<PeerData*> peer);
	void addChannelBlockedUsers(not_null<ChannelData*> channel);
	void addChannelRecentActions(not_null<ChannelData*> channel);

	void fillChatActions(not_null<ChatData*> chat);
	void fillChannelActions(not_null<ChannelData*> channel);

	not_null<Controller*> _controller;
	not_null<Ui::RpWidget*> _parent;
	not_null<PeerData*> _peer;
	object_ptr<Ui::VerticalLayout> _wrap = { nullptr };
};

DetailsFiller::DetailsFiller(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer)
: _controller(controller)
, _parent(parent)
, _peer(peer)
, _wrap(_parent) {
}

template <typename T>
bool SetClickContext(
		const ClickHandlerPtr &handler,
		const ClickContext &context) {
	if (const auto casted = std::dynamic_pointer_cast<T>(handler)) {
		casted->T::onClick(context);
		return true;
	}
	return false;
}

object_ptr<Ui::RpWidget> DetailsFiller::setupInfo() {
	auto result = object_ptr<Ui::VerticalLayout>(_wrap);
	auto tracker = Ui::MultiSlideTracker();

	// Fill context for a mention / hashtag / bot command link.
	const auto infoClickFilter = [=,
		peer = _peer.get(),
		window = _controller->parentController()](
			const ClickHandlerPtr &handler,
			Qt::MouseButton button) {
		const auto context = ClickContext{
			button,
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(window.get()),
				.peer = peer,
			})
		};
		if (SetClickContext<BotCommandClickHandler>(handler, context)) {
			return false;
		} else if (SetClickContext<MentionClickHandler>(handler, context)) {
			return false;
		} else if (SetClickContext<HashtagClickHandler>(handler, context)) {
			return false;
		} else if (SetClickContext<CashtagClickHandler>(handler, context)) {
			return false;
		}
		return true;
	};

	auto addInfoLineGeneric = [&](
			rpl::producer<QString> &&label,
			rpl::producer<TextWithEntities> &&text,
			const style::FlatLabel &textSt = st::infoLabeled) {
		auto line = CreateTextWithLabel(
			result,
			std::move(label) | Ui::Text::ToWithEntities(),
			std::move(text),
			textSt,
			st::infoProfileLabeledPadding);
		tracker.track(result->add(std::move(line.wrap)));

		line.text->setClickHandlerFilter(infoClickFilter);
		return line.text;
	};
	auto addInfoLine = [&](
			rpl::producer<QString> &&label,
			rpl::producer<TextWithEntities> &&text,
			const style::FlatLabel &textSt = st::infoLabeled) {
		return addInfoLineGeneric(
			std::move(label),
			std::move(text),
			textSt);
	};
	auto addInfoOneLine = [&](
			rpl::producer<QString> &&label,
			rpl::producer<TextWithEntities> &&text,
			const QString &contextCopyText) {
		auto result = addInfoLine(
			std::move(label),
			std::move(text),
			st::infoLabeledOneLine);
		result->setDoubleClickSelectsParagraph(true);
		result->setContextCopyText(contextCopyText);
		return result;
	};
	auto addInfoOneLineInline = [&](
			rpl::producer<QString> &&label,
			rpl::producer<TextWithEntities> &&text,
			const QString &contextCopyText) {
		auto result = addInfoLine(
			std::move(label),
			std::move(text),
			st::infoLabeledOneLineInline);
		result->setContextCopyText(contextCopyText);
		return result;
	};
	if (const auto user = _peer->asUser()) {
		if (ShowChatId() != 0) {
			auto idDrawableText = IDValue(
				user
			) | rpl::map([](TextWithEntities &&text) {
				return Ui::Text::Link(text.text);
			});
			auto idInfo = addInfoOneLineInline(
				(user->isBot()
					? rktr("ktg_profile_bot_id")
					: rktr("ktg_profile_user_id")),
				std::move(idDrawableText),
				ktr("ktg_profile_copy_id"));

			idInfo->setClickHandlerFilter([user](auto&&...) {
				const auto idText = IDString(user);
				if (!idText.isEmpty()) {
					QGuiApplication::clipboard()->setText(idText);
					Ui::Toast::Show(user->isBot()
						? ktr("ktg_bot_id_copied")
						: ktr("ktg_user_id_copied"));
				}
				return false;
			});
		}

		if (user->session().supportMode()) {
			addInfoLineGeneric(
				user->session().supportHelper().infoLabelValue(user),
				user->session().supportHelper().infoTextValue(user));
		}
		
		auto phoneDrawableText = rpl::combine(
			PhoneValue(user),
			UsernameValue(user),
			AboutValue(user),
			tr::lng_info_mobile_hidden()
		) | rpl::map([](
				const TextWithEntities &phone,
				const TextWithEntities &username,
				const TextWithEntities &bio,
				const QString &hidden) {
			return (phone.text.isEmpty() && username.text.isEmpty() && bio.text.isEmpty())
				? Ui::Text::WithEntities(hidden)
				: Ui::Text::Link(phone.text);
		});

		auto phoneInfo = addInfoOneLineInline(
			tr::lng_info_mobile_label(),
			std::move(phoneDrawableText),
			tr::lng_profile_copy_phone(tr::now));

		phoneInfo->setClickHandlerFilter([user](auto&&...) {
			const auto phoneText = user->phone();
			if (!phoneText.isEmpty()) {
				QGuiApplication::clipboard()->setText(Ui::FormatPhone(phoneText));
				Ui::Toast::Show(ktr("ktg_phone_copied"));
			}
			return false;
		});
		
		auto label = user->isBot()
			? tr::lng_info_about_label()
			: tr::lng_info_bio_label();
		addInfoLine(std::move(label), AboutValue(user));

		auto usernameDrawableText = UsernameValue(
			user
		) | rpl::map([](TextWithEntities &&username) {
			return username.text.isEmpty()
				? TextWithEntities()
				: Ui::Text::Link(username.text);
		});

		auto usernameInfo = addInfoOneLineInline(
			tr::lng_info_username_label(),
			std::move(usernameDrawableText),
			tr::lng_context_copy_mention(tr::now));

		usernameInfo->setClickHandlerFilter([user](auto&&...) {
			const auto usernameText = user->userName();
			if (!usernameText.isEmpty()) {
				QGuiApplication::clipboard()->setText('@' + usernameText);
				Ui::Toast::Show(ktr("ktg_mention_copied"));
			}
			return false;
		});

		const auto controller = _controller->parentController();
		AddMainButton(
			result,
			tr::lng_info_add_as_contact(),
			CanAddContactValue(user),
			[=] { controller->window().show(Box(EditContactBox, controller, user)); },
			tracker);
	} else {
		if (ShowChatId() != 0) {
			auto idDrawableText = IDValue(
				_peer
			) | rpl::map([](TextWithEntities &&text) {
				return Ui::Text::Link(text.text);
			});
			auto idInfo = addInfoOneLineInline(
				(_peer->isChat()
					? rktr("ktg_profile_group_id")
					: _peer->isMegagroup()
					? rktr("ktg_profile_supergroup_id")
					: rktr("ktg_profile_channel_id")),
				std::move(idDrawableText),
				ktr("ktg_profile_copy_id"));

			idInfo->setClickHandlerFilter([peer = _peer](auto&&...) {
				const auto idText = IDString(peer);
				if (!idText.isEmpty()) {
					QGuiApplication::clipboard()->setText(idText);
					Ui::Toast::Show(peer->isChat()
						? ktr("ktg_group_id_copied")
						: peer->isMegagroup()
						? ktr("ktg_supergroup_id_copied")
						: ktr("ktg_channel_id_copied"));
				}
				return false;
			});
		}

		auto linkText = LinkValue(
			_peer
		) | rpl::map([](const QString &link) {
			return link.isEmpty()
				? TextWithEntities()
				: Ui::Text::Link(
					(link.startsWith(qstr("https://"))
						? link.mid(qstr("https://").size())
						: link),
					link);
		});
		auto link = addInfoOneLine(
			tr::lng_info_link_label(),
			std::move(linkText),
			QString());
		link->setClickHandlerFilter([peer = _peer](auto&&...) {
			const auto link = peer->session().createInternalLinkFull(
				peer->userName());
			if (!link.isEmpty()) {
				QGuiApplication::clipboard()->setText(link);
				Ui::Toast::Show(tr::lng_username_copied(tr::now));
			}
			return false;
		});

		if (const auto channel = _peer->asChannel()) {
			auto locationText = LocationValue(
				channel
			) | rpl::map([](const ChannelLocation *location) {
				return location
					? Ui::Text::Link(
						TextUtilities::SingleLine(location->address),
						LocationClickHandler::Url(location->point))
					: TextWithEntities();
			});
			addInfoOneLine(
				tr::lng_info_location_label(),
				std::move(locationText),
				QString()
			)->setLinksTrusted();
		}

		addInfoLine(tr::lng_info_about_label(), AboutValue(_peer));

		if (const auto channel = _peer->asChannel()) {
			const auto controller = _controller->parentController();
			auto viewLinkedGroup = [=] {
				controller->showPeerHistory(
					channel->linkedChat(),
					Window::SectionShow::Way::Forward);
			};
			AddMainButton(
				result,
				(channel->isBroadcast() ? tr::lng_channel_discuss() : tr::lng_manage_linked_channel()),
				HasLinkedChatValue(channel),
				std::move(viewLinkedGroup),
				tracker);
		}
	}
	if (!_peer->isSelf() && !cProfileTopBarNotifications()) {
		// No notifications toggle for Self => no separator.
		result->add(object_ptr<Ui::SlideWrap<>>(
			result,
			object_ptr<Ui::PlainShadow>(result),
			st::infoProfileSeparatorPadding)
		)->setDuration(
			st::infoSlideDuration
		)->toggleOn(
			std::move(tracker).atLeastOneShownValue()
		);
	}
	object_ptr<FloatingIcon>(
		result,
		st::infoIconInformation,
		st::infoInformationIconPosition);
	return result;
}

object_ptr<Ui::RpWidget> DetailsFiller::setupMuteToggle() {
	const auto peer = _peer;
	auto result = object_ptr<Ui::SettingsButton>(
		_wrap,
		tr::lng_profile_enable_notifications(),
		st::infoNotificationsButton);
	result->toggleOn(
		NotificationsEnabledValue(peer)
	)->addClickHandler([=] {
		const auto muteForSeconds = peer->owner().notifyIsMuted(peer)
			? 0
			: Data::NotifySettings::kDefaultMutePeriod;
		peer->owner().updateNotifySettings(peer, muteForSeconds);
	});
	object_ptr<FloatingIcon>(
		result,
		st::infoIconNotifications,
		st::infoNotificationsIconPosition);
	return result;
}

void DetailsFiller::setupMainButtons() {
	auto wrapButtons = [=](auto &&callback) {
		auto topSkip = _wrap->add(CreateSlideSkipWidget(_wrap));
		auto tracker = callback();
		topSkip->toggleOn(std::move(tracker).atLeastOneShownValue());
	};
	if (auto user = _peer->asUser()) {
		wrapButtons([=] {
			return fillUserButtons(user);
		});
	} else if (auto channel = _peer->asChannel()) {
		if (!channel->isMegagroup()) {
			wrapButtons([=] {
				return fillChannelButtons(channel);
			});
		}
	}
}

Ui::MultiSlideTracker DetailsFiller::fillUserButtons(
		not_null<UserData*> user) {
	using namespace rpl::mappers;

	Ui::MultiSlideTracker tracker;
	auto window = _controller->parentController();

	auto addSendMessageButton = [&] {
		auto activePeerValue = window->activeChatValue(
		) | rpl::map([](Dialogs::Key key) {
			return key.peer();
		});
		auto sendMessageVisible = rpl::combine(
			_controller->wrapValue(),
			std::move(activePeerValue),
			(_1 != Wrap::Side) || (_2 != user));
		auto sendMessage = [window, user] {
			window->showPeerHistory(
				user,
				Window::SectionShow::Way::Forward);
		};
		AddMainButton(
			_wrap,
			tr::lng_profile_send_message(),
			std::move(sendMessageVisible),
			std::move(sendMessage),
			tracker);
	};

	/*
	if (user->isSelf()) {
		auto separator = _wrap->add(object_ptr<Ui::SlideWrap<>>(
			_wrap,
			object_ptr<Ui::PlainShadow>(_wrap),
			st::infoProfileSeparatorPadding)
		)->setDuration(
			st::infoSlideDuration
		);

		addSendMessageButton();

		separator->toggleOn(
			std::move(tracker).atLeastOneShownValue()
		);
	} else {
	*/
		addSendMessageButton();
	/*
	}
	*/
	return tracker;
}

Ui::MultiSlideTracker DetailsFiller::fillChannelButtons(
		not_null<ChannelData*> channel) {
	using namespace rpl::mappers;

	Ui::MultiSlideTracker tracker;
	auto window = _controller->parentController();
	auto activePeerValue = window->activeChatValue(
	) | rpl::map([](Dialogs::Key key) {
		return key.peer();
	});
	auto viewChannelVisible = rpl::combine(
		_controller->wrapValue(),
		std::move(activePeerValue),
		(_1 != Wrap::Side) || (_2 != channel));
	auto viewChannel = [=] {
		window->showPeerHistory(
			channel,
			Window::SectionShow::Way::Forward);
	};
	AddMainButton(
		_wrap,
		tr::lng_profile_view_channel(),
		std::move(viewChannelVisible),
		std::move(viewChannel),
		tracker);

	return tracker;
}

object_ptr<Ui::RpWidget> DetailsFiller::fill() {
	add(object_ptr<Ui::BoxContentDivider>(_wrap));
	add(CreateSkipWidget(_wrap));
	add(setupInfo());
	if (!_peer->isSelf() && !cProfileTopBarNotifications()) {
		add(setupMuteToggle());
	}
	setupMainButtons();
	add(CreateSkipWidget(_wrap));
	return std::move(_wrap);
}

ActionsFiller::ActionsFiller(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer)
: _controller(controller)
, _parent(parent)
, _peer(peer) {
}

void ActionsFiller::addInviteToGroupAction(
		not_null<UserData*> user) {
	AddActionButton(
		_wrap,
		tr::lng_profile_invite_to_group(),
		CanInviteBotToGroupValue(user),
		[=] { AddBotToGroupBoxController::Start(user); });
}

void ActionsFiller::addShareContactAction(not_null<UserData*> user) {
	const auto controller = _controller;
	AddActionButton(
		_wrap,
		tr::lng_info_share_contact(),
		CanShareContactValue(user),
		[=] { Window::PeerMenuShareContactBox(controller, user); });
}

void ActionsFiller::addEditContactAction(not_null<UserData*> user) {
	const auto controller = _controller->parentController();
	AddActionButton(
		_wrap,
		tr::lng_info_edit_contact(),
		IsContactValue(user),
		[=] { controller->window().show(Box(EditContactBox, controller, user)); });
}

void ActionsFiller::addDeleteContactAction(
		not_null<UserData*> user) {
	AddActionButton(
		_wrap,
		tr::lng_info_delete_contact(),
		IsContactValue(user),
		[user] { Window::PeerMenuDeleteContact(user); });
}

void ActionsFiller::addClearHistoryAction(not_null<UserData*> user) {
	AddActionButton(
		_wrap,
		tr::lng_profile_clear_history(),
		rpl::single(true),
		Window::ClearHistoryHandler(user));
}

void ActionsFiller::addDeleteConversationAction(
		not_null<UserData*> user) {
	AddActionButton(
		_wrap,
		tr::lng_profile_delete_conversation(),
		rpl::single(true),
		Window::DeleteAndLeaveHandler(user));
}

void ActionsFiller::addBotCommandActions(not_null<UserData*> user) {
	auto findBotCommand = [user](const QString &command) {
		if (!user->isBot()) {
			return QString();
		}
		for (const auto &data : user->botInfo->commands) {
			const auto isSame = !data.command.compare(
				command,
				Qt::CaseInsensitive);
			if (isSame) {
				return data.command;
			}
		}
		return QString();
	};
	auto hasBotCommandValue = [=](const QString &command) {
		return user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::BotCommands
		) | rpl::map([=] {
			return !findBotCommand(command).isEmpty();
		});
	};
	auto sendBotCommand = [=, window = _controller->parentController()](
			const QString &command) {
		const auto original = findBotCommand(command);
		if (original.isEmpty()) {
			return;
		}
		BotCommandClickHandler('/' + original).onClick(ClickContext{
			Qt::LeftButton,
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(window.get()),
				.peer = user,
			})
		});
	};
	auto addBotCommand = [=](
			rpl::producer<QString> text,
			const QString &command) {
		AddActionButton(
			_wrap,
			std::move(text),
			hasBotCommandValue(command),
			[=] { sendBotCommand(command); });
	};
	addBotCommand(tr::lng_profile_bot_help(), qsl("help"));
	addBotCommand(tr::lng_profile_bot_settings(), qsl("settings"));
	addBotCommand(tr::lng_profile_bot_privacy(), qsl("privacy"));
}

void ActionsFiller::addReportAction() {
	const auto peer = _peer;
	const auto controller = _controller->parentController();
	const auto report = [=] {
		HistoryView::ShowReportPeerBox(controller, peer);
	};
	AddActionButton(
		_wrap,
		tr::lng_profile_report(),
		rpl::single(true),
		report,
		st::infoBlockButton);
}

void ActionsFiller::addBlockAction(not_null<UserData*> user) {
	const auto window = &_controller->parentController()->window();

	auto text = user->session().changes().peerFlagsValue(
		user,
		Data::PeerUpdate::Flag::IsBlocked
	) | rpl::map([=] {
		switch (user->blockStatus()) {
		case UserData::BlockStatus::Blocked:
			return ((user->isBot() && !user->isSupport())
				? tr::lng_profile_restart_bot
				: tr::lng_profile_unblock_user)();
		case UserData::BlockStatus::NotBlocked:
		default:
			return ((user->isBot() && !user->isSupport())
				? tr::lng_profile_block_bot
				: tr::lng_profile_block_user)();
		}
	}) | rpl::flatten_latest(
	) | rpl::start_spawning(_wrap->lifetime());

	auto toggleOn = rpl::duplicate(
		text
	) | rpl::map([](const QString &text) {
		return !text.isEmpty();
	});
	auto callback = [=] {
		if (user->isBlocked()) {
			Window::PeerMenuUnblockUserWithBotRestart(user);
			if (user->isBot()) {
				Ui::showPeerHistory(user, ShowAtUnreadMsgId);
			}
		} else if (user->isBot()) {
			user->session().api().blockedPeers().block(user);
		} else {
			window->show(Box(
				Window::PeerMenuBlockUserBox,
				window,
				user,
				v::null,
				v::null));
		}
	};
	AddActionButton(
		_wrap,
		rpl::duplicate(text),
		std::move(toggleOn),
		std::move(callback),
		st::infoBlockButton);
}

void ActionsFiller::addLeaveChannelAction(
		not_null<ChannelData*> channel) {
	AddActionButton(
		_wrap,
		tr::lng_profile_leave_channel(),
		AmInChannelValue(channel),
		Window::DeleteAndLeaveHandler(channel));
}

void ActionsFiller::addJoinChannelAction(
		not_null<ChannelData*> channel) {
	using namespace rpl::mappers;
	auto joinVisible = AmInChannelValue(channel)
		| rpl::map(!_1)
		| rpl::start_spawning(_wrap->lifetime());
	AddActionButton(
		_wrap,
		tr::lng_profile_join_channel(),
		rpl::duplicate(joinVisible),
		[=] { channel->session().api().joinChannel(channel); });
	_wrap->add(object_ptr<Ui::SlideWrap<Ui::FixedHeightWidget>>(
		_wrap,
		CreateSkipWidget(
			_wrap,
			st::infoBlockButtonSkip))
	)->setDuration(
		st::infoSlideDuration
	)->toggleOn(
		rpl::duplicate(joinVisible)
	);
}

void ActionsFiller::fillUserActions(not_null<UserData*> user) {
	if (user->isBot()) {
		addInviteToGroupAction(user);
	}
	addShareContactAction(user);
	if (!user->isSelf()) {
		addEditContactAction(user);
		addDeleteContactAction(user);
	}
	addClearHistoryAction(user);
	addDeleteConversationAction(user);
	if (!user->isSelf() && !user->isSupport()) {
		if (user->isBot()) {
			addBotCommandActions(user);
		}
		_wrap->add(CreateSkipWidget(
			_wrap,
			st::infoBlockButtonSkip));
		if (user->isBot()) {
			addReportAction();
		}
		addBlockAction(user);
	}
}

void ActionsFiller::fillChannelActions(
		not_null<ChannelData*> channel) {
	using namespace rpl::mappers;

	addJoinChannelAction(channel);
	addLeaveChannelAction(channel);
	if (!channel->amCreator()) {
		addReportAction();
	}
}

object_ptr<Ui::RpWidget> ActionsFiller::fill() {
	auto wrapResult = [=](auto &&callback) {
		_wrap = object_ptr<Ui::VerticalLayout>(_parent);
		_wrap->add(CreateSkipWidget(_wrap));
		callback();
		_wrap->add(CreateSkipWidget(_wrap));
		object_ptr<FloatingIcon>(
			_wrap,
			st::infoIconActions,
			st::infoIconPosition);
		return std::move(_wrap);
	};
	if (auto user = _peer->asUser()) {
		return wrapResult([=] {
			fillUserActions(user);
		});
	} else if (auto channel = _peer->asChannel()) {
		if (channel->isMegagroup()) {
			return { nullptr };
		}
		return wrapResult([=] {
			fillChannelActions(channel);
		});
	}
	return { nullptr };
}

ManageFiller::ManageFiller(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer)
: _controller(controller)
, _parent(parent)
, _peer(peer) {
}

void ManageFiller::addPeerPermissions(
		not_null<PeerData*> peer) {
	if (peer->isUser() || (peer->isChannel() && !peer->isMegagroup())) return;

	const auto canEditPermissions = [&] {
		return peer->isChannel()
			? peer->asChannel()->canEditPermissions()
			: peer->asChat()->canEditPermissions();
	}();

	if (canEditPermissions) {
		const auto controller = _controller;
		auto button = AddActionButton(
			_wrap,
			tr::lng_manage_peer_permissions(),
			rpl::single(true),
			[=] { ShowEditPermissions(controller->parentController(), peer); }
			);
		object_ptr<FloatingIcon>(
			button,
			st::infoIconPermissions,
			st::infoSharedMediaButtonIconPosition);
	}
}

void ManageFiller::addPeerAdmins(
		not_null<PeerData*> peer) {
	if (peer->isUser()) return;

	const auto canViewAdmins = [&] {
		return peer->isChannel()
			? peer->asChannel()->canViewAdmins()
			: peer->asChat()->amIn();
	}();
	if (canViewAdmins) {
		const auto controller = _controller;
		auto button = AddActionButton(
			_wrap,
			tr::lng_manage_peer_administrators(),
			rpl::single(true),
			[=] { ParticipantsBoxController::Start(
					controller->parentController(),
					peer,
					ParticipantsBoxController::Role::Admins);}
			);
		object_ptr<FloatingIcon>(
			button,
			st::infoIconAdministrators,
			st::infoSharedMediaButtonIconPosition);
	}
}

void ManageFiller::addPeerInviteLinks(
		not_null<PeerData*> peer) {
	if (peer->isUser()) return;

	const auto canHaveInviteLink = [&] {
		return peer->isChannel()
			? peer->asChannel()->canHaveInviteLink()
			: peer->asChat()->canHaveInviteLink();
	}();
	if (canHaveInviteLink) {
		auto button = AddActionButton(
			_wrap,
			tr::lng_manage_peer_invite_links(),
			rpl::single(true),
			[=] {
				Ui::show(Box(ManageInviteLinksBox, peer, peer->session().user(), 0, 0),
					Ui::LayerOption::KeepOther);
			});
		object_ptr<FloatingIcon>(
			button,
			st::infoIconInviteLinks,
			st::infoSharedMediaButtonIconPosition);
	}
}

void ManageFiller::addChannelBlockedUsers(
		not_null<ChannelData*> channel) {
	if (channel->hasAdminRights() || channel->amCreator()) {
		const auto controller = _controller;
		auto button = AddActionButton(
			_wrap,
			tr::lng_manage_peer_removed_users(),
			rpl::single(true),
			[=] { ParticipantsBoxController::Start(
					controller->parentController(),
					channel,
					ParticipantsBoxController::Role::Kicked);}
			);
		object_ptr<FloatingIcon>(
			button,
			st::infoIconBlacklist,
			st::infoSharedMediaButtonIconPosition);
	}
}

void ManageFiller::addChannelRecentActions(
		not_null<ChannelData*> channel) {
	if (channel->hasAdminRights() || channel->amCreator()) {
		const auto controller = _controller;
		auto button = AddActionButton(
			_wrap,
			tr::lng_manage_peer_recent_actions(),
			rpl::single(true),
			[=] {
				if (!App::main()->areRecentActionsOpened()) {
					controller->showSection(
						std::make_shared<AdminLog::SectionMemento>(channel));
				}
			});
		object_ptr<FloatingIcon>(
			button,
			st::infoIconRecentActions,
			st::infoSharedMediaButtonIconPosition);
	}
}

void ManageFiller::fillChatActions(
		not_null<ChatData*> chat) {
	addPeerPermissions(chat);
	addPeerAdmins(chat);
	addPeerInviteLinks(chat);
}

void ManageFiller::fillChannelActions(
		not_null<ChannelData*> channel) {
	addPeerPermissions(channel);
	addPeerAdmins(channel);
	addPeerInviteLinks(channel);
	addChannelBlockedUsers(channel);
	addChannelRecentActions(channel);
}

object_ptr<Ui::RpWidget> ManageFiller::fill() {
	auto wrapResult = [=](auto &&callback) {
		_wrap = object_ptr<Ui::VerticalLayout>(_parent);
		_wrap->add(CreateSkipWidget(_wrap));
		callback();
		_wrap->add(CreateSkipWidget(_wrap));
		return std::move(_wrap);
	};
	if (auto chat = _peer->asChat()) {
		return wrapResult([=] {
			fillChatActions(chat);
		});
	} else if (auto channel = _peer->asChannel()) {
		if (channel->isMegagroup() || channel->hasAdminRights() || channel->amCreator()) {
			return wrapResult([=] {
				fillChannelActions(channel);
			});
		}
	}
	return { nullptr };
}

} // namespace

object_ptr<Ui::RpWidget> SetupDetails(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer) {
	DetailsFiller filler(controller, parent, peer);
	return filler.fill();
}

object_ptr<Ui::RpWidget> SetupManage(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer) {
	ManageFiller filler(controller, parent, peer);
	return filler.fill();
}

object_ptr<Ui::RpWidget> SetupActions(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer) {
	ActionsFiller filler(controller, parent, peer);
	return filler.fill();
}

void SetupAddChannelMember(
		not_null<Window::SessionNavigation*> navigation,
		not_null<Ui::RpWidget*> parent,
		not_null<ChannelData*> channel) {
	auto add = Ui::CreateChild<Ui::IconButton>(
		parent.get(),
		st::infoMembersAddMember);
	add->showOn(CanAddMemberValue(channel));
	add->addClickHandler([=] {
		Window::PeerMenuAddChannelMembers(navigation, channel);
	});
	parent->widthValue(
	) | rpl::start_with_next([add](int newWidth) {
		auto availableWidth = newWidth
			- st::infoMembersButtonPosition.x();
		add->moveToLeft(
			availableWidth - add->width(),
			st::infoMembersButtonPosition.y(),
			newWidth);
	}, add->lifetime());
}

object_ptr<Ui::RpWidget> SetupChannelMembers(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer) {
	using namespace rpl::mappers;

	auto channel = peer->asChannel();
	if (!channel || channel->isMegagroup()) {
		return { nullptr };
	}

	auto membersShown = rpl::combine(
		MembersCountValue(channel),
		Data::PeerFlagValue(
			channel,
			ChannelDataFlag::CanViewParticipants),
			(_1 > 0) && _2);
	auto membersText = tr::lng_chat_status_subscribers(
		lt_count_decimal,
		MembersCountValue(channel) | tr::to_count());
	auto membersCallback = [=] {
		controller->showSection(std::make_shared<Info::Memento>(
			channel,
			Section::Type::Members));
	};

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	result->setDuration(
		st::infoSlideDuration
	)->toggleOn(
		std::move(membersShown)
	);

	auto members = result->entity();
	members->add(object_ptr<Ui::BoxContentDivider>(members));
	members->add(CreateSkipWidget(members));
	auto button = AddActionButton(
		members,
		std::move(membersText),
		rpl::single(true),
		std::move(membersCallback))->entity();

	SetupAddChannelMember(controller, button, channel);

	object_ptr<FloatingIcon>(
		members,
		st::infoIconMembers,
		st::infoChannelMembersIconPosition);
	members->add(CreateSkipWidget(members));

	return result;
}

} // namespace Profile
} // namespace Info
