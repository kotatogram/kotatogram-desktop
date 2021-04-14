/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once
#include "base/const_string.h"

// used in Updater.cpp and Setup.iss for Windows
constexpr auto AppId = "{C4A4AE8F-B9F7-4CC7-8A6C-BF7EEE87ACA5}"_cs;
constexpr auto AppName = "Kotatogram Desktop"_cs;
constexpr auto AppFile = "Kotatogram"_cs;
constexpr auto AppKotatoVersion = 1004000;
constexpr auto AppKotatoVersionStr = "1.4";
constexpr auto AppBetaVersion = false;

// Test branch related
constexpr auto AppKotatoTestBranch = "dev";

#define TDESKTOP_REQUESTED_ALPHA_VERSION (0ULL)

#ifdef TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION TDESKTOP_REQUESTED_ALPHA_VERSION
#else // TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION (0ULL)
#endif // TDESKTOP_ALLOW_CLOSED_ALPHA
