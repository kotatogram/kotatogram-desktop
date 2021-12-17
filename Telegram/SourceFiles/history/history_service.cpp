/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_service.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "main/main_domain.h" // Core::App().domain().activate().
#include "apiwrap.h"
#include "history/history.h"
#include "history/view/media/history_view_invoice.h"
#include "history/history_message.h"
#include "history/history_item_components.h"
#include "history/view/history_view_service_message.h"
#include "data/data_folder.h"
#include "data/data_session.h"
#include "data/data_media_types.h"
#include "data/data_game.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_changes.h"
#include "data/data_group_call.h" // Data::GroupCall::id().
#include "core/application.h"
#include "core/click_handler_types.h"
#include "base/unixtime.h"
#include "base/timer_rpl.h"
#include "calls/calls_instance.h" // Core::App().calls().joinGroupCall.
#include "window/notifications_manager.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "storage/storage_shared_media.h"
#include "payments/payments_checkout_process.h" // CheckoutProcess::Start.
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "base/unixtime.h"

namespace {

constexpr auto kNotificationTextLimit = 255;
constexpr auto kPinnedMessageTextLimit = 16;

using ItemPreview = HistoryView::ItemPreview;

QString GenerateServiceTime(TimeId date) {
	if (date > 0) {
		return qs(" · ") + base::unixtime::parse(date).toString(cTimeFormat());
	}
	return QString();
}

[[nodiscard]] bool PeerCallKnown(not_null<PeerData*> peer) {
	if (peer->groupCall() != nullptr) {
		return true;
	} else if (const auto chat = peer->asChat()) {
		return !(chat->flags() & ChatDataFlag::CallActive);
	} else if (const auto channel = peer->asChannel()) {
		return !(channel->flags() & ChannelDataFlag::CallActive);
	}
	return true;
}

[[nodiscard]] rpl::producer<bool> PeerHasThisCallValue(
		not_null<PeerData*> peer,
		CallId id) {
	return peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::GroupCall
	) | rpl::filter([=] {
		return PeerCallKnown(peer);
	}) | rpl::map([=] {
		const auto call = peer->groupCall();
		return (call && call->id() == id);
	}) | rpl::distinct_until_changed(
	) | rpl::take_while([=](bool hasThisCall) {
		return hasThisCall;
	}) | rpl::then(
		rpl::single(false)
	);
}

[[nodiscard]] CallId CallIdFromInput(const MTPInputGroupCall &data) {
	return data.match([&](const MTPDinputGroupCall &data) {
		return data.vid().v;
	});
}

[[nodiscard]] ClickHandlerPtr GroupCallClickHandler(
		not_null<PeerData*> peer,
		CallId callId) {
	return std::make_shared<LambdaClickHandler>([=] {
		const auto call = peer->groupCall();
		if (call && call->id() == callId) {
			const auto &windows = peer->session().windows();
			if (windows.empty()) {
				Core::App().domain().activate(&peer->session().account());
				if (windows.empty()) {
					return;
				}
			}
			windows.front()->startOrJoinGroupCall(peer);
		}
	});
}

} // namespace

