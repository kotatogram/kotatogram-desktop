/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/main_window.h"

#include "storage/localstorage.h"
#include "platform/platform_window_title.h"
#include "base/platform/base_platform_info.h"
#include "history/history.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "window/window_lock_widgets.h"
#include "window/window_outdated_bar.h"
#include "window/window_controller.h"
#include "boxes/confirm_box.h"
#include "main/main_account.h" // Account::sessionValue.
#include "core/click_handler_types.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "lang/lang_keys.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "base/crc32hash.h"
#include "base/call_delayed.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "apiwrap.h"
#include "mainwindow.h"
#include "facades.h"
#include "app.h"
#include "styles/style_window.h"
#include "styles/style_layers.h"

#include <QtWidgets/QDesktopWidget>
#include <QtCore/QMimeData>
#include <QtCore/QDir>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtGui/QScreen>
#include <QtGui/QDrag>

namespace Window {
namespace {

constexpr auto kSaveWindowPositionTimeout = crl::time(1000);

} // namespace

QString LogoVariant(int variant) {
	switch (variant) {
		case 1: return QString("_blue");
		case 2: return QString("_green");
		case 3: return QString("_orange");
		case 4: return QString("_red");
		case 5: return QString("_old");
	}

	return QString();
}

QImage LoadLogo(int variant) {
	return QImage(qsl(":/gui/art/logo_256%1.png").arg(LogoVariant(variant)));
}

QImage LoadLogoNoMargin(int variant) {
	return QImage(qsl(":/gui/art/logo_256_no_margin%1.png").arg(LogoVariant(variant)));
}

void ConvertIconToBlack(QImage &image) {
	if (image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	//const auto gray = red * 0.299 + green * 0.587 + blue * 0.114;
	//const auto result = (gray - 100 < 0) ? 0 : (gray - 100) * 255 / 155;
	constexpr auto scale = 255 / 155.;
	constexpr auto red = 0.299;
	constexpr auto green = 0.587;
	constexpr auto blue = 0.114;
	static constexpr auto shift = (1 << 24);
	auto shifter = [](double value) {
		return uint32(value * shift);
	};
	constexpr auto iscale = shifter(scale);
	constexpr auto ired = shifter(red);
	constexpr auto igreen = shifter(green);
	constexpr auto iblue = shifter(blue);
	constexpr auto threshold = 100;
	constexpr auto ithreshold = shifter(threshold);

	const auto width = image.width();
	const auto height = image.height();
	const auto data = reinterpret_cast<uint32*>(image.bits());
	const auto intsPerLine = image.bytesPerLine() / 4;
	const auto intsPerLineAdded = intsPerLine - width;

	auto pixel = data;
	for (auto j = 0; j != height; ++j) {
		for (auto i = 0; i != width; ++i) {
			const auto value = *pixel;
			const auto gray = (((value >> 16) & 0xFF) * ired
				+ ((value >> 8) & 0xFF) * igreen
				+ (value & 0xFF) * iblue) >> 24;
			const auto small = gray - threshold;
			const auto test = ~small;
			const auto result = (test >> 31) * small * iscale;
			const auto component = (result >> 24) & 0xFF;
			*pixel++ = (value & 0xFF000000U)
				| (component << 16)
				| (component << 8)
				| component;
		}
		pixel += intsPerLineAdded;
	}
}

QIcon CreateOfficialIcon(Main::Account *account) {
	const auto customIcon = QImage(cWorkingDir() + "tdata/icon.png");

	auto image = customIcon.isNull()
		? Core::IsAppLaunched()
			? Core::App().logo(cCustomAppIcon())
			: LoadLogo(cCustomAppIcon())
		: customIcon;

	if (account
		&& account->sessionExists()
		&& account->session().supportMode()) {
		ConvertIconToBlack(image);
	}

	return QIcon(App::pixmapFromImageInPlace(std::move(image)));
}

QIcon CreateIcon(Main::Account *account) {
	auto result = CreateOfficialIcon(account);

#ifdef Q_OS_LINUX
	if (
		(!account
			|| !account->sessionExists()
			|| !account->session().supportMode())
		&& cCustomAppIcon() == 0
		&& !QFileInfo::exists(cWorkingDir() + "tdata/icon.png")) {
		return QIcon::fromTheme(Platform::GetIconName(), result);
	}
#endif

	return result;
}

MainWindow::MainWindow(not_null<Controller*> controller)
: _controller(controller)
, _positionUpdatedTimer([=] { savePosition(); })
, _outdated(CreateOutdatedBar(this))
, _body(this)
, _titleText(qsl("Kotatogram")) {
	subscribe(Theme::Background(), [=](
			const Theme::BackgroundUpdate &data) {
		if (data.paletteChanged()) {
			updatePalette();
		}
	});
	subscribe(Global::RefUnreadCounterUpdate(), [=] {
		updateUnreadCounter();
	});
	subscribe(Global::RefWorkMode(), [=](DBIWorkMode mode) {
		workmodeUpdated(mode);
	});

	Core::App().termsLockValue(
	) | rpl::start_with_next([=] {
		checkLockByTerms();
	}, lifetime());

	Ui::Toast::SetDefaultParent(_body.data());

	if (_outdated) {
		_outdated->heightValue(
		) | rpl::filter([=] {
			return window()->windowHandle() != nullptr;
		}) | rpl::start_with_next([=](int height) {
			if (!height) {
				crl::on_main(this, [=] { _outdated.destroy(); });
			}
			updateControlsGeometry();
		}, _outdated->lifetime());
	}

	_isActiveTimer.setCallback([this] { updateIsActive(0); });
}

Main::Account &MainWindow::account() const {
	return _controller->account();
}

Window::SessionController *MainWindow::sessionController() const {
	return _controller->sessionController();
}

void MainWindow::checkLockByTerms() {
	const auto data = Core::App().termsLocked();
	if (!data || !account().sessionExists()) {
		if (_termsBox) {
			_termsBox->closeBox();
		}
		return;
	}
	Ui::hideSettingsAndLayer(anim::type::instant);
	const auto box = Ui::show(Box<TermsBox>(
		*data,
		tr::lng_terms_agree(),
		tr::lng_terms_decline()));

	box->setCloseByEscape(false);
	box->setCloseByOutsideClick(false);

	const auto id = data->id;
	box->agreeClicks(
	) | rpl::start_with_next([=] {
		const auto mention = box ? box->lastClickedMention() : QString();
		if (account().sessionExists()) {
			account().session().api().acceptTerms(id);
			if (!mention.isEmpty()) {
				MentionClickHandler(mention).onClick({});
			}
		}
		Core::App().unlockTerms();
	}, box->lifetime());

	box->cancelClicks(
	) | rpl::start_with_next([=] {
		showTermsDecline();
	}, box->lifetime());

	connect(box, &QObject::destroyed, [=] {
		crl::on_main(this, [=] { checkLockByTerms(); });
	});

	_termsBox = box;
}

void MainWindow::showTermsDecline() {
	const auto box = Ui::show(
		Box<Window::TermsBox>(
			TextWithEntities{ tr::lng_terms_update_sorry(tr::now) },
			tr::lng_terms_decline_and_delete(),
			tr::lng_terms_back(),
			true),
		Ui::LayerOption::KeepOther);

	box->agreeClicks(
	) | rpl::start_with_next([=] {
		if (box) {
			box->closeBox();
		}
		showTermsDelete();
	}, box->lifetime());

	box->cancelClicks(
	) | rpl::start_with_next([=] {
		if (box) {
			box->closeBox();
		}
	}, box->lifetime());
}

void MainWindow::showTermsDelete() {
	const auto box = std::make_shared<QPointer<Ui::BoxContent>>();
	const auto deleteByTerms = [=] {
		if (account().sessionExists()) {
			account().session().termsDeleteNow();
		} else {
			Ui::hideLayer();
		}
	};
	*box = Ui::show(
		Box<ConfirmBox>(
			tr::lng_terms_delete_warning(tr::now),
			tr::lng_terms_delete_now(tr::now),
			st::attentionBoxButton,
			deleteByTerms,
			[=] { if (*box) (*box)->closeBox(); }),
		Ui::LayerOption::KeepOther);
}

bool MainWindow::hideNoQuit() {
	if (App::quitting()) {
		return false;
	}
	if (Global::WorkMode().value() == dbiwmTrayOnly || Global::WorkMode().value() == dbiwmWindowAndTray) {
		if (minimizeToTray()) {
			Ui::showChatsList();
			return true;
		}
	} else if (Platform::IsMac()) {
		closeWithoutDestroy();
		updateIsActive(Global::OfflineBlurTimeout());
		updateGlobalMenu();
		Ui::showChatsList();
		return true;
	}
	return false;
}

void MainWindow::clearWidgets() {
	clearWidgetsHook();
	updateGlobalMenu();
}

void MainWindow::updateIsActive(int timeout) {
	if (timeout > 0) {
		return _isActiveTimer.callOnce(timeout);
	}
	_isActive = computeIsActive();
	updateIsActiveHook();
}

bool MainWindow::computeIsActive() const {
	return isActiveWindow() && isVisible() && !(windowState() & Qt::WindowMinimized);
}

void MainWindow::updateWindowIcon() {
	const auto supportIcon = account().sessionExists()
		&& account().session().supportMode();
	if (supportIcon != _usingSupportIcon || _icon.isNull() || _customIconId != cCustomAppIcon()) {
		_icon = CreateIcon(&account());
		_usingSupportIcon = supportIcon;
		_customIconId = cCustomAppIcon();
	}
	setWindowIcon(_icon);
}

void MainWindow::init() {
	Expects(!windowHandle());

	createWinId();

	initHook();
	updateWindowIcon();

	// Non-queued activeChanged handlers must use QtSignalProducer.
	connect(
		windowHandle(),
		&QWindow::activeChanged,
		this,
		[=] { handleActiveChanged(); },
		Qt::QueuedConnection);
	connect(
		windowHandle(),
		&QWindow::windowStateChanged,
		this,
		[=](Qt::WindowState state) { handleStateChanged(state); });

	updatePalette();

	if (!UseNativeDecorations() && (_title = Platform::CreateTitleWidget(this))) {
		_title->init();
	}

	initSize();
	updateUnreadCounter();
}

void MainWindow::handleStateChanged(Qt::WindowState state) {
	stateChangedHook(state);
	updateIsActive((state == Qt::WindowMinimized) ? Global::OfflineBlurTimeout() : Global::OnlineFocusTimeout());
	Core::App().updateNonIdle();
	if (state == Qt::WindowMinimized && Global::WorkMode().value() == dbiwmTrayOnly) {
		minimizeToTray();
	}
	savePosition(state);
}

void MainWindow::handleActiveChanged() {
	if (isActiveWindow()) {
		Core::App().checkMediaViewActivation();
	}
	base::call_delayed(1, this, [this] {
		updateTrayMenu();
		handleActiveChangedHook();
	});
}

void MainWindow::updatePalette() {
	Ui::ForceFullRepaint(this);

	auto p = palette();
	p.setColor(QPalette::Window, st::windowBg->c);
	setPalette(p);
}

HitTestResult MainWindow::hitTest(const QPoint &p) const {
	auto titleResult = _title ? _title->hitTest(p - _title->geometry().topLeft()) : Window::HitTestResult::None;
	if (titleResult != Window::HitTestResult::None) {
		return titleResult;
	} else if (rect().contains(p)) {
		return Window::HitTestResult::Client;
	}
	return Window::HitTestResult::None;
}

int MainWindow::computeMinWidth() const {
	auto result = st::windowMinWidth;
	if (const auto session = _controller->sessionController()) {
		if (const auto add = session->filtersWidth()) {
			result += add;
		}
	}
	if (_rightColumn) {
		result += _rightColumn->width();
	}
	return result;
}

int MainWindow::computeMinHeight() const {
	const auto title = _title ? _title->height() : 0;
	const auto outdated = [&] {
		if (!_outdated) {
			return 0;
		}
		_outdated->resizeToWidth(st::windowMinWidth);
		return _outdated->height();
	}();
	return title + outdated + st::windowMinHeight;
}

void MainWindow::initSize() {
	setMinimumWidth(computeMinWidth());
	setMinimumHeight(computeMinHeight());

	auto position = cWindowPos();
	DEBUG_LOG(("Window Pos: Initializing first %1, %2, %3, %4 (maximized %5)").arg(position.x).arg(position.y).arg(position.w).arg(position.h).arg(Logs::b(position.maximized)));

	const auto primaryScreen = QGuiApplication::primaryScreen();
	auto geometryScreen = primaryScreen;
	const auto available = primaryScreen
		? primaryScreen->availableGeometry()
		: QRect(0, 0, st::windowDefaultWidth, st::windowDefaultHeight);
	bool maximized = false;
	const auto initialWidth = Main::Settings::ThirdColumnByDefault()
		? st::windowBigDefaultWidth
		: st::windowDefaultWidth;
	const auto initialHeight = Main::Settings::ThirdColumnByDefault()
		? st::windowBigDefaultHeight
		: st::windowDefaultHeight;
	auto geometry = QRect(
		available.x() + std::max(
			(available.width() - initialWidth) / 2,
			0),
		available.y() + std::max(
			(available.height() - initialHeight) / 2,
			0),
		initialWidth,
		initialHeight);
	if (position.w && position.h) {
		for (auto screen : QGuiApplication::screens()) {
			if (position.moncrc == screenNameChecksum(screen->name())) {
				auto screenGeometry = screen->geometry();
				DEBUG_LOG(("Window Pos: Screen found, screen geometry: %1, %2, %3, %4").arg(screenGeometry.x()).arg(screenGeometry.y()).arg(screenGeometry.width()).arg(screenGeometry.height()));

				auto w = screenGeometry.width(), h = screenGeometry.height();
				if (w >= st::windowMinWidth && h >= st::windowMinHeight) {
					if (position.x < 0) position.x = 0;
					if (position.y < 0) position.y = 0;
					if (position.w > w) position.w = w;
					if (position.h > h) position.h = h;
					position.x += screenGeometry.x();
					position.y += screenGeometry.y();
					if (position.x + st::windowMinWidth <= screenGeometry.x() + screenGeometry.width() &&
						position.y + st::windowMinHeight <= screenGeometry.y() + screenGeometry.height()) {
						DEBUG_LOG(("Window Pos: Resulting geometry is %1, %2, %3, %4").arg(position.x).arg(position.y).arg(position.w).arg(position.h));
						geometry = QRect(position.x, position.y, position.w, position.h);
						geometryScreen = screen;
					}
				}
				break;
			}
		}
		maximized = position.maximized;
	}
	DEBUG_LOG(("Window Pos: Setting first %1, %2, %3, %4").arg(geometry.x()).arg(geometry.y()).arg(geometry.width()).arg(geometry.height()));
	setGeometry(geometry);
}

void MainWindow::positionUpdated() {
	_positionUpdatedTimer.callOnce(kSaveWindowPositionTimeout);
}

bool MainWindow::titleVisible() const {
	return _title && !_title->isHidden();
}

void MainWindow::setTitleVisible(bool visible) {
	if (_title && (_title->isHidden() == visible)) {
		_title->setVisible(visible);
		updateControlsGeometry();
	}
	titleVisibilityChangedHook();
}

int32 MainWindow::screenNameChecksum(const QString &name) const {
	const auto bytes = name.toUtf8();
	return base::crc32(bytes.constData(), bytes.size());
}

void MainWindow::setPositionInited() {
	_positionInited = true;
}

void MainWindow::attachToTrayIcon(not_null<QSystemTrayIcon*> icon) {
	const auto workdir = QDir::toNativeSeparators(QDir::cleanPath(cWorkingDir()));
	icon->setToolTip(AppName.utf16()+"\n"+workdir);
	connect(icon, &QSystemTrayIcon::activated, this, [=](
			QSystemTrayIcon::ActivationReason reason) {
		Core::Sandbox::Instance().customEnterFromEventLoop([&] {
			handleTrayIconActication(reason);
		});
	});
	App::wnd()->updateTrayMenu();
}

void MainWindow::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

rpl::producer<> MainWindow::leaveEvents() const {
	return _leaveEvents.events();
}

void MainWindow::leaveEventHook(QEvent *e) {
	_leaveEvents.fire({});
}

void MainWindow::updateControlsGeometry() {
	auto bodyLeft = 0;
	auto bodyTop = 0;
	auto bodyWidth = width();
	if (_title && !_title->isHidden()) {
		_title->setGeometry(0, bodyTop, width(), _title->height());
		bodyTop += _title->height();
	}
	if (_outdated) {
		Ui::SendPendingMoveResizeEvents(_outdated.data());
		_outdated->resizeToWidth(width());
		_outdated->moveToLeft(0, bodyTop);
		bodyTop += _outdated->height();
	}
	if (_rightColumn) {
		bodyWidth -= _rightColumn->width();
		_rightColumn->setGeometry(bodyWidth, bodyTop, width() - bodyWidth, height() - bodyTop);
	}
	_body->setGeometry(bodyLeft, bodyTop, bodyWidth, height() - bodyTop);
}

void MainWindow::updateUnreadCounter() {
	if (!Global::started() || App::quitting()) return;

	const auto counter = account().sessionExists()
		? account().session().data().unreadBadge()
		: 0;
	_titleText = (counter > 0) ? qsl("Kotatogram (%1)").arg(counter) : qsl("Kotatogram");

	unreadCounterChangedHook();
}

void MainWindow::savePosition(Qt::WindowState state) {
	if (state == Qt::WindowActive) state = windowHandle()->windowState();
	if (state == Qt::WindowMinimized || !positionInited()) return;

	auto savedPosition = cWindowPos();
	auto realPosition = savedPosition;

	if (state == Qt::WindowMaximized) {
		realPosition.maximized = 1;
		DEBUG_LOG(("Window Pos: Saving maximized position."));
	} else {
		auto r = geometry();
		realPosition.x = r.x();
		realPosition.y = r.y();
		realPosition.w = r.width() - (_rightColumn ? _rightColumn->width() : 0);
		realPosition.h = r.height();
		realPosition.maximized = 0;
		realPosition.moncrc = 0;

		DEBUG_LOG(("Window Pos: Saving non-maximized position: %1, %2, %3, %4").arg(realPosition.x).arg(realPosition.y).arg(realPosition.w).arg(realPosition.h));

		auto centerX = realPosition.x + realPosition.w / 2;
		auto centerY = realPosition.y + realPosition.h / 2;
		int minDelta = 0;
		QScreen *chosen = nullptr;
		auto screens = QGuiApplication::screens();
		for (auto screen : QGuiApplication::screens()) {
			auto delta = (screen->geometry().center() - QPoint(centerX, centerY)).manhattanLength();
			if (!chosen || delta < minDelta) {
				minDelta = delta;
				chosen = screen;
			}
		}
		if (chosen) {
			auto screenGeometry = chosen->geometry();
			DEBUG_LOG(("Window Pos: Screen found, geometry: %1, %2, %3, %4").arg(screenGeometry.x()).arg(screenGeometry.y()).arg(screenGeometry.width()).arg(screenGeometry.height()));
			realPosition.x -= screenGeometry.x();
			realPosition.y -= screenGeometry.y();
			realPosition.moncrc = screenNameChecksum(chosen->name());
		}
	}
	if (realPosition.w >= st::windowMinWidth && realPosition.h >= st::windowMinHeight) {
		if (realPosition.x != savedPosition.x
			|| realPosition.y != savedPosition.y
			|| realPosition.w != savedPosition.w
			|| realPosition.h != savedPosition.h
			|| realPosition.moncrc != savedPosition.moncrc
			|| realPosition.maximized != savedPosition.maximized) {
			DEBUG_LOG(("Window Pos: Writing: %1, %2, %3, %4 (maximized %5)").arg(realPosition.x).arg(realPosition.y).arg(realPosition.w).arg(realPosition.h).arg(Logs::b(realPosition.maximized)));
			cSetWindowPos(realPosition);
			Local::writeSettings();
		}
	}
}

bool MainWindow::minimizeToTray() {
	if (App::quitting() || !hasTrayIcon()) return false;

	closeWithoutDestroy();
	updateIsActive(Global::OfflineBlurTimeout());
	updateTrayMenu();
	updateGlobalMenu();
	showTrayTooltip();
	return true;
}

void MainWindow::reActivateWindow() {
#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
	const auto reActivate = [=] {
		if (const auto w = App::wnd()) {
			if (auto f = QApplication::focusWidget()) {
				f->clearFocus();
			}
			w->activate();
			if (auto f = QApplication::focusWidget()) {
				f->clearFocus();
			}
			w->setInnerFocus();
		}
	};
	crl::on_main(this, reActivate);
	base::call_delayed(200, this, reActivate);
#endif // Q_OS_LINUX32 || Q_OS_LINUX64
}

void MainWindow::showRightColumn(object_ptr<TWidget> widget) {
	const auto wasWidth = width();
	const auto wasRightWidth = _rightColumn ? _rightColumn->width() : 0;
	_rightColumn = std::move(widget);
	if (_rightColumn) {
		_rightColumn->setParent(this);
		_rightColumn->show();
		_rightColumn->setFocus();
	} else if (App::wnd()) {
		App::wnd()->setInnerFocus();
	}
	const auto nowRightWidth = _rightColumn ? _rightColumn->width() : 0;
	const auto wasMaximized = isMaximized();
	const auto wasMinimumWidth = minimumWidth();
	const auto nowMinimumWidth = computeMinWidth();
	const auto firstResize = (nowMinimumWidth < wasMinimumWidth);
	if (firstResize) {
		setMinimumWidth(nowMinimumWidth);
	}
	if (!isMaximized()) {
		tryToExtendWidthBy(wasWidth + nowRightWidth - wasRightWidth - width());
	} else {
		updateControlsGeometry();
	}
	if (!firstResize) {
		setMinimumWidth(nowMinimumWidth);
	}
}

int MainWindow::maximalExtendBy() const {
	auto desktop = QDesktopWidget().availableGeometry(this);
	return std::max(desktop.width() - geometry().width(), 0);
}

bool MainWindow::canExtendNoMove(int extendBy) const {
	auto desktop = QDesktopWidget().availableGeometry(this);
	auto inner = geometry();
	auto innerRight = (inner.x() + inner.width() + extendBy);
	auto desktopRight = (desktop.x() + desktop.width());
	return innerRight <= desktopRight;
}

int MainWindow::tryToExtendWidthBy(int addToWidth) {
	auto desktop = QDesktopWidget().availableGeometry(this);
	auto inner = geometry();
	accumulate_min(
		addToWidth,
		std::max(desktop.width() - inner.width(), 0));
	auto newWidth = inner.width() + addToWidth;
	auto newLeft = std::min(
		inner.x(),
		desktop.x() + desktop.width() - newWidth);
	if (inner.x() != newLeft || inner.width() != newWidth) {
		setGeometry(newLeft, inner.y(), newWidth, inner.height());
	} else {
		updateControlsGeometry();
	}
	return addToWidth;
}

void MainWindow::launchDrag(std::unique_ptr<QMimeData> data) {
	auto weak = QPointer<MainWindow>(this);
	auto drag = std::make_unique<QDrag>(App::wnd());
	drag->setMimeData(data.release());
	drag->exec(Qt::CopyAction);

	// We don't receive mouseReleaseEvent when drag is finished.
	ClickHandler::unpressed();
	if (weak) {
		weak->dragFinished().notify();
	}
}

MainWindow::~MainWindow() = default;

} // namespace Window
