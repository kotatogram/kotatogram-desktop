/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/specific_linux.h"

#include "platform/linux/linux_libs.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "platform/linux/linux_desktop_environment.h"
#include "platform/linux/file_utilities_linux.h"
#include "platform/platform_notifications_manager.h"
#include "storage/localstorage.h"
#include "core/crash_reports.h"
#include "core/update_checker.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QDesktopWidget>
#include <QtCore/QStandardPaths>
#include <QtCore/QProcess>
#include <QtCore/QVersionNumber>

#include <sys/stat.h>
#include <sys/types.h>
#include <cstdlib>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>

#include <iostream>

using namespace Platform;
using Platform::File::internal::EscapeShell;

namespace {

bool RunShellCommand(const QByteArray &command) {
	auto result = system(command.constData());
	if (result) {
		DEBUG_LOG(("App Error: command failed, code: %1, command (in utf8): %2").arg(result).arg(command.constData()));
		return false;
	}
	DEBUG_LOG(("App Info: command succeeded, command (in utf8): %1").arg(command.constData()));
	return true;
}

void FallbackFontConfig() {
#ifndef DESKTOP_APP_USE_PACKAGED
	const auto custom = cWorkingDir() + "tdata/fc-custom-1.conf";
	const auto finish = gsl::finally([&] {
		if (QFile(custom).exists()) {
			LOG(("Custom FONTCONFIG_FILE: ") + custom);
			qputenv("FONTCONFIG_FILE", QFile::encodeName(custom));
		}
	});

	QProcess process;
	process.setProcessChannelMode(QProcess::MergedChannels);
	process.start("fc-list", QStringList() << "--version");
	process.waitForFinished();
	if (process.exitCode() > 0) {
		LOG(("App Error: Could not start fc-list. Process exited with code: %1.").arg(process.exitCode()));
		return;
	}

	QString result(process.readAllStandardOutput());
	DEBUG_LOG(("Fontconfig version string: ") + result);

	QVersionNumber version = QVersionNumber::fromString(result.split("version ").last());
	if (version.isNull()) {
		LOG(("App Error: Could not get version from fc-list output."));
		return;
	}

	LOG(("Fontconfig version: %1.").arg(version.toString()));
	if (version < QVersionNumber::fromString("2.13")) {
		if (qgetenv("TDESKTOP_FORCE_CUSTOM_FONTCONFIG").isEmpty()) {
			return;
		}
	}

	QFile(":/fc/fc-custom.conf").copy(custom);
#endif // !DESKTOP_APP_USE_PACKAGED
}

} // namespace

namespace Platform {

void SetApplicationIcon(const QIcon &icon) {
	QApplication::setWindowIcon(icon);
}

QString CurrentExecutablePath(int argc, char *argv[]) {
	constexpr auto kMaxPath = 1024;
	char result[kMaxPath] = { 0 };
	auto count = readlink("/proc/self/exe", result, kMaxPath);
	if (count > 0) {
		auto filename = QFile::decodeName(result);
		auto deletedPostfix = qstr(" (deleted)");
		if (filename.endsWith(deletedPostfix) && !QFileInfo(filename).exists()) {
			filename.chop(deletedPostfix.size());
		}
		return filename;
	}

	// Fallback to the first command line argument.
	return argc ? QFile::decodeName(argv[0]) : QString();
}

} // namespace Platform

namespace {

QRect _monitorRect;
auto _monitorLastGot = 0LL;

} // namespace

QRect psDesktopRect() {
	auto tnow = crl::now();
	if (tnow > _monitorLastGot + 1000LL || tnow < _monitorLastGot) {
		_monitorLastGot = tnow;
		_monitorRect = QApplication::desktop()->availableGeometry(App::wnd());
	}
	return _monitorRect;
}

void psWriteDump() {
}

bool _removeDirectory(const QString &path) { // from http://stackoverflow.com/questions/2256945/removing-a-non-empty-directory-programmatically-in-c-or-c
	QByteArray pathRaw = QFile::encodeName(path);
	DIR *d = opendir(pathRaw.constData());
	if (!d) return false;

	while (struct dirent *p = readdir(d)) {
		/* Skip the names "." and ".." as we don't want to recurse on them. */
		if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;

		QString fname = path + '/' + p->d_name;
		QByteArray fnameRaw = QFile::encodeName(fname);
		struct stat statbuf;
		if (!stat(fnameRaw.constData(), &statbuf)) {
			if (S_ISDIR(statbuf.st_mode)) {
				if (!_removeDirectory(fname)) {
					closedir(d);
					return false;
				}
			} else {
				if (unlink(fnameRaw.constData())) {
					closedir(d);
					return false;
				}
			}
		}
	}
	closedir(d);

	return !rmdir(pathRaw.constData());
}