void HistoryService::setMessageByAction(const MTPmessageAction &action) {
	setNeedTime(true);
	auto prepareChatAddUserText = [this](const MTPDmessageActionChatAddUser &action) {
		auto result = PreparedText{};
		auto &users = action.vusers().v;
		if (users.size() == 1) {
			auto u = history()->owner().user(users[0].v);
			if (u == _from) {
				result.links.push_back(fromLink());
				result.text = tr::lng_action_user_joined(tr::now, lt_from, fromLinkText());
			} else {
				result.links.push_back(fromLink());
				result.links.push_back(u->createOpenLink());
				result.text = tr::lng_action_add_user(tr::now, lt_from, fromLinkText(), lt_user, textcmdLink(2, u->name));
			}
		} else if (users.isEmpty()) {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_add_user(tr::now, lt_from, fromLinkText(), lt_user, qsl("somebody"));
		} else {
			result.links.push_back(fromLink());
			for (auto i = 0, l = int(users.size()); i != l; ++i) {
				auto user = history()->owner().user(users[i].v);
				result.links.push_back(user->createOpenLink());

				auto linkText = textcmdLink(i + 2, user->name);
				if (i == 0) {
					result.text = linkText;
				} else if (i + 1 == l) {
					result.text = tr::lng_action_add_users_and_last(tr::now, lt_accumulated, result.text, lt_user, linkText);
				} else {
					result.text = tr::lng_action_add_users_and_one(tr::now, lt_accumulated, result.text, lt_user, linkText);
				}
			}
			result.text = tr::lng_action_add_users_many(tr::now, lt_from, fromLinkText(), lt_users, result.text);
		}
		return result;
	};

	auto prepareChatJoinedByLink = [this](const MTPDmessageActionChatJoinedByLink &action) {
		auto result = PreparedText{};
		result.links.push_back(fromLink());
		result.text = tr::lng_action_user_joined_by_link(tr::now, lt_from, fromLinkText());
		return result;
	};

	auto prepareChatCreate = [this](const MTPDmessageActionChatCreate &action) {
		auto result = PreparedText{};
		result.links.push_back(fromLink());
		result.text = tr::lng_action_created_chat(tr::now, lt_from, fromLinkText(), lt_title, TextUtilities::Clean(qs(action.vtitle())));
		return result;
	};

	auto prepareChannelCreate = [this](const MTPDmessageActionChannelCreate &action) {
		auto result = PreparedText {};
		if (isPost()) {
			result.text = tr::lng_action_created_channel(tr::now);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_created_chat(tr::now, lt_from, fromLinkText(), lt_title, TextUtilities::Clean(qs(action.vtitle())));
		}
		return result;
	};

	auto prepareChatDeletePhoto = [this] {
		auto result = PreparedText{};
		if (isPost()) {
			result.text = tr::lng_action_removed_photo_channel(tr::now);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_removed_photo(tr::now, lt_from, fromLinkText());
		}
		return result;
	};

	auto prepareChatDeleteUser = [this](const MTPDmessageActionChatDeleteUser &action) {
		auto result = PreparedText{};
		if (peerFromUser(action.vuser_id()) == _from->id) {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_user_left(tr::now, lt_from, fromLinkText());
		} else {
			auto user = history()->owner().user(action.vuser_id().v);
			result.links.push_back(fromLink());
			result.links.push_back(user->createOpenLink());
			result.text = tr::lng_action_kick_user(tr::now, lt_from, fromLinkText(), lt_user, textcmdLink(2, user->name));
		}
		return result;
	};

	auto prepareChatEditPhoto = [this](const MTPDmessageActionChatEditPhoto &action) {
		auto result = PreparedText{};
		if (isPost()) {
			result.text = tr::lng_action_changed_photo_channel(tr::now);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_changed_photo(tr::now, lt_from, fromLinkText());
		}
		return result;
	};

	auto prepareChatEditTitle = [this](const MTPDmessageActionChatEditTitle &action) {
		auto result = PreparedText{};
		if (isPost()) {
			result.text = tr::lng_action_changed_title_channel(tr::now, lt_title, TextUtilities::Clean(qs(action.vtitle())));
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_changed_title(tr::now, lt_from, fromLinkText(), lt_title, TextUtilities::Clean(qs(action.vtitle())));
		}
		return result;
	};

	auto prepareScreenshotTaken = [this] {
		auto result = PreparedText{};
		if (out()) {
			result.text = tr::lng_action_you_took_screenshot(tr::now);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_took_screenshot(tr::now, lt_from, fromLinkText());
		}
		return result;
	};

	auto prepareCustomAction = [&](const MTPDmessageActionCustomAction &action) {
		auto result = PreparedText{};
		result.text = qs(action.vmessage());
		return result;
	};

	auto prepareBotAllowed = [&](const MTPDmessageActionBotAllowed &action) {
		auto result = PreparedText{};
		const auto domain = qs(action.vdomain());
		result.text = tr::lng_action_bot_allowed_from_domain(
			tr::now,
			lt_domain,
			textcmdLink(qstr("http://") + domain, domain));
		return result;
	};

	auto prepareSecureValuesSent = [&](const MTPDmessageActionSecureValuesSent &action) {
		auto result = PreparedText{};
		auto documents = QStringList();
		for (const auto &type : action.vtypes().v) {
			documents.push_back([&] {
				switch (type.type()) {
				case mtpc_secureValueTypePersonalDetails:
					return tr::lng_action_secure_personal_details(tr::now);
				case mtpc_secureValueTypePassport:
				case mtpc_secureValueTypeDriverLicense:
				case mtpc_secureValueTypeIdentityCard:
				case mtpc_secureValueTypeInternalPassport:
					return tr::lng_action_secure_proof_of_identity(tr::now);
				case mtpc_secureValueTypeAddress:
					return tr::lng_action_secure_address(tr::now);
				case mtpc_secureValueTypeUtilityBill:
				case mtpc_secureValueTypeBankStatement:
				case mtpc_secureValueTypeRentalAgreement:
				case mtpc_secureValueTypePassportRegistration:
				case mtpc_secureValueTypeTemporaryRegistration:
					return tr::lng_action_secure_proof_of_address(tr::now);
				case mtpc_secureValueTypePhone:
					return tr::lng_action_secure_phone(tr::now);
				case mtpc_secureValueTypeEmail:
					return tr::lng_action_secure_email(tr::now);
				}
				Unexpected("Type in prepareSecureValuesSent.");
			}());
		};
		result.links.push_back(history()->peer->createOpenLink());
		result.text = tr::lng_action_secure_values_sent(
			tr::now,
			lt_user,
			textcmdLink(1, history()->peer->name),
			lt_documents,
			documents.join(", "));
		return result;
	};

	auto prepareContactSignUp = [this] {
		auto result = PreparedText{};
		result.links.push_back(fromLink());
		result.text = tr::lng_action_user_registered(tr::now, lt_from, fromLinkText());
		return result;
	};

	auto prepareProximityReached = [this](const MTPDmessageActionGeoProximityReached &action) {
		auto result = PreparedText{};
		const auto fromId = peerFromMTP(action.vfrom_id());
		const auto fromPeer = history()->owner().peer(fromId);
		const auto toId = peerFromMTP(action.vto_id());
		const auto toPeer = history()->owner().peer(toId);
		const auto selfId = _from->session().userPeerId();
		const auto distanceMeters = action.vdistance().v;
		const auto distance = [&] {
			if (distanceMeters >= 1000) {
				const auto km = (10 * (distanceMeters / 10)) / 1000.;
				return tr::lng_action_proximity_distance_km(
					tr::now,
					lt_count,
					km);
			} else {
				return tr::lng_action_proximity_distance_m(
					tr::now,
					lt_count,
					distanceMeters);
			}
		}();
		result.text = [&] {
			if (fromId == selfId) {
				result.links.push_back(toPeer->createOpenLink());
				return tr::lng_action_you_proximity_reached(
					tr::now,
					lt_distance,
					distance,
					lt_user,
					textcmdLink(1, toPeer->name));
			} else if (toId == selfId) {
				result.links.push_back(fromPeer->createOpenLink());
				return tr::lng_action_proximity_reached_you(
					tr::now,
					lt_from,
					textcmdLink(1, fromPeer->name),
					lt_distance,
					distance);
			} else {
				result.links.push_back(fromPeer->createOpenLink());
				result.links.push_back(toPeer->createOpenLink());
				return tr::lng_action_proximity_reached(
					tr::now,
					lt_from,
					textcmdLink(1, fromPeer->name),
					lt_distance,
					distance,
					lt_user,
					textcmdLink(2, toPeer->name));
			}
		}();
		return result;
	};

	auto prepareGroupCall = [this](const MTPDmessageActionGroupCall &action) {
		auto result = PreparedText{};
		if (const auto duration = action.vduration()) {
			const auto seconds = duration->v;
			const auto days = seconds / 86400;
			const auto hours = seconds / 3600;
			const auto minutes = seconds / 60;
			auto text = (days > 1)
				? tr::lng_group_call_duration_days(tr::now, lt_count, days)
				: (hours > 1)
				? tr::lng_group_call_duration_hours(tr::now, lt_count, hours)
				: (minutes > 1)
				? tr::lng_group_call_duration_minutes(tr::now, lt_count, minutes)
				: tr::lng_group_call_duration_seconds(tr::now, lt_count, seconds);
			if (history()->peer->isBroadcast()) {
				result.text = tr::lng_action_group_call_finished(
					tr::now,
					lt_duration,
					text);
			} else {
				result.links.push_back(fromLink());
				result.text = tr::lng_action_group_call_finished_group(
					tr::now,
					lt_from,
					fromLinkText(),
					lt_duration,
					text);
			}
			return result;
		}
		if (history()->peer->isBroadcast()) {
			result.text = tr::lng_action_group_call_started_channel(tr::now);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_group_call_started_group(
				tr::now,
				lt_from,
				fromLinkText());
		}
		return result;
	};

	auto prepareInviteToGroupCall = [this](const MTPDmessageActionInviteToGroupCall &action) {
		const auto callId = CallIdFromInput(action.vcall());
		const auto owner = &history()->owner();
		const auto peer = history()->peer;
		for (const auto &id : action.vusers().v) {
			const auto user = owner->user(id.v);
			if (callId) {
				owner->registerInvitedToCallUser(callId, peer, user);
			}
		};
		const auto linkCallId = PeerHasThisCall(peer, callId).value_or(false)
			? callId
			: 0;
		return prepareInvitedToCallText(action.vusers().v, linkCallId);
	};

	auto prepareSetMessagesTTL = [this](const MTPDmessageActionSetMessagesTTL &action) {
		auto result = PreparedText{};
		const auto period = action.vperiod().v;
		const auto duration = (period == 5)
			? u"5 seconds"_q
			: (period < 2 * 86400)
			? tr::lng_ttl_about_duration1(tr::now)
			: (period < 8 * 86400)
			? tr::lng_ttl_about_duration2(tr::now)
			: tr::lng_ttl_about_duration3(tr::now);
		if (isPost()) {
			if (!period) {
				result.text = tr::lng_action_ttl_removed_channel(tr::now);
			} else {
				result.text = tr::lng_action_ttl_changed_channel(tr::now, lt_duration, duration);
			}
		} else if (_from->isSelf()) {
			if (!period) {
				result.text = tr::lng_action_ttl_removed_you(tr::now);
			} else {
				result.text = tr::lng_action_ttl_changed_you(tr::now, lt_duration, duration);
			}
		} else {
			result.links.push_back(fromLink());
			if (!period) {
				result.text = tr::lng_action_ttl_removed(tr::now, lt_from, fromLinkText());
			} else {
				result.text = tr::lng_action_ttl_changed(tr::now, lt_from, fromLinkText(), lt_duration, duration);
			}
		}
		return result;
	};

	auto prepareSetChatTheme = [this](const MTPDmessageActionSetChatTheme &action) {
		auto result = PreparedText{};
		const auto text = qs(action.vemoticon());
		if (!text.isEmpty()) {
			if (isPost()) {
				result.text = tr::lng_action_theme_changed_channel(tr::now, lt_emoji, text);
			} else if (_from->isSelf()) {
				result.text = tr::lng_action_you_theme_changed(tr::now, lt_emoji, text);
			} else {
				result.links.push_back(fromLink());
				result.text = tr::lng_action_theme_changed(tr::now, lt_from, fromLinkText(), lt_emoji, text);
			}
		} else {
			if (isPost()) {
				result.text = tr::lng_action_theme_disabled_channel(tr::now);
			} else if (_from->isSelf()) {
				result.text = tr::lng_action_you_theme_disabled(tr::now);
			} else {
				result.links.push_back(fromLink());
				result.text = tr::lng_action_theme_disabled(tr::now, lt_from, fromLinkText());
			}
		}
		return result;
	};

	auto prepareChatJoinedByRequest = [this](const MTPDmessageActionChatJoinedByRequest &action) {
		auto result = PreparedText{};
		result.links.push_back(fromLink());
		result.text = tr::lng_action_user_joined_by_request(tr::now, lt_from, fromLinkText());
		return result;
	};

	const auto messageText = action.match([&](
		const MTPDmessageActionChatAddUser &data) {
		return prepareChatAddUserText(data);
	}, [&](const MTPDmessageActionChatJoinedByLink &data) {
		return prepareChatJoinedByLink(data);
	}, [&](const MTPDmessageActionChatCreate &data) {
		return prepareChatCreate(data);
	}, [](const MTPDmessageActionChatMigrateTo &) {
		return PreparedText{ tr::lng_action_group_migrate(tr::now) };
	}, [](const MTPDmessageActionChannelMigrateFrom &) {
		return PreparedText{ tr::lng_action_group_migrate(tr::now) };
	}, [](const MTPDmessageActionHistoryClear &) {
		return PreparedText();
	}, [&](const MTPDmessageActionChannelCreate &data) {
		return prepareChannelCreate(data);
	}, [&](const MTPDmessageActionChatDeletePhoto &) {
		return prepareChatDeletePhoto();
	}, [&](const MTPDmessageActionChatDeleteUser &data) {
		return prepareChatDeleteUser(data);
	}, [&](const MTPDmessageActionChatEditPhoto &data) {
		return prepareChatEditPhoto(data);
	}, [&](const MTPDmessageActionChatEditTitle &data) {
		return prepareChatEditTitle(data);
	}, [&](const MTPDmessageActionPinMessage &) {
		return preparePinnedText();
	}, [&](const MTPDmessageActionGameScore &) {
		return prepareGameScoreText();
	}, [&](const MTPDmessageActionPhoneCall &) -> PreparedText {
		Unexpected("PhoneCall type in HistoryService.");
	}, [&](const MTPDmessageActionPaymentSent &) {
		return preparePaymentSentText();
	}, [&](const MTPDmessageActionScreenshotTaken &) {
		return prepareScreenshotTaken();
	}, [&](const MTPDmessageActionCustomAction &data) {
		return prepareCustomAction(data);
	}, [&](const MTPDmessageActionBotAllowed &data) {
		return prepareBotAllowed(data);
	}, [&](const MTPDmessageActionSecureValuesSent &data) {
		return prepareSecureValuesSent(data);
	}, [&](const MTPDmessageActionContactSignUp &data) {
		return prepareContactSignUp();
	}, [&](const MTPDmessageActionGeoProximityReached &data) {
		return prepareProximityReached(data);
	}, [](const MTPDmessageActionPaymentSentMe &) {
		LOG(("API Error: messageActionPaymentSentMe received."));
		return PreparedText{ tr::lng_message_empty(tr::now) };
	}, [](const MTPDmessageActionSecureValuesSentMe &) {
		LOG(("API Error: messageActionSecureValuesSentMe received."));
		return PreparedText{ tr::lng_message_empty(tr::now) };
	}, [&](const MTPDmessageActionGroupCall &data) {
		return prepareGroupCall(data);
	}, [&](const MTPDmessageActionInviteToGroupCall &data) {
		return prepareInviteToGroupCall(data);
	}, [&](const MTPDmessageActionSetMessagesTTL &data) {
		return prepareSetMessagesTTL(data);
	}, [&](const MTPDmessageActionGroupCallScheduled &data) {
		return prepareCallScheduledText(data.vschedule_date().v);
	}, [&](const MTPDmessageActionSetChatTheme &data) {
		return prepareSetChatTheme(data);
	}, [&](const MTPDmessageActionChatJoinedByRequest &data) {
		return prepareChatJoinedByRequest(data);
	}, [](const MTPDmessageActionEmpty &) {
		return PreparedText{ tr::lng_message_empty(tr::now) };
	});

	setServiceText(messageText);

	// Additional information.
	applyAction(action);
}

