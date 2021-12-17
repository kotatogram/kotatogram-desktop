/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>
#include <rpl/map.h>

struct ChannelLocation;

namespace Ui {
class RpWidget;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Storage {
enum class SharedMediaType : signed char;
} // namespace Storage

namespace Info {
namespace Profile {

inline auto ToSingleLine() {
	return rpl::map([](const QString &text) {
		return TextUtilities::SingleLine(text);
	});
}

rpl::producer<not_null<PeerData*>> MigratedOrMeValue(
	not_null<PeerData*> peer);

QString IDString(not_null<PeerData*> peer);

rpl::producer<TextWithEntities> IDValue(not_null<PeerData*> peer);
rpl::producer<TextWithEntities> NameValue(not_null<PeerData*> peer);
rpl::producer<TextWithEntities> PhoneValue(not_null<UserData*> user);
rpl::producer<TextWithEntities> PhoneOrHiddenValue(not_null<UserData*> user);
rpl::producer<TextWithEntities> UsernameValue(not_null<UserData*> user);
[[nodiscard]] TextWithEntities AboutWithEntities(
	not_null<PeerData*> peer,
	const QString &value);
rpl::producer<TextWithEntities> AboutValue(not_null<PeerData*> peer);
rpl::producer<QString> LinkValue(not_null<PeerData*> peer);
rpl::producer<const ChannelLocation*> LocationValue(
	not_null<ChannelData*> channel);
rpl::producer<bool> NotificationsEnabledValue(not_null<PeerData*> peer);
rpl::producer<bool> IsContactValue(not_null<UserData*> user);
rpl::producer<bool> CanInviteBotToGroupValue(not_null<UserData*> user);
rpl::producer<bool> CanShareContactValue(not_null<UserData*> user);
rpl::producer<bool> CanAddContactValue(not_null<UserData*> user);
rpl::producer<bool> HasLinkedChatValue(not_null<ChannelData*> channel);
rpl::producer<bool> AmInChannelValue(not_null<ChannelData*> channel);
rpl::producer<int> MembersCountValue(not_null<PeerData*> peer);
rpl::producer<int> PendingRequestsCountValue(not_null<PeerData*> peer);
rpl::producer<int> AdminsCountValue(not_null<PeerData*> peer);
rpl::producer<int> RestrictionsCountValue(not_null<PeerData*> peer);
rpl::producer<int> RestrictedCountValue(not_null<ChannelData*> channel);
rpl::producer<int> KickedCountValue(not_null<ChannelData*> channel);
rpl::producer<int> SharedMediaCountValue(
	not_null<PeerData*> peer,
	PeerData *migrated,
	Storage::SharedMediaType type);
rpl::producer<int> CommonGroupsCountValue(not_null<UserData*> user);
rpl::producer<bool> CanAddMemberValue(not_null<PeerData*> peer);

enum class Badge {
	None,
	Verified,
	Scam,
	Fake,
};
rpl::producer<Badge> BadgeValue(not_null<PeerData*> peer);

} // namespace Profile
} // namespace Info
