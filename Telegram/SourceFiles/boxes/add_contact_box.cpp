/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/add_contact_box.h"

#include "lang/lang_keys.h"
#include "base/random.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peers/add_participants_box.h"
#include "boxes/peers/edit_peer_common.h"
#include "boxes/peers/edit_participant_box.h"
#include "boxes/peers/edit_participants_box.h"
#include "core/application.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "countries/countries_instance.h" // Countries::ExtractPhoneCode.
#include "window/window_session_controller.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/toast/toast.h"
#include "ui/special_buttons.h"
#include "ui/special_fields.h"
#include "ui/text/text_options.h"
#include "ui/unread_badge.h"
#include "ui/ui_utility.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_cloud_file.h"
#include "apiwrap.h"
#include "api/api_invite_links.h"
#include "api/api_peer_photo.h"
#include "main/main_session.h"
#include "facades.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace {

bool IsValidPhone(QString phone) {
	phone = phone.replace(QRegularExpression(qsl("[^\\d]")), QString());
	return (phone.length() >= 8)
		|| (phone == qsl("333"))
		|| (phone.startsWith(qsl("42"))
			&& (phone.length() == 2
				|| phone.length() == 5
				|| phone.length() == 6
				|| phone == qsl("4242")));
}

void ChatCreateDone(
		not_null<Window::SessionNavigation*> navigation,
		QImage image,
		const MTPUpdates &updates) {
	navigation->session().api().applyUpdates(updates);

	const auto success = base::make_optional(&updates)
		| [](auto updates) -> std::optional<const QVector<MTPChat>*> {
			switch (updates->type()) {
			case mtpc_updates:
				return &updates->c_updates().vchats().v;
			case mtpc_updatesCombined:
				return &updates->c_updatesCombined().vchats().v;
			}
			LOG(("API Error: unexpected update cons %1 "
				"(GroupInfoBox::creationDone)").arg(updates->type()));
			return std::nullopt;
		}
		| [](auto chats) {
			return (!chats->empty()
				&& chats->front().type() == mtpc_chat)
				? base::make_optional(chats)
				: std::nullopt;
		}
		| [&](auto chats) {
			return navigation->session().data().chat(
				chats->front().c_chat().vid());
		}
		| [&](not_null<ChatData*> chat) {
			if (!image.isNull()) {
				chat->session().api().peerPhoto().upload(
					chat,
					std::move(image));
			}
			Ui::showPeerHistory(chat, ShowAtUnreadMsgId);
		};
	if (!success) {
		LOG(("API Error: chat not found in updates "
			"(ContactsBox::creationDone)"));
	}
}

} // namespace

style::InputField CreateBioFieldStyle() {
	auto result = st::newGroupDescription;
	result.textMargins.setRight(
		st::boxTextFont->spacew
		+ st::boxTextFont->width(QString::number(kMaxBioLength)));
	return result;
}

QString PeerFloodErrorText(
		not_null<Main::Session*> session,
		PeerFloodType type) {
	const auto link = textcmdLink(
		session->createInternalLinkFull(qsl("spambot")),
		tr::lng_cant_more_info(tr::now));
	if (type == PeerFloodType::InviteGroup) {
		return tr::lng_cant_invite_not_contact(tr::now, lt_more_info, link);
	}
	return tr::lng_cant_send_to_not_contact(tr::now, lt_more_info, link);
}

void ShowAddParticipantsError(
		const QString &error,
		not_null<PeerData*> chat,
		const std::vector<not_null<UserData*>> &users) {
	if (error == u"USER_BOT"_q) {
		const auto channel = chat->asChannel();
		if ((users.size() == 1)
			&& users.front()->isBot()
			&& channel
			&& !channel->isMegagroup()
			&& channel->canAddAdmins()) {
			const auto makeAdmin = [=] {
				const auto user = users.front();
				const auto weak = std::make_shared<QPointer<EditAdminBox>>();
				const auto close = [=](auto&&...) {
					if (*weak) {
						(*weak)->closeBox();
					}
				};
				const auto saveCallback = SaveAdminCallback(
					channel,
					user,
					close,
					close);
				auto box = Box<EditAdminBox>(
					channel,
					user,
					ChatAdminRightsInfo(),
					QString());
				box->setSaveCallback(saveCallback);
				*weak = Ui::show(std::move(box));
			};
			Ui::show(
				Box<Ui::ConfirmBox>(
					tr::lng_cant_invite_offer_admin(tr::now),
					tr::lng_cant_invite_make_admin(tr::now),
					tr::lng_cancel(tr::now),
					makeAdmin),
				Ui::LayerOption::KeepOther);
			return;
		}
	}
	const auto hasBot = ranges::any_of(users, &UserData::isBot);
	const auto text = [&] {
		if (error == u"USER_BOT"_q) {
			return tr::lng_cant_invite_bot_to_channel(tr::now);
		} else if (error == u"USER_LEFT_CHAT"_q) {
			// Trying to return a user who has left.
		} else if (error == u"USER_KICKED"_q) {
			// Trying to return a user who was kicked by admin.
			return tr::lng_cant_invite_banned(tr::now);
		} else if (error == u"USER_PRIVACY_RESTRICTED"_q) {
			return tr::lng_cant_invite_privacy(tr::now);
		} else if (error == u"USER_NOT_MUTUAL_CONTACT"_q) {
			// Trying to return user who does not have me in contacts.
			return tr::lng_failed_add_not_mutual(tr::now);
		} else if (error == u"USER_ALREADY_PARTICIPANT"_q && hasBot) {
			return tr::lng_bot_already_in_group(tr::now);
		} else if (error == u"BOT_GROUPS_BLOCKED"_q) {
			return tr::lng_error_cant_add_bot(tr::now);
		} else if (error == u"PEER_FLOOD"_q) {
			const auto type = (chat->isChat() || chat->isMegagroup())
				? PeerFloodType::InviteGroup
				: PeerFloodType::InviteChannel;
			return PeerFloodErrorText(&chat->session(), type);
		} else if (error == u"ADMINS_TOO_MUCH"_q) {
			return ((chat->isChat() || chat->isMegagroup())
				? tr::lng_error_admin_limit
				: tr::lng_error_admin_limit_channel)(tr::now);
		}
		return tr::lng_failed_add_participant(tr::now);
	}();
	Ui::show(Box<Ui::InformBox>(text), Ui::LayerOption::KeepOther);
}