void HistoryService::applyAction(const MTPMessageAction &action) {
	action.match([&](const MTPDmessageActionChatAddUser &data) {
		if (const auto channel = history()->peer->asMegagroup()) {
			const auto selfUserId = history()->session().userId();
			for (const auto &item : data.vusers().v) {
				if (peerFromUser(item) == selfUserId) {
					channel->mgInfo->joinedMessageFound = true;
					break;
				}
			}
		}
	}, [&](const MTPDmessageActionChatJoinedByLink &data) {
		if (_from->isSelf()) {
			if (const auto channel = history()->peer->asMegagroup()) {
				channel->mgInfo->joinedMessageFound = true;
			}
		}
	}, [&](const MTPDmessageActionChatEditPhoto &data) {
		data.vphoto().match([&](const MTPDphoto &photo) {
			_media = std::make_unique<Data::MediaPhoto>(
				this,
				history()->peer,
				history()->owner().processPhoto(photo));
		}, [](const MTPDphotoEmpty &) {
		});
	}, [&](const MTPDmessageActionChatCreate &) {
		_flags |= MessageFlag::IsGroupEssential;
	}, [&](const MTPDmessageActionChannelCreate &) {
		_flags |= MessageFlag::IsGroupEssential;
	}, [&](const MTPDmessageActionChatMigrateTo &) {
		_flags |= MessageFlag::IsGroupEssential;
	}, [&](const MTPDmessageActionChannelMigrateFrom &) {
		_flags |= MessageFlag::IsGroupEssential;
	}, [&](const MTPDmessageActionContactSignUp &) {
		_flags |= MessageFlag::IsContactSignUp;
	}, [&](const MTPDmessageActionChatJoinedByRequest &data) {
		if (_from->isSelf()) {
			if (const auto channel = history()->peer->asMegagroup()) {
				channel->mgInfo->joinedMessageFound = true;
			}
		}
	}, [](const auto &) {
	});
}

