#pragma once

#include <array>

#include "CODY_account_dashboard.hpp"
#include "CODY_reset_countdown_screen.hpp"
#include "CODY_token_history.hpp"
#include "CODY_widget_state.hpp"
#include "CODY_device_model.hpp"

namespace CODY {

// The display owner retains prepared text and controls until its frame retires.
struct PlainView {
    std::array<std::array<char, 32>, 6> text{};
    std::array<CODY2D::TextLine, 4> captions{};
    std::array<CODY2D::CapsuleControl, 2> actions{};
    uint32_t revision = 0;
    bool valid = false;
};

struct DeviceChrome {
    std::array<char, 32> battery_text{};
    CODY2D::CapsuleControl battery{};
    CODY2D::TextLine connection{};
    const char *connection_text = nullptr;
    uint32_t battery_sequence = 0;
};

struct DeviceView {
    PlainView plain{};
    DeviceChrome chrome{};
    ResetCountdownScreen countdown{};
    AccountDashboardPresentation account{};
    TokenHistoryPresentation history{};
};

struct DeviceFrameContent {
    SceneContent content{};
};

// Model values are projected to stable glyph buffers; chrome has no interaction state.
esp_err_t prepare_device_chrome(DeviceChrome &chrome, const DeviceDashboardData &data,
    bool show_connection, const BatteryAlertNotice &battery);
esp_err_t draw_device_chrome(const DeviceChrome &chrome, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column);

// All text is bounded and copied. Empty optional strings omit their line or action.
esp_err_t prepare_device_message(DeviceView &view, DeviceFrameContent &frame,
    const char *heading, const char *status, const char *detail = "",
    const char *first = nullptr, const char *second = nullptr, const char *extra = "");

// Returns 1 for the first action, 2 for the second, or 0 outside enabled controls.
int device_action_at(const DeviceView &view, CODY2D::Point point);

// Prepares the selected page from its read-only model projection.
esp_err_t prepare_device_dashboard(DeviceView &view, DeviceFrameContent &frame,
    WidgetScreen screen, TokenHistoryInterval interval, const DeviceDashboardData &data, int64_t now_us);

// Presents the owner-published voice phase. Session admission remains with the runtime.
esp_err_t prepare_device_voice(DeviceView &view, DeviceFrameContent &frame,
    const LUNASHELL::VoiceInputObservation &voice);

// Presents measured processor and heap data with explicit unavailable fields.
esp_err_t prepare_device_diagnostics(DeviceView &view, DeviceFrameContent &frame,
    const LUNASHELL::DiagnosticsObservation &observed, bool available);

} // namespace CODY