class RevokePublicLinkBox::Inner : public TWidget {
public:
	Inner(
		QWidget *parent,
		not_null<Main::Session*> session,
		Fn<void()> revokeCallback);

protected:
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	struct ChatRow {
		ChatRow(not_null<PeerData*> peer) : peer(peer) {
		}

		not_null<PeerData*> peer;
		mutable std::shared_ptr<Data::CloudImageView> userpic;
		Ui::Text::String name, status;
	};
	void paintChat(Painter &p, const ChatRow &row, bool selected) const;
	void updateSelected();

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	PeerData *_selected = nullptr;
	PeerData *_pressed = nullptr;

	std::vector<ChatRow> _rows;

	int _rowsTop = 0;
	int _rowHeight = 0;
	int _revokeWidth = 0;

	Fn<void()> _revokeCallback;
	mtpRequestId _revokeRequestId = 0;

};

AddContactBox::AddContactBox(
	QWidget*,
	not_null<Main::Session*> session)
: AddContactBox(nullptr, session, QString(), QString(), QString()) {
}

AddContactBox::AddContactBox(
	QWidget*,
	not_null<Main::Session*> session,
	QString fname,
	QString lname,
	QString phone)
: _session(session)
, _first(this, st::defaultInputField, tr::lng_signup_firstname(), fname)
, _last(this, st::defaultInputField, tr::lng_signup_lastname(), lname)
, _phone(
	this,
	st::defaultInputField,
	tr::lng_contact_phone(),
	Countries::ExtractPhoneCode(session->user()->phone()),
	phone,
	[](const QString &s) { return Countries::Groups(s); })
, _invertOrder(langFirstNameGoesSecond()) {
	if (!phone.isEmpty()) {
		_phone->setDisabled(true);
	}
}

void AddContactBox::prepare() {
	if (_invertOrder) {
		setTabOrder(_last, _first);
	}
	const auto readyToAdd = !_phone->getLastText().isEmpty()
		&& (!_first->getLastText().isEmpty()
			|| !_last->getLastText().isEmpty());
	setTitle(readyToAdd
		? tr::lng_confirm_contact_data()
		: tr::lng_enter_contact_data());
	updateButtons();

	connect(_first, &Ui::InputField::submitted, [=] { submit(); });
	connect(_last, &Ui::InputField::submitted, [=] { submit(); });
	connect(_phone, &Ui::PhoneInput::submitted, [=] { submit(); });

	setDimensions(
		st::boxWideWidth,
		st::contactPadding.top()
			+ _first->height()
			+ st::contactSkip
			+ _last->height()
			+ st::contactPhoneSkip
			+ _phone->height()
			+ st::contactPadding.bottom()
			+ st::boxPadding.bottom());
}

void AddContactBox::setInnerFocus() {
	if ((_first->getLastText().isEmpty() && _last->getLastText().isEmpty())
		|| !_phone->isEnabled()) {
		(_invertOrder ? _last : _first)->setFocusFast();
		_phone->finishAnimating();
	} else {
		_phone->setFocusFast();
	}
}

void AddContactBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);
	if (_retrying) {
		p.setPen(st::boxTextFg);
		p.setFont(st::boxTextFont);
		const auto textHeight = height()
			- st::contactPadding.top()
			- st::contactPadding.bottom()
			- st::boxPadding.bottom();
		p.drawText(
			QRect(
				st::boxPadding.left(),
				st::contactPadding.top(),
				width() - st::boxPadding.left() - st::boxPadding.right(),
				textHeight),
			tr::lng_contact_not_joined(tr::now, lt_name, _sentName),
			style::al_topleft);
	} else {
		st::contactUserIcon.paint(
			p,
			st::boxPadding.left() + st::contactIconPosition.x(),
			_first->y() + st::contactIconPosition.y(),
			width());
		st::contactPhoneIcon.paint(
			p,
			st::boxPadding.left() + st::contactIconPosition.x(),
			_phone->y() + st::contactIconPosition.y(),
			width());
	}
}

void AddContactBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_first->resize(
		width()
			- st::boxPadding.left()
			- st::contactPadding.left()
			- st::boxPadding.right(),
		_first->height());
	_last->resize(_first->width(), _last->height());
	_phone->resize(_first->width(), _last->height());
	const auto left = st::boxPadding.left() + st::contactPadding.left();
	const auto &firstRow = _invertOrder ? _last : _first;
	const auto &secondRow = _invertOrder ? _first : _last;
	const auto &thirdRow = _phone;
	firstRow->moveToLeft(left, st::contactPadding.top());
	secondRow->moveToLeft(
		left,
		firstRow->y() + firstRow->height() + st::contactSkip);
	thirdRow->moveToLeft(
		left,
		secondRow->y() + secondRow->height() + st::contactPhoneSkip);
}

void AddContactBox::submit() {
	if (_first->hasFocus()) {
		_last->setFocus();
	} else if (_last->hasFocus()) {
		if (_phone->isEnabled()) {
			_phone->setFocus();
		} else {
			save();
		}
	} else if (_phone->hasFocus()) {
		save();
	}
}

void AddContactBox::save() {
	if (_addRequest) {
		return;
	}

	auto firstName = TextUtilities::PrepareForSending(
		_first->getLastText());
	auto lastName = TextUtilities::PrepareForSending(
		_last->getLastText());
	const auto phone = _phone->getLastText().trimmed();
	if (firstName.isEmpty() && lastName.isEmpty()) {
		if (_invertOrder) {
			_last->setFocus();
			_last->showError();
		} else {
			_first->setFocus();
			_first->showError();
		}
		return;
	} else if (!IsValidPhone(phone)) {
		_phone->setFocus();
		_phone->showError();
		return;
	}
	if (firstName.isEmpty()) {
		firstName = lastName;
		lastName = QString();
	}
	_sentName = firstName;
	_contactId = base::RandomValue<uint64>();
	_addRequest = _session->api().request(MTPcontacts_ImportContacts(
		MTP_vector<MTPInputContact>(
			1,
			MTP_inputPhoneContact(
				MTP_long(_contactId),
				MTP_string(phone),
				MTP_string(firstName),
				MTP_string(lastName)))
	)).done(crl::guard(this, [=](
			const MTPcontacts_ImportedContacts &result) {
		result.match([&](const MTPDcontacts_importedContacts &data) {
			_session->data().processUsers(data.vusers());

			const auto extractUser = [&](const MTPImportedContact &data) {
				return data.match([&](const MTPDimportedContact &data) {
					return (data.vclient_id().v == _contactId)
						? _session->data().userLoaded(data.vuser_id())
						: nullptr;
				});
			};
			const auto &list = data.vimported().v;
			const auto user = list.isEmpty()
				? nullptr
				: extractUser(list.front());
			if (user) {
				if (user->isContact() || user->session().supportMode()) {
					Ui::showPeerHistory(user, ShowAtTheEndMsgId);
				}
				Ui::hideLayer();
			} else if (isBoxShown()) {
				hideChildren();
				_retrying = true;
				updateButtons();
				update();
			}
		});
	})).send();
}

