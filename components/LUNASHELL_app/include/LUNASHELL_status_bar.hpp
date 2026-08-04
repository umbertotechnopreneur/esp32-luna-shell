#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "brookesia/system_core.hpp"
#include "LUNASHELL_device_snapshot.hpp"

namespace LUNASHELL {

// Owns the complete snapshot-to-visual policy for the persistent top ribbon.
class LUNASHELLStatusBar final {
public:
    void invalidate();
    std::expected<void, std::string> update(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        const DeviceSnapshot &snapshot
    );

private:
    struct VisualState {
        std::string_view usb_color;
        std::string_view battery_color;
        std::string_view ble_color;
        std::string_view wifi_color;
        std::string_view gps_color;
        std::string_view sd_color;
        std::string_view battery_icon;
        bool battery_charging;

        bool operator==(const VisualState &) const = default;
    };

    std::optional<VisualState> visual_state_;
};

}  // namespace LUNASHELL
