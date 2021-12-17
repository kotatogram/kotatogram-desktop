# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(td_ui OBJECT)
init_non_host_target(td_ui)
add_library(tdesktop::td_ui ALIAS td_ui)

include(lib_ui/cmake/generate_styles.cmake)
include(cmake/generate_numbers.cmake)

set(style_files
    ui/td_common.style
    ui/filter_icons.style
    ui/chat/chat.style
    boxes/boxes.style
    dialogs/dialogs.style
    chat_helpers/chat_helpers.style
    calls/calls.style
    export/view/export.style
    info/info.style
    intro/intro.style
    media/player/media_player.style
    passport/passport.style
    payments/ui/payments.style
    profile/profile.style
    settings/settings.style
    media/view/media_view.style
    overview/overview.style
    window/window.style
    editor/editor.style
)

set(dependent_style_files
    ${submodules_loc}/lib_ui/ui/colors.palette
    ${submodules_loc}/lib_ui/ui/basic.style
    ${submodules_loc}/lib_ui/ui/layers/layers.style
    ${submodules_loc}/lib_ui/ui/widgets/widgets.style
)

generate_styles(td_ui ${src_loc} "${style_files}" "${dependent_style_files}")

target_precompile_headers(td_ui PRIVATE ${src_loc}/ui/ui_pch.h)
nice_target_sources(td_ui ${src_loc}
PRIVATE
    ${style_files}

    calls/group/ui/calls_group_recording_box.cpp
    calls/group/ui/calls_group_recording_box.h
    calls/group/ui/calls_group_scheduled_labels.cpp
    calls/group/ui/calls_group_scheduled_labels.h
    calls/group/ui/desktop_capture_choose_source.cpp
    calls/group/ui/desktop_capture_choose_source.h

    core/file_location.cpp
    core/file_location.h
    core/mime_type.cpp
    core/mime_type.h

    countries/countries_instance.cpp
    countries/countries_instance.h

    layout/abstract_layout_item.cpp
    layout/abstract_layout_item.h
    layout/layout_mosaic.cpp
    layout/layout_mosaic.h
    layout/layout_position.cpp
    layout/layout_position.h
    layout/layout_selection.cpp
    layout/layout_selection.h

    media/clip/media_clip_check_streaming.cpp
    media/clip/media_clip_check_streaming.h
    media/clip/media_clip_ffmpeg.cpp
    media/clip/media_clip_ffmpeg.h
    media/clip/media_clip_implementation.cpp
    media/clip/media_clip_implementation.h
    media/clip/media_clip_reader.cpp
    media/clip/media_clip_reader.h

    media/player/media_player_dropdown.cpp
    media/player/media_player_dropdown.h

    passport/ui/passport_details_row.cpp
    passport/ui/passport_details_row.h
    passport/ui/passport_form_row.cpp
    passport/ui/passport_form_row.h

    payments/ui/payments_edit_card.cpp
    payments/ui/payments_edit_card.h
    payments/ui/payments_edit_information.cpp
    payments/ui/payments_edit_information.h
    payments/ui/payments_form_summary.cpp
    payments/ui/payments_form_summary.h
    payments/ui/payments_field.cpp
    payments/ui/payments_field.h
    payments/ui/payments_panel.cpp
    payments/ui/payments_panel.h
    payments/ui/payments_panel_data.h
    payments/ui/payments_panel_delegate.h

    platform/mac/file_bookmark_mac.h
    platform/mac/file_bookmark_mac.mm
    platform/platform_file_bookmark.h

    ui/boxes/auto_delete_settings.cpp
    ui/boxes/auto_delete_settings.h
    ui/boxes/calendar_box.cpp
    ui/boxes/calendar_box.h
    ui/boxes/choose_date_time.cpp
    ui/boxes/choose_date_time.h
    ui/boxes/confirm_box.cpp
    ui/boxes/confirm_box.h
    ui/boxes/confirm_phone_box.cpp
    ui/boxes/confirm_phone_box.h
    ui/boxes/country_select_box.cpp
    ui/boxes/country_select_box.h
    ui/boxes/edit_invite_link.cpp
    ui/boxes/edit_invite_link.h
    ui/boxes/rate_call_box.cpp
    ui/boxes/rate_call_box.h
    ui/boxes/report_box.cpp
    ui/boxes/report_box.h
    ui/boxes/single_choice_box.cpp
    ui/boxes/single_choice_box.h

    ui/chat/attach/attach_abstract_single_file_preview.cpp
    ui/chat/attach/attach_abstract_single_file_preview.h
    ui/chat/attach/attach_abstract_single_media_preview.cpp
    ui/chat/attach/attach_abstract_single_media_preview.h
    ui/chat/attach/attach_abstract_single_preview.h
    ui/chat/attach/attach_album_preview.cpp
    ui/chat/attach/attach_album_preview.h
    ui/chat/attach/attach_album_thumbnail.cpp
    ui/chat/attach/attach_album_thumbnail.h
    ui/chat/attach/attach_controls.cpp
    ui/chat/attach/attach_controls.h
    ui/chat/attach/attach_extensions.cpp
    ui/chat/attach/attach_extensions.h
    ui/chat/attach/attach_prepare.cpp
    ui/chat/attach/attach_prepare.h
    ui/chat/attach/attach_send_files_way.cpp
    ui/chat/attach/attach_send_files_way.h
    ui/chat/attach/attach_single_file_preview.cpp
    ui/chat/attach/attach_single_file_preview.h
    ui/chat/attach/attach_single_media_preview.cpp
    ui/chat/attach/attach_single_media_preview.h
    ui/chat/chat_style.cpp
    ui/chat/chat_style.h
    ui/chat/chat_theme.cpp
    ui/chat/chat_theme.h
    ui/chat/continuous_scroll.cpp
    ui/chat/continuous_scroll.h
    #ui/chat/forward_options_box.cpp
    #ui/chat/forward_options_box.h
    ui/chat/group_call_bar.cpp
    ui/chat/group_call_bar.h
    ui/chat/group_call_userpics.cpp
    ui/chat/group_call_userpics.h
    ui/chat/message_bar.cpp
    ui/chat/message_bar.h
    ui/chat/message_bubble.cpp
    ui/chat/message_bubble.h
    ui/chat/pinned_bar.cpp
    ui/chat/pinned_bar.h
    ui/chat/requests_bar.cpp
    ui/chat/requests_bar.h
    ui/chat/select_scroll_manager.cpp
    ui/chat/select_scroll_manager.h
    ui/controls/call_mute_button.cpp
    ui/controls/call_mute_button.h
    ui/controls/delete_message_context_action.cpp
    ui/controls/delete_message_context_action.h
    ui/controls/emoji_button.cpp
    ui/controls/emoji_button.h
    ui/controls/invite_link_buttons.cpp
    ui/controls/invite_link_buttons.h
    ui/controls/invite_link_label.cpp
    ui/controls/invite_link_label.h
    ui/controls/send_as_button.cpp
    ui/controls/send_as_button.h
    ui/controls/send_button.cpp
    ui/controls/send_button.h
    ui/controls/who_read_context_action.cpp
    ui/controls/who_read_context_action.h
    ui/text/format_song_name.cpp
    ui/text/format_song_name.h
    ui/text/format_values.cpp
    ui/text/format_values.h
    ui/text/text_options.cpp
    ui/text/text_options.h
    ui/toasts/common_toasts.cpp
    ui/toasts/common_toasts.h

    ui/widgets/continuous_sliders.cpp
    ui/widgets/continuous_sliders.h
    ui/widgets/discrete_sliders.cpp
    ui/widgets/discrete_sliders.h
    ui/widgets/sent_code_field.cpp
    ui/widgets/sent_code_field.h
    ui/widgets/separate_panel.cpp
    ui/widgets/separate_panel.h

    ui/cached_round_corners.cpp
    ui/cached_round_corners.h
    ui/grouped_layout.cpp
    ui/grouped_layout.h
    ui/special_fields.cpp
    ui/special_fields.h

    ui/ui_pch.h
)

target_include_directories(td_ui
PUBLIC
    ${src_loc}
)

target_link_libraries(td_ui
PUBLIC
    tdesktop::td_lang
    desktop-app::lib_ui
    desktop-app::lib_lottie
PRIVATE
    tdesktop::lib_tgcalls
    desktop-app::lib_ffmpeg
    desktop-app::lib_webview
    desktop-app::lib_webrtc
    desktop-app::lib_stripe
)