void AddContactBox::retry() {
	_addRequest = 0;
	_contactId = 0;
	showChildren();
	_retrying = false;
	updateButtons();
	_first->setText(QString());
	_last->setText(QString());
	_phone->clearText();
	_phone->setDisabled(false);
	_first->setFocus();
	update();
}

void AddContactBox::updateButtons() {
	clearButtons();
	if (_retrying) {
		addButton(tr::lng_try_other_contact(), [=] { retry(); });
	} else {
		addButton(tr::lng_add_contact(), [=] { save(); });
		addButton(tr::lng_cancel(), [=] { closeBox(); });
	}
}

GroupInfoBox::GroupInfoBox(
	QWidget*,
	not_null<Window::SessionNavigation*> navigation,
	Type type,
	const QString &title,
	Fn<void(not_null<ChannelData*>)> channelDone)
: _navigation(navigation)
, _api(&_navigation->session().mtp())
, _type(type)
, _initialTitle(title)
, _channelDone(std::move(channelDone)) {
}

void GroupInfoBox::prepare() {
	setMouseTracking(true);

	_photo.create(
		this,
		&_navigation->parentController()->window(),
		((_type == Type::Channel)
			? tr::lng_create_channel_crop
			: tr::lng_create_group_crop)(tr::now),
		Ui::UserpicButton::Role::ChangePhoto,
		st::defaultUserpicButton);
	_title.create(
		this,
		st::defaultInputField,
		(_type == Type::Channel
			? tr::lng_dlg_new_channel_name
			: tr::lng_dlg_new_group_name)(),
		_initialTitle);
	_title->setMaxLength(Ui::EditPeer::kMaxGroupChannelTitle);
	_title->setInstantReplaces(Core::App().settings().instantReplacesValue());
	Ui::Emoji::SuggestionsController::Init(
		getDelegate()->outerContainer(),
		_title,
		&_navigation->session());

	if (_type != Type::Group) {
		_description.create(
			this,
			st::newGroupDescription,
			Ui::InputField::Mode::MultiLine,
			tr::lng_create_group_description());
		_description->show();
		_description->setMaxLength(Ui::EditPeer::kMaxChannelDescription);
		_description->setInstantReplaces(Core::App().settings().instantReplacesValue());
		_description->setSubmitSettings(
			Core::App().settings().sendSubmitWay());

		connect(_description, &Ui::InputField::resized, [=] {
			descriptionResized();
		});
		connect(_description, &Ui::InputField::submitted, [=] {
			submit();
		});
		connect(_description, &Ui::InputField::cancelled, [=] {
			closeBox();
		});

		Ui::Emoji::SuggestionsController::Init(
			getDelegate()->outerContainer(),
			_description,
			&_navigation->session());
	}

	connect(_title, &Ui::InputField::submitted, [=] { submitName(); });

	addButton(
		(_type != Type::Group
			? tr::lng_create_group_create()
			: tr::lng_create_group_next()),
		[=] { submit(); });
	addButton(tr::lng_cancel(), [this] { closeBox(); });

	updateMaxHeight();
}

void GroupInfoBox::setInnerFocus() {
	_title->setFocusFast();
}

void GroupInfoBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_photo->moveToLeft(
		st::boxPadding.left() + st::newGroupInfoPadding.left(),
		st::boxPadding.top() + st::newGroupInfoPadding.top());

	const auto nameLeft = st::defaultUserpicButton.size.width()
		+ st::newGroupNamePosition.x();
	_title->resize(
		width()
			- st::boxPadding.left()
			- st::newGroupInfoPadding.left()
			- st::boxPadding.right()
			- nameLeft,
		_title->height());
	_title->moveToLeft(
		st::boxPadding.left() + st::newGroupInfoPadding.left() + nameLeft,
		st::boxPadding.top()
			+ st::newGroupInfoPadding.top()
			+ st::newGroupNamePosition.y());
	if (_description) {
		_description->resize(
			width()
				- st::boxPadding.left()
				- st::newGroupInfoPadding.left()
				- st::boxPadding.right(),
			_description->height());
		const auto descriptionLeft = st::boxPadding.left()
			+ st::newGroupInfoPadding.left();
		const auto descriptionTop = st::boxPadding.top()
			+ st::newGroupInfoPadding.top()
			+ st::defaultUserpicButton.size.height()
			+ st::newGroupDescriptionPadding.top();
		_description->moveToLeft(descriptionLeft, descriptionTop);
	}
}

void GroupInfoBox::submitName() {
	if (_title->getLastText().trimmed().isEmpty()) {
		_title->setFocus();
		_title->showError();
	} else if (_description) {
		_description->setFocus();
	} else {
		submit();
	}
}

void GroupInfoBox::createGroup(
		not_null<PeerListBox*> selectUsersBox,
		const QString &title,
		const std::vector<not_null<PeerData*>> &users) {
	if (_creationRequestId) {
		return;
	}
	using TLUsers = MTPInputUser;
	auto inputs = QVector<TLUsers>();
	inputs.reserve(users.size());
	for (auto peer : users) {
		auto user = peer->asUser();
		Assert(user != nullptr);
		if (!user->isSelf()) {
			inputs.push_back(user->inputUser);
		}
	}
	if (inputs.empty()) {
		return;
	}
	_creationRequestId = _api.request(MTPmessages_CreateChat(
		MTP_vector<TLUsers>(inputs),
		MTP_string(title)
	)).done([=](const MTPUpdates &result) {
		auto image = _photo->takeResultImage();
		const auto navigation = _navigation;

		Ui::hideLayer(); // Destroys 'this'.
		ChatCreateDone(navigation, std::move(image), result);
	}).fail([=](const MTP::Error &error) {
		const auto &type = error.type();
		_creationRequestId = 0;
		const auto controller = _navigation->parentController();
		if (type == u"NO_CHAT_TITLE"_q) {
			const auto weak = Ui::MakeWeak(this);
			selectUsersBox->closeBox();
			if (weak) {
				_title->showError();
			}
		} else if (type == u"USERS_TOO_FEW"_q) {
			controller->show(
				Box<Ui::InformBox>(tr::lng_cant_invite_privacy(tr::now)),
				Ui::LayerOption::KeepOther);
		} else if (type == u"PEER_FLOOD"_q) {
			controller->show(
				Box<Ui::InformBox>(
					PeerFloodErrorText(
						&_navigation->session(),
						PeerFloodType::InviteGroup)),
				Ui::LayerOption::KeepOther);
		} else if (type == u"USER_RESTRICTED"_q) {
			controller->show(
				Box<Ui::InformBox>(tr::lng_cant_do_this(tr::now)),
				Ui::LayerOption::KeepOther);
		}
	}).send();
}

