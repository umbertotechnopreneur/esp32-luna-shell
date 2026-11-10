#include "CODY_device_effects.hpp"

#include <cstring>

#include "LUNASHELL_platform.h"
#include "esp_log.h"
#include "esp_timer.h"

extern "C" {
extern const uint8_t boot_rise_start[] asm("_binary_cody_device_boot_rise_start");
extern const uint8_t boot_rise_end[] asm("_binary_cody_device_boot_rise_end");
}

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_effects";
constexpr uint32_t kBootRiseCue = 0x43430000U;
constexpr size_t kBootRiseSamples = 39690; // 1.8 seconds at the asset's authored 22.05 kHz.
constexpr uint32_t kTapCue = 0x4343FF01U; // Local feedback within the shared application cue channel.
constexpr uint32_t kKeyCue = 0x4343FF02U;
constexpr uint32_t kWakeCue = 0x4343FF03U;
constexpr uint32_t kJumpCue = 0x4343FF04U;
constexpr uint32_t kTapRateHz = 16000;
constexpr size_t kTapSamples = 480;
constexpr int64_t kDisconnectDwellUs = 3000000;
constexpr int64_t kWifiReadyDwellUs = 500000;
constexpr int64_t kQuestionReminderUs = 60000000;
constexpr int64_t kEventSoundWaitUs = 2500000;
constexpr int64_t kVoiceCountdownCueFreshnessUs = 200000;
constexpr int64_t kVoiceCloseCueFreshnessUs = 500000;

// No parameters: build a rounded-noise impulse with a short attack and quadratic decay.
constexpr std::array<int16_t, kTapSamples> make_tap_cue()
{
    std::array<int16_t, kTapSamples> samples{};
    uint32_t noise_state = 0x4c554e41U;
    int32_t rounded_noise = 0;
    int32_t slow_noise = 0;
    constexpr int32_t attack_samples = 24;
    constexpr int32_t last_sample = static_cast<int32_t>(kTapSamples - 1);
    for (int32_t index = 0; index <= last_sample; ++index) {
        noise_state ^= noise_state << 13U;
        noise_state ^= noise_state >> 17U;
        noise_state ^= noise_state << 5U;
        const auto noise = static_cast<int32_t>(noise_state & 0x7ffU) - 1024;
        rounded_noise = (2 * rounded_noise + noise) / 3;
        slow_noise = (7 * slow_noise + rounded_noise) / 8;
        const int32_t attack = index < attack_samples ? index : attack_samples;
        const int32_t remaining = last_sample - index;
        const int64_t shaped = static_cast<int64_t>(rounded_noise - slow_noise) * 10 *
            attack * remaining * remaining;
        samples[static_cast<size_t>(index)] = static_cast<int16_t>(
            shaped / (attack_samples * last_sample * last_sample));
    }
    return samples;
}

constexpr auto kTapPcm = make_tap_cue();
// No parameters: a short decaying 1 kHz triangle chime, authored at 16 kHz.
constexpr std::array<int16_t, 1920> make_wake_cue()
{
    std::array<int16_t, 1920> samples{};
    for (int index = 0; index < 1920; ++index) {
        const int phase = index % 16;
        const int triangle = phase < 8 ? phase * 2 - 8 : 24 - phase * 2;
        const int attack = index < 32 ? index : 32;
        samples[index] = static_cast<int16_t>(triangle * 80 * attack * (1919 - index) / (32 * 1919));
    }
    return samples;
}
constexpr auto kWakePcm = make_wake_cue();
// No parameters: a compact ascending arcade jump at the shared 16 kHz clock.
constexpr std::array<int16_t, 2400> make_jump_cue()
{
    std::array<int16_t, 2400> samples{};
    int phase = 0;
    for (int index = 0; index < 2400; ++index) {
        phase += 500 + index / 3;
        if (phase >= 16000) phase -= 16000;
        const int triangle = phase < 8000 ? phase - 4000 : 12000 - phase;
        const int attack = index < 32 ? index : 32;
        samples[index] = static_cast<int16_t>(triangle * attack * (2399 - index) / (32 * 2399));
    }
    return samples;
}
constexpr auto kJumpPcm = make_jump_cue();
static_assert(kTapSamples * 1000U / kTapRateHz == 30U && kTapPcm.front() == 0 && kTapPcm.back() == 0,
    "The tap is 30 ms of mono PCM16 with silent boundaries");

enum class SoundPriority : uint8_t {
    None, Feedback, Connection, Wifi, Battery, Confirmation, Attention, EventNotice, VoiceWarning, CriticalBattery
};

