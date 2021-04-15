/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#include "kotato/customboxes/changelog_box.h"

#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "base/qt_adapters.h"
#include "core/click_handler_types.h"
#include "info/profile/info_profile_icon.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>

namespace Kotato {
namespace {

bool ReadOption(QJsonObject obj, QString key, std::function<void(QJsonValue)> callback) {
	const auto it = obj.constFind(key);
	if (it == obj.constEnd()) {
		return false;
	}
	callback(*it);
	return true;
}

bool ReadObjectOption(QJsonObject obj, QString key, std::function<void(QJsonObject)> callback) {
	auto readResult = false;
	auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
		if (v.isObject()) {
			callback(v.toObject());
			readResult = true;
		}
	});
	return (readValueResult && readResult);
}

bool ReadArrayOption(QJsonObject obj, QString key, std::function<void(QJsonArray)> callback) {
	auto readResult = false;
	auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
		if (v.isArray()) {
			callback(v.toArray());
			readResult = true;
		}
	});
	return (readValueResult && readResult);
}

bool ReadStringOption(QJsonObject obj, QString key, std::function<void(QString)> callback) {
	auto readResult = false;
	auto readValueResult = ReadOption(obj, key, [&](QJsonValue v) {
		if (v.isString()) {
			callback(v.toString());
			readResult = true;
		}
	});
	return (readValueResult && readResult);
}

ChangelogBox::Entry ParseJSONChangelog(const QByteArray &json) {
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(json, &error);

	if (error.error != QJsonParseError::NoError) {
		return ChangelogBox::Entry();
	} else if (!document.isObject()) {
		return ChangelogBox::Entry();
	}

	const auto obj = document.object();
	auto changelog = ChangelogBox::Entry();

	ReadStringOption(obj, "version", [&] (auto v) {
		changelog.version = v;
	});

	ReadStringOption(obj, "tdVersion", [&] (auto v) {
		changelog.tdVersion = v;
	});

	ReadStringOption(obj, "nextVersion", [&] (auto v) {
		changelog.nextVersion = v;
	});

	ReadStringOption(obj, "prevVersion", [&] (auto v) {
		changelog.prevVersion = v;
	});

	ReadStringOption(obj, "releaseDate", [&] (auto v) {
		changelog.releaseDate = QDateTime::fromString(v, Qt::ISODate).toLocalTime();
	});

	ReadStringOption(obj, "whatToTest", [&] (auto v) {
		changelog.whatToTest = v;
	});

	ReadStringOption(obj, "features", [&] (auto v) {
		changelog.features = v;
	});

	ReadStringOption(obj, "changes", [&] (auto v) {
		changelog.changes = v;
	});

	ReadStringOption(obj, "fixes", [&] (auto v) {
		changelog.fixes = v;
	});

	ReadArrayOption(obj, "links", [&] (auto a) {
		for (auto i = a.constBegin(), e = a.constEnd(); i != e; ++i) {
			if (!(*i).isObject()) {
				continue;
			}

			auto link = (*i).toObject();

			auto linkTitle = QString();
			auto linkTarget = QString();

			ReadStringOption(link, "title", [&] (auto v) {
				linkTitle = v;
			});

			ReadStringOption(link, "link", [&] (auto v) {
				linkTarget = v;
			});

			changelog.links.push_back(QPair(linkTitle, linkTarget));
		}
		
	});

	return changelog;
}

} // namespace

class ChangelogBox::Context {
public:
	Context(const QString &version);

	void start() {
		_version.setForced(_version.value(), true);
	}

	const base::Variable<QString> &version() {
		return _version;
	}

	void setVersion(const QString &version) {
		_error.set(QString());
		_version.set(version);
	}

	const base::Variable<QString> &error() {
		return _error;
	}

	void setError(const QString &error) {
		_error.set(error);
	}

	const base::Variable<QString> &loaded() {
		return _loaded;
	}

	const ChangelogBox::Entry &changelog() {
		return _changelog;
	}

	void setChangelog(const ChangelogBox::Entry &changelog) {
		_loaded.set(changelog.version);
		_changelog = changelog;
	}

private:
	base::Variable<QString> _version;
	base::Variable<QString> _error;
	base::Variable<QString> _loaded;
	ChangelogBox::Entry _changelog;
};

ChangelogBox::Context::Context(const QString &version)
: _version(version) {
}

class ChangelogBox::Title : public TWidget, private base::Subscriber {
public:
	Title(QWidget *parent, not_null<Context*> context)
	: TWidget(parent)
	, _context(context)
	, _title(tr::ktg_changelog_title(tr::now)) {
		subscribe(_context->version(), [this](QString version) {
			versionChanged(version);
		});

		_titleWidth = st::semiboldFont->width(_title);
	}

protected:
	void paintEvent(QPaintEvent *e);

private:
	void versionChanged(const QString &version);