void GroupInfoBox::submit() {
	if (_creationRequestId || _creatingInviteLink) {
		return;
	}

	auto title = TextUtilities::PrepareForSending(_title->getLastText());
	auto description = _description
		? TextUtilities::PrepareForSending(
			_description->getLastText(),
			TextUtilities::PrepareTextOption::CheckLinks)
		: QString();
	if (title.isEmpty()) {
		_title->setFocus();
		_title->showError();
		return;
	}
	if (_type != Type::Group) {
		createChannel(title, description);
	} else {
		auto initBox = [title, weak = Ui::MakeWeak(this)](
				not_null<PeerListBox*> box) {
			auto create = [box, title, weak] {
				if (weak) {
					auto rows = box->collectSelectedRows();
					if (!rows.empty()) {
						weak->createGroup(box, title, rows);
					}
				}
			};
			box->addButton(tr::lng_create_group_create(), std::move(create));
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		};
		Ui::show(
			Box<PeerListBox>(
				std::make_unique<AddParticipantsBoxController>(
					&_navigation->session()),
				std::move(initBox)),
			Ui::LayerOption::KeepOther);
	}
}

void GroupInfoBox::createChannel(
		const QString &title,
		const QString &description) {
	Expects(!_creationRequestId);

	const auto flags = (_type == Type::Megagroup)
		? MTPchannels_CreateChannel::Flag::f_megagroup
		: MTPchannels_CreateChannel::Flag::f_broadcast;
	_creationRequestId = _api.request(MTPchannels_CreateChannel(
		MTP_flags(flags),
		MTP_string(title),
		MTP_string(description),
		MTPInputGeoPoint(), // geo_point
		MTPstring() // address
	)).done([=](const MTPUpdates &result) {
		_navigation->session().api().applyUpdates(result);

		const auto success = base::make_optional(&result)
			| [](auto updates) -> std::optional<const QVector<MTPChat>*> {
				switch (updates->type()) {
				case mtpc_updates:
					return &updates->c_updates().vchats().v;
				case mtpc_updatesCombined:
					return &updates->c_updatesCombined().vchats().v;
				}
				LOG(("API Error: unexpected update cons %1 "
					"(GroupInfoBox::createChannel)").arg(updates->type()));
				return std::nullopt;
			}
			| [](auto chats) {
				return (!chats->empty()
						&& chats->front().type() == mtpc_channel)
					? base::make_optional(chats)
					: std::nullopt;
			}
			| [&](auto chats) {
				return _navigation->session().data().channel(
					chats->front().c_channel().vid());
			}
			| [&](not_null<ChannelData*> channel) {
				auto image = _photo->takeResultImage();
				if (!image.isNull()) {
					channel->session().api().peerPhoto().upload(
						channel,
						std::move(image));
				}
				channel->session().api().requestFullPeer(channel);
				_createdChannel = channel;
				checkInviteLink();
			};
		if (!success) {
			LOG(("API Error: channel not found in updates "
				"(GroupInfoBox::creationDone)"));
			closeBox();
		}
	}).fail([this](const MTP::Error &error) {
		const auto &type = error.type();
		_creationRequestId = 0;
		const auto controller = _navigation->parentController();
		if (type == u"NO_CHAT_TITLE"_q) {
			_title->setFocus();
			_title->showError();
		} else if (type == u"USER_RESTRICTED"_q) {
			controller->show(
				Box<Ui::InformBox>(tr::lng_cant_do_this(tr::now)),
				Ui::LayerOption::CloseOther);
		} else if (type == u"CHANNELS_TOO_MUCH"_q) {
			controller->show(
				Box<Ui::InformBox>(tr::lng_cant_do_this(tr::now)),
				Ui::LayerOption::CloseOther); // TODO
		}
	}).send();
}

void GroupInfoBox::checkInviteLink() {
	Expects(_createdChannel != nullptr);

	if (!_createdChannel->inviteLink().isEmpty()) {
		channelReady();
	} else if (_createdChannel->isFullLoaded() && !_creatingInviteLink) {
		_creatingInviteLink = true;
		_createdChannel->session().api().inviteLinks().create(
			_createdChannel,
			crl::guard(this, [=](auto&&) { channelReady(); }));
	} else {
		_createdChannel->session().changes().peerUpdates(
			_createdChannel,
			Data::PeerUpdate::Flag::FullInfo
		) | rpl::take(1) | rpl::start_with_next([=] {
			checkInviteLink();
		}, lifetime());
	}
}

void GroupInfoBox::channelReady() {
	if (_channelDone) {
		const auto callback = _channelDone;
		const auto argument = _createdChannel;
		closeBox();
		callback(argument);
	} else {
		_navigation->parentController()->show(
			Box<SetupChannelBox>(_navigation, _createdChannel),
			Ui::LayerOption::CloseOther);
	}
}

void GroupInfoBox::descriptionResized() {
	updateMaxHeight();
	update();
}

void GroupInfoBox::updateMaxHeight() {
	auto newHeight = st::boxPadding.top()
		+ st::newGroupInfoPadding.top()
		+ st::defaultUserpicButton.size.height()
		+ st::boxPadding.bottom()
		+ st::newGroupInfoPadding.bottom();
	if (_description) {
		newHeight += st::newGroupDescriptionPadding.top()
			+ _description->height()
			+ st::newGroupDescriptionPadding.bottom();
	}
	setDimensions(st::boxWideWidth, newHeight);
}

SetupChannelBox::SetupChannelBox(
	QWidget*,
	not_null<Window::SessionNavigation*> navigation,
	not_null<ChannelData*> channel,
	bool existing)
: _navigation(navigation)
, _channel(channel)
, _api(&_channel->session().mtp())
, _existing(existing)
, _privacyGroup(
	std::make_shared<Ui::RadioenumGroup<Privacy>>(Privacy::Public))
, _public(
	this,
	_privacyGroup,
	Privacy::Public,
	(channel->isMegagroup()
		? tr::lng_create_public_group_title
		: tr::lng_create_public_channel_title)(tr::now),
	st::defaultBoxCheckbox)
, _private(
	this,
	_privacyGroup,
	Privacy::Private,
	(channel->isMegagroup()
		? tr::lng_create_private_group_title
		: tr::lng_create_private_channel_title)(tr::now),
	st::defaultBoxCheckbox)