struct SoundRequest {
    CodexAudioCue cue = CodexAudioCue::None;
    SoundPriority priority = SoundPriority::None;
    uint8_t repetitions = 1;
};

// sample: Original canonical validity and error fields, with freshness already applied.
bool current(const LUNASHELL::ComponentSnapshot &sample)
{
    return sample.data_valid && !sample.data_stale && sample.last_error == ESP_OK;
}

// request: One winning request for this tick; a current event notice can wait briefly.
// cue: Existing immutable PCM identity.
// priority: Semantic urgency independent of any rendered route.
// repetitions: Bounded repeated short notice for low battery or a blocked dictation.
void choose(SoundRequest &request, CodexAudioCue cue, SoundPriority priority, uint8_t repetitions = 1)
{
    if (priority > request.priority) {
        request = {cue, priority, repetitions};
    }
}

// model: Bounded recent identities, retained across route changes and source gaps.
// request_id: A canonical terminated request identity, never a UI caption.
DeviceEventSoundRecord &event_record(DeviceEffectsModel &model, const char *request_id)
{
    for (auto &record : model.recent_events) {
        if (std::strcmp(record.request_id, request_id) == 0) {
            return record;
        }
    }
    auto &record = model.recent_events[model.next_event_slot];
    record = {};
    std::memcpy(record.request_id, request_id, sizeof(record.request_id));
    model.next_event_slot = static_cast<uint8_t>((model.next_event_slot + 1) % model.recent_events.size());
    return record;
}

// model: Holds the canonical event and the only pending semantic sound.
// cue: Distinct immutable PCM identity for the observed transition.
// now_us: First observation time; subsequent attempts preserve the original deadline.
void schedule_event_sound(DeviceEffectsModel &model, CodexAudioCue cue, int64_t now_us)
{
    auto &sound = model.event_sound;
    sound = {};
    std::memcpy(sound.request_id, model.event.request_id, sizeof(sound.request_id));
    sound.state = model.event.state;
    sound.cue = cue;
    sound.deadline_us = now_us + kEventSoundWaitUs;
}

// cue: Exact immutable PCM identity used for the previous admission.
// sequence: Worker sequence, cleared only when the same request reports a busy drop.
// finished: True for a terminal result, a failed read or a superseding request.
// Exception: Returns the original asynchronous owner error if a status read fails.
esp_err_t observe_sound_attempt(CodexAudioCue cue, uint32_t &sequence, bool &finished)
{
    finished = false;
    if (sequence == 0) return ESP_OK;
    CodexAudioStatus status{};
    const auto error = get_codex_audio_status(status);
    if (error != ESP_OK || status.request_sequence != sequence || status.cue != cue) {
        finished = true;
    } else if (status.state == CodexAudioState::DroppedBusy) {
        sequence = 0;
    } else {
        finished = status.state != CodexAudioState::Dispatched && status.state != CodexAudioState::Playing;
    }
    return error;
}

// model: Owns the single current event sound and its correlated shared-worker admission.
// request: Receives the sound if still eligible and ready to attempt.
// Exception: Returns the original asynchronous owner error if a status read fails.
esp_err_t select_event_sound(DeviceEffectsModel &model, SoundRequest &request)
{
    auto &sound = model.event_sound;
    if (sound.cue == CodexAudioCue::None) return ESP_OK;
    bool finished = false;
    const auto error = observe_sound_attempt(sound.cue, sound.request_sequence, finished);
    if (finished) {
        sound = {};
    } else if (sound.request_sequence == 0) {
        choose(request, sound.cue, SoundPriority::EventNotice);
    }
    return error;
}

