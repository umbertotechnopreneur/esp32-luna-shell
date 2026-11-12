#include "CODY_local_timer.hpp"

#include <limits>

#include "CODY_codex_audio.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include "esp_log.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_timer";
constexpr int64_t kSecondUs = 1000000;
constexpr int64_t kAlarmAdmissionUs = 10 * kSecondUs;
constexpr int64_t kAlarmCompletionUs = kAlarmAdmissionUs + 4 * kSecondUs;
constexpr int64_t kAlarmRetryUs = 200000;

// model: Clears an expired or cancelled run while preserving the user's choices.
void reset_timer(LocalTimerModel &model)
{
    model.phase = LocalTimerPhase::Idle;
    model.alarm = LocalTimerAlarm::None;
    model.deadline_us = 0;
    model.next_alarm_attempt_at_us = 0;
    model.alarm_request_sequence = 0;
    model.wake_error = ESP_OK;
    model.alarm_error = ESP_OK;
}

// model: Records the terminal audio failure for this run.
// error: Original non-success error, or a specific deadline/admission failure.
void fail_alarm(LocalTimerModel &model, esp_err_t error)
{
    model.alarm = LocalTimerAlarm::Failed;
    model.alarm_error = error;
    ESP_LOGW(kTag, "Timer alarm failed (%s)", esp_err_to_name(error));
}

// model: Current alarm request identity and selected sound.
// status: Actual asynchronous disposition from the existing audio owner.
// now_us: Device monotonic time used to retry genuine busy/expired requests.
void observe_alarm(LocalTimerModel &model, const CodexAudioStatus &status, int64_t now_us)
{
    switch (status.state) {
    case CodexAudioState::Dispatched:
        model.alarm_request_sequence = status.request_sequence;
        break;
    case CodexAudioState::Playing:
        model.alarm = LocalTimerAlarm::Playing;
        break;
    case CodexAudioState::Completed:
        model.alarm = LocalTimerAlarm::Complete;
        break;
    case CodexAudioState::Suppressed:
        model.alarm = LocalTimerAlarm::Silent;
        break;
    case CodexAudioState::DroppedBusy:
    case CodexAudioState::DroppedCooldown:
    case CodexAudioState::Expired:
        model.alarm = LocalTimerAlarm::Pending;
        model.alarm_request_sequence = 0;
        model.next_alarm_attempt_at_us = now_us + kAlarmRetryUs;
        break;
    case CodexAudioState::Idle:
    case CodexAudioState::Failed:
    case CodexAudioState::Unavailable:
        fail_alarm(model, status.last_error != ESP_OK ? status.last_error : ESP_ERR_INVALID_STATE);
        break;
    }
}

} // namespace

// model: User settings for the next timer, writable only while idle.
// step: A single backward or forward minute from a retired touch control.
void adjust_local_timer(LocalTimerModel &model, int step)
{
    if (model.phase != LocalTimerPhase::Idle) return;
    if (step == -1 && model.duration_minutes > kLocalTimerMinMinutes) --model.duration_minutes;
    if (step == 1 && model.duration_minutes < kLocalTimerMaxMinutes) ++model.duration_minutes;
}

// model: Idle sound selection retained for the next run.
void cycle_local_timer_sound(LocalTimerModel &model)
{
    if (model.phase != LocalTimerPhase::Idle) return;
    model.sound = model.sound == LocalTimerSound::Bell ? LocalTimerSound::Chime : LocalTimerSound::Bell;
}

// model: The single worker-owned timer, whose settings stay fixed throughout the run.
// now_us: Device monotonic clock captured at the actual Start action.
// Exception: Rejects a non-idle model, invalid duration or a deadline outside its domain.
esp_err_t start_local_timer(LocalTimerModel &model, int64_t now_us)
{
    if (model.phase != LocalTimerPhase::Idle) {
        ESP_LOGW(kTag, "Start rejected in phase %u", static_cast<unsigned>(model.phase));
        return ESP_ERR_INVALID_STATE;
    }
    const int64_t duration_us = static_cast<int64_t>(model.duration_minutes) * 60 * kSecondUs;
    if (model.duration_minutes < kLocalTimerMinMinutes || model.duration_minutes > kLocalTimerMaxMinutes ||
        now_us < 0 || now_us > std::numeric_limits<int64_t>::max() - duration_us - kAlarmCompletionUs - kAlarmRetryUs) {
        ESP_LOGW(kTag, "Start rejected for duration=%u or monotonic clock",
            static_cast<unsigned>(model.duration_minutes));
        return ESP_ERR_INVALID_ARG;
    }
    model.phase = LocalTimerPhase::Running;
    model.deadline_us = now_us + duration_us;
    return ESP_OK;
}

