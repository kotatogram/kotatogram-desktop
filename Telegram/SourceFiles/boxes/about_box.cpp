/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/about_box.h"

#include "kotato/kotato_version.h"
#include "kotato/kotato_lang.h"
#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/text/text_utilities.h"
#include "base/platform/base_platform_info.h"
#include "core/click_handler_types.h"
#include "core/update_checker.h"
#include "core/application.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace {

rpl::producer<TextWithEntities> Text1() {
	return rktre("ktg_about_text1", {
		"tdesktop_link",
		Ui::Text::Link(ktr("ktg_about_text1_tdesktop"), "https://desktop.telegram.org/")
	});
}

rpl::producer<TextWithEntities> Text2() {
	return tr::lng_about_text2(
		lt_gpl_link,
		rpl::single(Ui::Text::Link(
			"GNU GPL",
			"https://github.com/kotatogram/kotatogram-desktop/blob/master/LICENSE")),
		lt_github_link,
		rpl::single(Ui::Text::Link(
			"GitHub",
			"https://github.com/kotatogram/kotatogram-desktop")),
		Ui::Text::WithEntities);
}

rpl::producer<TextWithEntities> Text3() {
	auto baseLang = Lang::GetInstance().baseId();
	auto currentLang = Lang::Id();
	QString channelLink;

	for (const auto language : { "ru", "uk", "be" }) {
		if (baseLang.startsWith(QLatin1String(language)) || currentLang == QString(language)) {
			channelLink = "https://t.me/kotatogram_ru";
			break;
		}
	}

	if (channelLink.isEmpty()) {
		channelLink = "https://t.me/kotatogram";
	}

	return rktre("ktg_about_text3", {
		"channel_link",
		Ui::Text::Link(ktr("ktg_about_text3_channel"), channelLink)
	}, {
		"faq_link",
		Ui::Text::Link(tr::lng_about_text3_faq(tr::now), telegramFaqLink())
	});
}

} // namespace

AboutBox::AboutBox(QWidget *parent)
: _version(this, tr::lng_about_version(tr::now, lt_version, currentVersionText()), st::aboutVersionLink)
, _text1(this, Text1(), st::aboutLabel)
, _text2(this, Text2(), st::aboutLabel)
, _text3(this, Text3(), st::aboutLabel) {
}

void AboutBox::prepare() {
	setTitle(rpl::single(qsl("Kotatogram Desktop")));

	addButton(tr::lng_close(), [this] { closeBox(); });

	_text1->setLinksTrusted();
	_text2->setLinksTrusted();
	_text3->setLinksTrusted();

	_version->setClickedCallback([this] { showVersionHistory(); });

	setDimensions(st::aboutWidth, st::aboutTextTop + _text1->height() + st::aboutSkip + _text2->height() + st::aboutSkip + _text3->height());
}

void AboutBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_version->moveToLeft(st::boxPadding.left(), st::aboutVersionTop);
	_text1->moveToLeft(st::boxPadding.left(), st::aboutTextTop);
	_text2->moveToLeft(st::boxPadding.left(), _text1->y() + _text1->height() + st::aboutSkip);
	_text3->moveToLeft(st::boxPadding.left(), _text2->y() + _text2->height() + st::aboutSkip);
}

void AboutBox::showVersionHistory() {
	/*
	if (cRealAlphaVersion()) {
		auto url = qsl("https://tdesktop.com/");
		if (Platform::IsWindows32Bit()) {
			url += qsl("win/%1.zip");
		} else if (Platform::IsWindows64Bit()) {
			url += qsl("win64/%1.zip");
		} else if (Platform::IsMac()) {
			url += qsl("mac/%1.zip");
		} else if (Platform::IsLinux()) {
			url += qsl("linux/%1.tar.xz");
		} else {
			Unexpected("Platform value.");
		}
		url = url.arg(qsl("talpha%1_%2").arg(cRealAlphaVersion()).arg(Core::countAlphaVersionSignature(cRealAlphaVersion())));

		QGuiApplication::clipboard()->setText(url);

		Ui::show(Box<Ui::InformBox>("The link to the current private alpha "
			"version of Telegram Desktop was copied to the clipboard."));
	} else {
	*/
		UrlClickHandler::Open(Core::App().changelogLink());
	/*
	}
	*/
}

void AboutBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		closeBox();
	} else {
		BoxContent::keyPressEvent(e);
	}
}

QString telegramFaqLink() {
	const auto result = qsl("https://telegram.org/faq");
	const auto langpacked = [&](const char *language) {
		return result + '/' + language;
	};
	const auto current = Lang::Id();
	for (const auto language : { "de", "es", "it", "ko" }) {
		if (current.startsWith(QLatin1String(language))) {
			return langpacked(language);
		}
	}
	if (current.startsWith(qstr("pt-br"))) {
		return langpacked("br");
	}
	return result;
}

QString currentVersionText() {
	auto result = QString::fromLatin1(AppKotatoVersionStr);
	if (cAlphaVersion()) {
		result += qsl("-%1.%2").arg(AppKotatoTestBranch).arg(AppKotatoTestVersion);
	} else if (AppKotatoBetaVersion) {
		result += " beta";
	}
	if (Platform::IsWindows64Bit()) {
		result += " x64";
	}
	result += qsl(" (TD %1)").arg(AppVersionStr);
	return result;
}