	not_null<Context*> _context;

	QString _title;
	QString _version;
	int _titleWidth = 0;
	int _versionWidth = 0;
};

void ChangelogBox::Title::versionChanged(const QString &version) {
	_version = tr::lng_about_version(tr::now, lt_version, version);
	_versionWidth = st::normalFont->width(_version);
	update();
}

void ChangelogBox::Title::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.setFont(st::semiboldFont);
	p.setPen(st::boxTitleFg);
	p.drawTextLeft(0, style::ConvertScale(6), width(), _title, _titleWidth);

	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawTextLeft(0, style::ConvertScale(26), width(), _version, _versionWidth);
}

class ChangelogBox::Inner : public RpWidget, private base::Subscriber {
public:
	Inner(QWidget *parent, not_null<Context*> context);

	void setLoadingText(const QString &text);
	void showChangelog(const ChangelogBox::Entry &changelog);

private:
	void recountHeightAndResize();

	struct Row {
		object_ptr<RpWidget> widget;
	};

	bool _isLoading = true;
	not_null<Context*> _context;
	object_ptr<Ui::FlatLabel> _whatToTestTitle;
	object_ptr<Ui::FlatLabel> _featuresTitle;
	object_ptr<Ui::FlatLabel> _changesTitle;
	object_ptr<Ui::FlatLabel> _fixesTitle;
	object_ptr<Ui::FlatLabel> _linksTitle;
	
	object_ptr<Ui::FlatLabel> _loadingText;

	object_ptr<Ui::FlatLabel> _tdVersion;
	object_ptr<Ui::FlatLabel> _releaseDate;

	object_ptr<Ui::FlatLabel> _whatToTest;
	object_ptr<Ui::FlatLabel> _features;
	object_ptr<Ui::FlatLabel> _changes;
	object_ptr<Ui::FlatLabel> _fixes;

	std::vector<Row> _links;

	int _maxWidth = 0;
};

ChangelogBox::Inner::Inner(QWidget*, not_null<Context*> context)
: _context(context)
, _whatToTestTitle(this, st::settingsSubsectionTitle)
, _featuresTitle(this, st::settingsSubsectionTitle)
, _changesTitle(this, st::settingsSubsectionTitle)
, _fixesTitle(this, st::settingsSubsectionTitle)
, _linksTitle(this, st::settingsSubsectionTitle)
, _loadingText(this, st::boxLabel)
, _tdVersion(this, st::aboutLabel)
, _releaseDate(this, st::aboutLabel)
, _whatToTest(this, st::aboutLabel)
, _features(this, st::aboutLabel)
, _changes(this, st::aboutLabel)
, _fixes(this, st::aboutLabel) {
	subscribe(_context->version(), [this](QString version) {
		setLoadingText(tr::lng_contacts_loading(tr::now));
	});

	subscribe(_context->error(), [this](QString error) {
		if (!error.isEmpty()) {
			setLoadingText(error);
		}
	});

	subscribe(_context->loaded(), [this](QString version) {
		showChangelog(_context->changelog());
	});

	_maxWidth = st::boxWideWidth - st::boxPadding.left() - st::boxPadding.right();

	_whatToTestTitle->setText(tr::ktg_changelog_what_to_test(tr::now));
	_whatToTestTitle->resizeToWidth(_maxWidth);
	_featuresTitle->setText(tr::ktg_changelog_features(tr::now));
	_featuresTitle->resizeToWidth(_maxWidth);
	_changesTitle->setText(tr::ktg_changelog_changes(tr::now));
	_changesTitle->resizeToWidth(_maxWidth);
	_fixesTitle->setText(tr::ktg_changelog_fixes(tr::now));
	_fixesTitle->resizeToWidth(_maxWidth);
	_linksTitle->setText(tr::ktg_changelog_more_info(tr::now));
	_linksTitle->resizeToWidth(_maxWidth);

	_whatToTestTitle->hide();
	_featuresTitle->hide();
	_changesTitle->hide();
	_fixesTitle->hide();
	_linksTitle->hide();
	_loadingText->hide();
	_tdVersion->hide();
	_releaseDate->hide();
	_whatToTest->hide();
	_features->hide();
	_changes->hide();
	_fixes->hide();

	recountHeightAndResize();
}

void ChangelogBox::Inner::setLoadingText(const QString &text) {
	_isLoading = true;
	_loadingText->setText(text);
	_loadingText->resizeToWidth(_maxWidth);
	_links.clear();
	recountHeightAndResize();
}