// model: Request ledger and successfully read canonical event.
// now_us: Monotonic time for event expiry and the quiet reminder cadence.
// request: Receives immediate effects for notices that do not wait for the speaker.
void observe_event(DeviceEffectsModel &model, int64_t now_us, SoundRequest &request)
{
    using Kind = LUNASHELL::CodexEventKind;
    using State = LUNASHELL::CodexEventState;
    const auto &event = model.event;
    if (!model.event_online || event.kind == Kind::None || event.request_id[0] == '\0' ||
        std::memchr(event.request_id, '\0', sizeof(event.request_id)) == nullptr) {
        model.event_sound = {};
        return;
    }
    const bool expired = event.expires_at_us > 0 && now_us >= event.expires_at_us;
    auto &sound = model.event_sound;
    if (sound.cue != CodexAudioCue::None &&
        (now_us >= sound.deadline_us || sound.state != event.state ||
         std::strcmp(sound.request_id, event.request_id) != 0 || (expired && sound.state == State::Pending))) {
        sound = {};
    }
    auto &record = event_record(model, event.request_id);
    if (event.kind == Kind::Question && event.state == State::Delivered &&
        event.selected_id[0] != '\0' && !record.delivery_announced) {
        record.delivery_announced = true;
        schedule_event_sound(model, CodexAudioCue::AnswerReceived, now_us);
    }
    if (event.state != State::Pending || expired) {
        record.reminder_at_us = 0;
        return;
    }
    const auto kind_value = static_cast<unsigned>(event.kind);
    if (kind_value >= 16) {
        return;
    }
    const auto kind_bit = static_cast<uint16_t>(1U << kind_value);
    const bool first = (record.announced_kinds & kind_bit) == 0;
    record.announced_kinds |= kind_bit;
    if (event.kind == Kind::Question) {
        if (first) {
            record.reminder_at_us = now_us;
            schedule_event_sound(model, CodexAudioCue::Question, now_us);
        } else if (record.reminder_at_us <= 0 || now_us < record.reminder_at_us) {
            record.reminder_at_us = now_us;
        } else if (now_us - record.reminder_at_us >= kQuestionReminderUs) {
            record.reminder_at_us = now_us;
            choose(request, CodexAudioCue::QuestionReminder, SoundPriority::Attention);
        }
        return;
    }
    if (!first) {
        return;
    }
    switch (event.kind) {
    case Kind::ReplyReady:
        schedule_event_sound(model, CodexAudioCue::ResponseReady, now_us);
        break;
    case Kind::ApprovalOnPc:
        choose(request, CodexAudioCue::ApprovalRequired, SoundPriority::Attention);
        break;
    case Kind::ChecksFailed:
        choose(request, CodexAudioCue::ChecksFailed, SoundPriority::Attention);
        break;
    case Kind::ChecksPassed:
        choose(request, CodexAudioCue::ChecksPassed, SoundPriority::Confirmation);
        break;
    default:
        // A generic notice, PR opening or work transition is not a completed response.
        break;
    }
}

// model: Retained connection edge and disconnect confirmation time.
// status: Already-aged link and app presence, independent of the selected dashboard.
// status_valid: A failed snapshot copy is not evidence that a peer disconnected.
// now_us: Monotonic time; transient lease gaps do not repeatedly ring.
// request: Receives one connection edge after the observed transition.
void observe_connection(DeviceEffectsModel &model, const LUNASHELL::CodexPresenceStatus &status,
    bool status_valid, int64_t now_us, SoundRequest &request)
{
    if (!status_valid) {
        return;
    }
    const bool connected = current(status.codex_link) && status.codex_app_open &&
        status.codex_link_state == LUNASHELL::CodexLinkState::Connected;
    if (!model.connection_known) {
        model.connection_known = true;
        model.connected = connected;
        return;
    }
    if (connected) {
        model.connection_lost_at_us = 0;
        if (!model.connected) {
            model.connected = true;
            choose(request, CodexAudioCue::ConnectionRestored, SoundPriority::Connection);
        }
    } else if (model.connected) {
        if (model.connection_lost_at_us <= 0 || now_us < model.connection_lost_at_us) {
            model.connection_lost_at_us = now_us;
        } else if (now_us - model.connection_lost_at_us >= kDisconnectDwellUs) {
            model.connected = false;
            model.connection_lost_at_us = 0;
            choose(request, CodexAudioCue::ConnectionLost, SoundPriority::Connection);
        }
    }
}