void HistoryService::setSelfDestruct(HistoryServiceSelfDestruct::Type type, int ttlSeconds) {
	UpdateComponents(HistoryServiceSelfDestruct::Bit());
	auto selfdestruct = Get<HistoryServiceSelfDestruct>();
	selfdestruct->timeToLive = ttlSeconds * 1000LL;
	selfdestruct->type = type;
}

bool HistoryService::updateDependent(bool force) {
	auto dependent = GetDependentData();
	Assert(dependent != nullptr);

	if (!force) {
		if (!dependent->msgId || dependent->msg) {
			return true;
		}
	}

	if (!dependent->lnk) {
		dependent->lnk = goToMessageClickHandler(
			(dependent->peerId
				? history()->owner().peer(dependent->peerId)
				: history()->peer),
			dependent->msgId);
	}
	auto gotDependencyItem = false;
	if (!dependent->msg) {
		dependent->msg = history()->owner().message(
			(dependent->peerId
				? peerToChannel(dependent->peerId)
				: channelId()),
			dependent->msgId);
		if (dependent->msg) {
			if (dependent->msg->isEmpty()) {
				// Really it is deleted.
				dependent->msg = nullptr;
				force = true;
			} else {
				history()->owner().registerDependentMessage(
					this,
					dependent->msg);
				gotDependencyItem = true;
			}
		}
	}
	if (dependent->msg) {
		updateDependentText();
	} else if (force) {
		if (dependent->msgId > 0) {
			dependent->msgId = 0;
			gotDependencyItem = true;
		}
		updateDependentText();
	}
	if (force && gotDependencyItem) {
		Core::App().notifications().checkDelayed();
	}
	return (dependent->msg || !dependent->msgId);
}

