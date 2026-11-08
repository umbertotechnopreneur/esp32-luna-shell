#pragma once

#include <cstdint>
#include <cstring>
#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {

enum class QuestionAudioAlert : uint8_t { None, Initial, Reminder };

struct QuestionAudioSchedule {
    char request_id[33]{};
    int64_t last_attempt_at_us = -1;
};
static_assert(sizeof(QuestionAudioSchedule) <= 48, "Question cadence retains only bounded identity and time");

// event: Existing canonical event projection; no second snapshot or hardware read is needed.
// current: True only for a visible event with a fresh, connected source and no observation error.
// reply_admitted: Stops reminders as soon as the local owner accepts Send.
// awake_time_us: Monotonic UI clock including animation pause and excluding standby.
// schedule: Display-owned request identity and last attempt; never stores audio or pending work.
inline QuestionAudioAlert question_audio_due(const LUNASHELL::CodexEventSnapshot &event,
    bool current, bool reply_admitted, int64_t awake_time_us, QuestionAudioSchedule &schedule)
{
    if (!current || reply_admitted || !event.data_valid || event.data_stale ||
        event.kind != LUNASHELL::CodexEventKind::Question ||
        event.state != LUNASHELL::CodexEventState::Pending || event.request_id[0] == '\0') {
        // Retain identity across hidden/offline periods so their initial alert cannot replay.
        schedule.last_attempt_at_us = -1;
        return QuestionAudioAlert::None;
    }
    if (std::strcmp(schedule.request_id, event.request_id) != 0) {
        std::memcpy(schedule.request_id, event.request_id, sizeof(schedule.request_id));
        schedule.last_attempt_at_us = awake_time_us;
        return QuestionAudioAlert::Initial;
    }
    if (schedule.last_attempt_at_us < 0 || awake_time_us < schedule.last_attempt_at_us) {
        schedule.last_attempt_at_us = awake_time_us;
        return QuestionAudioAlert::None;
    }
    if (awake_time_us - schedule.last_attempt_at_us < 10000000) {
        return QuestionAudioAlert::None;
    }
    // Advance on every attempt, including busy/quiet suppression; never accumulate missed alerts.
    schedule.last_attempt_at_us = awake_time_us;
    return QuestionAudioAlert::Reminder;
}

} // namespace CODY
