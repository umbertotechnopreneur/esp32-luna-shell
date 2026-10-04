#include "CODY_personality_audio.hpp"
#include "CODY_personality_config.hpp"
#include "LUNASHELL_platform.h"
#include "personality_audio.hpp"
#include "esp_log.h"

#include <atomic>

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_personality_audio";
std::atomic<bool> s_enabled{true};
std::atomic<bool> s_suspended{false};

// source: Coherent outcome from the canonical asynchronous audio owner.
// destination: Receives typed Cody identities without relying on catalog ordering.
void copy_status(const lunashell_audio_cue_status_t &source, PersonalityAudioStatus &destination)
{
    switch (source.state) {
    case LUNASHELL_CUE_IDLE: destination.state = PersonalityAudioState::Idle; break;
    case LUNASHELL_CUE_DISPATCHED: destination.state = PersonalityAudioState::Dispatched; break;
    case LUNASHELL_CUE_PLAYING: destination.state = PersonalityAudioState::Playing; break;
    case LUNASHELL_CUE_COMPLETED: destination.state = PersonalityAudioState::Completed; break;
    case LUNASHELL_CUE_DROPPED_BUSY: destination.state = PersonalityAudioState::DroppedBusy; break;
    case LUNASHELL_CUE_DROPPED_COOLDOWN: destination.state = PersonalityAudioState::DroppedCooldown; break;
    case LUNASHELL_CUE_SUPPRESSED: destination.state = PersonalityAudioState::Suppressed; break;
    case LUNASHELL_CUE_EXPIRED: destination.state = PersonalityAudioState::Expired; break;
    case LUNASHELL_CUE_FAILED: destination.state = PersonalityAudioState::Failed; break;
    case LUNASHELL_CUE_UNAVAILABLE: destination.state = PersonalityAudioState::Unavailable; break;
    }
    destination.last_error = source.last_error;
    destination.request_sequence = source.request_sequence;
    destination.cue = static_cast<PersonalityCue>(source.cue_id);
}
} // namespace

// No parameters: registers YAML-authored admission settings; PCM already lives in immutable flash.
// Exception: Returns the actual audio owner preparation failure.
esp_err_t prepare_personality_audio()
{
    constexpr lunashell_personality_audio_policy_t policy{
        PersonalityAudioConfig::enabled,
        PersonalityAudioConfig::cooldown_us,
        PersonalityAudioConfig::request_expiry_us,
    };
    return LUNASHELL_audio_prepare_personality(&policy);
}

// cue: Stable identity appearing in a retired visible frame, resolved against the YAML cue catalog.
// status: Receives the disposition of this one attempt.
// Exception: Returns the real identity or asynchronous audio owner error.
esp_err_t notify_personality_audio(PersonalityCue cue, PersonalityAudioStatus &status)
{
    const PersonalityAudioAssets::Clip *selected = nullptr;
    for (const auto &clip : PersonalityAudioAssets::clips) {
        if (clip.cue == cue) {
            selected = &clip;
            break;
        }
    }
    if (selected == nullptr) {
        status = {PersonalityAudioState::Failed, ESP_ERR_INVALID_ARG, 0, cue};
        ESP_LOGE(kTag, "Unknown sound cue=%u rejected (%s)", static_cast<unsigned>(cue),
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const PersonalityConfig::CueRule *authored_rule = nullptr;
    for (const auto &rule : PersonalityConfig::cue_rules) {
        if (rule.cue != cue) {
            continue;
        }
        if (authored_rule != nullptr) {
            status = {PersonalityAudioState::Failed, ESP_ERR_INVALID_STATE, 0, cue};
            ESP_LOGE(kTag, "Ambiguous authored sound cue=%u rejected (%s)",
                static_cast<unsigned>(cue), esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        authored_rule = &rule;
    }
    if (authored_rule == nullptr) {
        status = {PersonalityAudioState::Failed, ESP_ERR_INVALID_STATE, 0, cue};
        ESP_LOGE(kTag, "Missing authored sound cue=%u rejected (%s)",
            static_cast<unsigned>(cue), esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (!PersonalityAudioConfig::enabled || !authored_rule->sfx_enabled ||
        !s_enabled.load(std::memory_order_acquire) || !selected->enabled) {
        const esp_err_t error = get_personality_audio_status(status);
        if (error == ESP_OK) {
            status.state = PersonalityAudioState::Suppressed;
            status.cue = cue;
        }
        return error;
    }
    lunashell_audio_cue_status_t owner{};
    const esp_err_t error = LUNASHELL_audio_notify_personality(selected->samples, selected->sample_count,
        selected->sample_rate_hz, static_cast<uint32_t>(cue), &owner);
    copy_status(owner, status);
    return error;
}

// suspended: Scene/display policy that cancels unplayed sound without replay on resume.
// Exception: Returns the retained owner error.
esp_err_t set_personality_audio_suspended(bool suspended)
{
    s_suspended.store(suspended, std::memory_order_release);
    return LUNASHELL_audio_set_personality_suspended(
        suspended || !PersonalityAudioConfig::enabled || !s_enabled.load(std::memory_order_acquire));
}

// enabled: Session sound gate; true cannot override the YAML build-time disable policy.
// Exception: Returns the retained owner error.
esp_err_t set_personality_audio_enabled(bool enabled)
{
    s_enabled.store(enabled, std::memory_order_release);
    return LUNASHELL_audio_set_personality_suspended(
        !PersonalityAudioConfig::enabled || !enabled || s_suspended.load(std::memory_order_acquire));
}

// status: Receives actual retained owner state, including asynchronous failure.
// Exception: Returns the original retained owner error.
esp_err_t get_personality_audio_status(PersonalityAudioStatus &status)
{
    lunashell_audio_cue_status_t owner{};
    const esp_err_t error = LUNASHELL_audio_get_personality_status(&owner);
    copy_status(owner, status);
    return error;
}
} // namespace CODY
