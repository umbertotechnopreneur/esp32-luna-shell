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
 *
 * VBWR E */

#include "LUNASHELL_status_bar.hpp"

#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "LUNASHELL_palette.hpp"
#include "LUNASHELL_snapshot_semantics.hpp"

namespace {

constexpr std::string_view kStatusRoot =
    "/LUNASHELL_chrome_screen/chrome/status";
using LUNASHELL::snapshot_semantics::component_ready;
using LUNASHELL::snapshot_semantics::storage_cleanup_failed;
using LUNASHELL::snapshot_semantics::storage_format_required;
using LUNASHELL::snapshot_semantics::storage_mounted_ready;

bool component_standby(const LUNASHELL::ComponentSnapshot &component)
{
    return component.health == LUNASHELL::DeviceHealth::Unknown &&
           component.last_error == ESP_ERR_INVALID_STATE &&
           !component.data_valid &&
           !component.data_stale &&
           component.last_attempt_at_us == 0 &&
           component.last_success_at_us == 0;
}

std::string_view component_color(const LUNASHELL::ComponentSnapshot &component)
{
    if (component.data_stale) {
        return LUNASHELL::palette::kStatusWarning;
    }
    switch (component.health) {
    case LUNASHELL::DeviceHealth::Ready:
        if (component.last_error != ESP_OK) {
            return LUNASHELL::palette::kStatusFault;
        }
        return component.data_valid
            ? LUNASHELL::palette::kStatusReady
            : LUNASHELL::palette::kStatusWarning;
    case LUNASHELL::DeviceHealth::Degraded:
        return LUNASHELL::palette::kStatusWarning;
    case LUNASHELL::DeviceHealth::Missing:
        return LUNASHELL::palette::kStatusMissing;
    case LUNASHELL::DeviceHealth::Unknown:
        if (component_standby(component)) {
            return LUNASHELL::palette::kStatusMuted;
        }
        return component.last_error == ESP_OK
            ? LUNASHELL::palette::kStatusWarning
            : LUNASHELL::palette::kStatusFault;
    }
    std::abort();
}

std::string_view battery_icon(const LUNASHELL::BatterySnapshot &battery)
{
    if (!component_ready(battery) ||
        !battery.battery_present ||
        !battery.level_percent_valid ||
        battery.level_percent < 0 ||
        battery.level_percent > 100) {
        return "\xef\x89\x84";
    }
    if (battery.level_percent <= 12) {
        return "\xef\x89\x84";
    }
    if (battery.level_percent <= 35) {
        return "\xef\x89\x83";
    }
    if (battery.level_percent <= 60) {
        return "\xef\x89\x82";
    }
    if (battery.level_percent <= 85) {
        return "\xef\x89\x81";
    }
    return "\xef\x89\x80";
}

bool battery_charging(const LUNASHELL::BatterySnapshot &battery)
{
    return component_ready(battery) &&
           battery.battery_present &&
           battery.usb_present &&
           std::string_view(battery.power_state) == "charging";
}

void add_binding(
    std::vector<esp_brookesia::gui::BindingValueUpdate> &updates,
    std::string path,
    std::string key,
    std::string_view value
)
{
    updates.push_back({
        .absolute_path = std::move(path),
        .key = std::move(key),
        .value = std::string(value),
    });
}

void add_indicator(
    std::vector<esp_brookesia::gui::BindingValueUpdate> &updates,
    std::string_view id,
    std::string_view color
)
{
    const std::string base =
        std::string(kStatusRoot) + "/" + std::string(id);
    add_binding(updates, base + "/icon", "color", color);
    add_binding(updates, base + "/dot", "color", color);
}

}  // namespace

namespace LUNASHELL {

void LUNASHELLStatusBar::invalidate()
{
    visual_state_.reset();
}

std::expected<void, std::string> LUNASHELLStatusBar::update(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    const DeviceSnapshot &snapshot
)
{
    std::string_view usb_color = component_color(snapshot.battery);
    std::string_view battery_color = component_color(snapshot.battery);
    if (component_ready(snapshot.battery)) {
        usb_color = snapshot.battery.usb_present
            ? palette::kStatusReady
            : palette::kStatusMuted;
        if (!snapshot.battery.battery_present) {
            battery_color = palette::kStatusMuted;
        } else if (!snapshot.battery.level_percent_valid ||
                   snapshot.battery.level_percent < 0 ||
                   snapshot.battery.level_percent > 100 ||
                   snapshot.battery.level_percent <= 12) {
            battery_color = palette::kStatusFault;
        } else if (snapshot.battery.level_percent <= 30) {
            battery_color = palette::kStatusWarning;
        }
    }

    std::string_view sd_color = component_color(snapshot.storage);
    if (storage_cleanup_failed(snapshot.storage)) {
        sd_color = palette::kStatusFault;
    } else if (storage_format_required(snapshot.storage)) {
        sd_color = palette::kStatusWarning;
    } else if (storage_mounted_ready(snapshot.storage)) {
        sd_color = palette::kStatusReady;
    } else if (!snapshot.storage.card_response_verified) {
        sd_color = palette::kStatusWarning;
    }

    const VisualState next_state{
        .usb_color = usb_color,
        .battery_color = battery_color,
        .sd_color = sd_color,
        .battery_icon = battery_icon(snapshot.battery),
        .battery_charging = battery_charging(snapshot.battery),
    };
    if (visual_state_.has_value() && *visual_state_ == next_state) {
        return {};
    }

    std::vector<esp_brookesia::gui::BindingValueUpdate> updates;
    updates.reserve(15);

    add_indicator(updates, "usb", next_state.usb_color);
    add_indicator(updates, "battery", next_state.battery_color);
    add_indicator(updates, "sd", next_state.sd_color);

    add_binding(
        updates,
        std::string(kStatusRoot) + "/battery/icon",
        "icon",
        next_state.battery_icon
    );
    add_binding(
        updates,
        std::string(kStatusRoot) + "/battery/charge",
        "hidden",
        next_state.battery_charging ? "false" : "true"
    );
    add_binding(
        updates,
        std::string(kStatusRoot) + "/battery/charge",
        "color",
        palette::kStatusCharge
    );

    auto result = gui.set_binding_values(updates);
    if (!result) {
        return std::unexpected(
            "Failed to update reusable LUNASHELL status bar: " + result.error()
        );
    }
    visual_state_ = next_state;
    return {};
}

}  // namespace LUNASHELL
