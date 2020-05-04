/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/main_window_linux.h"

#include "styles/style_window.h"
#include "platform/linux/linux_libs.h"
#include "platform/linux/specific_linux.h"
#include "platform/linux/linux_desktop_environment.h"
#include "platform/platform_notifications_manager.h"
#include "history/history.h"
#include "history/history_widget.h"
#include "history/history_inner_widget.h"
#include "main/main_account.h" // Account::sessionChanges.
#include "mainwindow.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/about_box.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "window/window_session_controller.h"
#include "ui/widgets/input_fields.h"
#include "facades.h"
#include "app.h"

#include <QtCore/QSize>
#include <QtGui/QWindow>

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusConnectionInterface>
#include <QtDBus/QDBusServiceWatcher>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusError>
#include <QtDBus/QDBusMetaType>
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

namespace Platform {
namespace {

constexpr auto kDisableTrayCounter = "TDESKTOP_DISABLE_TRAY_COUNTER"_cs;
constexpr auto kForcePanelIcon = "TDESKTOP_FORCE_PANEL_ICON"_cs;
constexpr auto kUseTelegramPanelIcon = "KTGDESKTOP_USE_TELEGRAM_PANEL_ICON"_cs;
constexpr auto kPanelTrayIconName = "kotatogram-panel"_cs;
constexpr auto kMutePanelTrayIconName = "kotatogram-mute-panel"_cs;
constexpr auto kAttentionPanelTrayIconName = "kotatogram-attention-panel"_cs;
constexpr auto kTelegramPanelTrayIconName = "telegram-panel"_cs;
constexpr auto kTelegramMutePanelTrayIconName = "telegram-mute-panel"_cs;
constexpr auto kTelegramAttentionPanelTrayIconName = "telegram-attention-panel"_cs;
constexpr auto kSNIWatcherService = "org.kde.StatusNotifierWatcher"_cs;
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties"_cs;
constexpr auto kTrayIconFilename = "ktgdesktop-trayicon-XXXXXX.png"_cs;

constexpr auto kAppMenuService = "com.canonical.AppMenu.Registrar"_cs;
constexpr auto kAppMenuObjectPath = "/com/canonical/AppMenu/Registrar"_cs;
constexpr auto kAppMenuInterface = kAppMenuService;

bool TrayIconMuted = true;
int32 TrayIconCount = 0;
base::flat_map<int, QImage> TrayIconImageBack;
QIcon TrayIcon;
QString TrayIconThemeName, TrayIconName;

bool SNIAvailable = false;

QString GetPanelIconName(int counter, bool muted) {
	const auto useTelegramIcon = qEnvironmentVariableIsSet(
		kUseTelegramPanelIcon.utf8());

	const auto iconName = useTelegramIcon
		? kTelegramPanelTrayIconName.utf16()
		: kPanelTrayIconName.utf16();

	const auto muteIconName = useTelegramIcon
		? kTelegramMutePanelTrayIconName.utf16()
		: kMutePanelTrayIconName.utf16();

	const auto attentionIconName = useTelegramIcon
		? kTelegramAttentionPanelTrayIconName.utf16()
		: kAttentionPanelTrayIconName.utf16();

	return (counter > 0)
		? (muted
			? muteIconName
			: attentionIconName)
		: iconName;
}

QString GetTrayIconName(int counter, bool muted) {
	const auto iconName = GetIconName();
	const auto panelIconName = GetPanelIconName(counter, muted);

	if (QIcon::hasThemeIcon(panelIconName)
		|| qEnvironmentVariableIsSet(kForcePanelIcon.utf8())) {
		return panelIconName;
	} else if (QIcon::hasThemeIcon(iconName)) {
		return iconName;
	}

	return QString();
}

int GetCounterSlice(int counter) {
	return (counter >= 1000)
		? (1000 + (counter % 100))
		: counter;
}

bool IsIconRegenerationNeeded(
		int counter,
		bool muted,
		const QString &iconThemeName = QIcon::themeName()) {
	const auto iconName = GetTrayIconName(counter, muted);
	const auto counterSlice = GetCounterSlice(counter);

	return TrayIcon.isNull()
		|| iconThemeName != TrayIconThemeName
		|| iconName != TrayIconName
		|| muted != TrayIconMuted
		|| counterSlice != TrayIconCount;
}

void UpdateIconRegenerationNeeded(
		const QIcon &icon,
		int counter,
		bool muted,
		const QString &iconThemeName) {
	const auto iconName = GetTrayIconName(counter, muted);
	const auto counterSlice = GetCounterSlice(counter);

	TrayIcon = icon;
	TrayIconMuted = muted;
	TrayIconCount = counterSlice;
	TrayIconThemeName = iconThemeName;
	TrayIconName = iconName;
}

QIcon TrayIconGen(int counter, bool muted) {
	const auto iconThemeName = QIcon::themeName();

	if (!IsIconRegenerationNeeded(counter, muted, iconThemeName)) {
		return TrayIcon;
	}

	const auto iconName = GetTrayIconName(counter, muted);

	if (qEnvironmentVariableIsSet(kDisableTrayCounter.utf8())
		&& !iconName.isEmpty()) {
		const auto result = QIcon::fromTheme(iconName);
		UpdateIconRegenerationNeeded(result, counter, muted, iconThemeName);
		return result;
	}

	QIcon result;
	QIcon systemIcon;

	const auto iconSizes = {
		16,
		22,
		24,
		32,
		48,
		64
	};

	for (const auto iconSize : iconSizes) {
		auto &currentImageBack = TrayIconImageBack[iconSize];
		const auto desiredSize = QSize(iconSize, iconSize);

		if (currentImageBack.isNull()
			|| iconThemeName != TrayIconThemeName
			|| iconName != TrayIconName) {
			if (QFileInfo::exists(cWorkingDir() + "tdata/icon.png")) {
				currentImageBack = QImage(cWorkingDir() + "tdata/icon.png");
			} else if (cCustomAppIcon() != 0) {
				currentImageBack = Core::App().logo(cCustomAppIcon());
			} else if (!iconName.isEmpty()) {
				if (systemIcon.isNull()) {
					systemIcon = QIcon::fromTheme(iconName);
				}

				if (systemIcon.actualSize(desiredSize) == desiredSize) {
					currentImageBack = systemIcon
						.pixmap(desiredSize)
						.toImage();
				} else {
					const auto availableSizes = systemIcon.availableSizes();

					const auto biggestSize = ranges::max_element(
						availableSizes,
						std::less<>(),
						&QSize::width);

					currentImageBack = systemIcon
						.pixmap(*biggestSize)
						.toImage();
				}
			} else {
				currentImageBack = Core::App().logo();
			}

			if (currentImageBack.size() != desiredSize) {
				currentImageBack = currentImageBack.scaled(
					desiredSize,
					Qt::IgnoreAspectRatio,
					Qt::SmoothTransformation);
			}
		}

		auto iconImage = currentImageBack;

		if (!qEnvironmentVariableIsSet(kDisableTrayCounter.utf8())
			&& counter > 0) {
			QPainter p(&iconImage);
			int32 layerSize = -16;

			if (iconSize >= 48) {
				layerSize = -32;
			} else if (iconSize >= 36) {
				layerSize = -24;
			} else if (iconSize >= 32) {
				layerSize = -20;
			}

			auto &bg = muted
				? st::trayCounterBgMute
				: st::trayCounterBg;

			auto &fg = st::trayCounterFg;

			auto layer = App::wnd()->iconWithCounter(
				layerSize,
				counter,
				bg,
				fg,
				false);

			p.drawImage(
				iconImage.width() - layer.width() - 1,
				iconImage.height() - layer.height() - 1,
				layer);
		}

		result.addPixmap(App::pixmapFromImageInPlace(
			std::move(iconImage)));
	}

	UpdateIconRegenerationNeeded(result, counter, muted, iconThemeName);

	return result;
}

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
bool IsIndicatorApplication() {
	// Hack for indicator-application, which doesn't handle icons sent across D-Bus:
	// save the icon to a temp file and set the icon name to that filename.
	static const auto IndicatorApplication = [] {
		const auto interface = QDBusConnection::sessionBus().interface();

		if (!interface) {
			return false;
		}

		const auto ubuntuIndicator = interface->isServiceRegistered(
			qsl("com.canonical.indicator.application"));

		const auto ayatanaIndicator = interface->isServiceRegistered(
			qsl("org.ayatana.indicator.application"));

		return ubuntuIndicator || ayatanaIndicator;
	}();

	return IndicatorApplication;
}

std::unique_ptr<QTemporaryFile> TrayIconFile(
		const QIcon &icon,
		int size,
		QObject *parent) {
	static const auto templateName = AppRuntimeDirectory()
		+ kTrayIconFilename.utf16();

	const auto desiredSize = QSize(size, size);

	auto ret = std::make_unique<QTemporaryFile>(
		templateName,
		parent);

	ret->open();

	if (icon.actualSize(desiredSize) == desiredSize) {
		icon.pixmap(desiredSize).save(ret.get());
	} else {
		const auto availableSizes = icon.availableSizes();

		const auto biggestSize = ranges::max_element(
			availableSizes,
			std::less<>(),
			&QSize::width);

		icon
			.pixmap(*biggestSize)
			.scaled(
				desiredSize,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation)
			.save(ret.get());
	}

	ret->close();

	return ret;
}

bool UseUnityCounter() {
	static const auto UnityCounter = QDBusInterface(
		"com.canonical.Unity",
		"/").isValid();

	return UnityCounter;
}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

bool IsSNIAvailable() {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	auto message = QDBusMessage::createMethodCall(
		kSNIWatcherService.utf16(),
		qsl("/StatusNotifierWatcher"),
		kPropertiesInterface.utf16(),
		qsl("Get"));

	message.setArguments({
		kSNIWatcherService.utf16(),
		qsl("IsStatusNotifierHostRegistered")
	});

	const QDBusReply<QVariant> reply = QDBusConnection::sessionBus().call(
		message);

	if (reply.isValid()) {
		return reply.value().toBool();
	} else if (reply.error().type() != QDBusError::ServiceUnknown) {
		LOG(("SNI Error: %1").arg(reply.error().message()));
	}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

	return false;
}

quint32 djbStringHash(QString string) {
	quint32 hash = 5381;
	QByteArray chars = string.toLatin1();
	for(int i = 0; i < chars.length(); i++){
		hash = (hash << 5) + hash + chars[i];
	}
	return hash;
}

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
bool AppMenuSupported() {
	static const auto Available = []() -> bool {
		const auto interface = QDBusConnection::sessionBus().interface();

		if (!interface) {
			return false;
		}

		return interface->isServiceRegistered(kAppMenuService.utf16());
	}();

	return Available;
}

void RegisterAppMenu(uint winId, const QDBusObjectPath &menuPath) {
	auto message = QDBusMessage::createMethodCall(
		kAppMenuService.utf16(),
		kAppMenuObjectPath.utf16(),
		kAppMenuInterface.utf16(),
		qsl("RegisterWindow"));

	message.setArguments({
		winId,
		QVariant::fromValue(menuPath)
	});

	QDBusConnection::sessionBus().send(message);
}

void UnregisterAppMenu(uint winId) {
	auto message = QDBusMessage::createMethodCall(
		kAppMenuService.utf16(),
		kAppMenuObjectPath.utf16(),
		kAppMenuInterface.utf16(),
		qsl("UnregisterWindow"));

	message.setArguments({
		winId
	});

	QDBusConnection::sessionBus().send(message);
}

void SendKeySequence(
	Qt::Key key,
	Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
	const auto focused = QApplication::focusWidget();
	if (qobject_cast<QLineEdit*>(focused)
		|| qobject_cast<QTextEdit*>(focused)
		|| qobject_cast<HistoryInner*>(focused)) {
		QApplication::postEvent(
			focused,
			new QKeyEvent(QEvent::KeyPress, key, modifiers));

		QApplication::postEvent(
			focused,
			new QKeyEvent(QEvent::KeyRelease, key, modifiers));
	}
}

void ForceDisabled(QAction *action, bool disabled) {
	if (action->isEnabled()) {
		if (disabled) action->setDisabled(true);
	} else if (!disabled) {
		action->setDisabled(false);
	}
}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

} // namespace

MainWindow::MainWindow(not_null<Window::Controller*> controller)
: Window::MainWindow(controller) {
}

void MainWindow::initHook() {
	SNIAvailable = IsSNIAvailable();

	const auto trayAvailable = SNIAvailable
		|| QSystemTrayIcon::isSystemTrayAvailable();

	LOG(("System tray available: %1").arg(Logs::b(trayAvailable)));
	cSetSupportTray(trayAvailable);

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	auto sniWatcher = new QDBusServiceWatcher(
		kSNIWatcherService.utf16(),
		QDBusConnection::sessionBus(),
		QDBusServiceWatcher::WatchForOwnerChange,
		this);

	connect(
		sniWatcher,
		&QDBusServiceWatcher::serviceOwnerChanged,
		this,
		&MainWindow::onSNIOwnerChanged);

	connect(
		windowHandle(),
		&QWindow::visibleChanged,
		this,
		&MainWindow::onVisibleChanged);

	if (AppMenuSupported()) {
		LOG(("Using D-Bus global menu."));
	} else {
		LOG(("Not using D-Bus global menu."));
	}

	if (UseUnityCounter()) {
		LOG(("Using Unity launcher counter."));
	} else {
		LOG(("Not using Unity launcher counter."));
	}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		updateWaylandDecorationColors();
	}, lifetime());
}

