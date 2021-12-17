/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/empty_userpic.h"

#include "data/data_peer.h"
#include "ui/emoji_config.h"
#include "ui/effects/animation_value.h"
#include "ui/ui_utility.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h" // style::IconButton
#include "styles/style_info.h" // st::topBarCall

namespace Ui {
namespace {

[[nodiscard]] bool IsExternal(const QString &name) {
	return !name.isEmpty()
		&& (name.front() == QChar(0))
		&& QStringView(name).mid(1) == qstr("external");
}

void PaintSavedMessagesInner(
		Painter &p,
		int x,
		int y,
		int size,
		const style::color &fg) {
	// |<----width----->|
	//
	// XXXXXXXXXXXXXXXXXX  ---
	// X                X   |
	// X                X   |
	// X                X   |
	// X                X height
	// X       XX       X   |     ---
	// X     XX  XX     X   |      |
	// X   XX      XX   X   |     add
	// X XX          XX X   |      |
	// XX              XX  ---    ---

	const auto thinkness = base::SafeRound(size * 0.055);
	const auto increment = int(thinkness) % 2 + (size % 2);
	const auto width = base::SafeRound(size * 0.15) * 2 + increment;
	const auto height = base::SafeRound(size * 0.19) * 2 + increment;
	const auto add = base::SafeRound(size * 0.064);

	const auto left = x + (size - width) / 2;
	const auto top = y + (size - height) / 2;
	const auto right = left + width;
	const auto bottom = top + height;
	const auto middle = (left + right) / 2;
	const auto half = (top + bottom) / 2;

	p.setBrush(Qt::NoBrush);
	auto pen = fg->p;
	pen.setWidthF(thinkness);
	pen.setCapStyle(Qt::FlatCap);

	{
		// XXXXXXXXXXXXXXXXXX
		// X                X
		// X                X
		// X                X
		// X                X
		// X                X

		pen.setJoinStyle(Qt::RoundJoin);
		p.setPen(pen);
		QPainterPath path;
		path.moveTo(left, half);
		path.lineTo(left, top);
		path.lineTo(right, top);
		path.lineTo(right, half);
		p.drawPath(path);
	}
	{
		// X                X
		// X       XX       X
		// X     XX  XX     X
		// X   XX      XX   X
		// X XX          XX X
		// XX              XX

		pen.setJoinStyle(Qt::MiterJoin);
		p.setPen(pen);
		QPainterPath path;
		path.moveTo(left, half);
		path.lineTo(left, bottom);
		path.lineTo(middle, bottom - add);
		path.lineTo(right, bottom);
		path.lineTo(right, half);
		p.drawPath(path);
	}
}

void PaintIconInner(
		Painter &p,
		int x,
		int y,
		int size,
		int defaultSize,
		const style::icon &icon,
		const style::color &fg) {
	if (size == defaultSize) {
		const auto rect = QRect{ x, y, size, size };
		icon.paintInCenter(
			p,
			rect,
			fg->c);
	} else {
		p.save();
		const auto ratio = size / float64(defaultSize);
		p.translate(x + size / 2., y + size / 2.);
		p.scale(ratio, ratio);
		const auto skip = defaultSize;
		const auto rect = QRect{ -skip, -skip, 2 * skip, 2 * skip };
		icon.paintInCenter(
			p,
			rect,
			fg->c);
		p.restore();
	}
}

void PaintRepliesMessagesInner(
		Painter &p,
		int x,
		int y,
		int size,
		const style::color &fg) {
	PaintIconInner(
		p,
		x,
		y,
		size,
		st::dialogsPhotoSize,
		st::dialogsRepliesUserpic,
		fg);
}

void PaintExternalMessagesInner(
		Painter &p,
		int x,
		int y,
		int size,
		const style::color &fg) {
	PaintIconInner(
		p,
		x,
		y,
		size,
		st::msgPhotoSize,
		st::topBarCall.icon,
		fg);
}

template <typename Callback>
[[nodiscard]] QPixmap Generate(int size, Callback callback) {
	auto result = QImage(
		QSize(size, size) * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		callback(p);
	}
	return Ui::PixmapFromImage(std::move(result));
}

} // namespace

EmptyUserpic::EmptyUserpic(const style::color &color, const QString &name)
: _color(color) {
	fillString(name);
}

QString EmptyUserpic::ExternalName() {
	return QChar(0) + u"external"_q;
}

template <typename Callback>
void EmptyUserpic::paint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		Callback paintBackground) const {
	x = rtl() ? (outerWidth - x - size) : x;

	const auto fontsize = (size * 13) / 33;
	auto font = style::font(fontsize, st::historyPeerUserpicFont->flags(), st::historyPeerUserpicFont->family());

	PainterHighQualityEnabler hq(p);
	p.setBrush(_color);
	p.setPen(Qt::NoPen);
	paintBackground();

	if (IsExternal(_string)) {
		PaintExternalMessagesInner(p, x, y, size, st::historyPeerUserpicFg);
	} else {
		p.setFont(font);
		p.setBrush(Qt::NoBrush);
		p.setPen(st::historyPeerUserpicFg);
		p.drawText(
			QRect(x, y, size, size),
			_string,
			QTextOption(style::al_center));
	}
}

