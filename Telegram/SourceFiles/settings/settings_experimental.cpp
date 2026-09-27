/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_experimental.h"

#include "kotato/kotato_lang.h"
#include "data/components/passkeys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/search_field_controller.h"
#include "ui/text/text_entity.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/vertical_list.h"
#include "ui/gl/gl_detection.h"
#include "ui/chat/chat_style_radius.h"
#include "ui/controls/compose_ai_button_factory.h"
#include "base/options.h"
#include "boxes/moderate_messages_box.h"
#include "core/application.h"
#include "core/launcher.h"
#include "core/sandbox.h"
#include "chat_helpers/tabbed_panel.h"
#include "dialogs/dialogs_widget.h"
#include "dialogs/ui/dialogs_layout.h"
#include "history/history_item_components.h"
#include "history/view/controls/compose_controls_common.h"
#include "history/view/history_view_message.h"
#include "info/profile/info_profile_actions.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "media/player/media_player_instance.h"
#include "mtproto/session_private.h"
#include "webview/webview_embed.h"
#include "window/main_window.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "window/notifications_manager.h"
#include "info/info_flexible_scroll.h"
#include "chat_helpers/stickers_list_widget.h"
#include "styles/style_info.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QJsonDocument>
#include <QtGui/QGuiApplication>