// model: Retains the last announced LAN state and debounces its next genuine edge.
// wifi: Canonical Wi-Fi association and IP facts, separate from Codex and Internet reachability.
// status_valid: False when reading the enclosing snapshot failed.
// now_us: Monotonic time used only for the continuous observation dwell.
// startup_wifi_offline: The startup owner has finished its passive saved-network grace.
// request: Receives a local Wi-Fi edge, ahead of a coincident Codex connection cue.
void observe_wifi(DeviceEffectsModel &model, const LUNASHELL::WifiSnapshot &wifi,
    bool status_valid, int64_t now_us, bool startup_wifi_offline, SoundRequest &request)
{
    if (!status_valid || !wifi.connection_state_valid || wifi.data_stale) {
        model.wifi_edge_since_us = 0;
        return;
    }
    if (!wifi.started) {
        // Deliberate radio teardown, including standby, is a silent baseline reset.
        model.wifi_ready = false;
        model.wifi_edge_since_us = 0;
        return;
    }
    const bool ready = wifi.connected && wifi.ip_acquired && wifi.ip_address[0] != '\0';
    if (!model.wifi_result_known && startup_wifi_offline && wifi.credentials_configured &&
        !wifi.connected && wifi.last_error != ESP_OK) {
        // The startup grace already elapsed. A known failed saved connection is
        // a real first result, unlike missing credentials or an in-progress lease.
        model.wifi_result_known = true;
        model.wifi_edge_since_us = 0;
        choose(request, CodexAudioCue::ChecksFailed, SoundPriority::Wifi);
        return;
    }
    if (ready == model.wifi_ready || (wifi.connected && !ready)) {
        // Association while DHCP is pending is neither a successful connection nor a loss.
        model.wifi_edge_since_us = 0;
        return;
    }
    if (model.wifi_edge_since_us <= 0 || now_us < model.wifi_edge_since_us) {
        model.wifi_edge_since_us = now_us;
        return;
    }
    const int64_t dwell_us = ready ? kWifiReadyDwellUs : kDisconnectDwellUs;
    if (now_us - model.wifi_edge_since_us < dwell_us) return;

    model.wifi_ready = ready;
    model.wifi_result_known = true;
    model.wifi_edge_since_us = 0;
    choose(request, ready ? CodexAudioCue::ChecksPassed : CodexAudioCue::ChecksFailed, SoundPriority::Wifi);
}

// model: Successfully read provisioning lifecycle and previously observed live session.
// request: Receives success only for a verified online session, or its real failure.
void observe_pairing(DeviceEffectsModel &model, SoundRequest &request)
{
    using State = LUNASHELL::ProvisioningState;
    const auto &pairing = model.pairing.provisioning;
    if (!pairing.data_valid || pairing.data_stale || pairing.session_id == 0) {
        return;
    }
    switch (pairing.state) {
    case State::Starting:
    case State::WaitingForApp:
    case State::CredentialsReceived:
    case State::Connecting:
        if (pairing.session_id != model.pairing_session) {
            model.pairing_session = pairing.session_id;
            model.pairing_terminal_announced = false;
        }
        return;
    default: break;
    }
    if (model.pairing_session != pairing.session_id || model.pairing_terminal_announced) {
        return;
    }
    if (pairing.state == State::Success && pairing.last_error == ESP_OK && model.pairing.wifi_online) {
        model.pairing_terminal_announced = true;
        choose(request, CodexAudioCue::ConnectionRestored, SoundPriority::Confirmation);
    } else if (pairing.state == State::Failed || pairing.state == State::Poisoned) {
        model.pairing_terminal_announced = true;
        choose(request, CodexAudioCue::Interrupted, SoundPriority::Attention);
    }
}

// model: Sequence already attempted; audio never acknowledges the visual battery banner.
// battery: Canonical threshold notice maintained by the device's battery model.
// request: Receives one severity-specific cue using existing short PCM assets.
void observe_battery(DeviceEffectsModel &model, const BatteryAlertNotice &battery, SoundRequest &request)
{
    if (battery.sequence == 0 || battery.sequence == model.battery_sequence || battery.kind == BatteryAlertKind::None) {
        return;
    }
    model.battery_sequence = battery.sequence;
    switch (battery.kind) {
    case BatteryAlertKind::Level50:
        choose(request, CodexAudioCue::QuestionReminder, SoundPriority::Battery);
        break;
    case BatteryAlertKind::Level20:
        choose(request, CodexAudioCue::Paused, SoundPriority::Battery);
        break;
    case BatteryAlertKind::Level10:
        choose(request, CodexAudioCue::ConnectionLost, SoundPriority::Battery);
        break;
    case BatteryAlertKind::Level5:
        choose(request, CodexAudioCue::ConnectionLost, SoundPriority::CriticalBattery, 2);
        break;
    case BatteryAlertKind::Empty:
        choose(request, CodexAudioCue::ConnectionLost, SoundPriority::CriticalBattery, 3);
        break;
    case BatteryAlertKind::None: break;
    }
}

// phase: Canonical input lifecycle phase, independent of AFE heartbeat freshness.
bool voice_input_stopped(LUNASHELL::VoiceInputPhase phase)
{
    using Phase = LUNASHELL::VoiceInputPhase;
    return phase == Phase::Idle || phase == Phase::Failed || phase == Phase::Cancelled ||
        phase == Phase::Unconfirmed || phase == Phase::Delivered;
}

