/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/userpic/info_userpic_emoji_builder_common.h"

#include "kotato/kotato_radius.h"
#include "ui/image/image_prepare.h"

namespace UserpicBuilder {

[[nodiscard]] QImage GenerateGradient(
		const QSize &size,
		const std::vector<QColor> &colors,
		bool circle,
		bool roundForumRect) {
	constexpr auto kRotation = int(45);
	auto gradient = Images::GenerateGradient(size, colors, kRotation);
	if (!circle && !roundForumRect) {
		return gradient;
	}
	const auto processModifier = [&](QImage &&i) {
		const auto multiplier = Kotato::UserpicRadius(roundForumRect);
		if (multiplier >= 0.5) {
			return Images::Circle(std::move(i));
		} else if (const auto corner = int(std::min(i.height(), i.width())
				* multiplier
				/ style::DevicePixelRatio())) {
			return Images::Round(std::move(i), Images::CornersMask(corner));
		} else {
			return std::move(i);
		}
	};
	if (style::DevicePixelRatio() == 1) {
		return processModifier(std::move(gradient));
	}
	auto image = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		p.drawImage(QRect(QPoint(), size), gradient);
	}
	return processModifier(std::move(image));
}

} // namespace UserpicBuilder
