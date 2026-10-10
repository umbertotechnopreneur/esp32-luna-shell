/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 * Original file notice:
 *
 * ESP32 Luna Shell
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 ESP32 Luna Shell. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Author website: https://umbertogiacobbi.biz
 *
 *
 * VBWR E */


#include "LUNASHELL_alert_service.hpp"

#include <utility>

namespace LUNASHELL {

using esp_brookesia::system::core::AppContext;
using esp_brookesia::system::core::MessageDialogButton;
using esp_brookesia::system::core::MessageDialogButtonRole;
using esp_brookesia::system::core::MessageDialogIcon;
using esp_brookesia::system::core::MessageDialogOptions;
using esp_brookesia::system::core::MessageDialogRequestId;
using esp_brookesia::system::core::MessageDialogResult;

namespace {

MessageDialogOptions make_alert_options(
    std::string_view title,
    std::string_view details,
    MessageDialogIcon icon,
    std::string_view button_text
)
{
    return MessageDialogOptions{
        .text = std::string(title),
        .informative_text = std::string(details),
        .icon = icon,
        .buttons = {
            MessageDialogButton{
                .text = std::string(button_text),
                .role = MessageDialogButtonRole::Accept,
            },
        },
        .auto_close_ms = 0,
    };
}

MessageDialogOptions make_notice_options(
    std::string_view title,
    std::string_view details,
    MessageDialogIcon icon,
    std::string_view button_text
)
{
    return make_alert_options(title, details, icon, button_text);
}

MessageDialogOptions make_confirmation_options(
    std::string_view title,
    std::string_view details,
    std::string_view cancel_text,
    std::string_view confirm_text,
    MessageDialogButtonRole confirm_role,
    MessageDialogIcon icon
)
{
    return MessageDialogOptions{
        .text = std::string(title),
        .informative_text = std::string(details),
        .icon = icon,
        .buttons = {
            MessageDialogButton{
                .text = std::string(cancel_text),
                .role = MessageDialogButtonRole::No,
            },
            MessageDialogButton{
                .text = std::string(confirm_text),
                .role = confirm_role,
            },
        },
        .auto_close_ms = 0,
    };
}

}  // namespace

std::expected<void, std::string> AlertService::hide_dialog(
    AppContext &context,
    DialogRequestId AlertService::*slot,
    std::string_view slot_name,
    bool keep_slot_on_failure
)
{
    DialogRequestId &request_id = this->*slot;
    if (request_id == esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID) {
        return {};
    }

    auto hide_result = context.system_service().hide_message_dialog(request_id);
    if (!hide_result) {
        if (!keep_slot_on_failure) {
            request_id = esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
        }
        return std::unexpected(
            "Failed to hide LUNASHELL " + std::string(slot_name) +
            " dialog: " + hide_result.error()
        );
    }

    request_id = esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
    return {};
}

std::expected<void, std::string> AlertService::show_dialog(
    AppContext &context,
    DialogRequestId AlertService::*slot,
    DialogOptions options,
    std::function<void(const DialogResult &)> on_result,
    bool replace_existing,
    std::string_view slot_name
)
{
    DialogRequestId &request_id = this->*slot;
    if (request_id != esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID) {
        if (!replace_existing) {
            return {};
        }

        auto hide_result = context.system_service().hide_message_dialog(request_id);
        if (!hide_result) {
            return std::unexpected(
                "Failed to replace the visible LUNASHELL " + std::string(slot_name) +
                " dialog: " + hide_result.error()
            );
        }
        request_id = esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
    }

    auto result = context.system_service().show_message_dialog(
        std::move(options),
        [this, slot, on_result = std::move(on_result)](const DialogResult &dialog_result) mutable {
            this->*slot = esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
            if (on_result) {
                on_result(dialog_result);
            }
        }
    );
    if (!result) {
        return std::unexpected(
            "Failed to show LUNASHELL " + std::string(slot_name) +
            " dialog: " + result.error()
        );
    }

    request_id = *result;
    return {};
}

std::expected<void, std::string> AlertService::hide_all(AppContext &context)
{
    std::string hide_error;
    if (auto result = hide_dialog(context, &AlertService::alert_dialog_, "alert", false);
        !result && hide_error.empty()) {
        hide_error = result.error();
    }
    if (auto result = hide_dialog(context, &AlertService::notice_dialog_, "notice", false);
        !result && hide_error.empty()) {
        hide_error = result.error();
    }
    if (!hide_error.empty()) {
        return std::unexpected(std::move(hide_error));
    }
    return {};
}

std::expected<void, std::string> AlertService::show_alert(
    AppContext &context,
    std::string_view title,
    std::string_view details,
    MessageDialogIcon icon,
    std::string_view button_text
)
{
    if (notice_dialog_ !=
        esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID) {
        if (auto result = hide_dialog(
                context,
                &AlertService::notice_dialog_,
                "notice",
                true
            ); !result) {
            return result;
        }
    }
    return show_dialog(
        context,
        &AlertService::alert_dialog_,
        make_alert_options(title, details, icon, button_text),
        {},
        true,
        "alert"
    );
}

std::expected<void, std::string> AlertService::show_notice(
    AppContext &context,
    std::string_view title,
    std::string_view details,
    MessageDialogIcon icon,
    std::string_view button_text
)
{
    if (alert_dialog_ !=
        esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID) {
        return std::unexpected(
            "Cannot show LUNASHELL notice while a critical alert is visible"
        );
    }
    return show_dialog(
        context,
        &AlertService::notice_dialog_,
        make_notice_options(title, details, icon, button_text),
        {},
        false,
        "notice"
    );
}

std::expected<void, std::string> AlertService::show_confirmation(
    AppContext &context,
    std::string_view title,
    std::string_view details,
    std::string_view cancel_text,
    std::string_view confirm_text,
    MessageDialogButtonRole confirm_role,
    std::function<void()> on_confirm,
    MessageDialogIcon icon
)
{
    if (alert_dialog_ !=
        esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID) {
        return std::unexpected(
            "Cannot show LUNASHELL confirmation while a critical alert is visible"
        );
    }
    return show_dialog(
        context,
        &AlertService::notice_dialog_,
        make_confirmation_options(
            title,
            details,
            cancel_text,
            confirm_text,
            confirm_role,
            icon
        ),
        [on_confirm = std::move(on_confirm), confirm_role](const DialogResult &dialog_result) mutable {
            if (dialog_result.button_role == confirm_role && on_confirm) {
                on_confirm();
            }
        },
        false,
        "notice"
    );
}

}  // namespace LUNASHELL