// voice: Canonical input lifecycle and its independently sampled AFE heartbeat.
// now_us: Monotonic observation time used to reject future input timestamps.
bool voice_sound_state_known(const LUNASHELL::VoiceSnapshot &voice, int64_t now_us)
{
    if (current(voice)) return true;
    // A published idle/terminal input remains stopped when the AFE heartbeat ages.
    const bool stopped = voice.state == LUNASHELL::VoiceState::Disabled ||
        voice.state == LUNASHELL::VoiceState::Failed;
    const bool published_input = voice.input.changed_at_us > 0 && voice.input.changed_at_us <= now_us;
    return voice_input_stopped(voice.input.phase) &&
        (published_input || (stopped && voice.last_attempt_at_us > 0));
}

// model: Voice session identity and one-shot start, send and close flags.
// voice: Optional original canonical voice snapshot; absent means no voice feature.
// now_us: Monotonic time used to admit sounds only near their actual transitions.
// request: Replaces other sounds on the voice route and suppresses them during capture.
// Returns whether the shared Codex sound channel must remain suspended this tick.
bool observe_voice(DeviceEffectsModel &model, const LUNASHELL::VoiceSnapshot *voice,
    int64_t now_us, SoundRequest &request)
{
    using Phase = LUNASHELL::VoiceInputPhase;
    if (voice == nullptr) {
        return false;
    }
    if (!voice_sound_state_known(*voice, now_us)) {
        request = {};
        return true;
    }
    const auto &input = voice->input;
    if (input.started_at_us > 0 && input.started_at_us != model.voice_session_us) {
        model.voice_session_us = input.started_at_us;
        model.voice_warning_sequence = 0;
        model.voice_start_announced = false;
        model.voice_send_announced = false;
        model.voice_close_announced = false;
    }
    if (input.phase == Phase::StartCountdown) {
        request = {};
        // The start chime ends well before PCM can enter the recording.
        if (!model.voice_start_announced && input.countdown_seconds == 3 && input.changed_at_us > 0 &&
            now_us >= input.changed_at_us && now_us - input.changed_at_us <= kVoiceCountdownCueFreshnessUs) {
            model.voice_start_announced = true;
            choose(request, CodexAudioCue::ResponseReady, SoundPriority::Confirmation);
        }
        return input.countdown_seconds < 2;
    }
    const bool destination_blocked = input.phase == Phase::Failed && !input.end_sent &&
        input.failure == LUNASHELL::VoiceInputFailure::Destination;
    if ((destination_blocked || input.phase == Phase::Unconfirmed) &&
        input.started_at_us > 0 && input.changed_at_us > 0 && now_us >= input.changed_at_us) {
        // The visible terminal result owns this notice. Do not play a late Send or generic Close cue.
        if (input.end_sent) model.voice_send_announced = true;
        if (now_us - input.changed_at_us < kVoiceResultDwellUs) {
            request = {};
            if (!model.voice_close_announced) {
                const auto cue = destination_blocked ? CodexAudioCue::Interrupted : CodexAudioCue::ChecksFailed;
                choose(request, cue, SoundPriority::VoiceWarning, destination_blocked ? 2 : 1);
            }
        } else {
            model.voice_warning_sequence = 0;
            model.voice_close_announced = true;
        }
        return false;
    }
    if (input.end_sent && !model.voice_send_announced && input.changed_at_us > 0 && now_us >= input.changed_at_us) {
        model.voice_send_announced = true;
        if (now_us - input.changed_at_us <= kVoiceCloseCueFreshnessUs) {
            request = {};
            choose(request, CodexAudioCue::Running, SoundPriority::Confirmation);
            return false;
        }
    }
    if (!voice_input_stopped(input.phase)) {
        request = {};
        // After END, keep the admitted send cue playing while waiting for Codex.
        return !input.end_sent;
    }
    if (input.phase != Phase::Idle && input.started_at_us > 0 && input.changed_at_us > 0 &&
        now_us >= input.changed_at_us && !model.voice_close_announced) {
        const auto age_us = now_us - input.changed_at_us;
        if (age_us < kVoiceResultDwellUs) {
            request = {};
        } else {
            model.voice_close_announced = true;
            if (age_us - kVoiceResultDwellUs <= kVoiceCloseCueFreshnessUs) {
                request = {};
                const bool delivered = input.phase == Phase::Delivered && input.error == ESP_OK;
                choose(request, delivered ? CodexAudioCue::SessionEnded : CodexAudioCue::Interrupted,
                    SoundPriority::Confirmation);
            }
        }
    }
    return false;
}

// request: One selected cue and its bounded repeat count.
// playback: Receives the actual owner admission result.
// Exception: Returns the original shared audio-owner error.
esp_err_t dispatch_sound(const SoundRequest &request, CodexAudioStatus &playback)
{
    return request.repetitions == 1
        ? notify_codex_audio(request.cue, playback)
        : notify_codex_audio_sequence(request.cue, request.repetitions, 150, playback);
}

