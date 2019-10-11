/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once

#include <QtCore/QTimer>

namespace KotatoSettings {

class Manager : public QObject {
	Q_OBJECT

public:
	Manager();
	void fill();
	void clear();
	void write(bool force = false);

	const QStringList &errors() const;

public slots:
	void writeTimeout();

private:
	void writeDefaultFile();
	void writeCurrentSettings();
	bool readCustomFile();
	void writing();

	QStringList _errors;
	QTimer _jsonWriteTimer;

};

void Start();
void Write();
void Finish();

const QStringList &Errors();

} // namespace KotatoSettings