namespace Settings {
namespace {

// format: { key, { name, description }}
const std::map<QString, std::pair<QString, QString>> TranslationMap {
	{ ChatHelpers::kOptionTabbedPanelShowOnClick, {
		"ktg_experimental_tabbed_panel_by_click",
		"ktg_experimental_tabbed_panel_by_click_description",
	}},
	{ Dialogs::kOptionForumHideChatsList, {
		"ktg_experimental_forum_hide_chats_list",
		"ktg_experimental_forum_hide_chats_list_description",
	}},
	{ Dialogs::Ui::kOptionDialogsMuteIcon, {
		"ktg_experimental_dialogs_mute_icon",
		"ktg_experimental_dialogs_mute_icon_description",
	}},
	{ Core::kOptionFractionalScalingEnabled, {
		"ktg_experimental_fractional_scaling_enabled",
		"ktg_experimental_fractional_scaling_enabled_description",
	}},
	{ Core::kOptionHighDpiDownscale, {
		"ktg_experimental_high_dpi_downscale",
		"ktg_experimental_high_dpi_downscale_description",
	}},
	{ Ui::GL::kOptionUseQtRhi, {
		"ktg_experimental_use_qt_rhi",
		"",
	}},
	{ Window::kOptionViewProfileInChatsListContextMenu, {
		"ktg_experimental_view_profile_context_menu",
		"ktg_experimental_view_profile_context_menu_description",
	}},
	{ Info::Profile::kOptionShowPeerIdBelowAbout, {
		"ktg_experimental_show_peer_id_below_about",
		"ktg_experimental_show_peer_id_below_about_description",
	}},
	{ Info::Profile::kOptionShowChannelJoinedBelowAbout, {
		"ktg_experimental_show_channel_joined_below_about",
		"ktg_experimental_show_channel_joined_below_about_description",
	}},
	{ Ui::kOptionUseSmallMsgBubbleRadius, {
		"ktg_experimental_use_small_msg_bubble_radius",
		"ktg_experimental_use_small_msg_bubble_radius_description",
	}},
	{ Media::Player::kOptionDisableAutoplayNext, {
		"ktg_experimental_disable_autoplay_next",
		"ktg_experimental_disable_autoplay_next_description",
	}},
	{ Webview::kOptionWebviewDebugEnabled, {
		"ktg_experimental_webview_debug_enabled",
		"ktg_experimental_webview_debug_enabled_description",
	}},
	{ Webview::kOptionWebviewLegacyEdge, {
		"ktg_experimental_webview_legacy_edge",
		"ktg_experimental_webview_legacy_edge_description",
	}},
	{ kOptionAutoScrollInactiveChat, {
		"ktg_experimental_auto_scroll_inactive_chat",
		"ktg_experimental_auto_scroll_inactive_chat_description",
	}},
	{ Window::Notifications::kOptionHideReplyButton, {
		"ktg_experimental_hide_reply_button",
		"ktg_experimental_hide_reply_button_description",
	}},
	{ Window::Notifications::kOptionCustomNotification, {
		"ktg_experimental_custom_notification",
		"ktg_experimental_custom_notification_description",
	}},
	{ Window::Notifications::kOptionGNotification, {
		"ktg_experimental_g_notification",
		"ktg_experimental_g_notification_description",
	}},
	{ Core::kOptionFreeType, {
		"ktg_experimental_freetype",
		"ktg_experimental_freetype_description",
	}},
	{ Core::kOptionSkipUrlSchemeRegister, {
		"ktg_experimental_skip_url_scheme_register",
		"ktg_experimental_skip_url_scheme_register_description",
	}},
	{ Core::kOptionDeadlockDetector, {
		"ktg_experimental_deadlock_detector",
		"ktg_experimental_deadlock_detector_description",
	}},
	{ Window::kOptionExternalMediaViewer, {
		"ktg_experimental_external_media_viewer",
		"ktg_experimental_external_media_viewer_description",
	}},
	{ Window::kOptionNewWindowsSizeAsFirst, {
		"ktg_experimental_new_windows_size_as_first",
		"ktg_experimental_new_windows_size_as_first_description",
	}},
	{ MTP::details::kOptionPreferIPv6, {
		"ktg_experimental_prefer_ipv6",
		"ktg_experimental_prefer_ipv6_description",
	}},
	{ kOptionFastButtonsMode, {
		"ktg_experimental_fast_buttons_mode",
		"ktg_experimental_fast_buttons_mode_description",
	}},
	{ Window::kOptionDisableTouchbar, {
		"ktg_experimental_disable_touchbar",
		"",
	}},
	{ Info::kAlternativeScrollProcessing, {
		"ktg_experimental_alternative_scroll_processing",
		"",
	}},
	{ kModerateCommonGroups, {
		"ktg_experimental_moderate_common_groups",
		"",
	}},
	{ kForceComposeSearchOneColumn, {
		"ktg_experimental_force_compose_search_one_column",
		"ktg_experimental_force_compose_search_one_column_description",
	}},
	{ ChatHelpers::kOptionUnlimitedRecentStickers, {
		"ktg_experimental_unlimited_recent_stickers",
		"ktg_experimental_unlimited_recent_stickers_description",
	}},
	{ Ui::kOptionHideAiButton, {
		"ktg_experimental_hide_ai_button",
		"ktg_experimental_hide_ai_button_description",
	}},
	{ HistoryView::kOptionUnlimitedMessageWidth, {
		"ktg_experimental_unlimited_message_width",
		"ktg_experimental_unlimited_message_width_description",
	}},
	{ HistoryView::Controls::kOptionMacCmdReplyImmediately, {
		"ktg_experimental_mac_cmd_reply_immediately",
		"ktg_experimental_mac_cmd_reply_immediately_description",
	}},
};

const auto kOptionsClipboardPrefix = u"tdesktop-flags:"_q;

[[nodiscard]] QString TranslatedExperimental(
		const QString &key,
		const QString &fallback) {
	const auto translated = ktr(key);
	return translated.isEmpty() ? fallback : translated;
}

struct DecodeOptionsResult {
	bool ok = false;
	QString json;
};

[[nodiscard]] QString EncodeOptionsToText(const QString &json) {
	const auto flags = QByteArray::Base64UrlEncoding
		| QByteArray::OmitTrailingEquals;
	return kOptionsClipboardPrefix
		+ qs(qCompress(json.toLatin1(), 9).toBase64(flags));
}

[[nodiscard]] DecodeOptionsResult DecodeOptionsFromText(const QString &text) {
	auto result = DecodeOptionsResult();
	if (!text.startsWith(kOptionsClipboardPrefix)) {
		return result;
	}
	auto encoded = QStringView(text).mid(
		kOptionsClipboardPrefix.size()).toLatin1();
	const auto compressed = QByteArray::fromBase64Encoding(
		std::move(encoded),
		QByteArray::Base64UrlEncoding
			| QByteArray::AbortOnBase64DecodingErrors);
	if (!compressed || (*compressed).isEmpty()) {
		return result;
	}
	const auto decoded = qUncompress(*compressed);
	if (decoded.isEmpty()) {
		return result;
	}

	auto error = QJsonParseError();
	const auto parsed = QJsonDocument::fromJson(decoded, &error);
	if ((error.error != QJsonParseError::NoError) || !parsed.isObject()) {
		return result;
	}
	result.ok = true;
	result.json = QString::fromUtf8(decoded);
	return result;
}

void AddOption(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container,
		base::options::option<bool> &option,
		rpl::producer<> resetClicks,
		rpl::producer<> reloadOptionsRequests,
		rpl::producer<QString> query,
		Fn<void(const QString&, not_null<QWidget*>)> registerHighlight) {
	const auto translation = TranslationMap.find(option.id());
	const auto name = (translation != TranslationMap.end())
		? TranslatedExperimental(
			translation->second.first,
			option.name().isEmpty() ? option.id() : option.name())
		: (option.name().isEmpty() ? option.id() : option.name());
	const auto &description = (translation != TranslationMap.end()
		&& !translation->second.second.isEmpty())
			? TranslatedExperimental(
				translation->second.second,
				option.description())
			: option.description();

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = wrap->entity();

	auto &lifetime = inner->lifetime();
	const auto toggles = lifetime.make_state<rpl::event_stream<bool>>();
	std::move(
		resetClicks
	) | rpl::map_to(
		option.defaultValue()
	) | rpl::start_to_stream(*toggles, lifetime);
	std::move(reloadOptionsRequests) | rpl::on_next([=, &option] {
		toggles->fire_copy(option.value());
	}, lifetime);

	const auto button = inner->add(object_ptr<Button>(
		inner,
		rpl::single(name),
		(option.relevant()
			? st::settingsButtonNoIcon
			: st::settingsOptionDisabled)
	))->toggleOn(toggles->events_starting_with(option.value()));

	if (registerHighlight) {
		registerHighlight(u"experimental/"_q + option.id(), button);
	}

	const auto link = u"tg://settings/experimental/"_q + option.id();
	const auto menu
		= button->lifetime().make_state<base::unique_qptr<Ui::PopupMenu>>();
	button->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return e->type() == QEvent::ContextMenu;
	}) | rpl::on_next([=](not_null<QEvent*> e) {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		(*menu)->addAction(u"Copy deep link"_q, [=] {
			TextUtilities::SetClipboardText({ link });
			window->showToast(u"Deep link copied to clipboard."_q);
		}, &st::menuIconCopy);
		(*menu)->popup(QCursor::pos());
		e->accept();
	}, button->lifetime());

