#pragma once

#include <array>
#include <cstdint>
#include "CODY_scene.hpp"

namespace CODY {

enum class ScreensaverPhase : uint8_t { Disabled, Waiting, Visible };

// Device-owned overlay state; animation never reports activity to the power owner.
struct ScreensaverModel {
    ScreensaverPhase phase = ScreensaverPhase::Disabled;
    int64_t eligible_since_us = -1;
    int64_t started_at_us = 0;
    uint32_t frame = 0;
    esp_err_t settings_error = ESP_OK;
};

inline constexpr int64_t kScreensaverDelayUs = 15000000;
inline constexpr int64_t kScreensaverFrameUs = 40000;

// Small persistent raster cache, owned by the display worker in PSRAM.
struct ScreensaverPresentation {
    static constexpr uint16_t kGrid = 65;
    std::array<uint16_t, kGrid * kGrid> pixels{};
    uint32_t frame = 0;
    uint32_t revision = 0;
    bool valid = false;
};

// model: Retained device state, including the current optional-settings error.
// eligible: Only an unobstructed dashboard can enter the saver.
// last_interaction_us: Actual touch, shake or lifecycle activity timestamp.
// now_us: Current monotonic time, independent of the animated view.
// Exception: Retains genuine snapshot/preference errors and disables this optional overlay.
void update_screensaver(ScreensaverModel &model, bool eligible,
    int64_t last_interaction_us, int64_t now_us);

// model: Retained overlay state to dismiss without changing the dashboard route.
// now_us: Actual interaction or lifecycle timestamp; never called for animation frames.
void dismiss_screensaver(ScreensaverModel &model, int64_t now_us);

// model: Immutable active animation state from the device model.
// presentation: Persistent grid retained until the full display frame retires.
// content: Receives the borrowed globe raster and fixed damage bounds.
// Exception: Returns invalid phase or exhausted revision; no dynamic allocation.
esp_err_t prepare_screensaver(const ScreensaverModel &model,
    ScreensaverPresentation &presentation, SceneContent &content);

} // namespace CODY
