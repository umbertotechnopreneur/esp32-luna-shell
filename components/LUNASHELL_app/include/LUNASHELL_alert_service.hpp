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


#pragma once

#include <expected>
#include <functional>
#include <string>
#include <string_view>

#include "brookesia/system_core.hpp"

namespace LUNASHELL {

class AlertService final {
public:
    std::expected<void, std::string> hide_all(
        esp_brookesia::system::core::AppContext &context
    );

    bool dialog_visible() const noexcept
    {
        return alert_dialog_ !=
                   esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID ||
               notice_dialog_ !=
                   esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
    }

    std::expected<void, std::string> show_alert(
        esp_brookesia::system::core::AppContext &context,
        std::string_view title,
        std::string_view details,
        esp_brookesia::system::core::MessageDialogIcon icon =
            esp_brookesia::system::core::MessageDialogIcon::Critical,
        std::string_view button_text = "OK"
    );

    std::expected<void, std::string> show_notice(
        esp_brookesia::system::core::AppContext &context,
        std::string_view title,
        std::string_view details,
        esp_brookesia::system::core::MessageDialogIcon icon =
            esp_brookesia::system::core::MessageDialogIcon::Information,
        std::string_view button_text = "OK"
    );

    std::expected<void, std::string> show_confirmation(
        esp_brookesia::system::core::AppContext &context,
        std::string_view title,
        std::string_view details,
        std::string_view cancel_text,
        std::string_view confirm_text,
        esp_brookesia::system::core::MessageDialogButtonRole confirm_role,
        std::function<void()> on_confirm = {},
        esp_brookesia::system::core::MessageDialogIcon icon =
            esp_brookesia::system::core::MessageDialogIcon::Warning
    );

private:
    using DialogRequestId = esp_brookesia::system::core::MessageDialogRequestId;
    using DialogOptions = esp_brookesia::system::core::MessageDialogOptions;
    using DialogResult = esp_brookesia::system::core::MessageDialogResult;

    std::expected<void, std::string> show_dialog(
        esp_brookesia::system::core::AppContext &context,
        DialogRequestId AlertService::*slot,
        DialogOptions options,
        std::function<void(const DialogResult &)> on_result,
        bool replace_existing,
        std::string_view slot_name
    );

    std::expected<void, std::string> hide_dialog(
        esp_brookesia::system::core::AppContext &context,
        DialogRequestId AlertService::*slot,
        std::string_view slot_name,
        bool keep_slot_on_failure
    );

    DialogRequestId alert_dialog_ =
        esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
    DialogRequestId notice_dialog_ =
        esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
};

}  // namespace LUNASHELL