	const auto restarter = (option.relevant() && option.restartRequired())
		? button->lifetime().make_state<base::Timer>()
		: nullptr;
	if (restarter) {
		restarter->setCallback([=] {
			window->show(Ui::MakeConfirmBox({
				.text = tr::lng_settings_need_restart(),
				.confirmed = [] { Core::Restart(); },
				.confirmText = tr::lng_settings_restart_now(),
				.cancelText = tr::lng_settings_restart_later(),
			}));
		});
	}
	button->toggledChanges(
	) | rpl::on_next([=, &option](bool toggled) {
		if (!option.relevant() && toggled != option.defaultValue()) {
			toggles->fire_copy(option.defaultValue());
			window->showToast(
				tr::lng_settings_experimental_irrelevant(tr::now));
			return;
		}
		option.set(toggled);
		if (restarter) {
			restarter->callOnce(st::settingsButtonNoIcon.toggle.duration);
		}
	}, inner->lifetime());

	if (!description.isEmpty()) {
		Ui::AddSkip(inner, st::settingsCheckboxesSkip);
		Ui::AddDividerText(inner, rpl::single(description));
		Ui::AddSkip(inner, st::settingsCheckboxesSkip);
	}

	std::move(
		query
	) | rpl::on_next([=](const QString &text) {
		const auto trimmed = text.trimmed();
		const auto matches = trimmed.isEmpty()
			|| name.contains(trimmed, Qt::CaseInsensitive)
			|| description.contains(trimmed, Qt::CaseInsensitive);
		wrap->toggle(matches, anim::type::instant);
	}, wrap->lifetime());
}

