/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
#pragma once

constexpr auto AppKotatoVersion = 1004008;
constexpr auto AppKotatoVersionStr = "1.4.8";
constexpr auto AppKotatoBetaVersion = false;

//#define KTGDESKTOP_IS_TEST_VERSION
constexpr auto AppKotatoTestBranch = "dev";
constexpr auto AppKotatoTestVersion = 0;

#ifdef KTGDESKTOP_IS_TEST_VERSION
constexpr auto AppKotatoTestVersionFull = (1000ULL * AppKotatoVersion + AppKotatoTestVersion);
#else // KTGDESKTOP_IS_TEST_VERSION
constexpr auto AppKotatoTestVersionFull = (0ULL);
#endif // KTGDESKTOP_IS_TEST_VERSION
