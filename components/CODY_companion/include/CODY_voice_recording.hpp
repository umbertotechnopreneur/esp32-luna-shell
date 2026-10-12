#pragma once

#include "CODY_companion_gestures.hpp"

namespace CODY {
enum class VoiceRecordingAction : uint8_t { None, Send, Discard };
inline constexpr int64_t kVoiceDoubleTapUs = 350000;
inline constexpr const char *kVoiceSendHint = "Tap once to send";
inline constexpr const char *kVoiceSilenceHint = "Pause over 2s to send";
inline constexpr const char *kVoiceDiscardHint = "Shake or double tap";
inline constexpr const char *kVoiceDiscardDetail = "to discard";

struct VoiceRecordingGesture {
    CompanionTapState contact{};
    int64_t session_at_us = 0;
    int64_t send_due_us = 0;
    uint16_t rotation = 0;
    int16_t x = 0;
    int16_t y = 0;
};

// state: Display-owned gesture and pending action, cleared at route/session boundaries.
inline void reset_voice_recording_gesture(VoiceRecordingGesture &state)
{
    state = {};
}

// state: Pending first tap; a second held contact postpones single-tap arbitration.
// now_us: Monotonic presentation time.
// session_at_us: Original start of the currently visible recording.
// rotation: Orientation of that completely retired view.
// eligible: True only while this visible session still accepts recording gestures.
inline VoiceRecordingAction take_voice_recording_action(VoiceRecordingGesture &state,
    int64_t now_us, int64_t session_at_us, uint16_t rotation, bool eligible)
{
    if (!eligible || state.session_at_us != session_at_us || state.rotation != rotation) {
        reset_voice_recording_gesture(state);
        return VoiceRecordingAction::None;
    }
    if (state.send_due_us != 0 && now_us > state.send_due_us && !state.contact.contact_active) {
        state.send_due_us = 0;
        return VoiceRecordingAction::Send;
    }
    return VoiceRecordingAction::None;
}

// state: Per-recording gesture state; never reused across a new utterance or rotation.
// x: Acquired logical touch coordinate; release coordinates need not be present.
// y: Acquired logical touch coordinate.
// pressed: Actual acquired contact state.
// pressed_edge: Actual first sample of a new contact.
// sampled_at_us: Original successful touch acquisition time.
// session_at_us: Original start of the visible recording.
// rotation: Shared touch/render orientation.
// eligible: Gesture admission after the recording screen has completely retired.
inline VoiceRecordingAction observe_voice_recording_touch(VoiceRecordingGesture &state,
    int16_t x, int16_t y, bool pressed, bool pressed_edge, int64_t sampled_at_us,
    int64_t session_at_us, uint16_t rotation, bool eligible)
{
    if (!eligible || session_at_us <= 0) {
        reset_voice_recording_gesture(state);
        return VoiceRecordingAction::None;
    }
    if (state.session_at_us != session_at_us || state.rotation != rotation) {
        reset_voice_recording_gesture(state);
        state.session_at_us = session_at_us;
        state.rotation = rotation;
    }
    const auto due = take_voice_recording_action(state, sampled_at_us, session_at_us, rotation, eligible);
    if (due != VoiceRecordingAction::None) return due;
    if (!observe_companion_tap(state.contact, x, y, pressed, pressed_edge,
            sampled_at_us, rotation, eligible, false)) return VoiceRecordingAction::None;
    const int dx = state.contact.origin_x - state.x;
    const int dy = state.contact.origin_y - state.y;
    if (state.send_due_us != 0 && state.contact.started_at_us <= state.send_due_us &&
        dx * dx + dy * dy <= 1600) {
        state.send_due_us = 0;
        return VoiceRecordingAction::Discard;
    }
    state.send_due_us = sampled_at_us + kVoiceDoubleTapUs;
    state.x = state.contact.origin_x;
    state.y = state.contact.origin_y;
    return VoiceRecordingAction::None;
}
}