HistoryService::PreparedText HistoryService::prepareInvitedToCallText(
		const QVector<MTPlong> &users,
		CallId linkCallId) {
	const auto owner = &history()->owner();
	auto chatText = tr::lng_action_invite_user_chat(tr::now);
	auto result = PreparedText{};
	result.links.push_back(fromLink());
	auto linkIndex = 1;
	if (linkCallId) {
		const auto peer = history()->peer;
		result.links.push_back(GroupCallClickHandler(peer, linkCallId));
		chatText = textcmdLink(++linkIndex, chatText);
	}
	if (users.size() == 1) {
		auto user = owner->user(users[0].v);
		result.links.push_back(user->createOpenLink());
		result.text = tr::lng_action_invite_user(tr::now, lt_from, fromLinkText(), lt_user, textcmdLink(++linkIndex, user->name), lt_chat, chatText);
	} else if (users.isEmpty()) {
		result.text = tr::lng_action_invite_user(tr::now, lt_from, fromLinkText(), lt_user, qsl("somebody"), lt_chat, chatText);
	} else {
		for (auto i = 0, l = int(users.size()); i != l; ++i) {
			auto user = owner->user(users[i].v);
			result.links.push_back(user->createOpenLink());

			auto linkText = textcmdLink(++linkIndex, user->name);
			if (i == 0) {
				result.text = linkText;
			} else if (i + 1 == l) {
				result.text = tr::lng_action_invite_users_and_last(tr::now, lt_accumulated, result.text, lt_user, linkText);
			} else {
				result.text = tr::lng_action_invite_users_and_one(tr::now, lt_accumulated, result.text, lt_user, linkText);
			}
		}
		result.text = tr::lng_action_invite_users_many(tr::now, lt_from, fromLinkText(), lt_users, result.text, lt_chat, chatText);
	}
	return result;
}

HistoryService::PreparedText HistoryService::preparePinnedText() {
	auto result = PreparedText {};
	auto pinned = Get<HistoryServicePinned>();
	if (pinned && pinned->msg) {
		const auto mediaText = [&] {
			using TTL = HistoryServiceSelfDestruct;
			if (const auto media = pinned->msg->media()) {
				return media->pinnedTextSubstring();
			} else if (const auto selfdestruct = pinned->msg->Get<TTL>()) {
				if (selfdestruct->type == TTL::Type::Photo) {
					return tr::lng_action_pinned_media_photo(tr::now);
				} else if (selfdestruct->type == TTL::Type::Video) {
					return tr::lng_action_pinned_media_video(tr::now);
				}
			}
			return QString();
		}();
		result.links.push_back(fromLink());
		result.links.push_back(pinned->lnk);
		if (mediaText.isEmpty()) {
			auto original = pinned->msg->originalText().text;
			auto cutAt = 0;
			auto limit = kPinnedMessageTextLimit;
			auto size = original.size();
			for (; limit != 0;) {
				--limit;
				if (cutAt >= size) break;
				if (original.at(cutAt).isLowSurrogate() && cutAt + 1 < size && original.at(cutAt + 1).isHighSurrogate()) {
					cutAt += 2;
				} else {
					++cutAt;
				}
			}
			if (!limit && cutAt + 5 < size) {
				original = original.mid(0, cutAt) + qstr("...");
			}
			result.text = tr::lng_action_pinned_message(tr::now, lt_from, fromLinkText(), lt_text, textcmdLink(2, original));
		} else {
			result.text = tr::lng_action_pinned_media(tr::now, lt_from, fromLinkText(), lt_media, textcmdLink(2, mediaText));
		}
	} else if (pinned && pinned->msgId) {
		result.links.push_back(fromLink());
		result.links.push_back(pinned->lnk);
		result.text = tr::lng_action_pinned_media(tr::now, lt_from, fromLinkText(), lt_media, textcmdLink(2, tr::lng_contacts_loading(tr::now)));
	} else {
		result.links.push_back(fromLink());
		result.text = tr::lng_action_pinned_media(tr::now, lt_from, fromLinkText(), lt_media, tr::lng_deleted_message(tr::now));
	}
	return result;
}

HistoryService::PreparedText HistoryService::prepareGameScoreText() {
	auto result = PreparedText {};
	auto gamescore = Get<HistoryServiceGameScore>();

	auto computeGameTitle = [&]() -> QString {
		if (gamescore && gamescore->msg) {
			if (const auto media = gamescore->msg->media()) {
				if (const auto game = media->game()) {
					const auto row = 0;
					const auto column = 0;
					result.links.push_back(
						std::make_shared<ReplyMarkupClickHandler>(
							&history()->owner(),
							row,
							column,
							gamescore->msg->fullId()));
					auto titleText = game->title;
					return textcmdLink(result.links.size(), titleText);
				}
			}
			return tr::lng_deleted_message(tr::now);
		} else if (gamescore && gamescore->msgId) {
			return tr::lng_contacts_loading(tr::now);
		}
		return QString();
	};

	const auto scoreNumber = gamescore ? gamescore->score : 0;
	if (_from->isSelf()) {
		auto gameTitle = computeGameTitle();
		if (gameTitle.isEmpty()) {
			result.text = tr::lng_action_game_you_scored_no_game(
				tr::now,
				lt_count,
				scoreNumber);
		} else {
			result.text = tr::lng_action_game_you_scored(
				tr::now,
				lt_count,
				scoreNumber,
				lt_game,
				gameTitle);
		}
	} else {
		result.links.push_back(fromLink());
		auto gameTitle = computeGameTitle();
		if (gameTitle.isEmpty()) {
			result.text = tr::lng_action_game_score_no_game(
				tr::now,
				lt_count,
				scoreNumber,
				lt_from,
				fromLinkText());
		} else {
			result.text = tr::lng_action_game_score(
				tr::now,
				lt_count,
				scoreNumber,
				lt_from,
				fromLinkText(),
				lt_game,
				gameTitle);
		}
	}
	return result;
}

