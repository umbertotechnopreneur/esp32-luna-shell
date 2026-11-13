#include "CODY_settings_view.hpp"
#include "sdkconfig.h"

namespace CODY {

// identity: One stable preference from a visible row or admitted worker command.
SettingsDomains preference_domains(PreferenceId identity)
{
    switch (identity) {
    case PreferenceId::IdleMode:
    case PreferenceId::IdleDelay: return settings_domain(SettingsDomain::Idle);
    case PreferenceId::EventCody: return settings_domain(SettingsDomain::Events);
    case PreferenceId::Parallax: return settings_domain(SettingsDomain::Parallax);
    case PreferenceId::CharacterTheme: return settings_domain(SettingsDomain::Theme);
    case PreferenceId::QuietEnabled:
    case PreferenceId::QuietStart:
    case PreferenceId::QuietEnd: return settings_domain(SettingsDomain::Clock);
    default: return settings_domain(SettingsDomain::Options);
    }
}

// view: Owner-provided bounded scratch; invalidated domains never expose retained snapshot bytes.
// domains: Canonical sources needed by this presentation, read once each without hardware acquisition.
// Exception: Original errors are retained independently, rather than hiding partial availability.
void capture_settings_view(SettingsView &view, SettingsDomains domains)
{
    // Do not materialize a composite temporary on the display/worker stack.
    view.options_error = view.idle_error = view.events_error = view.parallax_error =
        view.theme_error = view.clock_error = ESP_ERR_INVALID_STATE;
    auto &source = LUNASHELL::DeviceSnapshotService::instance();
    if (domains & settings_domain(SettingsDomain::Options)) view.options_error = source.readCodyOptionsObservation(view.options);
    if (domains & settings_domain(SettingsDomain::Idle)) view.idle_error = source.readCodyIdleObservation(view.idle);
    if (domains & settings_domain(SettingsDomain::Events)) view.events_error = source.readCodyEventVisualObservation(view.events);
    if (domains & settings_domain(SettingsDomain::Parallax)) view.parallax_error = source.readCodyParallaxObservation(view.parallax);
    if (domains & settings_domain(SettingsDomain::Theme)) view.theme_error = source.readCodyCharacterThemeObservation(view.theme);
    if (domains & settings_domain(SettingsDomain::Clock)) view.clock_error = source.readClockObservation(view.clock);
}

// view: One immutable capture; the clock's changing time is not part of this settings projection.
QuietHoursPresentation project_quiet_hours(const SettingsView &view)
{
    const auto &clock = view.clock;
    QuietHoursPresentation result{};
    result.error = view.clock_error;
    if (result.error == ESP_OK && !clock.preferences_valid)
        result.error = clock.last_error != ESP_OK ? clock.last_error : ESP_ERR_INVALID_STATE;
    if (result.error == ESP_OK && (clock.quiet_start_minute >= 1440U || clock.quiet_end_minute >= 1440U))
        result.error = ESP_ERR_INVALID_RESPONSE;
    result.available = result.error == ESP_OK;
    if (result.available) {
        result.enabled = clock.quiet_hours_enabled;
        result.start_minute = clock.quiet_start_minute;
        result.end_minute = clock.quiet_end_minute;
    }
    return result;
}

// identity: Stable setting identity, independent of row order.
// view: Captured canonical inputs. No mutable owner or snapshot service is consulted here.
PreferencePresentation project_settings_preference(PreferenceId identity, const SettingsView &view)
{
    const auto &preferences = view.options.preferences;
    const auto &orientation = view.options.orientation;
    PreferencePresentation result{false, false, 0, view.options_error};
    const auto use_switch = [&](bool valid, bool enabled, esp_err_t error) {
        result.error = view.options_error != ESP_OK ? view.options_error : error;
        result.available = result.error == ESP_OK && valid;
        result.active = enabled;
    };
    switch (identity) {
    case PreferenceId::SynthEnabled:
        use_switch(preferences.synth_voice_valid, preferences.synth_voice_enabled, preferences.synth_voice_error); break;
    case PreferenceId::SynthVolume:
    case PreferenceId::SynthVoice:
        result.error = ESP_ERR_NOT_SUPPORTED; break;
    case PreferenceId::PlaybackVolume:
        use_switch(preferences.playback_volume_valid, false, preferences.playback_volume_error);
        result.value = preferences.playback_volume_percent; break;
    case PreferenceId::MicrophoneGain:
        use_switch(preferences.microphone_gain_valid, false, preferences.microphone_gain_error);
        result.value = preferences.microphone_gain_db; break;
    case PreferenceId::Screensaver:
        use_switch(preferences.screensaver_valid, preferences.screensaver_enabled, preferences.screensaver_error); break;
    case PreferenceId::AssistantQuotaRing:
        use_switch(preferences.assistant_quota_ring_valid, preferences.assistant_quota_ring_enabled, preferences.assistant_quota_ring_error); break;
    case PreferenceId::BatteryRing:
        use_switch(preferences.battery_ring_valid, preferences.battery_ring_enabled, preferences.battery_ring_error); break;
    case PreferenceId::AutoTime:
        use_switch(preferences.auto_time_valid, preferences.auto_time_enabled, preferences.auto_time_error); break;
    case PreferenceId::Dictation:
        use_switch(preferences.dictation_valid, preferences.dictation_enabled, preferences.dictation_error); break;
    case PreferenceId::Sofia:
        use_switch(preferences.sofia_valid, preferences.sofia_enabled, preferences.sofia_error); break;
    case PreferenceId::CounterMode:
        use_switch(preferences.counter_mode_valid, preferences.counter_mode_enabled, preferences.counter_mode_error); break;
    case PreferenceId::WeeklyAlarm:
        use_switch(preferences.weekly_alarm_valid, preferences.weekly_alarm_enabled, preferences.weekly_alarm_error); break;
    case PreferenceId::IdleMode:
    case PreferenceId::IdleDelay:
        result.error = view.idle_error != ESP_OK ? view.idle_error : view.idle.last_error;
        result.available = result.error == ESP_OK && view.idle.valid;
        result.active = view.idle.show_muse;
        result.value = view.idle.delay_seconds;
        break;
    case PreferenceId::EventCody:
        result.error = view.events_error != ESP_OK ? view.events_error : view.events.last_error;
        result.available = result.error == ESP_OK && view.events.valid;
        result.active = view.events.enabled;
        break;
    case PreferenceId::Parallax:
        result.error = view.parallax_error != ESP_OK ? view.parallax_error : view.parallax.last_error;
        if (result.error == ESP_OK && view.parallax.level > 3U) result.error = ESP_ERR_INVALID_RESPONSE;
        result.available = result.error == ESP_OK && view.parallax.valid;
        result.active = view.parallax.level != 0;
        result.value = view.parallax.level;
        break;
    case PreferenceId::CharacterTheme:
        result.error = view.theme_error != ESP_OK ? view.theme_error : view.theme.last_error;
        if (result.error == ESP_OK && view.theme.theme >= 8U) result.error = ESP_ERR_INVALID_RESPONSE;
        result.available = result.error == ESP_OK && view.theme.valid;
        result.value = view.theme.theme;
        result.active = view.theme.theme != 0;
        break;
    case PreferenceId::ShakeWake:
    case PreferenceId::Sounds:
        result.error = view.options_error != ESP_OK ? view.options_error : preferences.last_error;
        result.available = result.error == ESP_OK && preferences.data_valid && !preferences.data_stale;
        result.active = identity == PreferenceId::ShakeWake ? preferences.shake_to_wake : preferences.sounds_enabled;
#if !CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
        if (identity == PreferenceId::ShakeWake) { result.available = false; result.error = ESP_ERR_NOT_SUPPORTED; }
#endif
#if !CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO || !CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
        if (identity == PreferenceId::Sounds) { result.available = false; result.error = ESP_ERR_NOT_SUPPORTED; }
#endif
        break;
    case PreferenceId::AutoRotate:
    case PreferenceId::Orientation:
        result.error = view.options_error != ESP_OK ? view.options_error : orientation.last_error;
        result.available = result.error == ESP_OK && orientation.data_valid && !orientation.data_stale && !orientation.rotation_pending;
        result.active = orientation.auto_rotation_enabled;
        if (identity == PreferenceId::Orientation && !result.active) result.value = orientation.rendering_rotation_degrees;
        break;
    case PreferenceId::Brightness:
        result.available = view.options_error == ESP_OK && view.options.brightness_valid &&
            view.options.brightness_percent >= 10U && view.options.brightness_percent <= 100U;
        result.value = view.options.brightness_percent;
        break;
    case PreferenceId::QuietEnabled:
    case PreferenceId::QuietStart:
    case PreferenceId::QuietEnd: {
        const auto quiet = project_quiet_hours(view);
        result.available = quiet.available;
        result.error = quiet.error;
        result.active = quiet.enabled;
        result.value = identity == PreferenceId::QuietStart ? quiet.start_minute : quiet.end_minute;
        break;
    }
    }
    if (!result.available) {
        if (result.error == ESP_OK) result.error = ESP_ERR_INVALID_STATE;
        result.active = false;
        result.value = 0;
    }
    return result;
}

// left: Last rendered value and availability.
// right: Newly captured projection; no source timestamps participate in redraw decisions.
bool same_preference_presentation(const PreferencePresentation &left, const PreferencePresentation &right)
{
    return left.available == right.available && left.active == right.active && left.value == right.value && left.error == right.error;
}

// left: Last rendered quiet-hours preferences.
// right: New canonical preferences, excluding local editor drafts.
bool same_quiet_hours_presentation(const QuietHoursPresentation &left, const QuietHoursPresentation &right)
{
    return left.available == right.available && left.enabled == right.enabled && left.start_minute == right.start_minute &&
        left.end_minute == right.end_minute && left.error == right.error;
}

// preference: Stable identity of the write about to be attempted.
// value: Explicit requested value.
// view: Captured pre-write state, independent from the display's retained values.
bool preference_would_change(PreferenceId preference, uint32_t value, const SettingsView &view)
{
    const auto previous = project_settings_preference(preference, view);
    if (!previous.available || (preference == PreferenceId::Orientation && previous.active)) return false;
    const bool numeric = preference == PreferenceId::Brightness || preference == PreferenceId::Orientation ||
        preference == PreferenceId::IdleDelay || preference == PreferenceId::Parallax || preference == PreferenceId::CharacterTheme ||
        preference == PreferenceId::QuietStart || preference == PreferenceId::QuietEnd ||
        preference == PreferenceId::PlaybackVolume || preference == PreferenceId::MicrophoneGain;
    return value != (numeric ? previous.value : static_cast<uint32_t>(previous.active));
}

} // namespace CODY