bool MainWindow::hasTrayIcon() const {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	return trayIcon || (SNIAvailable && _sniTrayIcon);
#else
	return trayIcon;
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION
}

void MainWindow::psShowTrayMenu() {
	_trayIconMenuXEmbed->popup(QCursor::pos());
}

void MainWindow::psTrayMenuUpdated() {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	if (_sniTrayIcon && trayIconMenu) {
		_sniTrayIcon->setContextMenu(trayIconMenu);
	}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION
}

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
void MainWindow::setSNITrayIcon(int counter, bool muted) {
	const auto iconName = GetTrayIconName(counter, muted);

	if (qEnvironmentVariableIsSet(kDisableTrayCounter.utf8())
		&& !iconName.isEmpty()
		&& (!InSnap()
			|| qEnvironmentVariableIsSet(kForcePanelIcon.utf8()))) {
		if (_sniTrayIcon->iconName() == iconName) {
			return;
		}

		_sniTrayIcon->setIconByName(iconName);
		_sniTrayIcon->setToolTipIconByName(iconName);
	} else if (IsIndicatorApplication()) {
		if (!IsIconRegenerationNeeded(counter, muted)
			&& !_sniTrayIcon->iconName().isEmpty()) {
			return;
		}

		const auto icon = TrayIconGen(counter, muted);
		_trayIconFile = TrayIconFile(icon, 22, this);

		if (_trayIconFile) {
			// indicator-application doesn't support tooltips
			_sniTrayIcon->setIconByName(_trayIconFile->fileName());
		}
	} else {
		if (!IsIconRegenerationNeeded(counter, muted)
			&& !_sniTrayIcon->iconPixmap().isEmpty()) {
			return;
		}

		const auto icon = TrayIconGen(counter, muted);
		_sniTrayIcon->setIconByPixmap(icon);
		_sniTrayIcon->setToolTipIconByPixmap(icon);
	}
}

