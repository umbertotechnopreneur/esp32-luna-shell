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
    // No parameters: discard the last rendered state after GUI remount.
    // Exceptions: none.
    void invalidate();

    // gui: Mounted Brookesia runtime receiving one coherent binding batch.
    // snapshot: Canonical device state; no hardware owner is queried here.
    // Exceptions: std::bad_alloc if the binding batch or display text cannot allocate.
    std::expected<void, std::string> update(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        const DeviceSnapshot &snapshot
    );

private:
    struct VisualState {
        std::string_view usb_color;
        std::string_view battery_color;
        std::string_view sd_color;
        std::string_view wifi_color;
        std::string_view clock_color;
        std::string_view battery_icon;
        std::string battery_text;
        std::string clock_text;
        bool battery_charging;
        bool wifi_visible;
        bool clock_visible;

        bool operator==(const VisualState &) const = default;
    };

    std::optional<VisualState> visual_state_;
};

}  // namespace LUNASHELL
