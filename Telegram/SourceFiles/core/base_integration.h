/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/integration.h"

namespace Core {

class BaseIntegration : public base::Integration {
public:
	BaseIntegration(int argc, char *argv[]);

	void enterFromEventLoop(FnMut<void()> &&method) override;
	void logMessage(const QString &message) override;
	void logAssertionViolation(const QString &info) override;
	[[nodiscard]] bool gtkIntegrationEnabled() const override;

};

} // namespace Core