void MainWindow::attachToSNITrayIcon() {
	_sniTrayIcon->setToolTipTitle(AppName.utf16());
	connect(_sniTrayIcon,
		&StatusNotifierItem::activateRequested,
		this,
		[=](const QPoint &) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				handleTrayIconActication(QSystemTrayIcon::Trigger);
			});
	});
	connect(_sniTrayIcon,
		&StatusNotifierItem::secondaryActivateRequested,
		this,
		[=](const QPoint &) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				handleTrayIconActication(QSystemTrayIcon::MiddleClick);
			});
	});
	updateTrayMenu();
}

void MainWindow::onSNIOwnerChanged(
		const QString &service,
		const QString &oldOwner,
		const QString &newOwner) {
	if (Global::WorkMode().value() == dbiwmWindowOnly) {
		return;
	}

	if (oldOwner.isEmpty() && !newOwner.isEmpty()) {
		LOG(("Switching to SNI tray icon..."));
	} else if (!oldOwner.isEmpty() && newOwner.isEmpty()) {
		LOG(("Switching to Qt tray icon..."));
	} else {
		return;
	}

	if (trayIcon) {
		trayIcon->setContextMenu(0);
		trayIcon->deleteLater();
	}
	trayIcon = nullptr;

	SNIAvailable = IsSNIAvailable();

	const auto trayAvailable = SNIAvailable
		|| QSystemTrayIcon::isSystemTrayAvailable();

	cSetSupportTray(trayAvailable);

	if (cSupportTray()) {
		psSetupTrayIcon();
	} else {
		LOG(("System tray is not available."));
	}
}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