void ChangelogBox::Inner::showChangelog(const ChangelogBox::Entry &changelog) {
	_tdVersion->setText(
		tr::ktg_changelog_base_version(tr::now, 
			lt_td_version,
			changelog.tdVersion));
	_tdVersion->resizeToWidth(_maxWidth);

	_releaseDate->setText(
		tr::ktg_changelog_release_date(tr::now,
			lt_date,
			langDateTimeFull(changelog.releaseDate)));
	_releaseDate->resizeToWidth(_maxWidth);

	_whatToTest->setText(changelog.whatToTest);
	_whatToTest->resizeToWidth(_maxWidth);
	_features->setText(changelog.features);
	_features->resizeToWidth(_maxWidth);
	_changes->setText(changelog.changes);
	_changes->resizeToWidth(_maxWidth);
	_fixes->setText(changelog.fixes);
	_fixes->resizeToWidth(_maxWidth);

	_links.reserve(changelog.links.size());

	for (auto i = changelog.links.begin(), e = changelog.links.end(); i != e; ++i) {
		auto button = object_ptr<Ui::SettingsButton>(this,
			rpl::single(i->first),
			st::inviteViaLinkButton);
		object_ptr<Info::Profile::FloatingIcon>(
			button,
			st::inviteViaLinkIcon,
			st::inviteViaLinkIconPosition);
		const auto link = i->second;
		button->addClickHandler([this, link] {
			UrlClickHandler::Open(link);
		});
		button->resizeToWidth(_maxWidth + st::boxPadding.left() + st::boxPadding.right());
		_links.push_back(Row{
			.widget = std::move(button)
		});
	}

	_isLoading = false;
	recountHeightAndResize();
}

void ChangelogBox::Inner::recountHeightAndResize() {
	auto newHeight = 0;
	if (_isLoading) {
		newHeight = st::calendarTitleHeight + st::noContactsHeight;
		_whatToTestTitle->hide();
		_featuresTitle->hide();
		_changesTitle->hide();
		_fixesTitle->hide();
		_linksTitle->hide();
		_tdVersion->hide();
		_releaseDate->hide();
		_whatToTest->hide();
		_features->hide();
		_changes->hide();
		_fixes->hide();

		_loadingText->move(
			(_maxWidth - _loadingText->naturalWidth()) / 2 + st::boxPadding.left(),
			(newHeight - _loadingText->heightNoMargins()) / 2);
		_loadingText->show();
	} else {
		_loadingText->hide();

		int left = st::boxPadding.left();

		_tdVersion->move(left, newHeight);
		newHeight += _tdVersion->heightNoMargins();

		_releaseDate->move(left, newHeight);
		newHeight += _releaseDate->heightNoMargins()
			+ st::boxPadding.bottom();

		_tdVersion->show();
		_releaseDate->show();

		if (_whatToTest->heightNoMargins() > 0) {
			newHeight += (st::boxPadding.bottom() * 2);
			_whatToTestTitle->move(left, newHeight);

			newHeight += _whatToTestTitle->heightNoMargins()
				+ (st::boxPadding.bottom() * 2);
			_whatToTest->move(left, newHeight);
			newHeight += _whatToTest->heightNoMargins()
				+ st::boxPadding.bottom();

			_whatToTestTitle->show();
			_whatToTest->show();
		}

		if (_features->heightNoMargins() > 0) {
			newHeight += (st::boxPadding.bottom() * 2);
			_featuresTitle->move(left, newHeight);

			newHeight += _featuresTitle->heightNoMargins()
				+ (st::boxPadding.bottom() * 2);
			_features->move(left, newHeight);
			newHeight += _features->heightNoMargins()
				+ st::boxPadding.bottom();

			_featuresTitle->show();
			_features->show();
		}

		if (_changes->heightNoMargins() > 0) {
			newHeight += (st::boxPadding.bottom() * 2);
			_changesTitle->move(left, newHeight);

			newHeight += _changesTitle->heightNoMargins()
				+ (st::boxPadding.bottom() * 2);
			_changes->move(left, newHeight);
			newHeight += _changes->heightNoMargins()
				+ st::boxPadding.bottom();

			_changesTitle->show();
			_changes->show();
		}

		if (_fixes->heightNoMargins() > 0) {
			newHeight += (st::boxPadding.bottom() * 2);
			_fixesTitle->move(left, newHeight);

			newHeight += _fixesTitle->heightNoMargins()
				+ (st::boxPadding.bottom() * 2);
			_fixes->move(left, newHeight);
			newHeight += _fixes->heightNoMargins()
				+ st::boxPadding.bottom();

			_fixesTitle->show();
			_fixes->show();
		}

		if (_links.size() > 0) {
			newHeight += (st::boxPadding.bottom() * 2);
			_linksTitle->move(left, newHeight);

			newHeight += _linksTitle->heightNoMargins()
				+ st::boxPadding.bottom();
			_linksTitle->show();

			for (auto i = _links.begin(), e = _links.end(); i != e; ++i) {
				i->widget->move(0, newHeight);
				newHeight += i->widget->heightNoMargins();

				i->widget->show();
			}

			newHeight += st::boxPadding.bottom();
		}
	}

	resize(width(), newHeight);
}

