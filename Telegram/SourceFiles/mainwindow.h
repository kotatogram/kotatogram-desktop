/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_main_window.h"
#include "base/unique_qptr.h"
#include "ui/layers/layer_widget.h"
#include "ui/effects/animation_value.h"

class MainWidget;

namespace Intro {
class Widget;
enum class EnterPoint : uchar;
} // namespace Intro

namespace Media {
class SystemMediaControlsManager;
} // namespace Media

namespace Window {
class MediaPreviewWidget;
class SectionMemento;
struct SectionShow;
class PasscodeLockWidget;
namespace Theme {
struct BackgroundUpdate;
class WarningWidget;
} // namespace Theme
} // namespace Window

namespace Ui {
class LinkButton;
class BoxContent;
class LayerStackWidget;
} // namespace Ui

class MediaPreviewWidget;

class MainWindow : public Platform::MainWindow {
public:
	explicit MainWindow(not_null<Window::Controller*> controller);
	~MainWindow();

	void finishFirstShow();

	void preventOrInvoke(Fn<void()> callback);

	void setupPasscodeLock();
	void clearPasscodeLock();
	void setupIntro(Intro::EnterPoint point);
	void setupMain();

	void showSettings();

	void setInnerFocus() override;

	MainWidget *sessionContent() const;

	[[nodiscard]] bool doWeMarkAsRead();


	bool takeThirdSectionFromLayer();

	void checkHistoryActivation();

	void sendPaths();

	bool contentOverlapped(const QRect &globalRect);
	bool contentOverlapped(QWidget *w, QPaintEvent *e) {
		return contentOverlapped(QRect(w->mapToGlobal(e->rect().topLeft()), e->rect().size()));
	}
	bool contentOverlapped(QWidget *w, const QRegion &r) {
		return contentOverlapped(QRect(w->mapToGlobal(r.boundingRect().topLeft()), r.boundingRect().size()));
	}

	void showMainMenu();
	void updateTrayMenu() override;
	void fixOrder() override;

	void showLayer(
		std::unique_ptr<Ui::LayerWidget> &&layer,
		Ui::LayerOptions options,
		anim::type animated);
	void showSpecialLayer(
		object_ptr<Ui::LayerWidget> layer,
		anim::type animated);
	bool showSectionInExistingLayer(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params);
	void ui_showBox(
		object_ptr<Ui::BoxContent> box,
		Ui::LayerOptions options,
		anim::type animated);
	void ui_hideSettingsAndLayer(anim::type animated);
	void ui_removeLayerBlackout();
	bool ui_isLayerShown();
	bool showMediaPreview(
		Data::FileOrigin origin,
		not_null<DocumentData*> document);
	bool showMediaPreview(
		Data::FileOrigin origin,
		not_null<PhotoData*> photo);
	void hideMediaPreview();

	void updateControlsGeometry() override;

protected:
	bool eventFilter(QObject *o, QEvent *e) override;
	void closeEvent(QCloseEvent *e) override;

	void initHook() override;
	void activeChangedHook() override;
	void clearWidgetsHook() override;

private:
	[[nodiscard]] bool skipTrayClick() const;

	void createTrayIconMenu();
	void handleTrayIconActication(
		QSystemTrayIcon::ActivationReason reason) override;

	void applyInitialWorkMode();
	void ensureLayerCreated();
	void destroyLayer();

	void showBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions options,
		anim::type animated);

	void themeUpdated(const Window::Theme::BackgroundUpdate &data);

	void toggleDisplayNotifyFromTray();
	void toggleSoundNotifyFromTray();

	QPixmap grabInner();

	std::unique_ptr<Media::SystemMediaControlsManager> _mediaControlsManager;

	crl::time _lastTrayClickTime = 0;
	QPoint _lastMousePosition;
	bool _activeForTrayIconAction = true;

	object_ptr<Window::PasscodeLockWidget> _passcodeLock = { nullptr };
	object_ptr<Intro::Widget> _intro = { nullptr };
	object_ptr<MainWidget> _main = { nullptr };
	base::unique_qptr<Ui::LayerStackWidget> _layer;
	object_ptr<Window::MediaPreviewWidget> _mediaPreview = { nullptr };

	object_ptr<Window::Theme::WarningWidget> _testingThemeWarning = { nullptr };

	rpl::event_stream<> _updateTrayMenuTextActions;

};

namespace App {
MainWindow *wnd();
} // namespace App