void EmptyUserpic::paint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size) const {
	switch (cUserpicCornersType()) {
		case 0:
			paintSquare(p, x, y, outerWidth, size);
			break;

		case 1:
			paintRounded(p, x, y, outerWidth, size);
			break;

		case 2:
			paintRoundedLarge(p, x, y, outerWidth, size);
			break;

		default:
			paint(p, x, y, outerWidth, size, [&p, x, y, size] {
				p.drawEllipse(x, y, size, size);
			});
	}
}

void EmptyUserpic::paintRoundedLarge(Painter &p, int x, int y, int outerWidth, int size) const {
	paint(p, x, y, outerWidth, size, [&p, x, y, size] {
		p.drawRoundedRect(x, y, size, size, st::dateRadius, st::dateRadius);
	});
}

void EmptyUserpic::paintRounded(Painter &p, int x, int y, int outerWidth, int size) const {
	paint(p, x, y, outerWidth, size, [&p, x, y, size] {
		p.drawRoundedRect(x, y, size, size, st::roundRadiusSmall, st::roundRadiusSmall);
	});
}

void EmptyUserpic::paintSquare(Painter &p, int x, int y, int outerWidth, int size) const {
	paint(p, x, y, outerWidth, size, [&p, x, y, size] {
		p.fillRect(x, y, size, size, p.brush());
	});
}

void EmptyUserpic::PaintSavedMessages(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size) {
	switch (cUserpicCornersType()) {
		case 0:
			PaintSavedMessagesSquared(p, x, y, outerWidth, size);
			break;

		case 1:
			PaintSavedMessagesRounded(p, x, y, outerWidth, size);
			break;

		case 2:
			PaintSavedMessagesRoundedLarge(p, x, y, outerWidth, size);
			break;

		default:
			const auto &bg = st::historyPeerSavedMessagesBg;
			const auto &fg = st::historyPeerUserpicFg;
			PaintSavedMessages(p, x, y, outerWidth, size, bg, fg);
	}
}

void EmptyUserpic::PaintSavedMessagesRoundedLarge(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size) {
	const auto &bg = st::historyPeerSavedMessagesBg;
	const auto &fg = st::historyPeerUserpicFg;
	PaintSavedMessagesRoundedLarge(p, x, y, outerWidth, size, bg, fg);
}

void EmptyUserpic::PaintSavedMessagesRounded(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size) {
	const auto &bg = st::historyPeerSavedMessagesBg;
	const auto &fg = st::historyPeerUserpicFg;
	PaintSavedMessagesRounded(p, x, y, outerWidth, size, bg, fg);
}

void EmptyUserpic::PaintSavedMessagesSquared(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size) {
	const auto &bg = st::historyPeerSavedMessagesBg;
	const auto &fg = st::historyPeerUserpicFg;
	PaintSavedMessagesSquared(p, x, y, outerWidth, size, bg, fg);
}

