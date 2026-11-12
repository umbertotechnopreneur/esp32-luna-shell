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

#pragma once
#include "esp_err.h"
#include <cstdint>

namespace CODY {
struct WidgetVisibility;

enum class PreferenceId : uint8_t {
    ShakeWake = 0, AutoRotate = 1, Sounds = 2, Orientation = 3, Brightness = 5,
    Dictation = 6, Screensaver = 7, SynthEnabled = 8, SynthVolume = 9, SynthVoice = 10,
    AssistantQuotaRing = 11, BatteryRing = 12, AutoTime = 13,
    PlaybackVolume = 15, MicrophoneGain = 16,
    QuietEnabled = 14, QuietStart = 17, QuietEnd = 18,
    Sofia = 19, CounterMode = 20,
};

// No parameters: loads exact persisted values, creating only genuinely absent keys.
// Exception: Returns the real storage, owner-policy or publication error.
esp_err_t load_companion_preferences();

// preference: Stable identity of the explicit user choice.
// value: Boolean/mode 0/1, manual angle 0/180, brightness percent or local quiet minute 0..1439.
// Exception: Returns the real validation, persistence, application or publication error.
esp_err_t persist_companion_preference(PreferenceId preference, uint32_t value);

// No parameters: reconcile stored output and microphone preferences with the codec on a worker.
// Exception: Returns the first original NVS, codec, readback or snapshot-publication error.
esp_err_t reconcile_audio_preferences();

// No parameters: restore only the saved output policy before the optional boot cue.
// Exception: Returns the original NVS, codec, readback or snapshot-publication error.
esp_err_t reconcile_audio_output_preference();

// visibility: Receives the validated preference after a confirmed read or first initialization.
// Exception: Returns the original storage error or ESP_ERR_INVALID_RESPONSE for an invalid record.
// Call from the settings worker or startup owner.
esp_err_t load_widget_visibility(WidgetVisibility &visibility);

// visibility: Explicit visibility choice with stable screen and child masks.
// Exception: Returns validation, storage or readback failure without repairing a rejected record.
// Call from the settings worker or startup owner.
esp_err_t persist_widget_visibility(const WidgetVisibility &visibility);

struct OnboardingSettings {
    bool persisted_first_boot = false;
    bool force_first_boot = false;
    bool effective_first_boot = false;
    bool birth_intro_pending = false;
    bool setup_progress_pending = false;
};

// settings: Receives persisted/default/explicit-development decisions on success only.
esp_err_t load_onboarding_settings(OnboardingSettings &settings);

// first_boot: Explicit user preference; greeting completion never writes this key.
// Exception: Returns the original write/readback error or ESP_ERR_INVALID_RESPONSE for a mismatched value.
esp_err_t persist_first_boot(bool first_boot);

// pending: Whether the first-run Muse introduction still needs to retire.
// Exception: Returns the original write/readback error or a readback mismatch.
esp_err_t persist_birth_intro_pending(bool pending);

// pending: Whether the initial Wi-Fi and Codex connection milestone remains outstanding.
// Exception: Returns the original write/readback error or a readback mismatch.
esp_err_t persist_setup_progress_pending(bool pending);
} // namespace CODY
