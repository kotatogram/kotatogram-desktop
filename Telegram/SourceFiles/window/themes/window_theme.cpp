/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_theme.h"

#include "window/themes/window_theme_preview.h"
#include "window/themes/window_themes_embedded.h"
#include "window/themes/window_theme_editor.h"
#include "window/window_controller.h"
#include "platform/platform_specific.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "storage/localstorage.h"
#include "storage/localimageloader.h"
#include "storage/file_upload.h"
#include "base/random.h"
#include "base/parse_helper.h"
#include "base/zlib_help.h"
#include "base/unixtime.h"
#include "base/crc32hash.h"
#include "data/data_session.h"
#include "data/data_document_resolver.h"
#include "main/main_account.h" // Account::local.
#include "main/main_domain.h" // Domain::activeSessionValue.
#include "ui/chat/chat_theme.h"
#include "ui/image/image.h"
#include "ui/style/style_palette_colorizer.h"
#include "ui/ui_utility.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/background_box.h"
#include "core/application.h"
#include "styles/style_widgets.h"
#include "styles/style_chat.h"

#include <QtCore/QBuffer>

namespace Window {
namespace Theme {
namespace {

constexpr auto kThemeFileSizeLimit = 5 * 1024 * 1024;
constexpr auto kBackgroundSizeLimit = 25 * 1024 * 1024;
constexpr auto kNightThemeFile = ":/gui/night.tdesktop-theme"_cs;
constexpr auto kDarkValueThreshold = 0.5;

struct Applying {
	Saved data;
	QByteArray paletteForRevert;
	Fn<void()> overrideKeep;
};

NeverFreedPointer<ChatBackground> GlobalBackground;
Applying GlobalApplying;

inline bool AreTestingTheme() {
	return !GlobalApplying.paletteForRevert.isEmpty();
}

[[nodiscard]] QImage ReadDefaultImage() {
	return Ui::ReadBackgroundImage(
		u":/gui/art/background.tgv"_q,
		QByteArray(),
		true);
}

[[nodiscard]] bool GoodImageFormatAndSize(const QImage &image) {
	return !image.size().isEmpty()
		&& (image.format() == QImage::Format_ARGB32_Premultiplied
			|| image.format() == QImage::Format_RGB32);
}

QByteArray readThemeContent(const QString &path) {
	QFile file(path);
	if (!file.exists()) {
		LOG(("Theme Error: theme file not found: %1").arg(path));
		return QByteArray();
	}

	if (file.size() > kThemeFileSizeLimit) {
		LOG(("Theme Error: theme file too large: %1 (should be less than 5 MB, got %2)").arg(path).arg(file.size()));
		return QByteArray();
	}
	if (!file.open(QIODevice::ReadOnly)) {
		LOG(("Theme Error: could not open theme file: %1").arg(path));
		return QByteArray();
	}

	return file.readAll();
}

inline uchar readHexUchar(char code, bool &error) {
	if (code >= '0' && code <= '9') {
		return ((code - '0') & 0xFF);
	} else if (code >= 'a' && code <= 'f') {
		return ((code + 10 - 'a') & 0xFF);
	} else if (code >= 'A' && code <= 'F') {
		return ((code + 10 - 'A') & 0xFF);
	}
	error = true;
	return 0xFF;
}

inline uchar readHexUchar(char char1, char char2, bool &error) {
	return ((readHexUchar(char1, error) & 0x0F) << 4) | (readHexUchar(char2, error) & 0x0F);
}

bool readNameAndValue(const char *&from, const char *end, QLatin1String *outName, QLatin1String *outValue) {
	using base::parse::skipWhitespaces;
	using base::parse::readName;

	if (!skipWhitespaces(from, end)) return true;

	*outName = readName(from, end);
	if (outName->size() == 0) {
		LOG(("Theme Error: Could not read name in the color scheme."));
		return false;
	}
	if (!skipWhitespaces(from, end)) {
		LOG(("Theme Error: Unexpected end of the color scheme."));
		return false;
	}
	if (*from != ':') {
		LOG(("Theme Error: Expected ':' between each name and value in the color scheme (while reading key '%1')").arg(*outName));
		return false;
	}
	if (!skipWhitespaces(++from, end)) {
		LOG(("Theme Error: Unexpected end of the color scheme."));
		return false;
	}
	auto valueStart = from;
	if (*from == '#') ++from;

	if (readName(from, end).size() == 0) {
		LOG(("Theme Error: Expected a color value in #rrggbb or #rrggbbaa format in the color scheme (while reading key '%1')").arg(*outName));
		return false;
	}
	*outValue = QLatin1String(valueStart, from - valueStart);

	if (!skipWhitespaces(from, end)) {
		LOG(("Theme Error: Unexpected end of the color scheme."));
		return false;
	}
	if (*from != ';') {
		LOG(("Theme Error: Expected ';' after each value in the color scheme (while reading key '%1')").arg(*outName));
		return false;
	}
	++from;
	return true;
}

enum class SetResult {
	Ok,
	NotFound,
};
SetResult setColorSchemeValue(
		QLatin1String name,
		QLatin1String value,
		const style::colorizer &colorizer,
		Instance *out) {
	auto result = style::palette::SetResult::Ok;
	auto size = value.size();
	auto data = value.data();
	if (data[0] == '#' && (size == 7 || size == 9)) {
		auto error = false;
		auto r = readHexUchar(data[1], data[2], error);
		auto g = readHexUchar(data[3], data[4], error);
		auto b = readHexUchar(data[5], data[6], error);
		auto a = (size == 9) ? readHexUchar(data[7], data[8], error) : uchar(255);
		if (colorizer) {
			style::colorize(name, r, g, b, colorizer);
		}
		if (error) {
			LOG(("Theme Warning: Skipping value '%1: %2' (expected a color value in #rrggbb or #rrggbbaa or a previously defined key in the color scheme)").arg(name).arg(value));
			return SetResult::Ok;
		} else if (out) {
			result = out->palette.setColor(name, r, g, b, a);
		} else {
			result = style::main_palette::setColor(name, r, g, b, a);
		}
	} else {
		if (out) {
			result = out->palette.setColor(name, value);
		} else {
			result = style::main_palette::setColor(name, value);
		}
	}
	if (result == style::palette::SetResult::Ok) {
		return SetResult::Ok;
	} else if (result == style::palette::SetResult::KeyNotFound) {
		return SetResult::NotFound;
	} else if (result == style::palette::SetResult::ValueNotFound) {
		LOG(("Theme Warning: Skipping value '%1: %2' (expected a color value in #rrggbb or #rrggbbaa or a previously defined key in the color scheme)").arg(name).arg(value));
		return SetResult::Ok;
	} else if (result == style::palette::SetResult::Duplicate) {
		LOG(("Theme Warning: Color value appears more than once in the color scheme (while applying '%1: %2')").arg(name).arg(value));
		return SetResult::Ok;
	} else {
		LOG(("Theme Error: Unexpected internal error."));
	}
	Unexpected("Value after palette.setColor().");
}

bool loadColorScheme(
		const QByteArray &content,
		const style::colorizer &colorizer,
		Instance *out) {
	auto unsupported = QMap<QLatin1String, QLatin1String>();
	return ReadPaletteValues(content, [&](QLatin1String name, QLatin1String value) {
		// Find the named value in the already read unsupported list.
		value = unsupported.value(value, value);

		auto result = setColorSchemeValue(name, value, colorizer, out);
		if (result == SetResult::NotFound) {
			unsupported.insert(name, value);
		}
		return true;
	});
}

void applyBackground(QImage &&background, bool tiled, Instance *out) {
	if (out) {
		out->background = std::move(background);
		out->tiled = tiled;
	} else {
		Background()->setThemeData(std::move(background), tiled);
	}
}

enum class LoadResult {
	Loaded,
	Failed,
	NotFound,
};

LoadResult loadBackgroundFromFile(zlib::FileToRead &file, const char *filename, QByteArray *outBackground) {
	*outBackground = file.readFileContent(filename, zlib::kCaseInsensitive, kThemeBackgroundSizeLimit);
	if (file.error() == UNZ_OK) {
		return LoadResult::Loaded;
	} else if (file.error() == UNZ_END_OF_LIST_OF_FILE) {
		file.clearError();
		return LoadResult::NotFound;
	}
	LOG(("Theme Error: could not read '%1' in the theme file.").arg(filename));
	return LoadResult::Failed;
}

bool loadBackground(zlib::FileToRead &file, QByteArray *outBackground, bool *outTiled) {
	auto result = loadBackgroundFromFile(file, "background.jpg", outBackground);
	if (result != LoadResult::NotFound) return (result == LoadResult::Loaded);

	result = loadBackgroundFromFile(file, "background.png", outBackground);
	if (result != LoadResult::NotFound) return (result == LoadResult::Loaded);

	*outTiled = true;
	result = loadBackgroundFromFile(file, "tiled.jpg", outBackground);
	if (result != LoadResult::NotFound) return (result == LoadResult::Loaded);

	result = loadBackgroundFromFile(file, "tiled.png", outBackground);
	if (result != LoadResult::NotFound) return (result == LoadResult::Loaded);
	return true;
}

bool LoadTheme(
		const QByteArray &content,
		const style::colorizer &colorizer,
		const std::optional<QByteArray> &editedPalette,
		Cached *cache = nullptr,
		Instance *out = nullptr) {
	if (content.size() < 4) {
		LOG(("Theme Error: Bad theme content size: %1").arg(content.size()));
		return false;
	}

	if (cache) {
		*cache = Cached();
	}
	zlib::FileToRead file(content);

	const auto emptyColorizer = style::colorizer();
	const auto &paletteColorizer = editedPalette ? emptyColorizer : colorizer;

	unz_global_info globalInfo = { 0 };
	file.getGlobalInfo(&globalInfo);
	if (file.error() == UNZ_OK) {
		auto schemeContent = editedPalette.value_or(QByteArray());
		if (schemeContent.isEmpty()) {
			schemeContent = file.readFileContent("colors.tdesktop-theme", zlib::kCaseInsensitive, kThemeSchemeSizeLimit);
		}
		if (schemeContent.isEmpty()) {
			file.clearError();
			schemeContent = file.readFileContent("colors.tdesktop-palette", zlib::kCaseInsensitive, kThemeSchemeSizeLimit);
		}
		if (file.error() != UNZ_OK) {
			LOG(("Theme Error: could not read 'colors.tdesktop-theme' or 'colors.tdesktop-palette' in the theme file."));
			return false;
		}
		if (!loadColorScheme(schemeContent, paletteColorizer, out)) {
			DEBUG_LOG(("Theme: Could not loadColorScheme."));
			return false;
		}
		if (!out) {
			Background()->saveAdjustableColors();
		}

		auto backgroundTiled = false;
		auto backgroundContent = QByteArray();
		if (!loadBackground(file, &backgroundContent, &backgroundTiled)) {
			DEBUG_LOG(("Theme: Could not loadBackground."));
			return false;
		}

		if (!backgroundContent.isEmpty()) {
			auto check = QBuffer(&backgroundContent);
			auto reader = QImageReader(&check);
			const auto size = reader.size();
			if (size.isEmpty()
				|| (size.width() * size.height() > kBackgroundSizeLimit)) {
				LOG(("Theme Error: bad background image size in the theme file."));
				return false;
			}
			auto background = Images::Read({
				.content = backgroundContent,
				.forceOpaque = true,
			}).image;
			if (background.isNull()) {
				LOG(("Theme Error: could not read background image in the theme file."));
				return false;
			}
			if (colorizer) {
				style::colorize(background, colorizer);
			}
			if (cache) {
				auto buffer = QBuffer(&cache->background);
				if (!background.save(&buffer, "BMP")) {
					LOG(("Theme Error: could not write background image as a BMP to cache."));
					return false;
				}
				cache->tiled = backgroundTiled;
			}
			applyBackground(std::move(background), backgroundTiled, out);
		}
	} else {
		// Looks like it is not a .zip theme.
		if (!loadColorScheme(editedPalette.value_or(content), paletteColorizer, out)) {
			DEBUG_LOG(("Theme: Could not loadColorScheme from non-zip."));
			return false;
		}
		if (!out) {
			Background()->saveAdjustableColors();
		}
	}
	if (out) {
		out->palette.finalize(paletteColorizer);
	}
	if (cache) {
		if (out) {
			cache->colors = out->palette.save();
		} else {
			cache->colors = style::main_palette::save();
		}
		cache->paletteChecksum = style::palette::Checksum();
		cache->contentChecksum = base::crc32(content.constData(), content.size());
	}
	return true;
}

bool InitializeFromCache(
		const QByteArray &content,
		const Cached &cache) {
	if (cache.paletteChecksum != style::palette::Checksum()) {
		return false;
	}
	if (cache.contentChecksum != base::crc32(content.constData(), content.size())) {
		return false;
	}

	QImage background;
	if (!cache.background.isEmpty()) {
		QDataStream stream(cache.background);
		QImageReader reader(stream.device());
		reader.setAutoTransform(true);
		if (!reader.read(&background) || background.isNull()) {
			return false;
		}
	}

	if (!style::main_palette::load(cache.colors)) {
		return false;
	}
	Background()->saveAdjustableColors();
	if (!background.isNull()) {
		applyBackground(std::move(background), cache.tiled, nullptr);
	}

	return true;
}

[[nodiscard]] std::optional<QByteArray> ReadEditingPalette() {
	auto file = QFile(EditingPalettePath());
	return file.open(QIODevice::ReadOnly)
		? std::make_optional(file.readAll())
		: std::nullopt;
}

bool InitializeFromSaved(Saved &&saved) {
	if (saved.object.content.size() < 4) {
		LOG(("Theme Error: Could not load theme from '%1' (%2)").arg(
			saved.object.pathRelative,
			saved.object.pathAbsolute));
		return false;
	}

	const auto editing = ReadEditingPalette();
	GlobalBackground.createIfNull();
	if (!editing && InitializeFromCache(saved.object.content, saved.cache)) {
		return true;
	}

	const auto colorizer = ColorizerForTheme(saved.object.pathAbsolute);
	if (!LoadTheme(saved.object.content, colorizer, editing, &saved.cache)) {
		DEBUG_LOG(("Theme: Could not load from saved."));
		return false;
	}
	if (editing) {
		Background()->setEditingTheme(ReadCloudFromText(*editing));
	} else {
		Local::writeTheme(saved);
	}
	return true;
}

[[nodiscard]] QImage PostprocessBackgroundImage(
		QImage image,
		const Data::WallPaper &paper) {
	if (image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	image.setDevicePixelRatio(cRetinaFactor());
	if (Data::IsLegacy3DefaultWallPaper(paper)) {
		return Images::DitherImage(std::move(image));
	}
	return image;
}

void ClearApplying() {
	GlobalApplying = Applying();
}

SendMediaReady PrepareWallPaper(MTP::DcId dcId, const QImage &image) {
	PreparedPhotoThumbs thumbnails;
	QVector<MTPPhotoSize> sizes;

	QByteArray jpeg;
	QBuffer jpegBuffer(&jpeg);
	image.save(&jpegBuffer, "JPG", 87);

	const auto scaled = [&](int size) {
		return image.scaled(
			size,
			size,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	};
	const auto push = [&](const char *type, QImage &&image) {
		sizes.push_back(MTP_photoSize(
			MTP_string(type),
			MTP_int(image.width()),
			MTP_int(image.height()), MTP_int(0)));
		thumbnails.emplace(
			type[0],
			PreparedPhotoThumb{ .image = std::move(image) });
	};
	push("s", scaled(320));

	const auto filename = qsl("wallpaper.jpg");
	auto attributes = QVector<MTPDocumentAttribute>(
		1,
		MTP_documentAttributeFilename(MTP_string(filename)));
	attributes.push_back(MTP_documentAttributeImageSize(
		MTP_int(image.width()),
		MTP_int(image.height())));
	const auto id = base::RandomValue<DocumentId>();
	const auto document = MTP_document(
		MTP_flags(0),
		MTP_long(id),
		MTP_long(0),
		MTP_bytes(),
		MTP_int(base::unixtime::now()),
		MTP_string("image/jpeg"),
		MTP_int(jpeg.size()),
		MTP_vector<MTPPhotoSize>(sizes),
		MTPVector<MTPVideoSize>(),
		MTP_int(dcId),
		MTP_vector<MTPDocumentAttribute>(attributes));

	return SendMediaReady(
		SendMediaType::ThemeFile,
		QString(), // filepath
		filename,
		jpeg.size(),
		jpeg,
		id,
		0,
		QString(),
		PeerId(),
		MTP_photoEmpty(MTP_long(0)),
		thumbnails,
		document,
		QByteArray(),
		0);
}

void ClearEditingPalette() {
	QFile(EditingPalettePath()).remove();
}

} // namespace

ChatBackground::AdjustableColor::AdjustableColor(style::color data)
: item(data)
, original(data->c) {
}

// They're duplicated in window_theme_editor_box.cpp:ReplaceAdjustableColors.
ChatBackground::ChatBackground() : _adjustableColors({
		st::msgServiceBg,
		st::msgServiceBgSelected,
		st::historyScrollBg,
		st::historyScrollBgOver,
		st::historyScrollBarBg,
		st::historyScrollBarBgOver }) {
}

void ChatBackground::setThemeData(QImage &&themeImage, bool themeTile) {
	_themeImage = PostprocessBackgroundImage(
		std::move(themeImage),
		Data::ThemeWallPaper());
	_themeTile = themeTile;
}

void ChatBackground::initialRead() {
	if (started()) {
		return;
	} else if (!Local::readBackground()) {
		set(Data::ThemeWallPaper());
	}
	if (_localStoredTileDayValue) {
		_tileDayValue = *_localStoredTileDayValue;
	}
	if (_localStoredTileNightValue) {
		_tileNightValue = *_localStoredTileNightValue;
	}
}

void ChatBackground::start() {
	saveAdjustableColors();

	_updates.events(
	) | rpl::start_with_next([=](const BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			style::NotifyPaletteChanged();
		}
	}, _lifetime);

	initialRead();

	Core::App().domain().activeSessionValue(
	) | rpl::filter([=](Main::Session *session) {
		return session != _session;
	}) | rpl::start_with_next([=](Main::Session *session) {
		_session = session;
		checkUploadWallPaper();
	}, _lifetime);

	Core::App().settings().setSystemDarkMode(Platform::IsDarkMode());
}

void ChatBackground::checkUploadWallPaper() {
	if (!_session) {
		_wallPaperUploadLifetime = rpl::lifetime();
		_wallPaperUploadId = FullMsgId();
		_wallPaperRequestId = 0;
		return;
	}
	if (const auto id = base::take(_wallPaperUploadId)) {
		_session->uploader().cancel(id);
	}
	if (const auto id = base::take(_wallPaperRequestId)) {
		_session->api().request(id).cancel();
	}
	if (!Data::IsCustomWallPaper(_paper)
		|| _original.isNull()
		|| _editingTheme.has_value()) {
		return;
	}

	const auto ready = PrepareWallPaper(_session->mainDcId(), _original);
	const auto documentId = ready.id;
	_wallPaperUploadId = FullMsgId(0, _session->data().nextLocalMessageId());
	_session->uploader().uploadMedia(_wallPaperUploadId, ready);
	if (_wallPaperUploadLifetime) {
		return;
	}
	_wallPaperUploadLifetime = _session->uploader().documentReady(
	) | rpl::start_with_next([=](const Storage::UploadedMedia &data) {
		if (data.fullId != _wallPaperUploadId) {
			return;
		}
		_wallPaperUploadId = FullMsgId();
		_wallPaperRequestId = _session->api().request(
			MTPaccount_UploadWallPaper(
				data.info.file,
				MTP_string("image/jpeg"),
				_paper.mtpSettings()
			)
		).done([=](const MTPWallPaper &result) {
			result.match([&](const MTPDwallPaper &data) {
				_session->data().documentConvert(
					_session->data().document(documentId),
					data.vdocument());
			}, [&](const MTPDwallPaperNoFile &data) {
				LOG(("API Error: "
					"Got wallPaperNoFile after account.UploadWallPaper."));
			});
			if (const auto paper = Data::WallPaper::Create(_session, result)) {
				setPaper(*paper);
				writeNewBackgroundSettings();
				_updates.fire({ BackgroundUpdate::Type::New, tile() });
			}
		}).send();
	});
}

QImage ChatBackground::postprocessBackgroundImage(QImage image) {
	return PostprocessBackgroundImage(std::move(image), _paper);
}

void ChatBackground::set(const Data::WallPaper &paper, QImage image) {
	image = Ui::PreprocessBackgroundImage(std::move(image));

	const auto needResetAdjustable = Data::IsDefaultWallPaper(paper)
		&& !Data::IsDefaultWallPaper(_paper)
		&& !nightMode()
		&& _themeObject.pathAbsolute.isEmpty();
	if (Data::IsThemeWallPaper(paper) && _themeImage.isNull()) {
		setPaper(Data::DefaultWallPaper());
	} else {
		setPaper(paper);
		if (needResetAdjustable) {
			// If we had a default color theme with non-default background,
			// and we switch to default background we must somehow switch from
			// adjusted service colors to default (non-adjusted) service colors.
			// The only way to do that right now is through full palette reset.
			restoreAdjustableColors();
		}
	}
	if (Data::IsThemeWallPaper(_paper)) {
		(nightMode() ? _tileNightValue : _tileDayValue) = _themeTile;
		setPrepared(_themeImage, _themeImage, QImage());
	} else if (Data::details::IsTestingThemeWallPaper(_paper)
		|| Data::details::IsTestingDefaultWallPaper(_paper)
		|| Data::details::IsTestingEditorWallPaper(_paper)) {
		if (Data::details::IsTestingDefaultWallPaper(_paper)
			|| image.isNull()) {
			image = ReadDefaultImage();
			setPaper(Data::details::TestingDefaultWallPaper());
		}
		setPreparedAfterPaper(std::move(image));
	} else {
		if (Data::IsLegacy1DefaultWallPaper(_paper)) {
			image.load(qsl(":/gui/art/bg_initial.jpg"));
			const auto scale = cScale() * cIntRetinaFactor();
			if (scale != 100) {
				image = image.scaledToWidth(
					style::ConvertScale(image.width(), scale),
					Qt::SmoothTransformation);
			}
		} else if (Data::IsDefaultWallPaper(_paper)
			|| (_paper.backgroundColors().empty() && image.isNull())) {
			setPaper(Data::DefaultWallPaper().withParamsFrom(_paper));
			image = ReadDefaultImage();
		}
		Local::writeBackground(
			_paper,
			((Data::IsDefaultWallPaper(_paper)
				|| Data::IsLegacy1DefaultWallPaper(_paper))
				? QImage()
				: image));
		setPreparedAfterPaper(std::move(image));
	}
	Assert(colorForFill()
		|| !_gradient.isNull()
		|| (!_original.isNull()
			&& !_prepared.isNull()
			&& !_preparedForTiled.isNull()));

	_updates.fire({ BackgroundUpdate::Type::New, tile() }); // delayed?
	if (needResetAdjustable) {
		_updates.fire({ BackgroundUpdate::Type::TestingTheme, tile() });
		_updates.fire({ BackgroundUpdate::Type::ApplyingTheme, tile() });
	}
	checkUploadWallPaper();
}

void ChatBackground::setPreparedAfterPaper(QImage image) {
	const auto &bgColors = _paper.backgroundColors();
	if (_paper.isPattern() && !image.isNull()) {
		if (bgColors.size() < 2) {
			auto prepared = postprocessBackgroundImage(
				Ui::PreparePatternImage(
					image,
					bgColors,
					_paper.gradientRotation(),
					_paper.patternOpacity()));
			setPrepared(
				std::move(image),
				std::move(prepared),
				QImage());
		} else {
			image = postprocessBackgroundImage(std::move(image));
			if (Ui::IsPatternInverted(bgColors, _paper.patternOpacity())) {
				image = Ui::InvertPatternImage(std::move(image));
			}
			setPrepared(
				image,
				image,
				Data::GenerateDitheredGradient(_paper));
		}
	} else if (bgColors.size() == 1) {
		setPrepared(QImage(), QImage(), QImage());
	} else if (!bgColors.empty()) {
		setPrepared(
			QImage(),
			QImage(),
			Data::GenerateDitheredGradient(_paper));
	} else {
		image = postprocessBackgroundImage(std::move(image));
		setPrepared(image, image, QImage());
	}
}

void ChatBackground::setPrepared(
		QImage original,
		QImage prepared,
		QImage gradient) {
	Expects(original.isNull() || GoodImageFormatAndSize(original));
	Expects(prepared.isNull() || GoodImageFormatAndSize(prepared));
	Expects(gradient.isNull() || GoodImageFormatAndSize(gradient));

	if (!prepared.isNull() && !_paper.isPattern() && _paper.isBlurred()) {
		prepared = Ui::PrepareBlurredBackground(std::move(prepared));
	}
	if (adjustPaletteRequired()) {
		if ((prepared.isNull() || _paper.isPattern())
			&& !_paper.backgroundColors().empty()) {
			adjustPaletteUsingColors(_paper.backgroundColors());
		} else if (!prepared.isNull()) {
			adjustPaletteUsingBackground(prepared);
		}
	}

	_original = std::move(original);
	_prepared = std::move(prepared);
	_gradient = std::move(gradient);
	_imageMonoColor = _gradient.isNull()
		? Ui::CalculateImageMonoColor(_prepared)
		: std::nullopt;
	_preparedForTiled = Ui::PrepareImageForTiled(_prepared);
}

void ChatBackground::setPaper(const Data::WallPaper &paper) {
	_paper = paper.withoutImageData();
}

bool ChatBackground::adjustPaletteRequired() {
	const auto usingThemeBackground = [&] {
		return Data::IsThemeWallPaper(_paper)
			|| Data::details::IsTestingThemeWallPaper(_paper);
	};
	const auto usingDefaultBackground = [&] {
		return Data::IsDefaultWallPaper(_paper)
			|| Data::details::IsTestingDefaultWallPaper(_paper);
	};

	if (_editingTheme.has_value()) {
		return false;
	} else if (isNonDefaultThemeOrBackground() || nightMode()) {
		return !usingThemeBackground();
	}
	return !usingDefaultBackground();
}

std::optional<Data::CloudTheme> ChatBackground::editingTheme() const {
	return _editingTheme;
}

void ChatBackground::setEditingTheme(const Data::CloudTheme &editing) {
	_editingTheme = editing;
}

void ChatBackground::clearEditingTheme(ClearEditing clear) {
	if (!_editingTheme) {
		return;
	}
	_editingTheme = std::nullopt;
	if (clear == ClearEditing::Temporary) {
		return;
	}
	ClearEditingPalette();
	if (clear == ClearEditing::RevertChanges) {
		reapplyWithNightMode(std::nullopt, _nightMode);
		KeepApplied();
	}
}

void ChatBackground::adjustPaletteUsingBackground(const QImage &image) {
	adjustPaletteUsingColor(Ui::CountAverageColor(image));
}

void ChatBackground::adjustPaletteUsingColors(
		const std::vector<QColor> &colors) {
	adjustPaletteUsingColor(Ui::CountAverageColor(colors));
}

void ChatBackground::adjustPaletteUsingColor(QColor color) {
	const auto prepared = color.toHsl();
	for (const auto &adjustable : _adjustableColors) {
		const auto adjusted = Ui::ThemeAdjustedColor(adjustable.item->c, prepared);
		adjustable.item.set(
			adjusted.red(),
			adjusted.green(),
			adjusted.blue(),
			adjusted.alpha());
	}
}

std::optional<QColor> ChatBackground::colorForFill() const {
	return !_prepared.isNull()
		? imageMonoColor()
		: (!_gradient.isNull() || _paper.backgroundColors().empty())
		? std::nullopt
		: std::make_optional(_paper.backgroundColors().front());
}

QImage ChatBackground::gradientForFill() const {
	return _gradient;
}

void ChatBackground::recacheGradientForFill(QImage gradient) {
	if (_gradient.size() == gradient.size()) {
		_gradient = std::move(gradient);
	}
}

QImage ChatBackground::createCurrentImage() const {
	if (const auto fill = colorForFill()) {
		auto result = QImage(512, 512, QImage::Format_ARGB32_Premultiplied);
		result.fill(*fill);
		return result;
	} else if (_gradient.isNull()) {
		return _prepared;
	} else if (_prepared.isNull()) {
		return _gradient;
	}
	auto result = _gradient.scaled(
		_prepared.size(),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	result.setDevicePixelRatio(1.);
	{
		auto p = QPainter(&result);
		const auto patternOpacity = paper().patternOpacity();
		if (patternOpacity >= 0.) {
			p.setCompositionMode(QPainter::CompositionMode_SoftLight);
			p.setOpacity(patternOpacity);
		} else {
			p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		}
		p.drawImage(QRect(QPoint(), _prepared.size()), _prepared);
		if (patternOpacity < 0. && patternOpacity > -1.) {
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			p.setOpacity(1. + patternOpacity);
			p.fillRect(QRect(QPoint(), _prepared.size()), Qt::black);
		}
	}
	return result;
}

bool ChatBackground::tile() const {
	return nightMode() ? _tileNightValue : _tileDayValue;
}

bool ChatBackground::tileDay() const {
	if (Data::details::IsTestingThemeWallPaper(_paper) ||
		Data::details::IsTestingDefaultWallPaper(_paper)) {
		if (!nightMode()) {
			return _tileForRevert;
		}
	}
	return _tileDayValue;
}

bool ChatBackground::tileNight() const {
	if (Data::details::IsTestingThemeWallPaper(_paper) ||
		Data::details::IsTestingDefaultWallPaper(_paper)) {
		if (nightMode()) {
			return _tileForRevert;
		}
	}
	return _tileNightValue;
}

std::optional<QColor> ChatBackground::imageMonoColor() const {
	return _imageMonoColor;
}

void ChatBackground::setTile(bool tile) {
	Expects(started());

	const auto old = this->tile();
	if (nightMode()) {
		setTileNightValue(tile);
	} else {
		setTileDayValue(tile);
	}
	if (this->tile() != old) {
		if (!Data::details::IsTestingThemeWallPaper(_paper)
			&& !Data::details::IsTestingDefaultWallPaper(_paper)) {
			Local::writeSettings();
		}
		_updates.fire({ BackgroundUpdate::Type::Changed, tile }); // delayed?
	}
}

void ChatBackground::setTileDayValue(bool tile) {
	if (started()) {
		_tileDayValue = tile;
	} else {
		_localStoredTileDayValue = tile;
	}
}

void ChatBackground::setTileNightValue(bool tile) {
	if (started()) {
		_tileNightValue = tile;
	} else {
		_localStoredTileNightValue = tile;
	}
}

void ChatBackground::setThemeObject(const Object &object) {
	_themeObject = object;
	_themeObject.content = QByteArray();
}

const Object &ChatBackground::themeObject() const {
	return _themeObject;
}

void ChatBackground::reset() {
	if (Data::details::IsTestingThemeWallPaper(_paper)
		|| Data::details::IsTestingDefaultWallPaper(_paper)) {
		if (_themeImage.isNull()) {
			_paperForRevert = Data::DefaultWallPaper();
			_originalForRevert = QImage();
			_tileForRevert = false;
		} else {
			_paperForRevert = Data::ThemeWallPaper();
			_originalForRevert = _themeImage;
			_tileForRevert = _themeTile;
		}
	} else {
		set(Data::ThemeWallPaper());
		restoreAdjustableColors();
		_updates.fire({ BackgroundUpdate::Type::TestingTheme, tile() });
		_updates.fire({ BackgroundUpdate::Type::ApplyingTheme, tile() });
	}
	writeNewBackgroundSettings();
}

bool ChatBackground::started() const {
	return !Data::details::IsUninitializedWallPaper(_paper);
}

void ChatBackground::saveForRevert() {
	Expects(started());

	if (!Data::details::IsTestingThemeWallPaper(_paper)
		&& !Data::details::IsTestingDefaultWallPaper(_paper)) {
		_paperForRevert = _paper;
		_originalForRevert = std::move(_original);
		_tileForRevert = tile();
	}
}

void ChatBackground::saveAdjustableColors() {
	for (auto &color : _adjustableColors) {
		color.original = color.item->c;
	}
}

void ChatBackground::restoreAdjustableColors() {
	for (const auto &color : _adjustableColors) {
		const auto value = color.original;
		color.item.set(value.red(), value.green(), value.blue(), value.alpha());
	}
}

void ChatBackground::setTestingTheme(Instance &&theme) {
	style::main_palette::apply(theme.palette);
	saveAdjustableColors();

	auto switchToThemeBackground = !theme.background.isNull()
		|| Data::IsThemeWallPaper(_paper)
		|| (Data::IsDefaultWallPaper(_paper)
			&& !nightMode()
			&& _themeObject.pathAbsolute.isEmpty());
	if (AreTestingTheme() && _editingTheme.has_value()) {
		// Grab current background image if it is not already custom
		// Use prepared pixmap, not original image, because we're
		// for sure switching to a non-pattern wall-paper (testing editor).
		if (!Data::IsCustomWallPaper(_paper)) {
			saveForRevert();
			set(
				Data::details::TestingEditorWallPaper(),
				base::take(_prepared));
		}
	} else if (switchToThemeBackground) {
		saveForRevert();
		set(
			Data::details::TestingThemeWallPaper(),
			std::move(theme.background));
		setTile(theme.tiled);
	} else {
		// Apply current background image so that service bg colors are recounted.
		set(_paper, std::move(_original));
	}
	_updates.fire({ BackgroundUpdate::Type::TestingTheme, tile() });
}

void ChatBackground::setTestingDefaultTheme() {
	style::main_palette::reset(ColorizerForTheme(QString()));
	saveAdjustableColors();

	saveForRevert();
	set(Data::details::TestingDefaultWallPaper());
	setTile(false);
	_updates.fire({ BackgroundUpdate::Type::TestingTheme, tile() });
}

void ChatBackground::keepApplied(const Object &object, bool write) {
	setThemeObject(object);
	if (Data::details::IsTestingEditorWallPaper(_paper)) {
		setPaper(Data::CustomWallPaper());
		_themeImage = QImage();
		_themeTile = false;
		if (write) {
			writeNewBackgroundSettings();
		}
	} else if (Data::details::IsTestingThemeWallPaper(_paper)) {
		setPaper(Data::ThemeWallPaper());
		_themeImage = postprocessBackgroundImage(base::duplicate(_original));
		_themeTile = tile();
		if (write) {
			writeNewBackgroundSettings();
		}
	} else if (Data::details::IsTestingDefaultWallPaper(_paper)) {
		setPaper(Data::DefaultWallPaper());
		_themeImage = QImage();
		_themeTile = false;
		if (write) {
			writeNewBackgroundSettings();
		}
	}
	_updates.fire({ BackgroundUpdate::Type::ApplyingTheme, tile() });
}

bool ChatBackground::isNonDefaultThemeOrBackground() {
	initialRead();
	return nightMode()
		? (_themeObject.pathAbsolute != NightThemePath()
			|| !Data::IsThemeWallPaper(_paper))
		: (!_themeObject.pathAbsolute.isEmpty()
			|| !Data::IsDefaultWallPaper(_paper));
}

bool ChatBackground::isNonDefaultBackground() {
	initialRead();
	return _themeObject.pathAbsolute.isEmpty()
		? !Data::IsDefaultWallPaper(_paper)
		: !Data::IsThemeWallPaper(_paper);
}

void ChatBackground::writeNewBackgroundSettings() {
	if (tile() != _tileForRevert) {
		Local::writeSettings();
	}
	Local::writeBackground(
		_paper,
		((Data::IsThemeWallPaper(_paper)
			|| Data::IsDefaultWallPaper(_paper))
			? QImage()
			: _original));
}

void ChatBackground::revert() {
	if (Data::details::IsTestingThemeWallPaper(_paper)
		|| Data::details::IsTestingDefaultWallPaper(_paper)
		|| Data::details::IsTestingEditorWallPaper(_paper)) {
		setTile(_tileForRevert);
		set(_paperForRevert, std::move(_originalForRevert));
	} else {
		// Apply current background image so that service bg colors are recounted.
		set(_paper, std::move(_original));
	}
	_updates.fire({ BackgroundUpdate::Type::RevertingTheme, tile() });
}

void ChatBackground::appliedEditedPalette() {
	_updates.fire({ BackgroundUpdate::Type::ApplyingEdit, tile() });
}

void ChatBackground::downloadingStarted(bool tile) {
	_updates.fire({ BackgroundUpdate::Type::Start, tile });
}

void ChatBackground::setNightModeValue(bool nightMode) {
	_nightMode = nightMode;
}

bool ChatBackground::nightMode() const {
	return _nightMode;
}

void ChatBackground::reapplyWithNightMode(
		std::optional<QString> themePath,
		bool newNightMode) {
	if (!started()) {
		// We can get here from legacy passcoded state.
		// In this case Background() is not started yet, because
		// some settings and the background itself were not read.
		return;
	} else if (_nightMode != newNightMode && !nightModeChangeAllowed()) {
		return;
	}
	const auto settingExactTheme = themePath.has_value();
	const auto nightModeChanged = (newNightMode != _nightMode);
	const auto oldNightMode = _nightMode;
	_nightMode = newNightMode;
	auto read = settingExactTheme ? Saved() : Local::readThemeAfterSwitch();
	auto path = read.object.pathAbsolute;

	_nightMode = oldNightMode;
	auto oldTileValue = (_nightMode ? _tileNightValue : _tileDayValue);
	const auto alreadyOnDisk = [&] {
		if (read.object.content.isEmpty()) {
			return false;
		}
		auto preview = std::make_unique<Preview>();
		preview->object = std::move(read.object);
		preview->instance.cached = std::move(read.cache);
		const auto loaded = LoadTheme(
			preview->object.content,
			ColorizerForTheme(path),
			std::nullopt,
			&preview->instance.cached,
			&preview->instance);
		if (!loaded) {
			return false;
		}
		Apply(std::move(preview));
		return true;
	}();
	if (!alreadyOnDisk) {
		path = themePath
			? *themePath
			: (newNightMode ? NightThemePath() : QString());
		ApplyDefaultWithPath(path);
	}

	// Theme editor could have already reverted the testing of this toggle.
	if (AreTestingTheme()) {
		GlobalApplying.overrideKeep = [=] {
			if (nightModeChanged) {
				_nightMode = newNightMode;

				// Restore the value, it was set inside theme testing.
				(oldNightMode ? _tileNightValue : _tileDayValue) = oldTileValue;
			}

			const auto saved = std::move(GlobalApplying.data);
			if (!alreadyOnDisk) {
				// First-time switch to default night mode should write it.
				Local::writeTheme(saved);
			}
			ClearApplying();
			keepApplied(saved.object, settingExactTheme);
			if (tile() != _tileForRevert || nightModeChanged) {
				Local::writeSettings();
			}
			if (!settingExactTheme && !Local::readBackground()) {
				set(Data::ThemeWallPaper());
			}
		};
	}
}

bool ChatBackground::nightModeChangeAllowed() const {
	const auto &settings = Core::App().settings();
	const auto allowedToBeAfterChange = settings.systemDarkModeEnabled()
		? settings.systemDarkMode().value_or(!_nightMode)
		: !_nightMode;
	return (_nightMode != allowedToBeAfterChange);
}

void ChatBackground::toggleNightMode(std::optional<QString> themePath) {
	reapplyWithNightMode(themePath, !_nightMode);
}

ChatBackground *Background() {
	GlobalBackground.createIfNull();
	return GlobalBackground.data();
}

bool IsEmbeddedTheme(const QString &path) {
	return path.isEmpty() || path.startsWith(qstr(":/gui/"));
}

bool Initialize(Saved &&saved) {
	if (InitializeFromSaved(std::move(saved))) {
		Background()->setThemeObject(saved.object);
		return true;
	}
	DEBUG_LOG(("Theme: Could not initialize from saved."));
	return false;
}

void Uninitialize() {
	GlobalBackground.clear();
	GlobalApplying = Applying();
}

bool Apply(
		const QString &filepath,
		const Data::CloudTheme &cloud) {
	if (auto preview = PreviewFromFile(QByteArray(), filepath, cloud)) {
		return Apply(std::move(preview));
	}
	return false;
}

bool Apply(std::unique_ptr<Preview> preview) {
	GlobalApplying.data.object = std::move(preview->object);
	GlobalApplying.data.cache = std::move(preview->instance.cached);
	if (GlobalApplying.paletteForRevert.isEmpty()) {
		GlobalApplying.paletteForRevert = style::main_palette::save();
	}
	Background()->setTestingTheme(std::move(preview->instance));
	return true;
}

void ApplyDefaultWithPath(const QString &themePath) {
	if (!themePath.isEmpty()) {
		if (auto preview = PreviewFromFile(QByteArray(), themePath, {})) {
			Apply(std::move(preview));
		}
	} else {
		GlobalApplying.data = Saved();
		if (GlobalApplying.paletteForRevert.isEmpty()) {
			GlobalApplying.paletteForRevert = style::main_palette::save();
		}
		Background()->setTestingDefaultTheme();
	}
}

bool ApplyEditedPalette(const QByteArray &content) {
	auto out = Instance();
	if (!loadColorScheme(content, style::colorizer(), &out)) {
		return false;
	}
	style::main_palette::apply(out.palette);
	Background()->appliedEditedPalette();
	return true;
}

void KeepApplied() {
	if (!AreTestingTheme()) {
		return;
	} else if (GlobalApplying.overrideKeep) {
		// This callback will be destroyed while running.
		// And it won't be able to safely access captures after that.
		// So we save it on stack for the time while it is running.
		const auto onstack = base::take(GlobalApplying.overrideKeep);
		onstack();
		return;
	}
	const auto saved = std::move(GlobalApplying.data);
	Local::writeTheme(saved);
	ClearApplying();
	Background()->keepApplied(saved.object, true);
}

void KeepFromEditor(
		const QByteArray &originalContent,
		const ParsedTheme &originalParsed,
		const Data::CloudTheme &cloud,
		const QByteArray &themeContent,
		const ParsedTheme &themeParsed,
		const QImage &background) {
	ClearApplying();
	const auto content = themeContent.isEmpty()
		? originalContent
		: themeContent;
	auto saved = Saved();
	auto &cache = saved.cache;
	auto &object = saved.object;
	cache.colors = style::main_palette::save();
	cache.paletteChecksum = style::palette::Checksum();
	cache.contentChecksum = base::crc32(content.constData(), content.size());
	cache.background = themeParsed.background;
	cache.tiled = themeParsed.tiled;
	object.cloud = cloud;
	object.content = themeContent.isEmpty()
		? originalContent
		: themeContent;
	object.pathAbsolute = object.pathRelative = CachedThemePath(
		cloud.documentId);
	Local::writeTheme(saved);
	Background()->keepApplied(saved.object, true);
	Background()->setThemeData(
		base::duplicate(background),
		themeParsed.tiled);
	Background()->set(Data::ThemeWallPaper());
	Background()->writeNewBackgroundSettings();
}

void Revert() {
	if (!AreTestingTheme()) {
		return;
	}
	style::main_palette::load(GlobalApplying.paletteForRevert);
	Background()->saveAdjustableColors();

	ClearApplying();
	Background()->revert();
}

QString NightThemePath() {
	return kNightThemeFile.utf16();
}

bool IsNonDefaultBackground() {
	return Background()->isNonDefaultBackground();
}

bool IsNightMode() {
	return GlobalBackground ? Background()->nightMode() : false;
}

rpl::producer<bool> IsNightModeValue() {
	auto changes = Background()->updates(
	) | rpl::filter([=](const BackgroundUpdate &update) {
		return update.type == BackgroundUpdate::Type::ApplyingTheme;
	}) | rpl::to_empty;

	return rpl::single(
		rpl::empty_value()
	) | rpl::then(
		std::move(changes)
	) | rpl::map([=] {
		return IsNightMode();
	}) | rpl::distinct_until_changed();
}

void SetNightModeValue(bool nightMode) {
	if (GlobalBackground || nightMode) {
		Background()->setNightModeValue(nightMode);
	}
}

void ToggleNightMode() {
	Background()->toggleNightMode(std::nullopt);
}

void ToggleNightMode(const QString &path) {
	Background()->toggleNightMode(path);
}

void ToggleNightModeWithConfirmation(
		not_null<Controller*> window,
		Fn<void()> toggle) {
	if (Background()->nightModeChangeAllowed()) {
		toggle();
	} else {
		const auto disableAndToggle = [=](Fn<void()> &&close) {
			Core::App().settings().setSystemDarkModeEnabled(false);
			Core::App().saveSettingsDelayed();
			toggle();
			close();
		};
		window->show(Box<Ui::ConfirmBox>(
			tr::lng_settings_auto_night_warning(tr::now),
			tr::lng_settings_auto_night_disable(tr::now),
			disableAndToggle));
	}
}

void ResetToSomeDefault() {
	Background()->reapplyWithNightMode(
		IsNightMode() ? NightThemePath() : QString(),
		IsNightMode());
}

bool LoadFromFile(
		const QString &path,
		not_null<Instance*> out,
		Cached *outCache,
		QByteArray *outContent) {
	const auto colorizer = ColorizerForTheme(path);
	return LoadFromFile(path, out, outCache, outContent, colorizer);
}

bool LoadFromFile(
		const QString &path,
		not_null<Instance*> out,
		Cached *outCache,
		QByteArray *outContent,
		const style::colorizer &colorizer) {
	const auto content = readThemeContent(path);
	if (outContent) {
		*outContent = content;
	}
	return LoadTheme(content, colorizer, std::nullopt, outCache, out);
}

bool LoadFromContent(
		const QByteArray &content,
		not_null<Instance*> out,
		Cached *outCache) {
	return LoadTheme(
		content,
		style::colorizer(),
		std::nullopt,
		outCache,
		out);
}

rpl::producer<bool> IsThemeDarkValue() {
	return rpl::single(
		rpl::empty_value()
	) | rpl::then(
		style::PaletteChanged()
	) | rpl::map([] {
		return (st::dialogsBg->c.valueF() < kDarkValueThreshold);
	});
}

QString EditingPalettePath() {
	return cWorkingDir() + "tdata/editing-theme.tdesktop-palette";
}

bool ReadPaletteValues(const QByteArray &content, Fn<bool(QLatin1String name, QLatin1String value)> callback) {
	if (content.size() > kThemeSchemeSizeLimit) {
		LOG(("Theme Error: color scheme file too large (should be less than 1 MB, got %2)").arg(content.size()));
		return false;
	}

	auto data = base::parse::stripComments(content);
	auto from = data.constData(), end = from + data.size();
	while (from != end) {
		auto name = QLatin1String("");
		auto value = QLatin1String("");
		if (!readNameAndValue(from, end, &name, &value)) {
			DEBUG_LOG(("Theme: Could not readNameAndValue."));
			return false;
		}
		if (name.size() == 0) { // End of content reached.
			return true;
		}
		if (!callback(name, value)) {
			return false;
		}
	}
	return true;
}

} // namespace Theme
} // namespace Window
