/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once

namespace Ui {
class EmptyUserpic;
} // namespace Ui

namespace Kotato {

void InitRadius();
[[nodiscard]] rpl::producer<> RadiusChanges();
float64 UserpicRadius(bool isForum = false);
void DrawUserpicShape(
	QPainter &p,
	QRect rect,
	float64 size,
	bool isForum = false);
void DrawUserpicShape(
	QPainter &p,
	QRectF rect,
	float64 size,
	bool isForum = false);
void DrawUserpicShape(
	QPainter &p,
	int x,
	int y,
	int w,
	int h,
	float64 size,
	bool isForum = false);
void PaintEmptyUserpic(
	const Ui::EmptyUserpic &empty,
	QPainter &p,
	int x,
	int y,
	int outerWidth,
	int size,
	bool isForum = false);

style::point UserpicOnlineBadgeSkip();

QPixmap MessageTailLeft(style::color color);
QPixmap MessageTailRight(style::color color);

} // namespace Kotato