void MainWindow::psSetupTrayIcon() {
	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();

	if (SNIAvailable) {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
		LOG(("Using SNI tray icon."));
		if (!_sniTrayIcon) {
			_sniTrayIcon = new StatusNotifierItem(
				QCoreApplication::applicationName(),
				this);

			_sniTrayIcon->setTitle(AppName.utf16());
			setSNITrayIcon(counter, muted);

			attachToSNITrayIcon();
		}
		updateIconCounters();
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION
	} else {
		LOG(("Using Qt tray icon."));
		if (!trayIcon) {
			trayIcon = new QSystemTrayIcon(this);
			trayIcon->setIcon(TrayIconGen(counter, muted));

			attachToTrayIcon(trayIcon);
		}
		updateIconCounters();

		trayIcon->show();
	}
}

void MainWindow::workmodeUpdated(DBIWorkMode mode) {
	if (!cSupportTray()) return;

	if (mode == dbiwmWindowOnly) {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
		if (_sniTrayIcon) {
			_sniTrayIcon->setContextMenu(0);
			_sniTrayIcon->deleteLater();
		}
		_sniTrayIcon = nullptr;
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

		if (trayIcon) {
			trayIcon->setContextMenu(0);
			trayIcon->deleteLater();
		}
		trayIcon = nullptr;
	} else {
		psSetupTrayIcon();
	}
}