, _aboutPublicWidth(st::boxWideWidth
	- st::boxPadding.left()
	- st::defaultBox.buttonPadding.right()
	- st::newGroupPadding.left()
	- st::defaultRadio.diameter
	- st::defaultBoxCheckbox.textPosition.x())
, _aboutPublic(
	st::defaultTextStyle,
	(channel->isMegagroup()
		? tr::lng_create_public_group_about
		: tr::lng_create_public_channel_about)(tr::now),
	_defaultOptions,
	_aboutPublicWidth)
, _aboutPrivate(
	st::defaultTextStyle,
	(channel->isMegagroup()
		? tr::lng_create_private_group_about
		: tr::lng_create_private_channel_about)(tr::now),
	_defaultOptions,
	_aboutPublicWidth)
, _link(
	this,
	st::setupChannelLink,
	nullptr,
	channel->username,
	channel->session().createInternalLink(QString()))
, _checkTimer([=] { check(); }) {
}

void SetupChannelBox::prepare() {
	_aboutPublicHeight = _aboutPublic.countHeight(_aboutPublicWidth);

	if (_channel->inviteLink().isEmpty()) {
		_channel->session().api().requestFullPeer(_channel);
	}

	setMouseTracking(true);

	_checkRequestId = _api.request(MTPchannels_CheckUsername(
		_channel->inputChannel,
		MTP_string("preston")
	)).fail([=](const MTP::Error &error) {
		_checkRequestId = 0;
		firstCheckFail(parseError(error.type()));
	}).send();

	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(
		_existing ? tr::lng_cancel() : tr::lng_create_group_skip(),
		[=] { closeBox(); });

	connect(_link, &Ui::MaskedInputField::changed, [=] { handleChange(); });
	_link->setVisible(_privacyGroup->value() == Privacy::Public);

	_privacyGroup->setChangedCallback([=](Privacy value) {
		privacyChanged(value);
	});

	_channel->session().changes().peerUpdates(
		_channel,
		Data::PeerUpdate::Flag::InviteLinks
	) | rpl::start_with_next([=] {
		rtlupdate(_invitationLink);
	}, lifetime());

	boxClosing() | rpl::start_with_next([=] {
		if (!_existing) {
			AddParticipantsBoxController::Start(_navigation, _channel);
		}
	}, lifetime());

	updateMaxHeight();
}

void SetupChannelBox::setInnerFocus() {
	if (_link->isHidden()) {
		setFocus();
	} else {
		_link->setFocusFast();
	}
}

void SetupChannelBox::updateMaxHeight() {
	auto newHeight = st::boxPadding.top()
		+ st::newGroupPadding.top()
		+ _public->heightNoMargins()
		+ _aboutPublicHeight
		+ st::newGroupSkip
		+ _private->heightNoMargins()
		+ _aboutPrivate.countHeight(_aboutPublicWidth)
		+ st::newGroupSkip
		+ st::newGroupPadding.bottom();
	if (!_channel->isMegagroup()
		|| _privacyGroup->value() == Privacy::Public) {
		newHeight += st::newGroupLinkPadding.top()
			+ _link->height()
			+ st::newGroupLinkPadding.bottom();
	}
	setDimensions(st::boxWideWidth, newHeight);
}

void SetupChannelBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_link->hasFocus()) {
			if (_link->text().trimmed().isEmpty()) {
				_link->setFocus();
				_link->showError();
			} else {
				save();
			}
		}
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void SetupChannelBox::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::boxBg);
	p.setPen(st::newGroupAboutFg);

	const auto aboutPublic = QRect(
		st::boxPadding.left()
			+ st::newGroupPadding.left()
			+ st::defaultRadio.diameter
			+ st::defaultBoxCheckbox.textPosition.x(),
		_public->bottomNoMargins(),
		_aboutPublicWidth,
		_aboutPublicHeight);
	_aboutPublic.drawLeft(
		p,
		aboutPublic.x(),
		aboutPublic.y(),
		aboutPublic.width(),
		width());

	const auto aboutPrivate = QRect(
		st::boxPadding.left()
			+ st::newGroupPadding.left()
			+ st::defaultRadio.diameter
			+ st::defaultBoxCheckbox.textPosition.x(),
		_private->bottomNoMargins(),
		_aboutPublicWidth,
		_aboutPublicHeight);
	_aboutPrivate.drawLeft(
		p,
		aboutPrivate.x(),
		aboutPrivate.y(),
		aboutPrivate.width(),
		width());

	if (!_channel->isMegagroup() || !_link->isHidden()) {
		p.setPen(st::boxTextFg);
		p.setFont(st::newGroupLinkFont);
		p.drawTextLeft(
			st::boxPadding.left()
				+ st::newGroupPadding.left()
				+ st::defaultInputField.textMargins.left(),
			_link->y() - st::newGroupLinkPadding.top() + st::newGroupLinkTop,
			width(),
			(_link->isHidden()
				? tr::lng_create_group_invite_link
				: tr::lng_create_group_link)(tr::now));
	}

	if (_link->isHidden()) {
		if (!_channel->isMegagroup()) {
			QTextOption option(style::al_left);
			option.setWrapMode(QTextOption::WrapAnywhere);
			p.setFont(_linkOver
				? st::boxTextFont->underline()
				: st::boxTextFont);
			p.setPen(st::defaultLinkButton.color);
			const auto inviteLinkText = _channel->inviteLink().isEmpty()
				? tr::lng_group_invite_create(tr::now)
				: _channel->inviteLink();
			p.drawText(_invitationLink, inviteLinkText, option);
		}
	} else {
		const auto top = _link->y()
			- st::newGroupLinkPadding.top()
			+ st::newGroupLinkTop
			+ st::newGroupLinkFont->ascent
			- st::boxTextFont->ascent;
		if (!_errorText.isEmpty()) {
			p.setPen(st::boxTextFgError);
			p.setFont(st::boxTextFont);
			p.drawTextRight(st::boxPadding.right(), top, width(), _errorText);
		} else if (!_goodText.isEmpty()) {
			p.setPen(st::boxTextFgGood);
			p.setFont(st::boxTextFont);
			p.drawTextRight(st::boxPadding.right(), top, width(), _goodText);
		}
	}
}

void SetupChannelBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	const auto left = st::boxPadding.left() + st::newGroupPadding.left();
	_public->moveToLeft(
		left,
		st::boxPadding.top() + st::newGroupPadding.top());
	_private->moveToLeft(
		left,
		_public->bottomNoMargins() + _aboutPublicHeight + st::newGroupSkip);

	_link->resize(
		width()
			- st::boxPadding.left()
			- st::newGroupLinkPadding.left()
			- st::boxPadding.right(),
		_link->height());
	_link->moveToLeft(
		st::boxPadding.left() + st::newGroupLinkPadding.left(),
		_private->bottomNoMargins()
			+ _aboutPrivate.countHeight(_aboutPublicWidth)
			+ st::newGroupSkip
			+ st::newGroupPadding.bottom()
			+ st::newGroupLinkPadding.top());
	_invitationLink = QRect(
		_link->x(),
		_link->y() + (_link->height() / 2) - st::boxTextFont->height,
		_link->width(),
		2 * st::boxTextFont->height);
}

void SetupChannelBox::mouseMoveEvent(QMouseEvent *e) {
	updateSelected(e->globalPos());
}

void SetupChannelBox::mousePressEvent(QMouseEvent *e) {
	if (!_linkOver) {
		return;
	} else if (!_channel->inviteLink().isEmpty()) {
		QGuiApplication::clipboard()->setText(_channel->inviteLink());
		Ui::Toast::Show(tr::lng_create_channel_link_copied(tr::now));
	} else if (_channel->isFullLoaded() && !_creatingInviteLink) {
		_creatingInviteLink = true;
		_channel->session().api().inviteLinks().create(_channel);
	}
}

void SetupChannelBox::leaveEventHook(QEvent *e) {
	updateSelected(QCursor::pos());
}

void SetupChannelBox::updateSelected(const QPoint &cursorGlobalPosition) {
	QPoint p(mapFromGlobal(cursorGlobalPosition));

	bool linkOver = _invitationLink.contains(p);
	if (linkOver != _linkOver) {
		_linkOver = linkOver;
		update();
		setCursor(_linkOver ? style::cur_pointer : style::cur_default);
	}
}

void SetupChannelBox::save() {
	const auto saveUsername = [&](const QString &link) {
		_sentUsername = link;
		_saveRequestId = _api.request(MTPchannels_UpdateUsername(
			_channel->inputChannel,
			MTP_string(_sentUsername)
		)).done([=] {
			_channel->setName(
				TextUtilities::SingleLine(_channel->name),
				_sentUsername);
			closeBox();
		}).fail([=](const MTP::Error &error) {
			_saveRequestId = 0;
			updateFail(parseError(error.type()));
		}).send();
	};
	if (_saveRequestId) {
		return;
	} else if (_privacyGroup->value() == Privacy::Private) {
		if (_existing) {
			saveUsername(QString());
		} else {
			closeBox();
		}
	} else {
		const auto link = _link->text().trimmed();
		if (link.isEmpty()) {
			_link->setFocus();
			_link->showError();
			return;
		}
		saveUsername(link);
	}
}

void SetupChannelBox::handleChange() {
	const auto name = _link->text().trimmed();
	if (name.isEmpty()) {
		if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
			_errorText = _goodText = QString();
			update();
		}
		_checkTimer.cancel();
	} else {
		const auto len = int(name.size());
		for (auto i = 0; i < len; ++i) {
			const auto ch = name.at(i);
			if ((ch < 'A' || ch > 'Z')
				&& (ch < 'a' || ch > 'z')
				&& (ch < '0' || ch > '9')
				&& ch != '_') {
				const auto badSymbols =
					tr::lng_create_channel_link_bad_symbols(tr::now);
				if (_errorText != badSymbols) {
					_errorText = badSymbols;
					update();
				}
				_checkTimer.cancel();
				return;
			}
		}
		if (name.size() < Ui::EditPeer::kMinUsernameLength) {
			const auto tooShort =
				tr::lng_create_channel_link_too_short(tr::now);
			if (_errorText != tooShort) {
				_errorText = tooShort;
				update();
			}
			_checkTimer.cancel();
		} else {
			if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
				_errorText = _goodText = QString();
				update();
			}
			_checkTimer.callOnce(Ui::EditPeer::kUsernameCheckTimeout);
		}
	}
}

void SetupChannelBox::check() {
	if (_checkRequestId) {
		_channel->session().api().request(_checkRequestId).cancel();
	}
	const auto link = _link->text().trimmed();
	if (link.size() >= Ui::EditPeer::kMinUsernameLength) {
		_checkUsername = link;
		_checkRequestId = _api.request(MTPchannels_CheckUsername(
			_channel->inputChannel,
			MTP_string(link)
		)).done([=](const MTPBool &result) {
			_checkRequestId = 0;
			_errorText = (mtpIsTrue(result)
					|| _checkUsername == _channel->username)
				? QString()
				: tr::lng_create_channel_link_occupied(tr::now);
			_goodText = _errorText.isEmpty()
				? tr::lng_create_channel_link_available(tr::now)
				: QString();
			update();
		}).fail([=](const MTP::Error &error) {
			_checkRequestId = 0;
			checkFail(parseError(error.type()));
		}).send();
	}
}

void SetupChannelBox::privacyChanged(Privacy value) {
	if (value == Privacy::Public) {
		if (_tooMuchUsernames) {
			_privacyGroup->setValue(Privacy::Private);
			const auto callback = crl::guard(this, [=] {
				_tooMuchUsernames = false;
				_privacyGroup->setValue(Privacy::Public);
				check();
			});
			Ui::show(
				Box<RevokePublicLinkBox>(
					&_channel->session(),
					callback),
				Ui::LayerOption::KeepOther);
			return;
		}
		_link->show();
		_link->setDisplayFocused(true);
		_link->setFocus();
	} else {
		_link->hide();
		setFocus();
	}
	if (_channel->isMegagroup()) {
		updateMaxHeight();
	}
	update();
}

SetupChannelBox::UsernameResult SetupChannelBox::parseError(
		const QString &error) {
	if (error == u"USERNAME_NOT_MODIFIED"_q) {
		return UsernameResult::Ok;
	} else if (error == u"USERNAME_INVALID"_q) {
		return UsernameResult::Invalid;
	} else if (error == u"USERNAME_OCCUPIED"_q) {
		return UsernameResult::Occupied;
	} else if (error == u"USERNAMES_UNAVAILABLE"_q) {
		return UsernameResult::Occupied;
	} else if (error == u"CHANNEL_PUBLIC_GROUP_NA"_q) {
		return UsernameResult::NA;
	} else if (error == u"CHANNELS_ADMIN_PUBLIC_TOO_MUCH"_q) {
		return UsernameResult::ChatsTooMuch;
	} else {
		return UsernameResult::Unknown;
	}
}

