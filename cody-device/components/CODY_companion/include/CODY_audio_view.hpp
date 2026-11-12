#pragma once

#include <array>

#include "CODY2D_controls.hpp"
#include "CODY_scene.hpp"
#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {

enum class MicrophoneLevelState : uint8_t { Unavailable, Waiting, Stale, Failed, Current };
enum class AudioAdjustment : uint8_t { None = 0, VolumeDown = 1, VolumeUp = 2, GainDown = 3, GainUp = 4 };

// Display-owned projection only; acquisition and freshness belong to the snapshot owner.
struct AudioView {
    struct Level {
        MicrophoneLevelState state = MicrophoneLevelState::Unavailable;
        esp_err_t error = ESP_OK;
        uint16_t mean_width = 0;
        uint16_t peak_offset = 0;
        uint8_t mean_percent = 0;
        uint8_t peak_percent = 0;
    };
    // The first level is the measured speaker mean; the next two are microphone channels.
    std::array<Level, 3> levels{};
    std::array<CODY2D::TextLine, 8> captions{};
    std::array<std::array<char, 20>, 3> readouts{};
    std::array<std::array<char, 20>, 3> labels{};
    std::array<char, 24> legend{};
    CODY2D::CapsuleControl close{};
    const CODY2D::MonoFont *title_font = nullptr;
    const CODY2D::MonoFont *body_font = nullptr;
    uint32_t revision = 0;
    uint8_t playback_volume = 0;
    uint8_t microphone_gain = 0;
    uint8_t enabled_controls = 0;
    bool playback_volume_valid = false;
    bool microphone_gain_valid = false;
    bool settings_pending = false;
    esp_err_t settings_error = ESP_OK;
    bool valid = false;
};
static_assert(sizeof(AudioView) <= 768, "Audio presentation uses bounded display storage");

// audio: Canonical audio projection already aged by DeviceSnapshotService.
// read_error: Original snapshot or microphone-demand admission error.
// playback_error: Original audio snapshot read error, independent of microphone demand.
// preferences: Canonical readback-confirmed audio settings.
// preferences_error: Original settings projection read error.
// settings_pending: A previously admitted audio settings write is in flight.
// settings_error: Original result of the latest audio settings operation.
// now_us: Monotonic time obtained after reading the canonical observation.
// title_font: Immutable native heading font.
// body_font: Immutable native status and control font.
// view: Persistent display-owned storage retained until frame retirement.
// content: Receives the central renderer, separate from lateral Back.
// Exception: Returns the original native layout failure; unavailable samples remain visible states.
esp_err_t prepare_audio_view(const LUNASHELL::AudioSnapshot &audio, esp_err_t read_error, esp_err_t playback_error,
    const LUNASHELL::CodyPreferencesSnapshot &preferences, esp_err_t preferences_error,
    bool settings_pending, esp_err_t settings_error,
    int64_t now_us, const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    AudioView &view, SceneContent &content);

// view: Last completely presented Audio page.
// point: Logical touch position in the same orientation as the visible control.
bool audio_close_at(const AudioView &view, CODY2D::Point point);

// view: Last completely presented Audio page with enabled controls.
// point: Logical touch position in the retired orientation.
AudioAdjustment audio_adjustment_at(const AudioView &view, CODY2D::Point point);

} // namespace CODY
