#include "CODY_device_effects.hpp"

#include <cstring>

#include "LUNASHELL_platform.h"
#include "esp_log.h"

extern "C" {
extern const uint8_t boot_rise_start[] asm("_binary_cody_device_boot_rise_start");
extern const uint8_t boot_rise_end[] asm("_binary_cody_device_boot_rise_end");
}

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_effects";
constexpr uint32_t kBootRiseCue = 0x43430000U;
constexpr size_t kBootRiseSamples = 39690; // 1.8 seconds at the asset's authored 22.05 kHz.
constexpr int64_t kDisconnectDwellUs = 3000000;
constexpr int64_t kQuestionReminderUs = 60000000;
constexpr int64_t kVoiceStartFreshnessUs = 200000;

enum class SoundPriority : uint8_t { None, Feedback, Connection, Battery, Confirmation, Attention, CriticalBattery };

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

// request: One winning request for this tick; lower priorities are consumed without a queue.
// cue: Existing immutable PCM identity.
// priority: Semantic urgency independent of any rendered route.
// repetitions: Bounded repeated short notice, used only for low battery severity.
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

// model: Request ledger and successfully read canonical event.
// now_us: Monotonic time for actual event expiry and the quiet reminder cadence.
// request: Receives an eligible high-priority sound; identity is consumed even if audio is busy.
void observe_event(DeviceEffectsModel &model, int64_t now_us, SoundRequest &request)
{
    using Kind = LUNASHELL::CodexEventKind;
    using State = LUNASHELL::CodexEventState;
    const auto &event = model.event;
    if (!model.event_online || event.kind == Kind::None || event.request_id[0] == '\0' ||
        std::memchr(event.request_id, '\0', sizeof(event.request_id)) == nullptr) {
        return;
    }
    auto &record = event_record(model, event.request_id);
    if (event.kind == Kind::Question && event.state == State::Delivered &&
        event.selected_id[0] != '\0' && !record.delivery_announced) {
        record.delivery_announced = true;
        choose(request, CodexAudioCue::AnswerReceived, SoundPriority::Confirmation);
    }
    if (event.state != State::Pending ||
        (event.expires_at_us > 0 && now_us >= event.expires_at_us)) {
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
            choose(request, CodexAudioCue::Question, SoundPriority::Attention);
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
        choose(request, CodexAudioCue::ResponseReady, SoundPriority::Confirmation);
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
void observe_connection(DeviceEffectsModel &model, const LUNASHELL::DeviceStatusObservation &status,
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

// model: Voice session identity and one-shot start/delivery flags.
// voice: Optional original canonical voice snapshot; absent means no voice feature.
// now_us: Monotonic time used to admit the start cue only at the beginning of its countdown.
// request: Replaces other sounds during voice preparation and suppresses them during capture.
// Returns whether the shared Codex sound channel must remain suspended this tick.
bool observe_voice(DeviceEffectsModel &model, const LUNASHELL::VoiceSnapshot *voice,
    int64_t now_us, SoundRequest &request)
{
    using Phase = LUNASHELL::VoiceInputPhase;
    if (voice == nullptr) {
        return false;
    }
    if (!current(*voice)) {
        // AFE freshness and input lifecycle have separate owners. A published
        // idle/terminal input remains stopped when the wake-word heartbeat ages.
        const auto phase = voice->input.phase;
        const bool terminal = phase == Phase::Idle || phase == Phase::Failed ||
            phase == Phase::Cancelled || phase == Phase::Unconfirmed || phase == Phase::Delivered;
        const bool stopped = voice->state == LUNASHELL::VoiceState::Disabled ||
            voice->state == LUNASHELL::VoiceState::Failed;
        const bool published_input = voice->input.changed_at_us > 0 &&
            voice->input.changed_at_us <= now_us;
        if (!terminal || (!published_input && !(stopped && voice->last_attempt_at_us > 0))) {
            request = {};
            return true;
        }
    }
    const auto &input = voice->input;
    if (input.started_at_us > 0 && input.started_at_us != model.voice_session_us) {
        model.voice_session_us = input.started_at_us;
        model.voice_start_announced = false;
        model.voice_delivery_announced = false;
    }
    if (input.phase == Phase::Preparing || input.phase == Phase::StartCountdown) {
        request = {};
        // Three seconds are owned by the recording service. A <=350-ms cue with
        // a 200-ms dispatch lease is admitted only in the first 200 ms of that window.
        const bool starting = input.phase == Phase::StartCountdown && input.countdown_seconds == 3 &&
            input.changed_at_us > 0 && now_us >= input.changed_at_us &&
            now_us - input.changed_at_us <= kVoiceStartFreshnessUs;
        if (starting && !model.voice_start_announced) {
            model.voice_start_announced = true;
            choose(request, CodexAudioCue::ConnectionRestored, SoundPriority::Confirmation);
        }
        // Keep an admitted start cue alive only while at least two countdown seconds remain.
        return input.phase == Phase::Preparing || input.countdown_seconds < 2;
    }
    if (input.phase == Phase::Listening || input.phase == Phase::SendCountdown ||
        input.phase == Phase::Sending || input.phase == Phase::Cancelling || input.phase == Phase::Received) {
        request = {};
        return true;
    }
    if (input.phase == Phase::Delivered && input.error == ESP_OK && input.started_at_us > 0 &&
        !model.voice_delivery_announced) {
        model.voice_delivery_announced = true;
        choose(request, CodexAudioCue::AnswerReceived, SoundPriority::Confirmation);
    }
    return false;
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

// model: Retained effect identities and canonical projection scratch, never a scene-owned cache.
// status: Existing canonical status used for source admission and connection edges.
// status_valid: Read success for status; false never manufactures a disconnection event.
// battery: Existing warning identity without altering its independent visual acknowledgment.
// voice: Optional already-read canonical recording observation.
// now_us: Monotonic time for expiry, debouncing and reminder policy.
// Exception: Preserves the first original read or playback error, logging only cause changes.
esp_err_t update_device_effects(DeviceEffectsModel &model,
    const LUNASHELL::DeviceStatusObservation &status, bool status_valid,
    const BatteryAlertNotice &battery, const LUNASHELL::VoiceSnapshot *voice, int64_t now_us)
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
        model.connection_known = false;
        model.connection_lost_at_us = 0;
    }
    observe_event(model, now_us, request);
    observe_connection(model, status, status_valid, now_us, request);
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
        error = suspend_error;
    }
    if (!suspended && suspend_error == ESP_OK && request.cue != CodexAudioCue::None) {
        const auto sound_error = request.repetitions == 1
            ? notify_codex_audio(request.cue, model.playback)
            : notify_codex_audio_sequence(request.cue, request.repetitions, 150, model.playback);
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