void MainWindow::unreadCounterChangedHook() {
	setWindowTitle(titleText());
	updateIconCounters();
}

void MainWindow::updateIconCounters() {
	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();

	updateWindowIcon();

#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	if (UseUnityCounter()) {
		const auto launcherUrl = "application://" + GetLauncherFilename();
		QVariantMap dbusUnityProperties;
		if (counter > 0) {
			// Gnome requires that count is a 64bit integer
			dbusUnityProperties.insert(
				"count",
				(qint64) ((counter > 9999)
					? 9999
					: counter));
			dbusUnityProperties.insert("count-visible", true);
		} else {
			dbusUnityProperties.insert("count-visible", false);
		}
		QDBusMessage signal = QDBusMessage::createSignal(
			"/com/canonical/unity/launcherentry/"
				+ QString::number(djbStringHash(launcherUrl)),
			"com.canonical.Unity.LauncherEntry",
			"Update");
		signal << launcherUrl;
		signal << dbusUnityProperties;
		QDBusConnection::sessionBus().send(signal);
	}

	if (_sniTrayIcon) {
		setSNITrayIcon(counter, muted);
	}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

	if (trayIcon && IsIconRegenerationNeeded(counter, muted)) {
		trayIcon->setIcon(TrayIconGen(counter, muted));
	}
}