// model: Retains admission and retry state for the current terminal voice result.
// request: Warning selected only within the result's visible lifetime.
// Exception: Returns the original shared-worker status or admission failure.
esp_err_t dispatch_voice_warning(DeviceEffectsModel &model, const SoundRequest &request)
{
    auto &sequence = model.voice_warning_sequence;
    auto error = observe_sound_attempt(request.cue, sequence, model.voice_close_announced);
    if (model.voice_close_announced || sequence != 0) return error;

    error = dispatch_sound(request, model.playback);
    if (error == ESP_OK && model.playback.state == CodexAudioState::Dispatched) {
        sequence = model.playback.request_sequence;
    } else if (error != ESP_OK || model.playback.state != CodexAudioState::DroppedBusy) {
        model.voice_close_announced = true;
    }
    return error;
}
} // namespace

// model: Persistent controller initialized once after the actual codec and saved sound policy.
// Exception: Returns and retains the existing worker's original preparation error.
esp_err_t prepare_device_effects(DeviceEffectsModel &model)
{
    if (model.prepared) {
        return model.preparation_error;
    }
    constexpr lunashell_personality_audio_policy_t policy{true, 0, 200000};
    model.preparation_error = LUNASHELL_audio_prepare_personality(&policy);
    model.prepared = true;
    model.last_error = model.preparation_error;
    if (model.preparation_error != ESP_OK) {
        ESP_LOGE(kTag, "Notification audio preparation failed (%s)", esp_err_to_name(model.preparation_error));
    }
    return model.preparation_error;
}

// model: Retains the one boot attempt and its genuine admission result.
// Exception: Returns original invalid asset or audio-owner failure without blocking animation.
esp_err_t play_device_boot_cue(DeviceEffectsModel &model)
{
    if (model.boot_cue_requested) return model.boot_audio_error;
    model.boot_cue_requested = true;
    const auto begin = reinterpret_cast<uintptr_t>(boot_rise_start);
    const auto end = reinterpret_cast<uintptr_t>(boot_rise_end);
    lunashell_audio_cue_status_t status{LUNASHELL_CUE_UNAVAILABLE, ESP_OK, 0, kBootRiseCue};
    auto error = model.preparation_error;
    if (error == ESP_OK) {
        if (end <= begin || begin % alignof(int16_t) != 0 ||
            end - begin != kBootRiseSamples * sizeof(int16_t)) {
            error = ESP_ERR_INVALID_SIZE;
        } else {
            error = LUNASHELL_audio_notify_boot(reinterpret_cast<const int16_t *>(boot_rise_start),
                kBootRiseSamples, kBootRiseCue, &status);
        }
    }
    model.boot_audio_error = error;
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "BootRise cue admission failed (%s)", esp_err_to_name(error));
    } else {
        ESP_LOGI(kTag, "BootRise cue admission: state=%u", static_cast<unsigned>(status.state));
    }
    return error;
}

// model: Retains the optional tap result without delaying touch handling.
// voice: Latest canonical input observation, or null for builds without voice.
void play_device_tap_cue(DeviceEffectsModel &model, const LUNASHELL::VoiceSnapshot *voice)
{
    if (!model.prepared || model.preparation_error != ESP_OK) return;
    if (voice != nullptr && (!voice_input_stopped(voice->input.phase) ||
            !voice_sound_state_known(*voice, esp_timer_get_time()))) return;

    // Use the existing short, nonpreemptive mailbox. Its owner applies the saved
    // volume, Sounds and quiet-hours policy without waiting on this touch path.
    lunashell_audio_cue_status_t status{};
    const auto error = LUNASHELL_audio_notify_codex(kTapPcm.data(), kTapPcm.size(), kTapRateHz,
        kTapCue, LUNASHELL_CODEX_CUE_NOTICE, &status);
    if (error != ESP_OK && error != model.tap_audio_error) {
        ESP_LOGW(kTag, "Tap cue admission failed (%s)", esp_err_to_name(error));
    }
    model.tap_audio_error = error;
}

// model: Retains optional admission failures without waiting for the speaker.
// voice: Current canonical voice observation, or null when voice is excluded.
void play_device_key_cue(DeviceEffectsModel &model, const LUNASHELL::VoiceSnapshot *voice)
{
    if (!model.prepared || model.preparation_error != ESP_OK) return;
    if (voice != nullptr && (!voice_input_stopped(voice->input.phase) ||
            !voice_sound_state_known(*voice, esp_timer_get_time()))) return;
    lunashell_audio_cue_status_t status{};
    const auto error = LUNASHELL_audio_notify_codex(kTapPcm.data(), 320, kTapRateHz,
        kKeyCue, LUNASHELL_CODEX_CUE_NOTICE, &status);
    if (error != ESP_OK && error != model.tap_audio_error) {
        ESP_LOGW(kTag, "Keyboard cue admission failed (%s)", esp_err_to_name(error));
    }
    model.tap_audio_error = error;
}

