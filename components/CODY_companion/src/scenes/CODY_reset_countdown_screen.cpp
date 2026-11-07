#include "CODY_reset_countdown_screen.hpp"

#include <ctime>

#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {
namespace {

constexpr int64_t kFlipDurationUs = 200000;

// sample: Aged observation from its existing snapshot owner.
bool current(const LUNASHELL::ComponentSnapshot &sample)
{
    return sample.health == LUNASHELL::DeviceHealth::Ready && sample.data_valid &&
        !sample.data_stale && sample.last_error == ESP_OK;
}

// allowance: Aged authenticated account limit observation.
// clock: Aged device-clock observation. Animation never extrapolates UTC.
ResetCountdownView project_view(const LUNASHELL::CodexWeeklyLimitSnapshot &allowance,
                                const LUNASHELL::ClockSnapshot &clock)
{
    ResetCountdownView view{};
    if (allowance.data_stale) {
        view.status = ResetCountdownStatus::Stale;
        return view;
    }
    if (!current(allowance) || allowance.reset_unix_seconds <= 0 ||
        allowance.remaining_basis_points > 10000U) {
        return view;
    }
    const auto epoch = static_cast<std::time_t>(allowance.reset_unix_seconds);
    std::tm calendar{};
    if (static_cast<int64_t>(epoch) != allowance.reset_unix_seconds ||
        gmtime_r(&epoch, &calendar) == nullptr || calendar.tm_year < 120 || calendar.tm_year > 199) {
        return view;
    }
    view.fields.year = static_cast<uint16_t>(calendar.tm_year + 1900);
    view.fields.month = static_cast<uint8_t>(calendar.tm_mon + 1);
    view.fields.day = static_cast<uint8_t>(calendar.tm_mday);
    view.fields.hour = static_cast<uint8_t>(calendar.tm_hour);
    view.fields.minute = static_cast<uint8_t>(calendar.tm_min);
    view.fields.remaining_basis_points = allowance.remaining_basis_points;
    if (!current(clock) || clock.utc_epoch_seconds <= 0) {
        view.status = ResetCountdownStatus::TimeUnavailable;
    } else if (clock.utc_epoch_seconds >= allowance.reset_unix_seconds) {
        view.status = ResetCountdownStatus::Elapsed;
    } else {
        view.status = ResetCountdownStatus::Active;
        view.remaining_seconds = static_cast<uint64_t>(allowance.reset_unix_seconds - clock.utc_epoch_seconds);
    }
    return view;
}

} // namespace

// allowance: Current App Server data with validity and age.
// clock: Current device time with validity and age.
// animation_time_us: Monotonic display clock for interpolation of the tile geometry.
// title_font: Native font used for the digits and title.
// body_font: Native monospaced font for the date and labels.
// content: Receives the next borrowed draw callback and content revision.
// Exception: Returns the real presentation preparation error.
esp_err_t ResetCountdownScreen::prepare(
    const LUNASHELL::CodexWeeklyLimitSnapshot &allowance, const LUNASHELL::ClockSnapshot &clock,
    int64_t animation_time_us, const CODY2D::MonoFont &title_font,
    const CODY2D::MonoFont &body_font, SceneContent &content)
{
    auto view = project_view(allowance, clock);
    if (view.status != last_status_ || view.remaining_seconds != last_remaining_seconds_) {
        // Flip only on an observed one-second transition. Corrections and
        // skipped samples show the observed value immediately.
        if (view.status == ResetCountdownStatus::Active && last_status_ == ResetCountdownStatus::Active &&
            last_remaining_seconds_ == view.remaining_seconds + 1 && animation_time_us >= 0) {
            previous_seconds_ = last_remaining_seconds_;
            flip_started_us_ = animation_time_us;
        } else {
            flip_started_us_ = -1;
        }
    }
    if (flip_started_us_ >= 0 && animation_time_us >= flip_started_us_ &&
        animation_time_us - flip_started_us_ < kFlipDurationUs) {
        view.previous_seconds = previous_seconds_;
        view.flip_progress = static_cast<uint16_t>(
            (animation_time_us - flip_started_us_) * kResetCountdownFlipComplete / kFlipDurationUs);
    } else {
        flip_started_us_ = -1;
    }
    last_status_ = view.status;
    last_remaining_seconds_ = view.remaining_seconds;
    return prepare_reset_countdown(view, title_font, body_font, presentation_, content);
}

// No parameters. Clears the animation origin for the next screen entry.
void ResetCountdownScreen::reset_animation()
{
    last_status_ = ResetCountdownStatus::Unavailable;
    last_remaining_seconds_ = 0;
    previous_seconds_ = 0;
    flip_started_us_ = -1;
}

// No parameters. True while the presentation needs another flip frame.
bool ResetCountdownScreen::animating() const
{
    return flip_started_us_ >= 0;
}

// No parameters. Borrows the active presentation until the frame retires.
const ResetCountdownPresentation &ResetCountdownScreen::presentation() const
{
    return presentation_;
}

} // namespace CODY