ChangelogBox::ChangelogBox(
	QWidget*,
	const QString &version)
: _context(std::make_unique<Context>(version))
, _title(this, _context.get())
, _previous(this, st::calendarPrevious)
, _next(this, st::calendarNext)
, _changelogManager(std::make_unique<QNetworkAccessManager>()) {
	setTopShadowWithSkip(true);
}

void ChangelogBox::prepare() {
	_previous->setClickedCallback([this] { 
		if (!_prevVersion.isEmpty()) {
			_context->setVersion(_prevVersion);
		}
	});
	_next->setClickedCallback([this] {
		if (!_nextVersion.isEmpty()) {
			_context->setVersion(_nextVersion);
		}
	});

	addButton(tr::lng_close(), [this] { closeBox(); });
	subscribe(_context->version(), [this](QString version) { versionChanged(version); });

	_inner = setInnerWidget(
		object_ptr<Inner>(this, _context.get()),
		st::calendarTitleHeight);

	_inner->resizeToWidth(st::boxWideWidth);
	_inner->heightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::boxWideWidth, st::calendarTitleHeight + height);
	}, _inner->lifetime());
	_context->start();
}

void ChangelogBox::requestChangelog() {
	auto baseLang = Lang::GetInstance().baseId().replace("-raw", "");
	auto currentLang = Lang::Id().replace("-raw", "");
	QString languagePath;

	for (const auto language : { "ru", "uk", "be" }) {
		if (baseLang == QString(language) || currentLang == QString(language)) {
			languagePath = "ru/";
			break;
		}
	}

	const auto request = QNetworkRequest(qsl("https://kotatogram.github.io/%1json/changelog/%2.json").arg(
		languagePath,
		_context->version().value()));
	_changelogReply = _changelogManager->get(request);
	_changelogReply->connect(_changelogReply, &QNetworkReply::finished, [=] {
		onRequestFinished();
	});
	_changelogReply->connect(_changelogReply, base::QNetworkReply_error, [=](auto e) {
		onRequestError(e);
	});
}

void ChangelogBox::onRequestError(QNetworkReply::NetworkError e) {
	if (_changelogReply) {
		_changelogReply->deleteLater();
		_changelogReply = nullptr;
	};

	_context->setError(tr::ktg_changelog_load_error(tr::now));
}

void ChangelogBox::onRequestFinished() {
	if (!_changelogReply) {
		_context->setError(tr::ktg_changelog_load_error(tr::now));
		return;
	}

	auto result = _changelogReply->readAll().trimmed();
	_changelogReply->deleteLater();
	_changelogReply = nullptr;

	const auto changelog = ParseJSONChangelog(result);

	if (!changelog.version.isEmpty()) {
		_context->setChangelog(changelog);
		refreshTitleButtons(changelog.prevVersion, changelog.nextVersion);
	} else {
		_context->setError(tr::ktg_changelog_load_error(tr::now));
	}
}

void ChangelogBox::versionChanged(const QString &version) {
	refreshTitleButtons();
	requestChangelog();
}

void ChangelogBox::refreshTitleButtons(
	const QString &prevVersion,
	const QString &nextVersion) {

	_prevVersion = prevVersion;
	auto previousEnabled = !_prevVersion.isEmpty();
	_previous->setIconOverride(previousEnabled ? nullptr : &st::calendarPreviousDisabled);
	_previous->setRippleColorOverride(previousEnabled ? nullptr : &st::boxBg);
	_previous->setCursor(previousEnabled ? style::cur_pointer : style::cur_default);

	_nextVersion = nextVersion;
	auto nextEnabled = !_nextVersion.isEmpty();
	_next->setIconOverride(nextEnabled ? nullptr : &st::calendarNextDisabled);
	_next->setRippleColorOverride(nextEnabled ? nullptr : &st::boxBg);
	_next->setCursor(nextEnabled ? style::cur_pointer : style::cur_default);
}

void ChangelogBox::resizeEvent(QResizeEvent *e) {
	_previous->moveToRight(_next->width(), 0);
	_next->moveToRight(0, 0);
	_title->setGeometryToLeft(
		st::boxPadding.left(),
		0,
		width() - _previous->width() - _next->width() - st::boxPadding.left(),
		st::calendarTitleHeight);
	BoxContent::resizeEvent(e);
}

void ChangelogBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Left) {
		if (!_prevVersion.isEmpty()) {
			_context->setVersion(_prevVersion);
		}
	} else if (e->key() == Qt::Key_Right) {
		if (!_nextVersion.isEmpty()) {
			_context->setVersion(_nextVersion);
		}
	} else {
		BoxContent::keyPressEvent(e);
	}
}

} // namespace Kotato