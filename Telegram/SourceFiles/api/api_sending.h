/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

class History;
class PhotoData;
class DocumentData;
struct FileLoadResult;

namespace MTP {
class Error;
} // namespace MTP

namespace Data {
class LocationPoint;
} // namespace Data

namespace Api {

struct MessageToSend;
struct SendAction;

void SendWebDocument(
	MessageToSend &&message,
	not_null<DocumentData*> document,
	std::optional<MsgId> localMessageId = std::nullopt,
	Fn<void()> doneCallback = nullptr,
	bool forwarding = false);

void SendExistingDocument(
	MessageToSend &&message,
	not_null<DocumentData*> document,
	std::optional<MsgId> localMessageId = std::nullopt,
	Fn<void()> doneCallback = nullptr,
	bool forwarding = false);

void SendExistingPhoto(
	MessageToSend &&message,
	not_null<PhotoData*> photo,
	std::optional<MsgId> localMessageId = std::nullopt,
	Fn<void()> doneCallback = nullptr,
	bool forwarding = false);

bool SendDice(
	MessageToSend &message,
	Fn<void(const MTPUpdates &, mtpRequestId)> doneCallback = nullptr,
	bool forwarding = false);

void FillMessagePostFlags(
	const SendAction &action,
	not_null<PeerData*> peer,
	MessageFlags &flags);

void SendConfirmedFile(
	not_null<Main::Session*> session,
	const std::shared_ptr<FileLoadResult> &file);

void SendLocationPoint(
	const Data::LocationPoint &data,
	const SendAction &action,
	Fn<void()> done,
	Fn<void(const MTP::Error &error)> fail);

} // namespace Api
