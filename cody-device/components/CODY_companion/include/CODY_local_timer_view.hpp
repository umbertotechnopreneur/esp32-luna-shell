#pragma once

#include <array>

#include "CODY2D_controls.hpp"
#include "CODY_local_timer.hpp"
#include "CODY_scene.hpp"

namespace CODY {

enum class LocalTimerControl : uint8_t { None, Decrease, Increase, CycleSound, Start, Cancel, Dismiss };

// The display owner retains caption storage and controls through frame retirement.
struct LocalTimerView {
    struct Action {
        LocalTimerControl id = LocalTimerControl::None;
        CODY2D::CapsuleControl control{};
    };

    std::array<CODY2D::TextLine, 4> captions{};
    std::array<Action, 4> actions{};
    char readout[8]{};
    const CODY2D::MonoFont *title_font = nullptr;
    const CODY2D::MonoFont *body_font = nullptr;
    LocalTimerPhase phase = LocalTimerPhase::Idle;
    LocalTimerSound sound = LocalTimerSound::Bell;
    LocalTimerAlarm alarm = LocalTimerAlarm::None;
    uint32_t seconds = 0;
    uint32_t revision = 0;
    bool valid = false;
};
static_assert(sizeof(LocalTimerView) <= 640, "Local timer presentation uses bounded display storage");

// model: Canonical timer state owned by the caller.
// now_us: Current monotonic time from the timer owner.
// title_font: Immutable headline and timer readout font.
// body_font: Immutable status and control caption font.
// view: Persistent display-owned presentation storage.
// content: Receives the central timer renderer, separate from lateral Back.
// Exception: Returns an invalid timer state or the original native layout failure.
esp_err_t prepare_local_timer_view(const LocalTimerModel &model, int64_t now_us,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    LocalTimerView &view, SceneContent &content);

// view: Last fully presented timer state, as verified by the caller.
// point: Logical touch position after the display owner's orientation transform.
LocalTimerControl local_timer_control_at(const LocalTimerView &view, CODY2D::Point point);

} // namespace CODY
