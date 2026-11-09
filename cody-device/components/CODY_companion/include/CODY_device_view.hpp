#pragma once

#include <array>

#include "CODY_account_dashboard.hpp"
#include "CODY_reset_countdown_screen.hpp"
#include "CODY_token_history.hpp"
#include "CODY_widget_state.hpp"
#include "CODY_device_model.hpp"

namespace CODY {

enum class DeviceMessageLayout : uint8_t { Compact, Dashboard, Voice, VoiceCountdown, VoiceFailure };

inline constexpr CODY2D::Rect kVoiceCountdownFace{185, 176, 96, 74};

// The display owner retains prepared text and controls until its frame retires.
struct PlainView {
    std::array<std::array<char, 32>, 6> text{};
    std::array<CODY2D::TextLine, 4> captions{};
    std::array<CODY2D::CapsuleControl, 2> actions{};
    ResetCountdownPresentation::Tile voice_tile{};
    uint32_t revision = 0;
    uint16_t voice_flip_progress = kResetCountdownFlipComplete;
    DeviceMessageLayout layout = DeviceMessageLayout::Compact;
    bool valid = false;
};

// A flat Device dashboard uses its own prepared glyphs and one optional action.
struct DeviceOverviewView {
    struct Row {
        std::array<char, 16> value{};
        CODY2D::TextLine label{};
        CODY2D::TextLine status{};
        CODY2D::ActionIcon icon = CODY2D::ActionIcon::None;
        uint16_t color = 0;
    };
    std::array<Row, 3> rows{};
    std::array<char, 30> chat{};
    CODY2D::TextLine heading{};
    CODY2D::TextLine chat_line{};
    CODY2D::CapsuleControl record{};
    uint32_t revision = 0;
    int16_t battery_level = -1;
    bool valid = false;
};
static_assert(sizeof(DeviceOverviewView) <= 640, "Keep the flat dashboard preparation bounded");

struct DeviceChrome {
    std::array<char, 32> battery_text{};
    std::array<char, 32> saved_text{};
    CODY2D::CapsuleControl battery{};
    CODY2D::TextLine connection{};
    const char *connection_text = nullptr;
    int64_t saved_epoch = 0;
    uint32_t connection_revision = 0;
    uint32_t battery_sequence = 0;
};

struct DeviceView {
    ScreensaverPresentation screensaver{};
    PlainView plain{};
    DeviceOverviewView overview{};
    DeviceChrome chrome{};
    ResetCountdownScreen countdown{};
    AccountDashboardPresentation account{};
    TokenHistoryPresentation history{};
    SystemInfoPresentation info{};
};

struct DeviceFrameContent {
    SceneContent content{};
};

// Model values are projected to stable glyph buffers; chrome has no interaction state.
esp_err_t prepare_device_chrome(DeviceChrome &chrome, const DeviceDashboardData &data,
    bool show_connection, const BatteryAlertNotice &battery, int64_t saved_epoch = 0);
esp_err_t draw_device_chrome(const DeviceChrome &chrome, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column);

// All text is bounded and copied. Empty optional strings omit their line or action.
esp_err_t prepare_device_message(DeviceView &view, DeviceFrameContent &frame,
    const char *heading, const char *status, const char *detail = "",
    const char *first = nullptr, const char *second = nullptr, const char *extra = "",
    DeviceMessageLayout layout = DeviceMessageLayout::Compact);

// view: Persistent captions and visible controls for a live voucher warning.
// frame: Receives the bounded warning with the exact UTC time.
// model: Canonical live snapshot and selected warning expiry.
// Exception: Preserves original UTC conversion, formatting and native preparation errors.
esp_err_t prepare_device_voucher_warning(DeviceView &view, DeviceFrameContent &frame, const DeviceModel &model);

// Returns 1 for the first action, 2 for the second, or 0 outside enabled controls.
int device_action_at(const DeviceView &view, CODY2D::Point point);

// Prepares the selected page from its read-only model projection.
esp_err_t prepare_device_dashboard(DeviceView &view, DeviceFrameContent &frame,
    WidgetScreen screen, TokenHistoryInterval interval, const DeviceDashboardData &data, int64_t now_us,
    const VoucherList &vouchers = {});

// view: Persistent glyph and geometry owner for System, Memory and Firmware.
// frame: Receives the native central content without controls or the shared perimeter.
// model: Read-only selected Info page, canonical diagnostics and immutable build identity.
// Exception: Preserves original bounded-caption or native preparation errors.
esp_err_t prepare_device_info(DeviceView &view, DeviceFrameContent &frame, const DeviceModel &model);

// Presents the owner-published voice phase. Session admission remains with the runtime.
esp_err_t prepare_device_voice(DeviceView &view, DeviceFrameContent &frame,
    const LUNASHELL::VoiceInputObservation &voice, int64_t now_us);

// Presents measured processor and heap data with explicit unavailable fields.
esp_err_t prepare_device_diagnostics(DeviceView &view, DeviceFrameContent &frame,
    const LUNASHELL::DiagnosticsObservation &observed, bool available);

} // namespace CODY