void psDeleteDir(const QString &dir) {
	_removeDirectory(dir);
}

void psActivateProcess(uint64 pid) {
//	objc_activateProgram();
}

namespace {

QString getHomeDir() {
	auto home = QDir::homePath();

	if (home != QDir::rootPath())
		return home + '/';

	struct passwd *pw = getpwuid(getuid());
	return (pw && pw->pw_dir && strlen(pw->pw_dir)) ? (QFile::decodeName(pw->pw_dir) + '/') : QString();
}

} // namespace

QString psAppDataPath() {
	// We should not use ~/.TelegramDesktop, since it's a fork.

	// auto home = getHomeDir();
	// if (!home.isEmpty()) {
	// 	auto oldPath = home + qsl(".TelegramDesktop/");
	// 	auto oldSettingsBase = oldPath + qsl("tdata/settings");
	// 	if (QFile(oldSettingsBase + '0').exists() || QFile(oldSettingsBase + '1').exists()) {
	// 		return oldPath;
	// 	}
	// }

	return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + '/';
}

void psDoCleanup() {
	try {
		psAutoStart(false, true);
		psSendToMenu(false, true);
	} catch (...) {
	}
}

int psCleanup() {
	psDoCleanup();
	return 0;
}

void psDoFixPrevious() {
}

int psFixPrevious() {
	psDoFixPrevious();
	return 0;
}

namespace Platform {

void start() {
	FallbackFontConfig();
}

void finish() {
}

void RegisterCustomScheme() {
#ifndef TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME
	auto home = getHomeDir();
	if (home.isEmpty() || cAlphaVersion() || cExeName().isEmpty()) return; // don't update desktop file for alpha version
	if (Core::UpdaterDisabled())
		return;

#ifndef TDESKTOP_DISABLE_DESKTOP_FILE_GENERATION
	DEBUG_LOG(("App Info: placing .desktop file"));
	if (QDir(home + qsl(".local/")).exists()) {
		QString apps = home + qsl(".local/share/applications/");
		QString icons = home + qsl(".local/share/icons/");
		if (!QDir(apps).exists()) QDir().mkpath(apps);
		if (!QDir(icons).exists()) QDir().mkpath(icons);

		QString path = cWorkingDir() + qsl("tdata/"), file = path + qsl("kotatogramdesktop.desktop");
		QDir().mkpath(path);
		QFile f(file);
		if (f.open(QIODevice::WriteOnly)) {
			QString icon = icons + qsl("kotatogram.png");
			auto iconExists = QFile(icon).exists();
			if (Local::oldSettingsVersion() < 10021 && iconExists) {
				// Icon was changed.
				if (QFile(icon).remove()) {
					iconExists = false;
				}
			}
			if (!iconExists) {
				if (QFile(qsl(":/gui/art/logo_256.png")).copy(icon)) {
					DEBUG_LOG(("App Info: Icon copied to 'tdata'"));
				}
			}

			QTextStream s(&f);
			s.setCodec("UTF-8");
			s << "[Desktop Entry]\n";
			s << "Version=1.0\n";
			s << "Name=Kotatogram Desktop\n";
			s << "Comment=Experimental Telegram Desktop fork\n";
			s << "TryExec=" << EscapeShell(QFile::encodeName(cExeDir() + cExeName())) << "\n";
			s << "Exec=" << EscapeShell(QFile::encodeName(cExeDir() + cExeName())) << " -- %u\n";
			s << "Icon=kotatogram\n";
			s << "Terminal=false\n";
			s << "StartupWMClass=KotatogramDesktop\n";
			s << "Type=Application\n";
			s << "Categories=Network;InstantMessaging;Qt;\n";
			s << "MimeType=x-scheme-handler/tg;\n";
			s << "Keywords=tg;chat;im;messaging;messenger;sms;tdesktop;\n";
			s << "X-GNOME-UsesNotifications=true\n";
			f.close();

			if (RunShellCommand("desktop-file-install --dir=" + EscapeShell(QFile::encodeName(home + qsl(".local/share/applications"))) + " --delete-original " + EscapeShell(QFile::encodeName(file)))) {
				DEBUG_LOG(("App Info: removing old .desktop file"));
				QFile(qsl("%1.local/share/applications/kotatogram.desktop").arg(home)).remove();

				RunShellCommand("update-desktop-database " + EscapeShell(QFile::encodeName(home + qsl(".local/share/applications"))));
				RunShellCommand("xdg-mime default kotatogramdesktop.desktop x-scheme-handler/tg");
			}
		} else {
			LOG(("App Error: Could not open '%1' for write").arg(file));
		}
	}
#endif // !TDESKTOP_DISABLE_DESKTOP_FILE_GENERATION

	DEBUG_LOG(("App Info: registerting for Gnome"));
	if (RunShellCommand("gconftool-2 -t string -s /desktop/gnome/url-handlers/tg/command " + EscapeShell(EscapeShell(QFile::encodeName(cExeDir() + cExeName())) + " -- %s"))) {
		RunShellCommand("gconftool-2 -t bool -s /desktop/gnome/url-handlers/tg/needs_terminal false");
		RunShellCommand("gconftool-2 -t bool -s /desktop/gnome/url-handlers/tg/enabled true");
	}

	DEBUG_LOG(("App Info: placing .protocol file"));
	QString services;
	if (QDir(home + qsl(".kde4/")).exists()) {
		services = home + qsl(".kde4/share/kde4/services/");
	} else if (QDir(home + qsl(".kde/")).exists()) {
		services = home + qsl(".kde/share/kde4/services/");
	}
	if (!services.isEmpty()) {
		if (!QDir(services).exists()) QDir().mkpath(services);

		QString path = services, file = path + qsl("tg.protocol");
		QFile f(file);
		if (f.open(QIODevice::WriteOnly)) {
			QTextStream s(&f);
			s.setCodec("UTF-8");
			s << "[Protocol]\n";
			s << "exec=" << QFile::decodeName(EscapeShell(QFile::encodeName(cExeDir() + cExeName()))) << " -- %u\n";
			s << "protocol=tg\n";
			s << "input=none\n";
			s << "output=none\n";
			s << "helper=true\n";
			s << "listing=false\n";
			s << "reading=false\n";
			s << "writing=false\n";
			s << "makedir=false\n";
			s << "deleting=false\n";
			f.close();
		} else {
			LOG(("App Error: Could not open '%1' for write").arg(file));
		}
	}
#endif // !TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME
}

PermissionStatus GetPermissionStatus(PermissionType type) {
	return PermissionStatus::Granted;
}

void RequestPermission(PermissionType type, Fn<void(PermissionStatus)> resultCallback) {
	resultCallback(PermissionStatus::Granted);
}

void OpenSystemSettingsForPermission(PermissionType type) {
}

bool OpenSystemSettings(SystemSettingsType type) {
	if (type == SystemSettingsType::Audio) {
		auto options = std::vector<QString>();
		const auto add = [&](const char *option) {
			options.emplace_back(option);
		};
		if (DesktopEnvironment::IsUnity()) {
			add("unity-control-center sound");
		} else if (DesktopEnvironment::IsKDE()) {
			add("kcmshell5 kcm_pulseaudio");
			add("kcmshell4 phonon");
		} else if (DesktopEnvironment::IsGnome()) {
			add("gnome-control-center sound");
		}
		add("pavucontrol");
		add("alsamixergui");
		return ranges::find_if(options, [](const QString &command) {
			return QProcess::startDetached(command);
		}) != end(options);
	}
	return true;
}

namespace ThirdParty {

void start() {
	Libs::start();
	MainWindow::LibsLoaded();
}

void finish() {
}

} // namespace ThirdParty

} // namespace Platform