HistoryService::PreparedText HistoryService::preparePaymentSentText() {
	auto result = PreparedText {};
	const auto payment = Get<HistoryServicePayment>();
	Assert(payment != nullptr);

	auto invoiceTitle = [&] {
		if (payment->msg) {
			if (const auto media = payment->msg->media()) {
				if (const auto invoice = media->invoice()) {
					return textcmdLink(1, invoice->title);
				}
			}
		}
		return QString();
	}();

	if (invoiceTitle.isEmpty()) {
		result.text = tr::lng_action_payment_done(tr::now, lt_amount, payment->amount, lt_user, history()->peer->name);
	} else {
		result.text = tr::lng_action_payment_done_for(tr::now, lt_amount, payment->amount, lt_user, history()->peer->name, lt_invoice, invoiceTitle);
		if (payment->msg) {
			result.links.push_back(payment->lnk);
		}
	}
	return result;
}

HistoryService::PreparedText HistoryService::prepareCallScheduledText(
		TimeId scheduleDate) {
	const auto call = Get<HistoryServiceOngoingCall>();
	Assert(call != nullptr);

	const auto scheduled = base::unixtime::parse(scheduleDate);
	const auto date = scheduled.date();
	const auto now = QDateTime::currentDateTime();
	const auto secsToDateAddDays = [&](int days) {
		return now.secsTo(QDateTime(date.addDays(days), QTime(0, 0)));
	};
	auto result = PreparedText();
	const auto prepareWithDate = [&](const QString &date) {
		if (history()->peer->isBroadcast()) {
			result.text = tr::lng_action_group_call_scheduled_channel(
				tr::now,
				lt_date,
				date);
		} else {
			result.links.push_back(fromLink());
			result.text = tr::lng_action_group_call_scheduled_group(
				tr::now,
				lt_from,
				fromLinkText(),
				lt_date,
				date);
		}
	};
	const auto time = scheduled.time().toString(cTimeFormat());
	const auto prepareGeneric = [&] {
		prepareWithDate(tr::lng_group_call_starts_date(
			tr::now,
			lt_date,
			langDayOfMonthFull(date),
			lt_time,
			time));
	};
	auto nextIn = TimeId(0);
	if (now.date().addDays(1) < scheduled.date()) {
		nextIn = secsToDateAddDays(-1);
		prepareGeneric();
	} else if (now.date().addDays(1) == scheduled.date()) {
		nextIn = secsToDateAddDays(0);
		prepareWithDate(
			tr::lng_group_call_starts_tomorrow(tr::now, lt_time, time));
	} else if (now.date() == scheduled.date()) {
		nextIn = secsToDateAddDays(1);
		prepareWithDate(
			tr::lng_group_call_starts_today(tr::now, lt_time, time));
	} else {
		prepareGeneric();
	}
	if (nextIn) {
		call->lifetime = base::timer_once(
			(nextIn + 2) * crl::time(1000)
		) | rpl::start_with_next([=] {
			updateText(prepareCallScheduledText(scheduleDate));
		});
	}
	return result;
}

HistoryService::HistoryService(
	not_null<History*> history,
	MsgId id,
	const MTPDmessage &data,
	MessageFlags localFlags)
: HistoryItem(
		history,
		id,
		FlagsFromMTP(id, data.vflags().v, localFlags),
		data.vdate().v,
		data.vfrom_id() ? peerFromMTP(*data.vfrom_id()) : PeerId(0)) {
	createFromMtp(data);
	applyTTL(data);
}

HistoryService::HistoryService(
	not_null<History*> history,
	MsgId id,
	const MTPDmessageService &data,
	MessageFlags localFlags)
: HistoryItem(
		history,
		id,
		FlagsFromMTP(id, data.vflags().v, localFlags),
		data.vdate().v,
		data.vfrom_id() ? peerFromMTP(*data.vfrom_id()) : PeerId(0)) {
	createFromMtp(data);
	applyTTL(data);
}

HistoryService::HistoryService(
	not_null<History*> history,
	MsgId id,
	MessageFlags flags,
	TimeId date,
	const PreparedText &message,
	PeerId from,
	PhotoData *photo,
	bool showTime)
: HistoryItem(history, id, flags, date, from) {
	setNeedTime(showTime);
	setServiceText(message);
	if (photo) {
		_media = std::make_unique<Data::MediaPhoto>(
			this,
			history->peer,
			photo);
	}
}

bool HistoryService::updateDependencyItem() {
	if (GetDependentData()) {
		return updateDependent(true);
	}
	return HistoryItem::updateDependencyItem();
}

bool HistoryService::needCheck() const {
	return out() && !isEmpty();
}

ItemPreview HistoryService::toPreview(ToPreviewOptions options) const {
	// Don't show for service messages (chat photo changed).
	// Because larger version is shown exactly to the left of the preview.
	//auto media = _media ? _media->toPreview(options) : ItemPreview();
	return {
		.text = textcmdLink(1, TextUtilities::Clean(notificationText())),
		//.images = std::move(media.images),
		//.loadingContext = std::move(media.loadingContext),
	};
}

QString HistoryService::notificationText() const {
	const auto result = [&] {
		if (_media) {
			return _media->notificationText();
		} else if (!emptyText()) {
			return _cleanText.toString();
		}
		return QString();
	}();
	return (result.size() <= kNotificationTextLimit)
		? result
		: result.mid(0, kNotificationTextLimit) + qsl("...");
}

QString HistoryService::inReplyText() const {
	const auto result = HistoryService::notificationText();
	const auto &name = author()->name;
	const auto text = result.trimmed().startsWith(name)
		? result.trimmed().mid(name.size()).trimmed()
		: result;
	return textcmdLink(1, text);
}

