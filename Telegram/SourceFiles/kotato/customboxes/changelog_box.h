/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkAccessManager>

namespace Ui {
class FlatLabel;
class IconButton;
class VerticalLayout;
} // namespace Ui

namespace Kotato {
class ChangelogBox : public Ui::BoxContent, private base::Subscriber {
public:
	ChangelogBox(QWidget*, const QString &version);

	struct Entry {
		QString version;
		QString tdVersion;
		QString nextVersion;
		QString prevVersion;
		QDateTime releaseDate;
		QString whatToTest;
		QString features;
		QString changes;
		QString fixes;
		QVector<QPair<QString, QString>> links;
	};

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void requestChangelog();

	void versionChanged(const QString &version);
	void refreshTitleButtons(
		const QString &prevVersion = QString(),
		const QString &nextVersion = QString());

	void onRequestError(QNetworkReply::NetworkError e);
	void onRequestFinished();

	class Context;
	std::unique_ptr<Context> _context;

	class Inner;
	QPointer<Inner> _inner;

	class Title;
	object_ptr<Title> _title;
	object_ptr<Ui::IconButton> _previous;
	object_ptr<Ui::IconButton> _next;

	QString _prevVersion;
	QString _nextVersion;

	std::unique_ptr<QNetworkAccessManager> _changelogManager;
	QNetworkReply *_changelogReply = nullptr;

};

} // namespace Kotato