void SetupChannelBox::updateFail(UsernameResult result) {
	if ((result == UsernameResult::Ok)
		|| (_sentUsername == _channel->username)) {
		_channel->setName(
			TextUtilities::SingleLine(_channel->name),
			TextUtilities::SingleLine(_sentUsername));
		closeBox();
	} else if (result == UsernameResult::Invalid) {
		_link->setFocus();
		_link->showError();
		_errorText = tr::lng_create_channel_link_invalid(tr::now);
		update();
	} else if (result == UsernameResult::Occupied) {
		_link->setFocus();
		_link->showError();
		_errorText = tr::lng_create_channel_link_occupied(tr::now);
		update();
	} else {
		_link->setFocus();
	}
}

void SetupChannelBox::checkFail(UsernameResult result) {
	if (result == UsernameResult::NA) {
		Ui::hideLayer();
	} else if (result == UsernameResult::ChatsTooMuch) {
		if (_existing) {
			showRevokePublicLinkBoxForEdit();
		} else {
			_tooMuchUsernames = true;
			_privacyGroup->setValue(Privacy::Private);
		}
	} else if (result == UsernameResult::Invalid) {
		_errorText = tr::lng_create_channel_link_invalid(tr::now);
		update();
	} else if ((result == UsernameResult::Occupied)
			&& _checkUsername != _channel->username) {
		_errorText = tr::lng_create_channel_link_occupied(tr::now);
		update();
	} else {
		_goodText = QString();
		_link->setFocus();
	}
}

void SetupChannelBox::showRevokePublicLinkBoxForEdit() {
	const auto channel = _channel;
	const auto existing = _existing;
	const auto navigation = _navigation;
	const auto callback = [=] {
		Ui::show(
			Box<SetupChannelBox>(navigation, channel, existing),
			Ui::LayerOption::KeepOther);
	};
	closeBox();
	Ui::show(
		Box<RevokePublicLinkBox>(
			&channel->session(),
			callback),
		Ui::LayerOption::KeepOther);
}

void SetupChannelBox::firstCheckFail(UsernameResult result) {
	if (result == UsernameResult::NA) {
		Ui::hideLayer();
	} else if (result == UsernameResult::ChatsTooMuch) {
		if (_existing) {
			showRevokePublicLinkBoxForEdit();
		} else {
			_tooMuchUsernames = true;
			_privacyGroup->setValue(Privacy::Private);
		}
	} else {
		_goodText = QString();
		_link->setFocus();
	}
}

EditNameBox::EditNameBox(QWidget*, not_null<UserData*> user)
: _user(user)
, _api(&_user->session().mtp())
, _first(
	this,
	st::defaultInputField,
	tr::lng_signup_firstname(),
	_user->firstName)
, _last(
	this,
	st::defaultInputField,
	tr::lng_signup_lastname(),
	_user->lastName)
, _invertOrder(langFirstNameGoesSecond()) {
}

void EditNameBox::prepare() {
	auto newHeight = st::contactPadding.top() + _first->height();

	setTitle(tr::lng_edit_self_title());
	newHeight += st::contactSkip + _last->height();

	newHeight += st::boxPadding.bottom() + st::contactPadding.bottom();
	setDimensions(st::boxWideWidth, newHeight);

	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });
	if (_invertOrder) {
		setTabOrder(_last, _first);
	}
	_first->setMaxLength(Ui::EditPeer::kMaxUserFirstLastName);
	_last->setMaxLength(Ui::EditPeer::kMaxUserFirstLastName);

	connect(_first, &Ui::InputField::submitted, [=] { submit(); });
	connect(_last, &Ui::InputField::submitted, [=] { submit(); });
}

void EditNameBox::setInnerFocus() {
	(_invertOrder ? _last : _first)->setFocusFast();
}

void EditNameBox::submit() {
	if (_first->hasFocus()) {
		_last->setFocus();
	} else if (_last->hasFocus()) {
		if (_first->getLastText().trimmed().isEmpty()) {
			_first->setFocus();
			_first->showError();
		} else if (_last->getLastText().trimmed().isEmpty()) {
			_last->setFocus();
			_last->showError();
		} else {
			save();
		}
	}
}

void EditNameBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_first->resize(
		width()
			- st::boxPadding.left()
			- st::newGroupInfoPadding.left()
			- st::boxPadding.right(),
		_first->height());
	_last->resize(_first->size());
	const auto left = st::boxPadding.left() + st::newGroupInfoPadding.left();
	const auto skip = st::contactSkip;
	if (_invertOrder) {
		_last->moveToLeft(left, st::contactPadding.top());
		_first->moveToLeft(left, _last->y() + _last->height() + skip);
	} else {
		_first->moveToLeft(left, st::contactPadding.top());
		_last->moveToLeft(left, _first->y() + _first->height() + skip);
	}
}

void EditNameBox::save() {
	if (_requestId) {
		return;
	}

	auto first = TextUtilities::PrepareForSending(_first->getLastText());
	auto last = TextUtilities::PrepareForSending(_last->getLastText());
	if (first.isEmpty() && last.isEmpty()) {
		if (_invertOrder) {
			_last->setFocus();
			_last->showError();
		} else {
			_first->setFocus();
			_first->showError();
		}
		return;
	}
	if (first.isEmpty()) {
		first = last;
		last = QString();
	}
	_sentName = first;
	auto flags = MTPaccount_UpdateProfile::Flag::f_first_name
		| MTPaccount_UpdateProfile::Flag::f_last_name;
	_requestId = _api.request(MTPaccount_UpdateProfile(
		MTP_flags(flags),
		MTP_string(first),
		MTP_string(last),
		MTPstring()
	)).done([=](const MTPUser &user) {
		_user->owner().processUser(user);
		closeBox();
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		saveSelfFail(error.type());
	}).send();
}

void EditNameBox::saveSelfFail(const QString &error) {
	if (error == "NAME_NOT_MODIFIED") {
		_user->setName(
			TextUtilities::SingleLine(_first->getLastText().trimmed()),
			TextUtilities::SingleLine(_last->getLastText().trimmed()),
			QString(),
			TextUtilities::SingleLine(_user->username));
		closeBox();
	} else if (error == "FIRSTNAME_INVALID") {
		_first->setFocus();
		_first->showError();
	} else if (error == "LASTNAME_INVALID") {
		_last->setFocus();
		_last->showError();
	} else {
		_first->setFocus();
	}
}

RevokePublicLinkBox::Inner::Inner(
	QWidget *parent,
	not_null<Main::Session*> session,
	Fn<void()> revokeCallback)
: TWidget(parent)
, _session(session)
, _api(&_session->mtp())
, _rowHeight(st::contactsPadding.top()
	+ st::contactsPhotoSize
	+ st::contactsPadding.bottom())
, _revokeWidth(st::normalFont->width(
	tr::lng_channels_too_much_public_revoke(tr::now)))
