/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;
class PhotoData;
class DocumentData;
struct FileLoadResult;
class RPCError;

namespace Data {
class LocationPoint;
} // namespace Data

namespace Api {

struct MessageToSend;
struct SendAction;

void SendExistingDocument(
	Api::MessageToSend &&message,
	not_null<DocumentData*> document,
	Fn<void()> doneCallback = nullptr,
	bool forwarding = false);

void SendExistingPhoto(
	Api::MessageToSend &&message,
	not_null<PhotoData*> photo,
	Fn<void()> doneCallback = nullptr,
	bool forwarding = false);

bool SendDice(Api::MessageToSend &message);

void FillMessagePostFlags(
	const SendAction &action,
	not_null<PeerData*> peer,
	MTPDmessage::Flags &flags);

void SendConfirmedFile(
	not_null<Main::Session*> session,
	const std::shared_ptr<FileLoadResult> &file,
	const std::optional<FullMsgId> &oldId);

void SendLocationPoint(
	const Data::LocationPoint &data,
	const SendAction &action,
	Fn<void()> done,
	Fn<void(const RPCError &error)> fail);

} // namespace Api