// model: Retains the optional wake chime's admission result.
// voice: Current voice observation, or null where voice is excluded.
void play_device_wake_cue(DeviceEffectsModel &model, const LUNASHELL::VoiceSnapshot *voice)
{
    if (!model.prepared || model.preparation_error != ESP_OK) return;
    if (voice != nullptr && (!voice_input_stopped(voice->input.phase) ||
            !voice_sound_state_known(*voice, esp_timer_get_time()))) return;
    lunashell_audio_cue_status_t status{};
    const auto error = LUNASHELL_audio_notify_codex(kWakePcm.data(), kWakePcm.size(), kTapRateHz,
        kWakeCue, LUNASHELL_CODEX_CUE_NOTICE, &status);
    if (error != ESP_OK && error != model.wake_audio_error) {
        ESP_LOGW(kTag, "Wake cue admission failed (%s)", esp_err_to_name(error));
    }
    model.wake_audio_error = error;
}

// model: Retains the jump cue's admission result without changing the pet state.
// voice: Current voice observation, or null when voice is excluded.
void play_device_jump_cue(DeviceEffectsModel &model, const LUNASHELL::VoiceSnapshot *voice)
{
    if (!model.prepared || model.preparation_error != ESP_OK) return;
    if (voice != nullptr && (!voice_input_stopped(voice->input.phase) ||
            !voice_sound_state_known(*voice, esp_timer_get_time()))) return;
    lunashell_audio_cue_status_t status{};
    const auto error = LUNASHELL_audio_notify_codex(kJumpPcm.data(), kJumpPcm.size(), kTapRateHz,
        kJumpCue, LUNASHELL_CODEX_CUE_NOTICE, &status);
    if (error != ESP_OK && error != model.jump_audio_error) {
        ESP_LOGW(kTag, "Jump cue admission failed (%s)", esp_err_to_name(error));
    }
    model.jump_audio_error = error;
}

// model: Receives the optional icon cue's original admission error.
// symbol: One newly presented overlay, never a fabricated assistant event.
// voice: Current voice observation used to avoid interrupting capture.
void play_device_symbol_cue(DeviceEffectsModel &model, MuseSymbol symbol, const LUNASHELL::VoiceSnapshot *voice)
{
    if (!model.prepared || model.preparation_error != ESP_OK || symbol == MuseSymbol::None) return;
    if (voice != nullptr && (!voice_input_stopped(voice->input.phase) ||
            !voice_sound_state_known(*voice, esp_timer_get_time()))) return;
    CodexAudioCue cue = CodexAudioCue::None;
    switch (symbol) {
    case MuseSymbol::Question: cue = CodexAudioCue::Question; break;
    case MuseSymbol::Exclamation: cue = CodexAudioCue::ApprovalRequired; break;
    case MuseSymbol::Idea: cue = CodexAudioCue::ResponseReady; break;
    case MuseSymbol::Thinking: cue = CodexAudioCue::Reading; break;
    case MuseSymbol::Heart: cue = CodexAudioCue::AnswerReceived; break;
    case MuseSymbol::Sweat: cue = CodexAudioCue::Interrupted; break;
    case MuseSymbol::Tension: cue = CodexAudioCue::ChecksFailed; break;
    case MuseSymbol::Sleep: cue = CodexAudioCue::SessionEnded; break;
    case MuseSymbol::None: case MuseSymbol::Count: return;
    }
    CodexAudioStatus status{};
    const auto error = notify_codex_audio(cue, status);
    if (error != ESP_OK && error != model.symbol_audio_error) {
        ESP_LOGW(kTag, "Muse icon cue %u failed (%s)", static_cast<unsigned>(symbol), esp_err_to_name(error));
    }
    model.symbol_audio_error = error;
}

