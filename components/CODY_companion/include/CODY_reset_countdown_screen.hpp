#pragma once

#include "CODY_reset_countdown.hpp"

namespace LUNASHELL {
struct ClockLocalCalendar;
struct ClockSnapshot;
struct CodexWeeklyLimitSnapshot;
}

namespace CODY {

// The display owner keeps this instance through retirement of its drawn frame.
class ResetCountdownScreen {
public:
    // allowance: Aged authenticated App Server observation from DeviceSnapshotService.
    // clock: Current time observation from DeviceSnapshotService.
    // animation_time_us: Monotonic display clock for the flip and date alternation.
    // title_font: Native font used for digits and the screen title.
    // body_font: Native monospaced font used for the date and labels.
    // content: Receives the borrowed tile renderer between retired frames.
    // reset_local: Optional local projection of the same reset instant from the time owner.
    // Exception: Returns the original font, layout or frame preparation error.
    esp_err_t prepare(const LUNASHELL::CodexWeeklyLimitSnapshot &allowance,
                      const LUNASHELL::ClockSnapshot &clock, int64_t animation_time_us,
                      const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
                      SceneContent &content, const LUNASHELL::ClockLocalCalendar *reset_local = nullptr);

    // No parameters. Retires the animation before the route leaves or reopens.
    void reset_animation();

    // No parameters. The display scheduler can request frames until the flip ends.
    bool animating() const;

    // No parameters. The display owner borrows the same frame for its perimeter layer.
    const ResetCountdownPresentation &presentation() const;

private:
    ResetCountdownPresentation presentation_{};
    ResetCountdownStatus last_status_ = ResetCountdownStatus::Unavailable;
    uint64_t last_remaining_seconds_ = 0;
    uint64_t previous_seconds_ = 0;
    int64_t flip_started_us_ = -1;
    int64_t date_started_us_ = -1;
    int64_t date_epoch_seconds_ = 0;
};

} // namespace CODY