void MainWindow::updateWaylandDecorationColors() {
	windowHandle()->setProperty("__material_decoration_backgroundColor", st::titleBgActive->c);
	windowHandle()->setProperty("__material_decoration_foregroundColor", st::titleFgActive->c);
	windowHandle()->setProperty("__material_decoration_backgroundInactiveColor", st::titleBg->c);
	windowHandle()->setProperty("__material_decoration_foregroundInactiveColor", st::titleFg->c);
}

void MainWindow::LibsLoaded() {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	qDBusRegisterMetaType<ToolTip>();
	qDBusRegisterMetaType<IconPixmap>();
	qDBusRegisterMetaType<IconPixmapList>();
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION
}

void MainWindow::initTrayMenuHook() {
	_trayIconMenuXEmbed = new Ui::PopupMenu(nullptr, trayIconMenu);
	_trayIconMenuXEmbed->deleteOnHide(false);
}

#ifdef TDESKTOP_DISABLE_DBUS_INTEGRATION

void MainWindow::createGlobalMenu() {
}

void MainWindow::updateGlobalMenuHook() {
}

#else // TDESKTOP_DISABLE_DBUS_INTEGRATION

void MainWindow::createGlobalMenu() {
	if (!AppMenuSupported()) return;

	psMainMenu = new QMenu(this);

	auto file = psMainMenu->addMenu(tr::lng_mac_menu_file(tr::now));

	psLogout = file->addAction(
		tr::lng_mac_menu_logout(tr::now),
		App::wnd(),
		SLOT(onLogout()));

	auto quit = file->addAction(
		tr::lng_mac_menu_quit_telegram(tr::now, lt_telegram, qsl("Kotatogram")),
		App::wnd(),
		SLOT(quitFromTray()),
		QKeySequence::Quit);

	quit->setMenuRole(QAction::QuitRole);

	auto edit = psMainMenu->addMenu(tr::lng_mac_menu_edit(tr::now));

	psUndo = edit->addAction(
		tr::lng_linux_menu_undo(tr::now),
		this,
		SLOT(psLinuxUndo()),
		QKeySequence::Undo);

	psRedo = edit->addAction(
		tr::lng_linux_menu_redo(tr::now),
		this,
		SLOT(psLinuxRedo()),
		QKeySequence::Redo);

	edit->addSeparator();

	psCut = edit->addAction(
		tr::lng_mac_menu_cut(tr::now),
		this,
		SLOT(psLinuxCut()),
		QKeySequence::Cut);
	psCopy = edit->addAction(
		tr::lng_mac_menu_copy(tr::now),
		this,
		SLOT(psLinuxCopy()),
		QKeySequence::Copy);

	psPaste = edit->addAction(
		tr::lng_mac_menu_paste(tr::now),
		this,
		SLOT(psLinuxPaste()),
		QKeySequence::Paste);

	psDelete = edit->addAction(
		tr::lng_mac_menu_delete(tr::now),
		this,
		SLOT(psLinuxDelete()),
		QKeySequence(Qt::ControlModifier | Qt::Key_Backspace));

	edit->addSeparator();

	psBold = edit->addAction(
		tr::lng_menu_formatting_bold(tr::now),
		this,
		SLOT(psLinuxBold()),
		QKeySequence::Bold);

	psItalic = edit->addAction(
		tr::lng_menu_formatting_italic(tr::now),
		this,
		SLOT(psLinuxItalic()),
		QKeySequence::Italic);

	psUnderline = edit->addAction(
		tr::lng_menu_formatting_underline(tr::now),
		this,
		SLOT(psLinuxUnderline()),
		QKeySequence::Underline);

	psStrikeOut = edit->addAction(
		tr::lng_menu_formatting_strike_out(tr::now),
		this,
		SLOT(psLinuxStrikeOut()),
		Ui::kStrikeOutSequence);

	psMonospace = edit->addAction(
		tr::lng_menu_formatting_monospace(tr::now),
		this,
		SLOT(psLinuxMonospace()),
		Ui::kMonospaceSequence);

	psClearFormat = edit->addAction(
		tr::lng_menu_formatting_clear(tr::now),
		this,
		SLOT(psLinuxClearFormat()),
		Ui::kClearFormatSequence);

	edit->addSeparator();

	psSelectAll = edit->addAction(
		tr::lng_mac_menu_select_all(tr::now),
		this, SLOT(psLinuxSelectAll()),
		QKeySequence::SelectAll);

	edit->addSeparator();

	auto prefs = edit->addAction(
		tr::lng_mac_menu_preferences(tr::now),
		App::wnd(),
		SLOT(showSettings()),
		QKeySequence(Qt::ControlModifier | Qt::Key_Comma));

	prefs->setMenuRole(QAction::PreferencesRole);

	auto tools = psMainMenu->addMenu(tr::lng_linux_menu_tools(tr::now));

	psContacts = tools->addAction(
		tr::lng_mac_menu_contacts(tr::now),
		crl::guard(this, [=] {
			if (isHidden()) {
				App::wnd()->showFromTray();
			}

			if (!account().sessionExists()) {
				return;
			}

			Ui::show(
				Box<PeerListBox>(std::make_unique<ContactsBoxController>(
					sessionController()),
				[](not_null<PeerListBox*> box) {
					box->addButton(tr::lng_close(), [box] {
						box->closeBox();
					});

					box->addLeftButton(tr::lng_profile_add_contact(), [] {
						App::wnd()->onShowAddContact();
					});
				}));
		}));

	psAddContact = tools->addAction(
		tr::lng_mac_menu_add_contact(tr::now),
		App::wnd(),
		SLOT(onShowAddContact()));

	tools->addSeparator();

	psNewGroup = tools->addAction(
		tr::lng_mac_menu_new_group(tr::now),
		App::wnd(),
		SLOT(onShowNewGroup()));

	psNewChannel = tools->addAction(
		tr::lng_mac_menu_new_channel(tr::now),
		App::wnd(),
		SLOT(onShowNewChannel()));

	auto help = psMainMenu->addMenu(tr::lng_linux_menu_help(tr::now));

	auto about = help->addAction(
		tr::lng_mac_menu_about_telegram(
			tr::now,
			lt_telegram,
			qsl("Kotatogram")),
		[] {
			if (App::wnd() && App::wnd()->isHidden()) {
				App::wnd()->showFromTray();
			}

			Ui::show(Box<AboutBox>());
		});

	about->setMenuRole(QAction::AboutQtRole);

	_mainMenuPath.setPath(qsl("/MenuBar"));

	_mainMenuExporter = new DBusMenuExporter(
		_mainMenuPath.path(),
		psMainMenu);

	RegisterAppMenu(winId(), _mainMenuPath);

	updateGlobalMenu();
}

