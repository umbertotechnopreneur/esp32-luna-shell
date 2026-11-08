/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#include "CODY_personality_audio.hpp"
#include "CODY_personality_config.hpp"
#include "LUNASHELL_platform.h"
#include "personality_audio.hpp"
#include "esp_log.h"

#include <atomic>
#include <array>

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_personality_audio";
std::atomic<bool> s_enabled{true};
std::atomic<bool> s_suspended{false};

// No parameters: original two-note whistle, authored into flash at compile time.
constexpr std::array<int16_t, 4800> make_idle_whistle()
{
    constexpr int16_t wave[] = {0, 1148, 2121, 2772, 3000, 2772, 2121, 1148,
                               0, -1148, -2121, -2772, -3000, -2772, -2121, -1148};
    std::array<int16_t, 4800> samples{};
    uint32_t phase = 0;
    for (size_t index = 0; index < samples.size(); ++index) {
        // Each 140-ms chirp has a soft attack/release and a 20-ms silent gap.
        const size_t note_position = index < 2400 ? index : index - 2400;
        if (note_position >= 2240) {
            continue;
        }
        const uint32_t note_sample = static_cast<uint32_t>(note_position);
        const uint32_t frequency = index < 2400 ? 1500U + note_sample / 8U : 1850U - note_sample / 10U;
        phase = (phase + frequency * 65536U / 16000U) & 0xFFFFU;
        const uint32_t table_position = phase * 16U;
        const size_t cell = table_position >> 16;
        const uint32_t fraction = table_position & 0xFFFFU;
        const int32_t value = (wave[cell] * static_cast<int32_t>(65536U - fraction) +
            wave[(cell + 1) % 16] * static_cast<int32_t>(fraction)) / 65536;
        const size_t attack = note_position < 320 ? note_position : 320;
        const size_t release = 2240 - note_position < 320 ? 2240 - note_position : 320;
        const size_t envelope = attack < release ? attack : release;
        samples[index] = static_cast<int16_t>(value * static_cast<int32_t>(envelope) / 320);
    }
    return samples;
}
constexpr auto kIdleWhistle = make_idle_whistle();

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
    const bool idle_whistle = source.cue_id == static_cast<uint32_t>(IdleSceneId::Whistle);
    destination.cue = PersonalityCue::None;
    // The same owner also plays Codex notices; never cast their namespace to a symbol identity.
    for (const auto &clip : PersonalityAudioAssets::clips) {
        if (source.cue_id == static_cast<uint32_t>(clip.cue)) {
            destination.cue = clip.cue;
            break;
        }
    }
    destination.idle_scene = idle_whistle ? IdleSceneId::Whistle : IdleSceneId::None;
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
        status = {PersonalityAudioState::Failed, ESP_ERR_INVALID_ARG, 0, cue, IdleSceneId::None};
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
            status = {PersonalityAudioState::Failed, ESP_ERR_INVALID_STATE, 0, cue, IdleSceneId::None};
            ESP_LOGE(kTag, "Ambiguous authored sound cue=%u rejected (%s)",
                static_cast<unsigned>(cue), esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        authored_rule = &rule;
    }
    if (authored_rule == nullptr) {
        status = {PersonalityAudioState::Failed, ESP_ERR_INVALID_STATE, 0, cue, IdleSceneId::None};
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

// status: Receives the one nonqueued whistle outcome from the existing audio owner.
// Exception: Returns the original preparation, admission or retained playback error.
esp_err_t notify_idle_whistle_audio(PersonalityAudioStatus &status)
{
    lunashell_audio_cue_status_t owner{};
    if (!PersonalityAudioConfig::enabled || !s_enabled.load(std::memory_order_acquire)) {
        const esp_err_t error = LUNASHELL_audio_get_personality_status(&owner);
        copy_status(owner, status);
        status.idle_scene = IdleSceneId::Whistle;
        if (error == ESP_OK) {
            status.state = PersonalityAudioState::Suppressed;
        }
        return error;
    }
    const esp_err_t error = LUNASHELL_audio_notify_personality(kIdleWhistle.data(), kIdleWhistle.size(),
        16000, static_cast<uint32_t>(IdleSceneId::Whistle), &owner);
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