void psNewVersion() {
	Platform::RegisterCustomScheme();
}

bool psShowOpenWithMenu(int x, int y, const QString &file) {
	return false;
}

void psAutoStart(bool start, bool silent) {
}

void psSendToMenu(bool send, bool silent) {
}

bool linuxMoveFile(const char *from, const char *to) {
	FILE *ffrom = fopen(from, "rb"), *fto = fopen(to, "wb");
	if (!ffrom) {
		if (fto) fclose(fto);
		return false;
	}
	if (!fto) {
		fclose(ffrom);
		return false;
	}
	static const int BufSize = 65536;
	char buf[BufSize];
	while (size_t size = fread(buf, 1, BufSize, ffrom)) {
		fwrite(buf, 1, size, fto);
	}

	struct stat fst; // from http://stackoverflow.com/questions/5486774/keeping-fileowner-and-permissions-after-copying-file-in-c
	//let's say this wont fail since you already worked OK on that fp
	if (fstat(fileno(ffrom), &fst) != 0) {
		fclose(ffrom);
		fclose(fto);
		return false;
	}
	//update to the same uid/gid
	if (fchown(fileno(fto), fst.st_uid, fst.st_gid) != 0) {
		fclose(ffrom);
		fclose(fto);
		return false;
	}
	//update the permissions
	if (fchmod(fileno(fto), fst.st_mode) != 0) {
		fclose(ffrom);
		fclose(fto);
		return false;
	}

	fclose(ffrom);
	fclose(fto);

	if (unlink(from)) {
		return false;
	}

	return true;
}

bool psLaunchMaps(const Data::LocationPoint &point) {
	return false;
}