void EmptyUserpic::PaintSavedMessages(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		const style::color &bg,
		const style::color &fg) {
	switch (cUserpicCornersType()) {
		case 0:
			PaintSavedMessagesSquared(p, x, y, outerWidth, size, bg, fg);
			break;

		case 1:
			PaintSavedMessagesRounded(p, x, y, outerWidth, size, bg, fg);
			break;

		case 2:
			PaintSavedMessagesRoundedLarge(p, x, y, outerWidth, size, bg, fg);
			break;

		default:
			x = rtl() ? (outerWidth - x - size) : x;

			PainterHighQualityEnabler hq(p);
			p.setBrush(bg);
			p.setPen(Qt::NoPen);
			p.drawEllipse(x, y, size, size);

			PaintSavedMessagesInner(p, x, y, size, fg);
	}
}

void EmptyUserpic::PaintSavedMessagesRoundedLarge(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		const style::color &bg,
		const style::color &fg) {
	x = rtl() ? (outerWidth - x - size) : x;

	PainterHighQualityEnabler hq(p);
	p.setBrush(bg);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(x, y, size, size, st::dateRadius, st::dateRadius);

	PaintSavedMessagesInner(p, x, y, size, fg);
}

void EmptyUserpic::PaintSavedMessagesRounded(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		const style::color &bg,
		const style::color &fg) {
	x = rtl() ? (outerWidth - x - size) : x;

	PainterHighQualityEnabler hq(p);
	p.setBrush(bg);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(x, y, size, size, st::roundRadiusSmall, st::roundRadiusSmall);

	PaintSavedMessagesInner(p, x, y, size, fg);
}

void EmptyUserpic::PaintSavedMessagesSquared(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		const style::color &bg,
		const style::color &fg) {
	x = rtl() ? (outerWidth - x - size) : x;

	PainterHighQualityEnabler hq(p);
	p.setBrush(bg);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(x, y, size, size, 0, 0);

	PaintSavedMessagesInner(p, x, y, size, fg);
}

QPixmap EmptyUserpic::GenerateSavedMessages(int size) {
	return Generate(size, [&](Painter &p) {
		PaintSavedMessages(p, 0, 0, size, size);
	});
}

QPixmap EmptyUserpic::GenerateSavedMessagesRounded(int size) {
	return Generate(size, [&](Painter &p) {
		PaintSavedMessagesRounded(p, 0, 0, size, size);
	});
}

void EmptyUserpic::PaintRepliesMessages(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size) {
	switch (cUserpicCornersType()) {
		case 0:
			PaintRepliesMessagesSquared(p, x, y, outerWidth, size);
			break;

		case 1:
			PaintRepliesMessagesRounded(p, x, y, outerWidth, size);
			break;

		case 2:
			PaintRepliesMessagesRoundedLarge(p, x, y, outerWidth, size);
			break;

		default:
			const auto &bg = st::historyPeerSavedMessagesBg;
			const auto &fg = st::historyPeerUserpicFg;
			PaintRepliesMessages(p, x, y, outerWidth, size, bg, fg);
	}
}

void EmptyUserpic::PaintRepliesMessagesRoundedLarge(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size) {
	const auto &bg = st::historyPeerSavedMessagesBg;
	const auto &fg = st::historyPeerUserpicFg;
	PaintRepliesMessagesRoundedLarge(p, x, y, outerWidth, size, bg, fg);
}

void EmptyUserpic::PaintRepliesMessagesRounded(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size) {
	const auto &bg = st::historyPeerSavedMessagesBg;
	const auto &fg = st::historyPeerUserpicFg;
	PaintRepliesMessagesRounded(p, x, y, outerWidth, size, bg, fg);
}

void EmptyUserpic::PaintRepliesMessagesSquared(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size) {
	const auto &bg = st::historyPeerSavedMessagesBg;
	const auto &fg = st::historyPeerUserpicFg;
	PaintRepliesMessagesSquared(p, x, y, outerWidth, size, bg, fg);
}

void EmptyUserpic::PaintRepliesMessages(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		const style::color &bg,
		const style::color &fg) {
	x = rtl() ? (outerWidth - x - size) : x;

	PainterHighQualityEnabler hq(p);
	p.setBrush(bg);
	p.setPen(Qt::NoPen);
	p.drawEllipse(x, y, size, size);

	PaintRepliesMessagesInner(p, x, y, size, fg);
}

