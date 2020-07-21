# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

option(TDESKTOP_USE_FONTCONFIG_FALLBACK "Use custom fonts.conf (Linux only)." OFF)
option(TDESKTOP_USE_GTK_FILE_DIALOG "Use custom code for GTK file dialog (Linux only)." OFF)
option(TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME "Disable automatic 'tg://' URL scheme handler registration." ${DESKTOP_APP_USE_PACKAGED})
option(TDESKTOP_DISABLE_NETWORK_PROXY "Disable all code for working through Socks5 or MTProxy." OFF)
option(TDESKTOP_DISABLE_GTK_INTEGRATION "Disable all code for GTK integration (Linux only)." OFF)
option(TDESKTOP_USE_PACKAGED_TGVOIP "Find libtgvoip using CMake instead of bundled one." ${DESKTOP_APP_USE_PACKAGED})
option(TDESKTOP_API_TEST "Use test API credentials." OFF)
option(KTGDESKTOP_ENABLE_PACKER "Enable building update packer on non-special targets." OFF)
option(KTGDESKTOP_APPIMAGE_BUILD "Build with 'appimage' updater key." OFF)
set(TDESKTOP_API_ID "0" CACHE STRING "Provide 'api_id' for the Telegram API access.")
set(TDESKTOP_API_HASH "" CACHE STRING "Provide 'api_hash' for the Telegram API access.")
set(TDESKTOP_LAUNCHER_BASENAME "" CACHE STRING "Desktop file base name (Linux only).")

if (TDESKTOP_API_TEST)
    set(TDESKTOP_API_ID 17349)
    set(TDESKTOP_API_HASH 344583e45741c457fe1862106095a5eb)
endif()

if (NOT DESKTOP_APP_USE_PACKAGED)
    set(TDESKTOP_USE_GTK_FILE_DIALOG ON)
endif()

if (TDESKTOP_USE_GTK_FILE_DIALOG)
    set(TDESKTOP_DISABLE_GTK_INTEGRATION OFF)
endif()

if (DESKTOP_APP_DISABLE_SPELLCHECK)
    target_compile_definitions(Telegram PRIVATE TDESKTOP_DISABLE_SPELLCHECK)
else()
    target_link_libraries(Telegram PRIVATE desktop-app::lib_spellcheck)
endif()

if (DESKTOP_APP_DISABLE_AUTOUPDATE)
    target_compile_definitions(Telegram PRIVATE TDESKTOP_DISABLE_AUTOUPDATE)
endif()

# if (DESKTOP_APP_SPECIAL_TARGET)
#     target_compile_definitions(Telegram PRIVATE TDESKTOP_ALLOW_CLOSED_ALPHA)
# endif()

if (TDESKTOP_USE_FONTCONFIG_FALLBACK)
    target_compile_definitions(Telegram PRIVATE TDESKTOP_USE_FONTCONFIG_FALLBACK)
endif()

if (TDESKTOP_USE_GTK_FILE_DIALOG)
    target_compile_definitions(Telegram PRIVATE TDESKTOP_USE_GTK_FILE_DIALOG)
endif()

if (TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME)
    target_compile_definitions(Telegram PRIVATE TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME)
endif()

if (TDESKTOP_DISABLE_NETWORK_PROXY)
    target_compile_definitions(Telegram PRIVATE TDESKTOP_DISABLE_NETWORK_PROXY)
endif()

if (TDESKTOP_DISABLE_GTK_INTEGRATION)
    target_compile_definitions(Telegram PRIVATE TDESKTOP_DISABLE_GTK_INTEGRATION)
endif()

if (DESKTOP_APP_DISABLE_DBUS_INTEGRATION)
    target_compile_definitions(Telegram PRIVATE TDESKTOP_DISABLE_DBUS_INTEGRATION)
endif()

if (NOT TDESKTOP_LAUNCHER_BASENAME)
    set(TDESKTOP_LAUNCHER_BASENAME "kotatogramdesktop")
endif()
target_compile_definitions(Telegram PRIVATE TDESKTOP_LAUNCHER_BASENAME=${TDESKTOP_LAUNCHER_BASENAME})

if (KTGDESKTOP_APPIMAGE_BUILD)
    target_compile_definitions(Telegram PRIVATE KTGDESKTOP_APPIMAGE_BUILD)
endif()
