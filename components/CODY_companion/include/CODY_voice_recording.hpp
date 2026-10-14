#pragma once

#include "CODY_companion_gestures.hpp"

namespace CODY {
enum class VoiceRecordingAction : uint8_t { None, Send, Discard };
enum class VoiceRecordingMode : uint8_t { Disabled, Recording, CancelOnly };
inline constexpr int64_t kVoiceDoubleTapUs = 350000;
inline constexpr const char *kVoiceSendHint = "Tap once to send";
inline constexpr const char *kVoiceSilenceHint = "5s of silence to finish";
inline constexpr const char *kVoiceDiscardHint = "Shake or double tap";
inline constexpr const char *kVoiceDiscardDetail = "to discard";
inline constexpr const char *kVoicePreparingTitle = "Preparing audio";
inline constexpr const char *kVoiceStartCountdownTitle = "Start listening in";
inline constexpr const char *kVoiceSendCountdownTitle = "Sending the audio in";
inline constexpr const char *kVoiceCancelHint = "Tap to cancel";
inline constexpr const char *kVoiceShakeCancelHint = "Or shake to discard";
inline constexpr uint16_t kVoiceCountdownTop = 260;
inline constexpr uint16_t kVoiceCountdownDamageBottom = 288;

// seconds: Canonical remaining count; zero is a phase transition, never an invented extra second.
// Returns the exact bounded caption, or nullptr for an unavailable/invalid owner countdown.
inline const char *voice_recording_countdown_caption(uint8_t seconds)
{
    switch (seconds) {
    case 3: return "3 seconds";
    case 2: return "2 seconds";
    case 1: return "1 second";
    default: return nullptr;
    }
}

struct VoiceRecordingGesture {
    CompanionTapState contact{};
    int64_t session_at_us = 0;
    int64_t send_due_us = 0;
    uint16_t rotation = 0;
    VoiceRecordingMode mode = VoiceRecordingMode::Disabled;
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
// mode: Gesture policy of the completely retired view; mode changes retire pending sends and contacts.
inline VoiceRecordingAction take_voice_recording_action(VoiceRecordingGesture &state,
    int64_t now_us, int64_t session_at_us, uint16_t rotation, VoiceRecordingMode mode)
{
    if (mode == VoiceRecordingMode::Disabled || state.session_at_us != session_at_us ||
        state.rotation != rotation || state.mode != mode) {
        reset_voice_recording_gesture(state);
        return VoiceRecordingAction::None;
    }
    if (mode == VoiceRecordingMode::Recording && state.send_due_us != 0 &&
        now_us > state.send_due_us && !state.contact.contact_active) {
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
// mode: Disabled until retirement; countdowns and pre-END Sending use immediate single-tap cancellation.
inline VoiceRecordingAction observe_voice_recording_touch(VoiceRecordingGesture &state,
    int16_t x, int16_t y, bool pressed, bool pressed_edge, int64_t sampled_at_us,
    int64_t session_at_us, uint16_t rotation, VoiceRecordingMode mode)
{
    if (mode == VoiceRecordingMode::Disabled || session_at_us <= 0) {
        reset_voice_recording_gesture(state);
        return VoiceRecordingAction::None;
    }
    if (state.session_at_us != session_at_us || state.rotation != rotation || state.mode != mode) {
        reset_voice_recording_gesture(state);
        state.session_at_us = session_at_us;
        state.rotation = rotation;
        state.mode = mode;
    }
    const auto due = take_voice_recording_action(state, sampled_at_us, session_at_us, rotation, mode);
    if (due != VoiceRecordingAction::None) return due;
    if (!observe_companion_tap(state.contact, x, y, pressed, pressed_edge,
            sampled_at_us, rotation, true, false)) return VoiceRecordingAction::None;
    if (mode == VoiceRecordingMode::CancelOnly) {
        state.send_due_us = 0;
        return VoiceRecordingAction::Discard;
    }
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
