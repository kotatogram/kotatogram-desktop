/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_file_origin.h"

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

struct ExistingAlbumItem {
	PhotoData* photo;
	DocumentData* document;
	MTPInputMedia inputMedia;
	Data::FileOrigin origin;
	TextWithEntities text;
};

void SendExistingAlbum(
	Api::MessageToSend &&message,
	std::vector<ExistingAlbumItem> &&items,
	Fn<void()> doneCallback = nullptr,
	bool forwarding = false);

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

bool SendDice(
	Api::MessageToSend &message,
	Fn<void(const MTPUpdates &, mtpRequestId)> doneCallback = nullptr,
	bool forwarding = false);

void FillMessagePostFlags(
	const SendAction &action,
	not_null<PeerData*> peer,
	MTPDmessage::Flags &flags);

void SendConfirmedFile(
	not_null<Main::Session*> session,
	const std::shared_ptr<FileLoadResult> &file);

void SendLocationPoint(
	const Data::LocationPoint &data,
	const SendAction &action,
	Fn<void()> done,
	Fn<void(const MTP::Error &error)> fail);

} // namespace Api
