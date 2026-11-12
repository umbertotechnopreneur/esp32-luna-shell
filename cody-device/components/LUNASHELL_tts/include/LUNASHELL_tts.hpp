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

#include "LUNASHELL_speech_state.hpp"
#include "LUNASHELL_codex_events.hpp"
#include "esp_err.h"
#include <cstddef>
#include <cstdint>

namespace LUNASHELL {

struct SpeechRequest {
    SpeechLanguage language = SpeechLanguage::English;
    const char *text = nullptr;
    std::size_t text_bytes = 0;
    uint16_t words_per_minute = 145;
    uint8_t pitch = 40;
    uint8_t pitch_range = 20;
};

struct SpeechEventIdentity {
    const char *event_id = nullptr;
    uint64_t revision = 0;
    CodexEventKind kind = CodexEventKind::None;
    CodexEventState state = CodexEventState::None;
    const char *selected_id = nullptr;
};

// No parameters: creates a silent service; does not synthesize or read the card.
// Exception: Returns worker/allocation/publication errors; TTS is optional at startup.
esp_err_t initialize_speech();
// request: Bounded validated UTF-8 and explicit supported voice; copied on admission.
// request_id: Receives a stable nonzero identity; admission is not playback completion.
// Exception: Returns rejected format, language, lifecycle, busy or publication errors.
esp_err_t request_speech(const SpeechRequest &request, uint32_t &request_id);
// request: Validated Codex question or answer text, copied on admission.
// event: Canonical event identity revalidated during speech; strings are copied.
// request_id: Receives the nonzero speech request identity.
// Exception: Returns the original input, policy, lifecycle or admission failure.
esp_err_t request_codex_speech(const SpeechRequest &request, const SpeechEventIdentity &event, uint32_t &request_id);
// No parameters: cancels the active Codex event speech, if one is owned.
// Exception: None; explicit test speech and unrelated requests remain untouched.
void cancel_codex_speech();
#if CONFIG_LUNASHELL_TTS_BOOT_TEST_ITALIAN
// No parameters: queues the fixed Italian boot phrase once; worker awaits the initial SD report.
// Exception: Returns admission error; active power, card integrity and audio volume still apply.
esp_err_t request_boot_speech_test();
#endif
// sealed: True blocks admission, cancels the exact active request and waits for worker retirement.
// False reopens admission only after owned work has actually retired.
// Exception: Returns the real operation/publication error or bounded timeout; timeout keeps the seal.
esp_err_t set_speech_power_off_sealed(bool sealed);

// request_id: Exact current identity to cancel; never cancels a later request.
// Exception: Returns an invalid identity/state or retained publication failure.
esp_err_t cancel_speech(uint32_t request_id);

} // namespace LUNASHELL