void MainWindow::psLinuxUndo() {
	SendKeySequence(Qt::Key_Z, Qt::ControlModifier);
}

void MainWindow::psLinuxRedo() {
	SendKeySequence(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
}

void MainWindow::psLinuxCut() {
	SendKeySequence(Qt::Key_X, Qt::ControlModifier);
}

void MainWindow::psLinuxCopy() {
	SendKeySequence(Qt::Key_C, Qt::ControlModifier);
}

void MainWindow::psLinuxPaste() {
	SendKeySequence(Qt::Key_V, Qt::ControlModifier);
}

void MainWindow::psLinuxDelete() {
	SendKeySequence(Qt::Key_Delete);
}

void MainWindow::psLinuxSelectAll() {
	SendKeySequence(Qt::Key_A, Qt::ControlModifier);
}

void MainWindow::psLinuxBold() {
	SendKeySequence(Qt::Key_B, Qt::ControlModifier);
}

void MainWindow::psLinuxItalic() {
	SendKeySequence(Qt::Key_I, Qt::ControlModifier);
}

void MainWindow::psLinuxUnderline() {
	SendKeySequence(Qt::Key_U, Qt::ControlModifier);
}

void MainWindow::psLinuxStrikeOut() {
	SendKeySequence(Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier);
}

void MainWindow::psLinuxMonospace() {
	SendKeySequence(Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);
}

void MainWindow::psLinuxClearFormat() {
	SendKeySequence(Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
}

void MainWindow::updateGlobalMenuHook() {
	if (!AppMenuSupported() || !App::wnd() || !positionInited()) return;

	const auto focused = QApplication::focusWidget();
	auto canUndo = false;
	auto canRedo = false;
	auto canCut = false;
	auto canCopy = false;
	auto canPaste = false;
	auto canDelete = false;
	auto canSelectAll = false;
	const auto clipboardHasText = QGuiApplication::clipboard()
		->ownsClipboard();
	auto markdownEnabled = false;
	if (const auto edit = qobject_cast<QLineEdit*>(focused)) {
		canCut = canCopy = canDelete = edit->hasSelectedText();
		canSelectAll = !edit->text().isEmpty();
		canUndo = edit->isUndoAvailable();
		canRedo = edit->isRedoAvailable();
		canPaste = clipboardHasText;
	} else if (const auto edit = qobject_cast<QTextEdit*>(focused)) {
		canCut = canCopy = canDelete = edit->textCursor().hasSelection();
		canSelectAll = !edit->document()->isEmpty();
		canUndo = edit->document()->isUndoAvailable();
		canRedo = edit->document()->isRedoAvailable();
		canPaste = clipboardHasText;
		if (canCopy) {
			if (const auto inputField = qobject_cast<Ui::InputField*>(
				focused->parentWidget())) {
				markdownEnabled = inputField->isMarkdownEnabled();
			}
		}
	} else if (const auto list = qobject_cast<HistoryInner*>(focused)) {
		canCopy = list->canCopySelected();
		canDelete = list->canDeleteSelected();
	}
	App::wnd()->updateIsActive(0);
	const auto logged = account().sessionExists();
	const auto locked = Core::App().locked();
	const auto inactive = !logged || locked;
	const auto support = logged && account().session().supportMode();
	ForceDisabled(psLogout, !logged && !locked);
	ForceDisabled(psUndo, !canUndo);
	ForceDisabled(psRedo, !canRedo);
	ForceDisabled(psCut, !canCut);
	ForceDisabled(psCopy, !canCopy);
	ForceDisabled(psPaste, !canPaste);
	ForceDisabled(psDelete, !canDelete);
	ForceDisabled(psSelectAll, !canSelectAll);
	ForceDisabled(psContacts, inactive || support);
	ForceDisabled(psAddContact, inactive);
	ForceDisabled(psNewGroup, inactive || support);
	ForceDisabled(psNewChannel, inactive || support);

	ForceDisabled(psBold, !markdownEnabled);
	ForceDisabled(psItalic, !markdownEnabled);
	ForceDisabled(psUnderline, !markdownEnabled);
	ForceDisabled(psStrikeOut, !markdownEnabled);
	ForceDisabled(psMonospace, !markdownEnabled);
	ForceDisabled(psClearFormat, !markdownEnabled);
}

void MainWindow::onVisibleChanged(bool visible) {
	if (AppMenuSupported() && !_mainMenuPath.path().isEmpty()) {
		if (visible) {
			RegisterAppMenu(winId(), _mainMenuPath);
		} else {
			UnregisterAppMenu(winId());
		}
	}
}

#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

MainWindow::~MainWindow() {
#ifndef TDESKTOP_DISABLE_DBUS_INTEGRATION
	delete _sniTrayIcon;

	if (AppMenuSupported()) {
		UnregisterAppMenu(winId());
		delete _mainMenuExporter;
		delete psMainMenu;
	}
#endif // !TDESKTOP_DISABLE_DBUS_INTEGRATION

	delete _trayIconMenuXEmbed;
}

} // namespace Platform
