#pragma once

#include <array>
#include <cstdint>

#include "CODY_battery_alerts.hpp"
#include "CODY_codex_audio.hpp"
#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {

inline constexpr int64_t kVoiceResultDwellUs = 3000000;

enum class DeviceFeedback : uint8_t { None, OptionsOpened, TimerStarted };

// Recent request identities prevent navigation, refresh and reconnect from replaying notices.
struct DeviceEventSoundRecord {
    char request_id[33]{};
    int64_t reminder_at_us = 0;
    uint16_t announced_kinds = 0;
    bool delivery_announced = false;
};

// One current event may wait briefly for the shared speaker.
struct DeviceEventSound {
    char request_id[33]{};
    LUNASHELL::CodexEventState state = LUNASHELL::CodexEventState::None;
    CodexAudioCue cue = CodexAudioCue::None;
    int64_t deadline_us = 0;
    uint32_t request_sequence = 0;
};

// Retained by DeviceModel, never by a scene or a second polling task.
// The large event observation stays off the display worker's nested call stack.
struct DeviceEffectsModel {
    LUNASHELL::CodexEventSnapshot event{};
    LUNASHELL::PairingObservation pairing{};
    std::array<DeviceEventSoundRecord, 16> recent_events{};
    DeviceEventSound event_sound{};
    CodexAudioStatus playback{};
    DeviceFeedback feedback = DeviceFeedback::None;
    esp_err_t boot_audio_error = ESP_ERR_INVALID_STATE;
    esp_err_t tap_audio_error = ESP_OK;
    esp_err_t preparation_error = ESP_ERR_INVALID_STATE;
    esp_err_t event_error = ESP_ERR_INVALID_STATE;
    esp_err_t pairing_error = ESP_ERR_INVALID_STATE;
    esp_err_t last_error = ESP_OK;
    int64_t last_tick_us = 0;
    int64_t connection_lost_at_us = 0;
    int64_t wifi_edge_since_us = 0;
    int64_t voice_session_us = 0;
    uint32_t battery_sequence = 0;
    uint32_t pairing_session = 0;
    uint32_t voice_warning_sequence = 0;
    uint8_t next_event_slot = 0;
    bool prepared = false;
    bool boot_cue_requested = false;
    bool connection_known = false;
    bool connected = false;
    bool wifi_ready = false;
    bool wifi_result_known = false;
    bool event_online = false;
    bool pairing_terminal_announced = false;
    bool voice_start_announced = false;
    bool voice_send_announced = false;
    bool voice_close_announced = false;
};
static_assert(sizeof(DeviceEffectsModel) <= 4096,
    "Device effects retain bounded observations and identities in the device model");

// model: Persistent effects state, initialized after the audio driver and sound preference.
// Exception: Returns the original shared audio-worker preparation error; no silent fallback.
esp_err_t prepare_device_effects(DeviceEffectsModel &model);

// model: Records the one startup cue admitted with the first visible animation frame.
// Exception: Retains original asset or shared audio-owner errors as optional feedback.
esp_err_t play_device_boot_cue(DeviceEffectsModel &model);

// model: Retains optional admission errors from the shared audio owner.
// voice: Latest canonical voice observation, or null when voice is excluded.
// No replay is queued when the contact occurs during capture or playback.
void play_device_tap_cue(DeviceEffectsModel &model, const LUNASHELL::VoiceSnapshot *voice);

// model: Sole owner of consumed event identities and observed playback disposition.
// status: Already-aged connection snapshot shared with the device model.
// status_valid: True only after the canonical status projection was read successfully.
// wifi: Independently acquired hardware Wi-Fi observation.
// wifi_valid: Read success of the hardware projection, independent of app presence.
// battery: Current canonical threshold notice; its sequence admits at most one sound.
// voice: Optional current voice snapshot, null when the feature is absent.
// now_us: Monotonic runtime time, never a wall clock or a scene animation clock.
// startup_wifi_offline: The saved-network startup wait settled without a local Wi-Fi lease.
// Exception: Returns the first original projection/admission failure and retains its cause.
esp_err_t update_device_effects(DeviceEffectsModel &model,
    const LUNASHELL::CodexPresenceStatus &status, bool status_valid,
    const LUNASHELL::WifiSnapshot &wifi, bool wifi_valid,
    const BatteryAlertNotice &battery, const LUNASHELL::VoiceSnapshot *voice, int64_t now_us,
    bool startup_wifi_offline = false);

// model: Last successfully observed canonical event and its current source health.
// Attention is presentation-independent; hidden pages cannot silence a pending question.
bool device_effects_require_attention(const DeviceEffectsModel &model);

} // namespace CODY
