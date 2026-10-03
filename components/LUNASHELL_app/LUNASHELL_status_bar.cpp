#include "LUNASHELL_status_bar.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if CONFIG_LUNASHELL_BUILD_ENABLE_RTC
#include "LUNASHELL_clock.hpp"
#endif
#include "LUNASHELL_palette.hpp"
#include "LUNASHELL_snapshot_semantics.hpp"

namespace {

constexpr std::string_view kStatusRoot =
    "/LUNASHELL_chrome_screen/chrome/status";
#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
constexpr bool kWifiVisible = true;
#else
constexpr bool kWifiVisible = false;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_RTC
constexpr bool kClockVisible = true;
#else
constexpr bool kClockVisible = false;
#endif
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

// battery: Canonical PMU sample whose compact, truthful label is requested.
// Exceptions: std::bad_alloc if the returned string cannot allocate.
std::string battery_text(const LUNASHELL::BatterySnapshot &battery)
{
    if (battery.data_stale) {
        return "STALE";
    }
    if (!component_ready(battery)) {
        return "UNAVAILABLE";
    }
    if (!battery.battery_present) {
        return "NO BATTERY";
    }
    if (!battery.level_percent_valid || battery.level_percent < 0 ||
        battery.level_percent > 100) {
        return "UNAVAILABLE";
    }

    std::array<char, 5> text{};
    const int written = std::snprintf(
        text.data(),
        text.size(),
        "%d%%",
        battery.level_percent
    );
    if (written <= 0 || static_cast<size_t>(written) >= text.size()) {
        return "UNAVAILABLE";
    }
    return std::string(text.data(), static_cast<size_t>(written));
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
// wifi: Canonical station observation whose semantic status color is requested.
// Exceptions: none.
std::string_view wifi_color(const LUNASHELL::WifiSnapshot &wifi)
{
    if (!component_ready(wifi)) {
        return component_color(wifi);
    }
    if (!wifi.started || !wifi.credentials_configured) {
        return LUNASHELL::palette::kStatusMuted;
    }
    if (wifi.connected && wifi.ip_acquired) {
        return wifi.rssi < 0 && wifi.rssi <= -80
            ? LUNASHELL::palette::kStatusWarning
            : LUNASHELL::palette::kStatusReady;
    }
    if (wifi.connect_requested || wifi.reconnect_pending || wifi.connected) {
        return LUNASHELL::palette::kStatusWarning;
    }
    return LUNASHELL::palette::kStatusMuted;
}
#endif

struct ClockVisual {
    std::string text;
    std::string_view color;
};

#if CONFIG_LUNASHELL_BUILD_ENABLE_RTC
// clock: Canonical clock observation formatted without reading the live clock.
// Exceptions: std::bad_alloc if the returned text cannot allocate.
ClockVisual clock_visual(const LUNASHELL::ClockSnapshot &clock)
{
    if (!LUNASHELL::clock_observation_current(clock)) {
        return {
            .text = clock.data_stale ? "TIME STALE" : "TIME UNAVAILABLE",
            .color = component_color(clock),
        };
    }

    const std::time_t epoch = static_cast<std::time_t>(clock.utc_epoch_seconds);
    std::tm local{};
    if (localtime_r(&epoch, &local) == nullptr ||
        local.tm_wday < 0 || local.tm_wday >= 7 ||
        local.tm_mon < 0 || local.tm_mon >= 12) {
        return {
            .text = "TIME UNAVAILABLE",
            .color = LUNASHELL::palette::kStatusFault,
        };
    }

    static constexpr std::array<std::string_view, 7> kWeekdays = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
    };
    static constexpr std::array<std::string_view, 12> kMonths = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
    };
    std::array<char, 32> text{};
    const int written = std::snprintf(
        text.data(),
        text.size(),
        "%.*s %02d %.*s %04d %02d:%02d",
        static_cast<int>(kWeekdays[local.tm_wday].size()),
        kWeekdays[local.tm_wday].data(),
        local.tm_mday,
        static_cast<int>(kMonths[local.tm_mon].size()),
        kMonths[local.tm_mon].data(),
        local.tm_year + 1900,
        local.tm_hour,
        local.tm_min
    );
    if (written <= 0 || static_cast<size_t>(written) >= text.size()) {
        return {
            .text = "TIME UNAVAILABLE",
            .color = LUNASHELL::palette::kStatusFault,
        };
    }
    return {
        .text = std::string(text.data(), static_cast<size_t>(written)),
        .color = LUNASHELL::palette::kStatusReady,
    };
}
#endif

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

// No parameters: discard the last rendered state after GUI remount.
// Exceptions: none.
void LUNASHELLStatusBar::invalidate()
{
    visual_state_.reset();
}

// gui: Mounted Brookesia runtime receiving one coherent binding batch.
// snapshot: Canonical device state; no hardware owner is queried here.
// Exceptions: std::bad_alloc if the binding batch or display text cannot allocate.
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

#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
    const std::string_view wifi_status_color =
        wifi_color(snapshot.radios.wifi);
#else
    const std::string_view wifi_status_color = palette::kStatusMuted;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_RTC
    const ClockVisual clock = clock_visual(snapshot.clock);
#else
    const ClockVisual clock{
        .text = {},
        .color = palette::kStatusMuted,
    };
#endif
    const VisualState next_state{
        .usb_color = usb_color,
        .battery_color = battery_color,
        .sd_color = sd_color,
        .wifi_color = wifi_status_color,
        .clock_color = clock.color,
        .battery_icon = battery_icon(snapshot.battery),
        .battery_text = battery_text(snapshot.battery),
        .clock_text = clock.text,
        .battery_charging = battery_charging(snapshot.battery),
        .wifi_visible = kWifiVisible,
        .clock_visible = kClockVisible,
    };
    if (visual_state_.has_value() && *visual_state_ == next_state) {
        return {};
    }

    std::vector<esp_brookesia::gui::BindingValueUpdate> updates;
    updates.reserve(24);

    add_indicator(updates, "usb", next_state.usb_color);
    add_indicator(updates, "battery", next_state.battery_color);
    add_indicator(updates, "sd", next_state.sd_color);
    add_indicator(updates, "wifi", next_state.wifi_color);

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
    add_binding(
        updates,
        std::string(kStatusRoot) + "/battery/value",
        "text",
        next_state.battery_text
    );
    add_binding(
        updates,
        std::string(kStatusRoot) + "/battery/value",
        "color",
        next_state.battery_color
    );
    add_binding(
        updates,
        std::string(kStatusRoot) + "/date_time",
        "text",
        next_state.clock_text
    );
    add_binding(
        updates,
        std::string(kStatusRoot) + "/date_time",
        "color",
        next_state.clock_color
    );
    add_binding(
        updates,
        std::string(kStatusRoot) + "/date_time",
        "hidden",
        next_state.clock_visible ? "false" : "true"
    );
    add_binding(
        updates,
        std::string(kStatusRoot) + "/wifi",
        "hidden",
        next_state.wifi_visible ? "false" : "true"
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