, _revokeCallback(std::move(revokeCallback)) {
	setMouseTracking(true);

	resize(width(), 5 * _rowHeight);

	_api.request(MTPchannels_GetAdminedPublicChannels(
		MTP_flags(0)
	)).done([=](const MTPmessages_Chats &result) {
		const auto &chats = result.match([](const auto &data) {
			return data.vchats().v;
		});
		for (const auto &chat : chats) {
			if (const auto peer = _session->data().processChat(chat)) {
				if (!peer->isChannel() || peer->userName().isEmpty()) {
					continue;
				}

				auto row = ChatRow(peer);
				row.peer = peer;
				row.name.setText(
					st::contactsNameStyle,
					peer->name,
					Ui::NameTextOptions());
				row.status.setText(
					st::defaultTextStyle,
					_session->createInternalLink(
						textcmdLink(1, peer->userName())),
					Ui::DialogTextOptions());
				_rows.push_back(std::move(row));
			}
		}
		resize(width(), _rows.size() * _rowHeight);
		update();
	}).send();
}

RevokePublicLinkBox::RevokePublicLinkBox(
	QWidget*,
	not_null<Main::Session*> session,
	Fn<void()> revokeCallback)
: _session(session)
, _aboutRevoke(
	this,
	tr::lng_channels_too_much_public_about(tr::now),
	st::aboutRevokePublicLabel)
, _revokeCallback(std::move(revokeCallback)) {
}

void RevokePublicLinkBox::prepare() {
	_innerTop = st::boxPadding.top()
		+ _aboutRevoke->height()
		+ st::boxPadding.top();
	_inner = setInnerWidget(object_ptr<Inner>(this, _session, [=] {
		const auto callback = _revokeCallback;
		closeBox();
		if (callback) {
			callback();
		}
	}), st::boxScroll, _innerTop);

	addButton(tr::lng_cancel(), [=] { closeBox(); });

	_session->downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	_inner->resizeToWidth(st::boxWideWidth);
	setDimensions(st::boxWideWidth, _innerTop + _inner->height());
}

void RevokePublicLinkBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	updateSelected();
}

void RevokePublicLinkBox::Inner::updateSelected() {
	const auto point = mapFromGlobal(QCursor::pos());
	PeerData *selected = nullptr;
	auto top = _rowsTop;
	for (const auto &row : _rows) {
		const auto revokeLink = style::rtlrect(
			width()
				- st::contactsPadding.right()
				- st::contactsCheckPosition.x()
				- _revokeWidth,
			top
				+ st::contactsPadding.top()
				+ (st::contactsPhotoSize - st::normalFont->height) / 2,
			_revokeWidth,
			st::normalFont->height,
			width());
		if (revokeLink.contains(point)) {
			selected = row.peer;
			break;
		}
		top += _rowHeight;
	}
	if (selected != _selected) {
		_selected = selected;
		setCursor((_selected || _pressed)
			? style::cur_pointer
			: style::cur_default);
		update();
	}
}

void RevokePublicLinkBox::Inner::mousePressEvent(QMouseEvent *e) {
	if (_pressed != _selected) {
		_pressed = _selected;
		update();
	}
}

void RevokePublicLinkBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = base::take(_pressed);
	setCursor((_selected || _pressed)
		? style::cur_pointer
		: style::cur_default);
	if (pressed && pressed == _selected) {
		const auto textMethod = pressed->isMegagroup()
			? tr::lng_channels_too_much_public_revoke_confirm_group
			: tr::lng_channels_too_much_public_revoke_confirm_channel;
		const auto text = textMethod(
			tr::now,
			lt_link,
			_session->createInternalLink(pressed->userName()),
			lt_group,
			pressed->name);
		const auto confirmText = tr::lng_channels_too_much_public_revoke(
			tr::now);
		auto callback = crl::guard(this, [=](Fn<void()> &&close) {
			if (_revokeRequestId) {
				return;
			}
			_revokeRequestId = _api.request(MTPchannels_UpdateUsername(
				pressed->asChannel()->inputChannel,
				MTP_string()
			)).done([=, close = std::move(close)] {
				close();
				if (const auto callback = _revokeCallback) {
					callback();
				}
			}).send();
		});
		Ui::show(
			Box<Ui::ConfirmBox>(text, confirmText, std::move(callback)),
			Ui::LayerOption::KeepOther);
	}
}

void RevokePublicLinkBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.translate(0, _rowsTop);
	for (const auto &row : _rows) {
		paintChat(p, row, (row.peer == _selected));
		p.translate(0, _rowHeight);
	}
}

void RevokePublicLinkBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_aboutRevoke->moveToLeft(st::boxPadding.left(), st::boxPadding.top());
}

void RevokePublicLinkBox::Inner::paintChat(
		Painter &p,
		const ChatRow &row,
		bool selected) const {
	const auto peer = row.peer;
	peer->paintUserpicLeft(
		p,
		row.userpic,
		st::contactsPadding.left(),
		st::contactsPadding.top(),
		width(),
		st::contactsPhotoSize);

	p.setPen(st::contactsNameFg);

	const auto namex = st::contactsPadding.left()
		+ st::contactsPhotoSize
		+ st::contactsPadding.left();
	auto namew = width()
		- namex
		- st::contactsPadding.right()
		- (_revokeWidth + st::contactsCheckPosition.x() * 2);

	const auto badgeStyle = Ui::PeerBadgeStyle{
		&st::dialogsVerifiedIcon,
		&st::attentionButtonFg
	};
	namew -= Ui::DrawPeerBadgeGetWidth(
		peer,
		p,
		QRect(
			namex,
			st::contactsPadding.top() + st::contactsNameTop,
			row.name.maxWidth(),
			st::contactsNameStyle.font->height),
		namew,
		width(),
		badgeStyle);
	row.name.drawLeftElided(
		p,
		namex,
		st::contactsPadding.top() + st::contactsNameTop,
		namew,
		width());

	p.setFont(selected ? st::linkOverFont : st::linkFont);
	p.setPen(selected
		? st::defaultLinkButton.overColor
		: st::defaultLinkButton.color);
	p.drawTextRight(
		st::contactsPadding.right() + st::contactsCheckPosition.x(),
		st::contactsPadding.top()
			+ (st::contactsPhotoSize - st::normalFont->height) / 2,
		width(),
		tr::lng_channels_too_much_public_revoke(tr::now),
		_revokeWidth);

	p.setPen(st::contactsStatusFg);
	p.setTextPalette(st::revokePublicLinkStatusPalette);
	row.status.drawLeftElided(
		p,
		namex,
		st::contactsPadding.top() + st::contactsStatusTop,
		namew,
		width());
	p.restoreTextPalette();
}
