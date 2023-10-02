/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/userpic_view.h"

#include "kotato/kotato_radius.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/image/image_prepare.h"

#include <cmath>

namespace Ui {
namespace {

constexpr auto kPeek = 0.3; // Tunable. Strip width to the left of the userpic.
constexpr auto kCover = 0.5; // Tunable. How far the image reaches into the userpic.
constexpr auto kPivotY = 0.75; // Tunable. Rotation point, fraction of size down.
constexpr auto kCard1Scale = 0.82; // Tunable.
constexpr auto kCard1Angle = -9.; // Tunable.
constexpr auto kCard1Radius = 0.22; // Tunable. Less rounded than the userpic.
constexpr auto kCard1Opacity = 0.5; // Tunable.
constexpr auto kCard2Scale = 0.7; // Tunable.
constexpr auto kCard2Angle = -16.; // Tunable.
constexpr auto kCard2Radius = 0.16; // Tunable. Even less rounded.
constexpr auto kCard2Opacity = 0.3; // Tunable.
constexpr auto kGap = 0.02; // Tunable.

} // namespace

float64 ForumUserpicRadiusMultiplier() {
	return 0.3;
}

void PaintCommunityUserpicEffect(
		QPainter &p,
		CommunityUserpicEffect &cache,
		int x,
		int y,
		int size,
		QColor color) {
	if (size <= 0) {
		return;
	}
	const auto dpr = style::DevicePixelRatio();
	const auto version = style::PaletteVersion();
	const auto rgba = color.rgba();
	const auto peek = size * kPeek;
	const auto rounding = Kotato::UserpicRadius(true);
	const auto regenerate = cache.image.isNull()
		|| (cache.size != size)
		|| (cache.color != rgba)
		|| (cache.paletteVersion != version)
		|| (cache.dpr != dpr)
		|| (cache.rounding != rounding);
	if (regenerate) {
		cache.size = size;
		cache.color = rgba;
		cache.paletteVersion = version;
		cache.dpr = dpr;
		cache.rounding = rounding;

		const auto imageW = int(std::ceil((peek + size * kCover) * dpr));
		const auto imageH = int(std::ceil(size * dpr));
		if (cache.image.size() != QSize(imageW, imageH)) {
			cache.image = QImage(
				QSize(imageW, imageH),
				QImage::Format_ARGB32_Premultiplied);
		}
		cache.image.setDevicePixelRatio(dpr);
		cache.image.fill(Qt::transparent);

		auto q = QPainter(&cache.image);
		auto hq = PainterHighQualityEnabler(q);
		const auto gap = size * kGap;

		// The userpic and every card share a pivot on the userpic's left edge
		// where its bottom-left rounding starts; each card is pinned there and
		// rotated, so only its top-left corner peeks out to the left.
		const auto pivot = QPointF(peek, size * kPivotY);
		const auto card = [&](
				float64 scale,
				float64 round,
				float64 angle,
				float64 grow) {
			const auto side = size * scale;
			const auto radius = side * round;
			const auto top = pivot.y() - (side - radius);
			q.save();
			q.translate(pivot);
			q.rotate(angle);
			q.translate(-pivot);
			q.drawRoundedRect(
				QRectF(peek - grow, top - grow, side + 2 * grow, side + 2 * grow),
				radius + grow,
				radius + grow);
			q.restore();
		};

		q.setPen(Qt::NoPen);

		auto color2 = color;
		color2.setAlphaF(color.alphaF() * kCard2Opacity);
		q.setBrush(color2);
		card(kCard2Scale, kCard2Radius, kCard2Angle, 0.);

		// Carve a transparent gap, then draw card1 on top.
		q.setCompositionMode(QPainter::CompositionMode_Source);
		q.setBrush(Qt::transparent);
		card(kCard1Scale, kCard1Radius, kCard1Angle, gap);
		q.setCompositionMode(QPainter::CompositionMode_SourceOver);
		auto color1 = color;
		color1.setAlphaF(color.alphaF() * kCard1Opacity);
		q.setBrush(color1);
		card(kCard1Scale, kCard1Radius, kCard1Angle, 0.);

		// Carve the userpic gap at its real position; the userpic itself is
		// drawn by the caller into the hole.
		q.setCompositionMode(QPainter::CompositionMode_Source);
		q.setBrush(Qt::transparent);
		q.drawRoundedRect(
			QRectF(peek - gap, -gap, size + 2 * gap, size + 2 * gap),
			size * rounding + gap,
			size * rounding + gap);
	}
	p.drawImage(QPointF(x - peek, y), cache.image);
}

bool PeerUserpicLoading(const PeerUserpicView &view) {
	return view.cloud && view.cloud->isNull();
}

void ValidateUserpicCache(
		PeerUserpicView &view,
		const QImage *cloud,
		const EmptyUserpic *empty,
		int size,
		PeerUserpicShape shape) {
	Expects(cloud != nullptr || empty != nullptr);

	const auto radius = Kotato::UserpicRadius(shape == PeerUserpicShape::Forum);
	const auto full = QSize(size, size);
	const auto version = style::PaletteVersion();
	const auto shapeValue = static_cast<uint32>(shape) & 3;
	const auto regenerate = (view.cached.size() != QSize(size, size))
		|| (view.radius != radius)
		|| (view.shape != shapeValue)
		|| (cloud && !view.empty.null())
		|| (empty && empty != view.empty.get())
		|| (empty && view.paletteVersion != version);
	if (!regenerate) {
		return;
	}
	view.empty = empty;
	view.shape = shapeValue;
	view.radius = radius;
	view.paletteVersion = version;

	if (cloud) {
		view.cached = cloud->scaled(
			full,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		if (shape == PeerUserpicShape::Monoforum) {
			view.cached = Ui::ApplyMonoforumShape(std::move(view.cached));
		} else if (radius >= 0.5) {
			view.cached = Images::Circle(std::move(view.cached));
		} else if (const auto corner = int(size
				* radius
				/ style::DevicePixelRatio())) {
			view.cached = Images::Round(
				std::move(view.cached),
				Images::CornersMask(corner));
		}
	} else {
		if (view.cached.size() != full) {
			view.cached = QImage(full, QImage::Format_ARGB32_Premultiplied);
		}
		view.cached.fill(Qt::transparent);

		auto p = QPainter(&view.cached);
		if (shape == PeerUserpicShape::Monoforum) {
			empty->paintMonoforum(p, 0, 0, size, size);
		} else if (radius >= 0.5) {
			empty->paintCircle(p, 0, 0, size, size);
		} else if (radius) {
			empty->paintRounded(
				p,
				0,
				0,
				size,
				size,
				size * radius);
		} else {
			empty->paintSquare(
				p,
				0,
				0,
				size,
				size);
		}
	}
}

} // namespace Ui
