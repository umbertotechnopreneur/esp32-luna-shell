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

#include "CODY_codex_audio.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_codex_audio";

// source: Coherent shared audio owner state with its actual application cue identity.
// destination: Receives a typed Codex view without treating another channel as a Codex sound.
void copy_status(const lunashell_audio_cue_status_t &source, CodexAudioStatus &destination)
{
    switch (source.state) {
    case LUNASHELL_CUE_IDLE: destination.state = CodexAudioState::Idle; break;
    case LUNASHELL_CUE_DISPATCHED: destination.state = CodexAudioState::Dispatched; break;
    case LUNASHELL_CUE_PLAYING: destination.state = CodexAudioState::Playing; break;
    case LUNASHELL_CUE_COMPLETED: destination.state = CodexAudioState::Completed; break;
    case LUNASHELL_CUE_DROPPED_BUSY: destination.state = CodexAudioState::DroppedBusy; break;
    case LUNASHELL_CUE_DROPPED_COOLDOWN: destination.state = CodexAudioState::DroppedCooldown; break;
    case LUNASHELL_CUE_SUPPRESSED: destination.state = CodexAudioState::Suppressed; break;
    case LUNASHELL_CUE_EXPIRED: destination.state = CodexAudioState::Expired; break;
    case LUNASHELL_CUE_FAILED: destination.state = CodexAudioState::Failed; break;
    case LUNASHELL_CUE_UNAVAILABLE: destination.state = CodexAudioState::Unavailable; break;
    }
    destination.last_error = source.last_error;
    destination.request_sequence = source.request_sequence;
    destination.cue = CodexAudioCue::None;
    for (const auto &clip : CodexAudioAssets::clips) {
        if (source.cue_id == static_cast<uint32_t>(clip.cue)) {
            destination.cue = clip.cue;
            break;
        }
    }
}

// cue: Stable typed identity already resolved against the authored catalog.
// Returns activity throttling only for ongoing work transitions; notices still respect busy admission.
lunashell_codex_audio_kind_t cue_kind(CodexAudioCue cue)
{
    switch (cue) {
    case CodexAudioCue::Working:
    case CodexAudioCue::Reading:
    case CodexAudioCue::Editing:
    case CodexAudioCue::Running:
        return LUNASHELL_CODEX_CUE_ACTIVITY;
    default:
        return LUNASHELL_CODEX_CUE_NOTICE;
    }
}
} // namespace

// cue: Stable authored identity exposed in the current companion presentation.
// status: Receives explicit owner admission; no delayed replay is scheduled.
// Exception: Returns invalid identities or original shared-owner errors.
esp_err_t notify_codex_audio(CodexAudioCue cue, CodexAudioStatus &status)
{
    const CodexAudioAssets::Clip *selected = nullptr;
    for (const auto &clip : CodexAudioAssets::clips) {
        if (clip.cue == cue) {
            selected = &clip;
            break;
        }
    }
    if (selected == nullptr) {
        status = {CodexAudioState::Failed, ESP_ERR_INVALID_ARG, 0, cue};
        ESP_LOGE(kTag, "Unknown Codex audio cue=%lu rejected (%s)",
            static_cast<unsigned long>(cue), esp_err_to_name(ESP_ERR_INVALID_ARG));
        const esp_err_t publication_error = LUNASHELL::DeviceSnapshotService::instance().reportPlayback(
            false, 0, 16000, 0, ESP_ERR_INVALID_ARG, static_cast<uint32_t>(cue));
        if (publication_error != ESP_OK) {
            ESP_LOGE(kTag, "Rejected cue publication failed (%s)", esp_err_to_name(publication_error));
        }
        return ESP_ERR_INVALID_ARG;
    }
    lunashell_audio_cue_status_t owner{};
    const esp_err_t error = LUNASHELL_audio_notify_codex(selected->samples, selected->sample_count,
        selected->sample_rate_hz, static_cast<uint32_t>(cue), cue_kind(cue), &owner);
    copy_status(owner, status);
    return error;
}
// alert: One sustained initial bell or one shorter, quieter reminder.
// status: Receives the existing owner's actual result; no beep sequence is queued.
// Exception: Returns an invalid alert identity or the original shared-owner failure.
esp_err_t notify_question_audio(QuestionAudioAlert alert, CodexAudioStatus &status)
{
    switch (alert) {
    case QuestionAudioAlert::Initial:
        return notify_codex_audio(CodexAudioCue::Question, status);
    case QuestionAudioAlert::Reminder:
        return notify_codex_audio(CodexAudioCue::QuestionReminder, status);
    case QuestionAudioAlert::None:
        break;
    }
    status = {CodexAudioState::Failed, ESP_ERR_INVALID_ARG, 0, CodexAudioCue::Question};
    ESP_LOGE(kTag, "Question audio alert=%u rejected (%s)",
        static_cast<unsigned>(alert), esp_err_to_name(ESP_ERR_INVALID_ARG));
    return ESP_ERR_INVALID_ARG;
}

// suspended: Cancels only Codex presentation sounds; ordinary symbol visibility remains independent.
// Exception: Returns the original retained owner error.
esp_err_t set_codex_audio_suspended(bool suspended)
{
    return LUNASHELL_audio_set_codex_suspended(suspended);
}

// enabled: Explicit session sound choice, layered below the immutable shared build policy.
// Exception: Returns the original retained owner error.
esp_err_t set_codex_audio_enabled(bool enabled)
{
    return LUNASHELL_audio_set_codex_enabled(enabled);
}

// status: Receives the retained asynchronous owner result and a known Codex identity, if any.
// Exception: Returns the original retained owner failure.
esp_err_t get_codex_audio_status(CodexAudioStatus &status)
{
    lunashell_audio_cue_status_t owner{};
    const esp_err_t error = LUNASHELL_audio_get_personality_status(&owner);
    copy_status(owner, status);
    return error;
}
} // namespace CODY
