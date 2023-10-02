/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/kotato_radius.h"

#include "kotato/kotato_settings.h"
#include "ui/painter.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

namespace Kotato {
namespace {

struct Radius {
	float64 userpicRadius = 0.5;
	float64 forumUserpicRadius = 0.3;
	bool useDefaultRadiusForForum = false;
	style::point onlineBadgeSkip = st::dialogsOnlineBadgeSkip;
};

Radius radius;

} // namespace

void RefreshRadius() {
	radius.userpicRadius = float64(JsonSettings::GetInt("userpic_corner_radius")) / 100.0;
	radius.forumUserpicRadius = float64(JsonSettings::GetInt("userpic_corner_radius_forum")) / 100.0;
	radius.useDefaultRadiusForForum = JsonSettings::GetBool("userpic_corner_radius_forum_use_default");
	radius.onlineBadgeSkip = {
		style::ConvertScale(int(2 * radius.userpicRadius) - 1),
		style::ConvertScale(int(6 * radius.userpicRadius) - 1),
	};
}

float64 UserpicRadius(bool isForum) {
	if (isForum && !radius.useDefaultRadiusForForum) {
		return radius.forumUserpicRadius;
	}
	return radius.userpicRadius;
}

void DrawUserpicShape(
		QPainter &p,
		QRect rect,
		float64 size,
		bool isForum) {
	const auto r = UserpicRadius(isForum);
	if (r >= 0.5) {
		p.drawEllipse(rect);
	} else if (r) {
		p.drawRoundedRect(rect, size * r, size * r);
	} else {
		p.fillRect(rect, p.brush());
	}
}

void DrawUserpicShape(
		QPainter &p,
		QRectF rect,
		float64 size,
		bool isForum) {
	const auto r = UserpicRadius(isForum);
	if (r >= 0.5) {
		p.drawEllipse(rect);
	} else if (r) {
		p.drawRoundedRect(rect, size * r, size * r);
	} else {
		p.fillRect(rect, p.brush());
	}
}

void DrawUserpicShape(
		QPainter &p,
		int x,
		int y,
		int w,
		int h,
		float64 size,
		bool isForum) {
	const auto r = UserpicRadius(isForum);
	if (r >= 0.5) {
		p.drawEllipse(x, y, w, h);
	} else if (r) {
		p.drawRoundedRect(x, y, w, h, size * r, size * r);
	} else {
		p.fillRect(x, y, w, h, p.brush());
	}
}

style::point UserpicOnlineBadgeSkip() {
	return radius.onlineBadgeSkip;
}

QPixmap MessageTailLeft(style::color color) {
	const auto tail = st::historyBubbleTailInLeft;
	QImage rect(tail.width(), tail.height(), QImage::Format_ARGB32_Premultiplied);
	rect.fill(color->c);
	{
		auto p = QPainter(&rect);
		PainterHighQualityEnabler hq(p);

		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::transparent);
		p.drawRoundedRect(
			tail.width()-st::msgPhotoSize+style::ConvertScale(1),
			tail.height()-st::msgPhotoSize+style::ConvertScale(2),
			st::msgPhotoSize,
			st::msgPhotoSize,
			st::msgPhotoSize * radius.userpicRadius,
			st::msgPhotoSize * radius.userpicRadius);
	}
	return QPixmap::fromImage(rect);
}

QPixmap MessageTailRight(style::color color) {
	const auto tail = st::historyBubbleTailInRight;
	QImage rect(tail.width(), tail.height(), QImage::Format_ARGB32_Premultiplied);
	rect.fill(color->c);
	{
		auto p = QPainter(&rect);
		PainterHighQualityEnabler hq(p);

		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::transparent);
		p.drawRoundedRect(
			-style::ConvertScale(1),
			tail.height()-st::msgPhotoSize+style::ConvertScale(2),
			st::msgPhotoSize,
			st::msgPhotoSize,
			st::msgPhotoSize * radius.userpicRadius,
			st::msgPhotoSize * radius.userpicRadius);
	}
	return QPixmap::fromImage(rect);
}


} // namespace Kotato