void EmptyUserpic::PaintRepliesMessagesRoundedLarge(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		const style::color &bg,
		const style::color &fg) {
	x = rtl() ? (outerWidth - x - size) : x;

	PainterHighQualityEnabler hq(p);
	p.setBrush(bg);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(x, y, size, size, st::dateRadius, st::dateRadius);

	PaintRepliesMessagesInner(p, x, y, size, fg);
}

void EmptyUserpic::PaintRepliesMessagesRounded(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		const style::color &bg,
		const style::color &fg) {
	x = rtl() ? (outerWidth - x - size) : x;

	PainterHighQualityEnabler hq(p);
	p.setBrush(bg);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(x, y, size, size, st::roundRadiusSmall, st::roundRadiusSmall);

	PaintRepliesMessagesInner(p, x, y, size, fg);
}

void EmptyUserpic::PaintRepliesMessagesSquared(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		const style::color &bg,
		const style::color &fg) {
	x = rtl() ? (outerWidth - x - size) : x;

	PainterHighQualityEnabler hq(p);
	p.setBrush(bg);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(x, y, size, size, 0, 0);

	PaintRepliesMessagesInner(p, x, y, size, fg);
}

QPixmap EmptyUserpic::GenerateRepliesMessages(int size) {
	return Generate(size, [&](Painter &p) {
		PaintRepliesMessages(p, 0, 0, size, size);
	});
}

QPixmap EmptyUserpic::GenerateRepliesMessagesRounded(int size) {
	return Generate(size, [&](Painter &p) {
		PaintRepliesMessagesRounded(p, 0, 0, size, size);
	});
}

InMemoryKey EmptyUserpic::uniqueKey() const {
	const auto first = (uint64(0xFFFFFFFFU) << 32)
		| anim::getPremultiplied(_color->c);
	auto second = uint64(0);
	memcpy(&second, _string.constData(), qMin(sizeof(second), _string.size() * sizeof(QChar)));
	return InMemoryKey(first, second);
}

QPixmap EmptyUserpic::generate(int size) {
	auto result = QImage(QSize(size, size) * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		paint(p, 0, 0, size, size);
	}
	return Ui::PixmapFromImage(std::move(result));
}

void EmptyUserpic::fillString(const QString &name) {
	if (IsExternal(name)) {
		_string = name;
		return;
	}
	QList<QString> letters;
	QList<int> levels;

	auto level = 0;
	auto letterFound = false;
	auto ch = name.constData(), end = ch + name.size();
	while (ch != end) {
		auto emojiLength = 0;
		if (Ui::Emoji::Find(ch, end, &emojiLength)) {
			ch += emojiLength;
		} else if (ch->isHighSurrogate()) {
			++ch;
			if (ch != end && ch->isLowSurrogate()) {
				++ch;
			}
		} else if (!letterFound && ch->isLetterOrNumber()) {
			letterFound = true;
			if (ch + 1 != end && Ui::Text::IsDiac(*(ch + 1))) {
				letters.push_back(QString(ch, 2));
				levels.push_back(level);
				++ch;
			} else {
				letters.push_back(QString(ch, 1));
				levels.push_back(level);
			}
			++ch;
		} else {
			if (*ch == ' ') {
				level = 0;
				letterFound = false;
			} else if (letterFound && *ch == '-') {
				level = 1;
				letterFound = true;
			}
			++ch;
		}
	}

	// We prefer the second letter to be after ' ', but it can also be after '-'.
	_string = QString();
	if (!letters.isEmpty()) {
		_string += letters.front();
		auto bestIndex = 0;
		auto bestLevel = 2;
		for (auto i = letters.size(); i != 1;) {
			if (levels[--i] < bestLevel) {
				bestIndex = i;
				bestLevel = levels[i];
			}
		}
		if (bestIndex > 0) {
			_string += letters[bestIndex];
		}
	}
	_string = _string.toUpper();
}

EmptyUserpic::~EmptyUserpic() = default;

} // namespace Ui
