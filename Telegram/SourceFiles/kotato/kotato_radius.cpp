/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/kotato_radius.h"

#include "kotato/kotato_settings.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "styles/style_chat.h"
#include "styles/style_chat_style.h"
#include "styles/style_dialogs.h"

namespace Kotato {
namespace {

struct Radius {
	float64 userpicRadius = 0.5;
	float64 forumUserpicRadius = 0.3;
	bool useDefaultRadiusForForum = false;
};

Radius radius;

void RefreshRadius() {
	radius.userpicRadius = float64(JsonSettings::GetInt("userpic_corner_radius")) / 100.0;
	radius.forumUserpicRadius = float64(JsonSettings::GetInt("userpic_corner_radius_forum")) / 100.0;
	radius.useDefaultRadiusForForum = JsonSettings::GetBool("userpic_corner_radius_forum_use_default");
}

} // namespace

void InitRadius() {
	static auto lifetime = rpl::lifetime();
	RefreshRadius();

	// Subscribed first, so other subscribers already see the new radius.
	RadiusChanges() | rpl::on_next(RefreshRadius, lifetime);
}

rpl::producer<> RadiusChanges() {
	return rpl::merge(
		JsonSettings::Events("userpic_corner_radius"),
		JsonSettings::Events("userpic_corner_radius_forum"),
		JsonSettings::Events("userpic_corner_radius_forum_use_default")
	) | rpl::to_empty;
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

void PaintEmptyUserpic(
		const Ui::EmptyUserpic &empty,
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		bool isForum) {
	const auto r = UserpicRadius(isForum);
	if (r >= 0.5) {
		empty.paintCircle(p, x, y, outerWidth, size);
	} else if (r) {
		empty.paintRounded(p, x, y, outerWidth, size, size * r);
	} else {
		empty.paintSquare(p, x, y, outerWidth, size);
	}
}

style::point UserpicOnlineBadgeSkip() {
	return {
		style::ConvertScale(int(2 * radius.userpicRadius) - 1),
		style::ConvertScale(int(6 * radius.userpicRadius) - 1),
	};
}

namespace {

[[nodiscard]] QPixmap MessageTail(
		const style::icon &tail,
		QColor color,
		bool right) {
	constexpr auto kMaxCached = 64;
	using Key = std::tuple<QRgb, float64, int, bool>;
	static auto cache = base::flat_map<Key, QPixmap>();
	const auto ratio = style::DevicePixelRatio();
	const auto key = Key(color.rgba(), radius.userpicRadius, ratio, right);
	if (const auto i = cache.find(key); i != end(cache)) {
		return i->second;
	} else if (cache.size() >= kMaxCached) {
		cache.clear();
	}
	auto image = QImage(
		QSize(tail.width(), tail.height()) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(color);
	{
		auto p = QPainter(&image);
		PainterHighQualityEnabler hq(p);

		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::transparent);
		const auto size = st::msgPhotoSize;
		const auto rounding = size * radius.userpicRadius;
		p.drawRoundedRect(
			(right
				? -style::ConvertScale(1)
				: (tail.width() - size + style::ConvertScale(1))),
			tail.height() - size + style::ConvertScale(2),
			size,
			size,
			rounding,
			rounding);
	}
	return cache.emplace(key, QPixmap::fromImage(std::move(image))).first->second;
}

} // namespace

QPixmap MessageTailLeft(style::color color) {
	return MessageTail(st::historyBubbleTailInLeft, color->c, false);
}

QPixmap MessageTailRight(style::color color) {
	return MessageTail(st::historyBubbleTailInRight, color->c, true);
}


} // namespace Kotato