// model: Retained effect identities and canonical projection scratch, never a scene-owned cache.
// status: Existing canonical status used for source admission and connection edges.
// status_valid: Read success for status; false never manufactures a disconnection event.
// wifi: Independent canonical hardware Wi-Fi observation.
// wifi_valid: Read success for Wi-Fi, independent of the application projection.
// battery: Existing warning identity without altering its independent visual acknowledgment.
// voice: Optional already-read canonical recording observation.
// now_us: Monotonic time for expiry, debouncing and reminder policy.
// startup_wifi_offline: The startup owner's saved-network wait has settled offline.
// Exception: Preserves the first original read or playback error, logging only cause changes.
esp_err_t update_device_effects(DeviceEffectsModel &model,
    const LUNASHELL::CodexPresenceStatus &status, bool status_valid,
    const LUNASHELL::WifiSnapshot &wifi, bool wifi_valid,
    const BatteryAlertNotice &battery, const LUNASHELL::VoiceSnapshot *voice, int64_t now_us,
    bool startup_wifi_offline)
{
    SoundRequest request{};
    switch (model.feedback) {
    case DeviceFeedback::OptionsOpened:
        choose(request, CodexAudioCue::ConnectionRestored, SoundPriority::Feedback);
        break;
    case DeviceFeedback::TimerStarted:
        choose(request, CodexAudioCue::AnswerReceived, SoundPriority::Feedback);
        break;
    case DeviceFeedback::None: break;
    }
    model.feedback = DeviceFeedback::None;
    auto &snapshots = LUNASHELL::DeviceSnapshotService::instance();
    model.event_error = snapshots.readCodexEventObservation(model.event);
    model.pairing_error = snapshots.readPairingObservation(model.pairing);
    const bool source_current = status_valid && current(status.codex_link) && status.codex_app_open &&
        status.codex_link_state == LUNASHELL::CodexLinkState::Connected;
    const bool previously_online = model.event_online;
    const bool cadence_gap = model.last_tick_us > 0 &&
        (now_us < model.last_tick_us || now_us - model.last_tick_us > 5000000);
    model.last_tick_us = now_us;
    model.event_online = model.event_error == ESP_OK && source_current && current(model.event);
    if (cadence_gap || !previously_online || !model.event_online) {
        // Standby/source gaps cannot accumulate overdue reminders. New request IDs
        // still get their initial notice; previously consumed IDs stay consumed.
        for (auto &record : model.recent_events) {
            if (record.reminder_at_us > 0) {
                record.reminder_at_us = now_us;
            }
        }
    }
    if (cadence_gap) {
        model.event_sound = {};
        model.connection_known = false;
        model.connection_lost_at_us = 0;
        model.wifi_edge_since_us = 0;
    }
    observe_event(model, now_us, request);
    const auto event_sound_error = select_event_sound(model, request);
    observe_connection(model, status, status_valid, now_us, request);
    observe_wifi(model, wifi, wifi_valid, now_us, startup_wifi_offline, request);
    if (model.pairing_error == ESP_OK) {
        observe_pairing(model, request);
    }
    observe_battery(model, battery, request);
    const bool suspended = observe_voice(model, voice, now_us, request);
    // Optional audio failure must not suppress the independent attention model.
    const auto suspend_error = model.prepared && model.preparation_error == ESP_OK
        ? set_codex_audio_suspended(suspended) : model.preparation_error;
    esp_err_t error = model.event_error != ESP_OK ? model.event_error : model.pairing_error;
    if (error == ESP_OK) {
        error = event_sound_error != ESP_OK ? event_sound_error : suspend_error;
    }
    if (!suspended && suspend_error == ESP_OK && request.cue != CodexAudioCue::None) {
        const auto sound_error = request.priority == SoundPriority::VoiceWarning
            ? dispatch_voice_warning(model, request) : dispatch_sound(request, model.playback);
        if (request.priority == SoundPriority::EventNotice) {
            if (sound_error == ESP_OK && model.playback.state == CodexAudioState::Dispatched) {
                model.event_sound.request_sequence = model.playback.request_sequence;
            } else if (sound_error != ESP_OK || model.playback.state != CodexAudioState::DroppedBusy) {
                model.event_sound = {};
            }
        }
        if (error == ESP_OK) {
            error = sound_error;
        }
    }
    if (error != ESP_OK && error != model.last_error) {
        ESP_LOGW(kTag, "Optional notification effect failed (%s)", esp_err_to_name(error));
    }
    model.last_error = error;
    return error;
}

// model: Latest canonical event with real source health, independent of its UI route.
bool device_effects_require_attention(const DeviceEffectsModel &model)
{
    using Kind = LUNASHELL::CodexEventKind;
    return model.event_online && model.event.state == LUNASHELL::CodexEventState::Pending &&
        (model.event.expires_at_us <= 0 || model.last_tick_us < model.event.expires_at_us) &&
        (model.event.kind == Kind::Question || model.event.kind == Kind::ApprovalOnPc ||
         model.event.kind == Kind::ReplyReady);
}

} // namespace CODY