void SetupExperimental(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> reloadOptionsRequests,
		rpl::producer<QString> query,
		Fn<void(const QString&, not_null<QWidget*>)> registerHighlight) {
	const auto headerWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto header = headerWrap->entity();

	Ui::AddSkip(header, st::settingsCheckboxesSkip);

	header->add(
		object_ptr<Ui::FlatLabel>(
			header,
			tr::lng_settings_experimental_about(),
			st::boxLabel),
		st::defaultBoxDividerLabelPadding);

	auto reset = (Button*)nullptr;
	if (base::options::changed()) {
		const auto wrap = header->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				header,
				object_ptr<Ui::VerticalLayout>(header)));
		const auto inner = wrap->entity();
		Ui::AddDivider(inner);
		Ui::AddSkip(inner, st::settingsCheckboxesSkip);
		reset = inner->add(object_ptr<Button>(
			inner,
			tr::lng_settings_experimental_restore(),
			st::settingsButtonNoIcon));
		reset->addClickHandler([=] {
			base::options::reset();
			wrap->hide(anim::type::normal);
		});
		Ui::AddSkip(inner, st::settingsCheckboxesSkip);
	}

	Ui::AddDivider(header);
	Ui::AddSkip(header, st::settingsCheckboxesSkip);

	rpl::duplicate(
		query
	) | rpl::on_next([=](const QString &text) {
		headerWrap->toggle(text.trimmed().isEmpty(), anim::type::instant);
	}, headerWrap->lifetime());

	const auto addToggle = [&](const char name[]) {
		AddOption(
			window,
			container,
			base::options::lookup<bool>(name),
			(reset
				? (reset->clicks() | rpl::to_empty)
				: rpl::producer<>()),
			rpl::duplicate(reloadOptionsRequests),
			rpl::duplicate(query),
			registerHighlight);
	};

	addToggle(ChatHelpers::kOptionTabbedPanelShowOnClick);
	addToggle(Dialogs::kOptionForumHideChatsList);
	addToggle(Dialogs::Ui::kOptionDialogsMuteIcon);
	addToggle(Core::kOptionFractionalScalingEnabled);
	addToggle(Core::kOptionHighDpiDownscale);
	addToggle(Ui::GL::kOptionUseQtRhi);
	addToggle(Window::kOptionViewProfileInChatsListContextMenu);
	addToggle(Info::Profile::kOptionShowPeerIdBelowAbout);
	addToggle(Info::Profile::kOptionShowChannelJoinedBelowAbout);
	addToggle(Ui::kOptionUseSmallMsgBubbleRadius);
	addToggle(Media::Player::kOptionDisableAutoplayNext);
	addToggle(Webview::kOptionWebviewDebugEnabled);
	addToggle(Webview::kOptionWebviewLegacyEdge);
	addToggle(kOptionAutoScrollInactiveChat);
	addToggle(Window::Notifications::kOptionHideReplyButton);
	addToggle(Window::Notifications::kOptionCustomNotification);
	addToggle(Window::Notifications::kOptionGNotification);
	addToggle(Core::kOptionFreeType);
	addToggle(Core::kOptionSkipUrlSchemeRegister);
	addToggle(Core::kOptionDeadlockDetector);
	addToggle(Window::kOptionExternalMediaViewer);
	addToggle(Window::kOptionNewWindowsSizeAsFirst);
	addToggle(MTP::details::kOptionPreferIPv6);
	if (base::options::lookup<bool>(kOptionFastButtonsMode).value()) {
		addToggle(kOptionFastButtonsMode);
	}
	addToggle(Window::kOptionDisableTouchbar);
	addToggle(Info::kAlternativeScrollProcessing);
	addToggle(kModerateCommonGroups);
	addToggle(kForceComposeSearchOneColumn);
	addToggle(ChatHelpers::kOptionUnlimitedRecentStickers);
	addToggle(Ui::kOptionHideAiButton);
	addToggle(HistoryView::kOptionUnlimitedMessageWidth);
	addToggle(HistoryView::Controls::kOptionMacCmdReplyImmediately);
}

} // namespace