// model: Running timer cancelled by the user before expiry.
void cancel_local_timer(LocalTimerModel &model)
{
    if (model.phase == LocalTimerPhase::Running) reset_timer(model);
}

// model: Finished timer and its optional alarm admission.
void dismiss_local_timer(LocalTimerModel &model)
{
    if (model.phase == LocalTimerPhase::Finished) reset_timer(model);
}

// model: Current duration or absolute deadline.
// now_us: Device monotonic time, valid from zero through the supported device uptime.
uint32_t local_timer_seconds_remaining(const LocalTimerModel &model, int64_t now_us)
{
    if (model.phase == LocalTimerPhase::Idle) return model.duration_minutes * 60U;
    if (model.phase == LocalTimerPhase::Finished || now_us < 0 || now_us >= model.deadline_us) return 0;
    const int64_t remaining_us = model.deadline_us - now_us;
    return static_cast<uint32_t>((remaining_us + kSecondUs - 1) / kSecondUs);
}

// model: Timer state maintained by the same worker while awake and in Cody standby.
// now_us: Current device monotonic time, never derived from frame count or UTC.
bool update_local_timer(LocalTimerModel &model, int64_t now_us)
{
    if (model.phase == LocalTimerPhase::Running && now_us >= model.deadline_us) {
        model.phase = LocalTimerPhase::Finished;
        model.alarm = LocalTimerAlarm::Pending;
        model.wake_error = LUNASHELL_power_manager_request_local_timer_wake();
        return true;
    }
    if (model.phase == LocalTimerPhase::Finished && model.alarm_request_sequence == 0 &&
        model.alarm == LocalTimerAlarm::Pending && now_us >= model.deadline_us + kAlarmAdmissionUs) {
        fail_alarm(model, model.wake_error != ESP_OK ? model.wake_error : ESP_ERR_TIMEOUT);
    }
    return false;
}

// model: Finished timer whose sound is either waiting for audio or already admitted.
// now_us: Device monotonic time used for bounded deferred audio admission.
// voice_busy: Keeps an alarm from competing with the active voice capture flow.
// Exception: Original snapshot and audio failures become a terminal visual outcome.
void update_local_timer_alarm(LocalTimerModel &model, int64_t now_us, bool voice_busy)
{
    if (model.phase != LocalTimerPhase::Finished ||
        (model.alarm != LocalTimerAlarm::Pending && model.alarm != LocalTimerAlarm::Playing)) return;

    CodexAudioStatus status{};
    if (model.alarm_request_sequence != 0) {
        const auto cue = model.sound == LocalTimerSound::Bell ? CodexAudioCue::Question : CodexAudioCue::ResponseReady;
        const esp_err_t error = get_codex_audio_status(model.alarm_request_sequence, cue, status);
        if (error != ESP_OK) {
            fail_alarm(model, error);
        } else if (status.request_sequence != model.alarm_request_sequence) {
            fail_alarm(model, ESP_ERR_NOT_FINISHED);
        } else if (now_us >= model.deadline_us + kAlarmCompletionUs) {
            fail_alarm(model, ESP_ERR_TIMEOUT);
        } else {
            observe_alarm(model, status, now_us);
        }
        return;
    }
    if (voice_busy || now_us < model.next_alarm_attempt_at_us ||
        now_us >= model.deadline_us + kAlarmAdmissionUs) return;

    LUNASHELL::PowerManagementSnapshot power{};
    const esp_err_t power_error = LUNASHELL::DeviceSnapshotService::instance().readPowerState(power);
    if (power_error != ESP_OK) {
        fail_alarm(model, power_error);
        return;
    }
    // A retired wake frame can precede the end of the power owner's transaction.
    // Require activity at or after the deadline so a pre-alarm Active snapshot
    // cannot admit sound while the power owner is still entering standby.
    if (power.changed_at_us <= 0 || power.state != LUNASHELL::DeviceOperatingState::Active ||
        power.audio_suspended || power.last_activity_at_us < model.deadline_us) return;

    const esp_err_t error = model.sound == LocalTimerSound::Bell
        ? notify_codex_audio(CodexAudioCue::Question, status)
        : notify_codex_audio_sequence(CodexAudioCue::ResponseReady, 3, 150, status);
    if (error != ESP_OK) {
        fail_alarm(model, error);
    } else {
        observe_alarm(model, status, now_us);
    }
}

} // namespace CODY
