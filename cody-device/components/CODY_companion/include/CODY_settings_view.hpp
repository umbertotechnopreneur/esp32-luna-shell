#pragma once

#include "CODY_settings.hpp"
#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {

// A bounded read projection. It never owns or writes the confirmed preferences.
enum class SettingsDomain : uint8_t { Options = 1, Idle = 2, Events = 4, Parallax = 8, Theme = 16, Clock = 32 };
using SettingsDomains = uint8_t;

// domain: One canonical source required by a visible setting.
constexpr SettingsDomains settings_domain(SettingsDomain domain) { return static_cast<SettingsDomains>(domain); }

struct SettingsView {
    LUNASHELL::CodyOptionsObservation options{};
    LUNASHELL::CodyIdleObservation idle{};
    LUNASHELL::CodyEventVisualObservation events{};
    LUNASHELL::CodyParallaxObservation parallax{};
    LUNASHELL::CodyCharacterThemeObservation theme{};
    LUNASHELL::ClockSnapshot clock{};
    esp_err_t options_error = ESP_ERR_INVALID_STATE;
    esp_err_t idle_error = ESP_ERR_INVALID_STATE;
    esp_err_t events_error = ESP_ERR_INVALID_STATE;
    esp_err_t parallax_error = ESP_ERR_INVALID_STATE;
    esp_err_t theme_error = ESP_ERR_INVALID_STATE;
    esp_err_t clock_error = ESP_ERR_INVALID_STATE;
};
static_assert(sizeof(SettingsView) <= 1024U, "Settings capture must remain bounded and owner-allocated");

struct PreferencePresentation {
    bool available = false;
    bool active = false;
    uint32_t value = 0;
    esp_err_t error = ESP_ERR_INVALID_STATE;
};

struct QuietHoursPresentation {
    bool available = false;
    bool enabled = false;
    uint16_t start_minute = 0;
    uint16_t end_minute = 0;
    esp_err_t error = ESP_ERR_INVALID_STATE;
};

// A local editor draft; confirmed values always come from SettingsView.
struct QuietHoursDraft {
    uint16_t minute = 0;
    bool editing_start = true;
};

// identity: Stable preference identity; indices are never interpreted as settings.
SettingsDomains preference_domains(PreferenceId identity);
// view: Caller-owned scratch, separate for the display and settings worker.
// domains: Sources required by the current operation; each is captured at most once.
// Exception: Stores original read errors per domain; unavailable domains remain unavailable.
void capture_settings_view(SettingsView &view, SettingsDomains domains);
// identity: Stable preference requested by the presenter.
// view: Immutable captured inputs; this projection performs no service reads.
PreferencePresentation project_settings_preference(PreferenceId identity, const SettingsView &view);
// view: Immutable canonical clock preferences captured with the other visible inputs.
QuietHoursPresentation project_quiet_hours(const SettingsView &view);
// left: Last successfully prepared projection.
// right: Newly captured projection, including its original error.
bool same_preference_presentation(const PreferencePresentation &left, const PreferencePresentation &right);
// left: Last presented quiet-hours values.
// right: Newly captured quiet-hours values; wall-clock ticks are intentionally excluded.
bool same_quiet_hours_presentation(const QuietHoursPresentation &left, const QuietHoursPresentation &right);
// preference: Exact setting selected by the user.
// value: Exact requested value.
// view: Canonical state captured immediately before the worker writes.
bool preference_would_change(PreferenceId preference, uint32_t value, const SettingsView &view);

} // namespace CODY