std::unique_ptr<HistoryView::Element> HistoryService::createView(
		not_null<HistoryView::ElementDelegate*> delegate,
		HistoryView::Element *replacing) {
	return delegate->elementCreate(this, replacing);
}

QString HistoryService::fromLinkText() const {
	return textcmdLink(1, _from->name);
}

ClickHandlerPtr HistoryService::fromLink() const {
	return _from->createOpenLink();
}

void HistoryService::setServiceText(const PreparedText &prepared) {
	_text.setText(
		st::serviceTextStyle,
		(needTime() && !prepared.text.isEmpty() ? prepared.text + GenerateServiceTime(date()) : prepared.text),
		Ui::ItemTextServiceOptions());
	_cleanText.setText(
		st::serviceTextStyle,
		prepared.text,
		Ui::ItemTextServiceOptions());
	auto linkIndex = 0;
	for (const auto &link : prepared.links) {
		// Link indices start with 1.
		_text.setLink(++linkIndex, link);
	}
	_textWidth = -1;
	_textHeight = 0;
}

void HistoryService::markMediaAsReadHook() {
	if (const auto selfdestruct = Get<HistoryServiceSelfDestruct>()) {
		if (!selfdestruct->destructAt) {
			selfdestruct->destructAt = crl::now() + selfdestruct->timeToLive;
			history()->owner().selfDestructIn(this, selfdestruct->timeToLive);
		}
	}
}

crl::time HistoryService::getSelfDestructIn(crl::time now) {
	if (auto selfdestruct = Get<HistoryServiceSelfDestruct>()) {
		if (selfdestruct->destructAt > 0) {
			if (selfdestruct->destructAt <= now) {
				auto text = [selfdestruct] {
					switch (selfdestruct->type) {
					case HistoryServiceSelfDestruct::Type::Photo: return tr::lng_ttl_photo_expired(tr::now);
					case HistoryServiceSelfDestruct::Type::Video: return tr::lng_ttl_video_expired(tr::now);
					}
					Unexpected("Type in HistoryServiceSelfDestruct::Type");
				};
				setServiceText({ text() });
				return 0;
			}
			return selfdestruct->destructAt - now;
		}
	}
	return 0;
}

void HistoryService::createFromMtp(const MTPDmessage &message) {
	const auto media = message.vmedia();
	Assert(media != nullptr);

	const auto mediaType = media->type();
	switch (mediaType) {
	case mtpc_messageMediaPhoto: {
		if (message.is_media_unread()) {
			const auto &photo = media->c_messageMediaPhoto();
			const auto ttl = photo.vttl_seconds();
			Assert(ttl != nullptr);

			setSelfDestruct(HistoryServiceSelfDestruct::Type::Photo, ttl->v);
			if (out()) {
				setServiceText({ tr::lng_ttl_photo_sent(tr::now) });
			} else {
				auto result = PreparedText();
				result.links.push_back(fromLink());
				result.text = tr::lng_ttl_photo_received(tr::now, lt_from, fromLinkText());
				setServiceText(std::move(result));
			}
		} else {
			setServiceText({ tr::lng_ttl_photo_expired(tr::now) });
		}
	} break;
	case mtpc_messageMediaDocument: {
		if (message.is_media_unread()) {
			const auto &document = media->c_messageMediaDocument();
			const auto ttl = document.vttl_seconds();
			Assert(ttl != nullptr);

			setSelfDestruct(HistoryServiceSelfDestruct::Type::Video, ttl->v);
			if (out()) {
				setServiceText({ tr::lng_ttl_video_sent(tr::now) });
			} else {
				auto result = PreparedText();
				result.links.push_back(fromLink());
				result.text = tr::lng_ttl_video_received(tr::now, lt_from, fromLinkText());
				setServiceText(std::move(result));
			}
		} else {
			setServiceText({ tr::lng_ttl_video_expired(tr::now) });
		}
	} break;

	default: Unexpected("Media type in HistoryService::createFromMtp()");
	}
}