Experimental::Experimental(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

Experimental::~Experimental() = default;

rpl::producer<QString> Experimental::title() {
	return tr::lng_settings_experimental();
}

void Experimental::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	const auto window = &controller()->window();
	addAction(
		u"Export"_q,
		[=] {
			TextUtilities::SetClipboardText(
				{ EncodeOptionsToText(base::options::serialize()) });
			window->showToast(u"Experimental settings code copied to clipboard."_q);
		},
		&st::menuIconCopy);
	if (!DecodeOptionsFromText(QGuiApplication::clipboard()->text()).ok) {
		return;
	}
	addAction(
		u"Import"_q,
		[=] {
			const auto decoded = DecodeOptionsFromText(
				QGuiApplication::clipboard()->text());
			if (!decoded.ok) {
				window->showToast(u"Clipboard does not contain "
					"a valid experimental settings code."_q);
				return;
			}
			if (!base::options::deserialize(decoded.json)) {
				window->showToast(u"Experimental settings code is valid"
					", but data format is not supported."_q);
				return;
			}
			_reloadOptionsRequests.fire({});
			window->showToast(u"Experimental settings imported "
				"from code in clipboard."_q);
		},
		&st::menuIconImportTheme);
}

void Experimental::setInnerFocus() {
	if (_searchField) {
		_searchField->setFocus();
	} else {
		setFocus();
	}
}

void Experimental::showFinished() {
	AbstractSection::showFinished();
	for (const auto &[id, widget] : _highlights) {
		if (widget) {
			controller()->checkHighlightControl(id, widget);
		}
	}
}

base::weak_qptr<Ui::RpWidget> Experimental::createPinnedToTop(
		not_null<QWidget*> parent) {
	_searchController = std::make_unique<Ui::SearchFieldController>(
		_query.current());
	auto rowView = _searchController->createRowView(
		parent,
		st::infoLayerMediaSearch);
	_searchField = rowView.field;

	const auto searchContainer = Ui::CreateChild<Ui::FixedHeightWidget>(
		parent.get(),
		st::infoLayerMediaSearch.height);
	const auto wrap = rowView.wrap.release();
	wrap->setParent(searchContainer);
	wrap->show();

	searchContainer->widthValue(
	) | rpl::on_next([=](int width) {
		wrap->resizeToWidth(width);
		wrap->moveToLeft(0, 0);
	}, searchContainer->lifetime());

	_searchController->queryValue(
	) | rpl::on_next([=](QString text) {
		_query = std::move(text);
	}, searchContainer->lifetime());

	return base::make_weak(not_null<Ui::RpWidget*>{ searchContainer });
}

void Experimental::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupExperimental(
		&controller()->window(),
		content,
		_reloadOptionsRequests.events(),
		_query.value(),
		[this](const QString &id, not_null<QWidget*> widget) {
			_highlights.push_back({ id, widget.get() });
		});

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