void HistoryService::createFromMtp(const MTPDmessageService &message) {
	const auto type = message.vaction().type();
	if (type == mtpc_messageActionGameScore) {
		const auto &data = message.vaction().c_messageActionGameScore();
		UpdateComponents(HistoryServiceGameScore::Bit());
		Get<HistoryServiceGameScore>()->score = data.vscore().v;
	} else if (type == mtpc_messageActionPaymentSent) {
		const auto &data = message.vaction().c_messageActionPaymentSent();
		UpdateComponents(HistoryServicePayment::Bit());
		const auto amount = data.vtotal_amount().v;
		const auto currency = qs(data.vcurrency());
		const auto payment = Get<HistoryServicePayment>();
		const auto id = fullId();
		const auto owner = &history()->owner();
		payment->amount = Ui::FillAmountAndCurrency(amount, currency);
		payment->invoiceLink = std::make_shared<LambdaClickHandler>([=](
				ClickContext context) {
			using namespace Payments;
			const auto my = context.other.value<ClickHandlerContext>();
			const auto weak = my.sessionWindow;
			if (const auto item = owner->message(id)) {
				CheckoutProcess::Start(
					item,
					Mode::Receipt,
					crl::guard(weak, [=] { weak->window().activate(); }));
			}
		});
	} else if (type == mtpc_messageActionGroupCall
		|| type == mtpc_messageActionGroupCallScheduled) {
		const auto started = (type == mtpc_messageActionGroupCall);
		const auto &callData = started
			? message.vaction().c_messageActionGroupCall().vcall()
			: message.vaction().c_messageActionGroupCallScheduled().vcall();
		const auto duration = started
			? message.vaction().c_messageActionGroupCall().vduration()
			: tl::conditional<MTPint>();
		if (duration) {
			RemoveComponents(HistoryServiceOngoingCall::Bit());
		} else {
			UpdateComponents(HistoryServiceOngoingCall::Bit());
			const auto call = Get<HistoryServiceOngoingCall>();
			call->id = CallIdFromInput(callData);
			call->link = GroupCallClickHandler(history()->peer, call->id);
		}
	} else if (type == mtpc_messageActionInviteToGroupCall) {
		const auto &data = message.vaction().c_messageActionInviteToGroupCall();
		const auto id = CallIdFromInput(data.vcall());
		const auto peer = history()->peer;
		const auto has = PeerHasThisCall(peer, id);
		auto hasLink = !has.has_value()
			? PeerHasThisCallValue(peer, id)
			: (*has)
			? PeerHasThisCallValue(
				peer,
				id) | rpl::skip(1) | rpl::type_erased()
			: rpl::producer<bool>();
		if (!hasLink) {
			RemoveComponents(HistoryServiceOngoingCall::Bit());
		} else {
			UpdateComponents(HistoryServiceOngoingCall::Bit());
			const auto call = Get<HistoryServiceOngoingCall>();
			call->id = id;
			call->lifetime.destroy();

			const auto users = data.vusers().v;
			std::move(hasLink) | rpl::start_with_next([=](bool has) {
				updateText(prepareInvitedToCallText(users, has ? id : 0));
				if (!has) {
					RemoveComponents(HistoryServiceOngoingCall::Bit());
				}
			}, call->lifetime);
		}
	}
	if (const auto replyTo = message.vreply_to()) {
		replyTo->match([&](const MTPDmessageReplyHeader &data) {
			const auto peerId = data.vreply_to_peer_id()
				? peerFromMTP(*data.vreply_to_peer_id())
				: history()->peer->id;
			if (message.vaction().type() == mtpc_messageActionPinMessage) {
				UpdateComponents(HistoryServicePinned::Bit());
			}
			if (const auto dependent = GetDependentData()) {
				dependent->peerId = (peerId != history()->peer->id)
					? peerId
					: 0;
				dependent->msgId = data.vreply_to_msg_id().v;
				if (!updateDependent()) {
					RequestDependentMessageData(
						this,
						dependent->peerId,
						dependent->msgId);
				}
			}
		});
	}
	setMessageByAction(message.vaction());
}

void HistoryService::applyEdition(const MTPDmessageService &message) {
	clearDependency();
	UpdateComponents(0);

	createFromMtp(message);
	applyServiceDateEdition(message);

	if (message.vaction().type() == mtpc_messageActionHistoryClear) {
		removeMedia();
		finishEditionToEmpty();
	} else {
		finishEdition(-1);
	}
}

void HistoryService::removeMedia() {
	if (!_media) return;

	_media.reset();
	_textWidth = -1;
	_textHeight = 0;
	history()->owner().requestItemResize(this);
}

Storage::SharedMediaTypesMask HistoryService::sharedMediaTypes() const {
	if (auto media = this->media()) {
		return media->sharedMediaTypes();
	}
	return {};
}

void HistoryService::updateDependentText() {
	auto text = PreparedText{};
	if (Has<HistoryServicePinned>()) {
		text = preparePinnedText();
	} else if (Has<HistoryServiceGameScore>()) {
		text = prepareGameScoreText();
	} else if (Has<HistoryServicePayment>()) {
		text = preparePaymentSentText();
	} else {
		return;
	}
	updateText(std::move(text));
}

void HistoryService::updateText(PreparedText &&text) {
	setServiceText(text);
	history()->owner().requestItemResize(this);
	invalidateChatListEntry();
	history()->owner().updateDependentMessages(this);
}

void HistoryService::clearDependency() {
	if (const auto dependent = GetDependentData()) {
		if (dependent->msg) {
			history()->owner().unregisterDependentMessage(
				this,
				dependent->msg);
			dependent->msg = nullptr;
			dependent->msgId = 0;
		}
	}
}

void HistoryService::dependencyItemRemoved(HistoryItem *dependency) {
	clearDependency();
	updateDependentText();
}

HistoryService::~HistoryService() {
	clearDependency();
	_media.reset();
}

HistoryService::PreparedText GenerateJoinedText(
		not_null<History*> history,
		not_null<UserData*> inviter,
		bool viaRequest) {
	if (inviter->id != history->session().userPeerId()) {
		auto result = HistoryService::PreparedText{};
		result.links.push_back(inviter->createOpenLink());
		result.text = (history->isMegagroup()
			? tr::lng_action_add_you_group
			: tr::lng_action_add_you)(
				tr::now,
				lt_from,
				textcmdLink(1, inviter->name));
		return result;
	} else if (history->isMegagroup()) {
		if (viaRequest) {
			return { tr::lng_action_you_joined_by_request(tr::now) };
		}
		auto self = history->session().user();
		auto result = HistoryService::PreparedText{};
		result.links.push_back(self->createOpenLink());
		result.text = tr::lng_action_user_joined(
			tr::now,
			lt_from,
			textcmdLink(1, self->name));
		return result;
	}
	return { viaRequest
		? tr::lng_action_you_joined_by_request_channel(tr::now)
		: tr::lng_action_you_joined(tr::now) };
}

not_null<HistoryService*> GenerateJoinedMessage(
		not_null<History*> history,
		TimeId inviteDate,
		not_null<UserData*> inviter,
		bool viaRequest) {
	return history->makeServiceMessage(
		history->owner().nextLocalMessageId(),
		MessageFlag::Local,
		inviteDate,
		GenerateJoinedText(history, inviter, viaRequest));
}

std::optional<bool> PeerHasThisCall(
		not_null<PeerData*> peer,
		CallId id) {
	const auto call = peer->groupCall();
	return call
		? std::make_optional(call->id() == id)
		: PeerCallKnown(peer)
		? std::make_optional(false)
		: std::nullopt;
}
