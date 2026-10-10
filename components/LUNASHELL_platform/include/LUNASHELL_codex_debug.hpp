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

#include <cstdint>
#include "esp_err.h"
#include "sdkconfig.h"

namespace LUNASHELL {

enum class CodexDebugScreen : uint8_t { Normal, FaceChannels, Character };

struct CodexDebugScreenRequest {
    CodexDebugScreen screen = CodexDebugScreen::Normal;
    uint8_t scroll = 0;
    char request_id[33]{};
};

#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG && CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION && \
    CONFIG_LUNASHELL_BUILD_ENABLE_WIFI && CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX
// request: Receives one authenticated, unexpired command for the rendering worker.
// Exception: Returns NOT_FOUND for no pending request; no sensors or settings are read.
esp_err_t takeCodexDebugScreen(CodexDebugScreenRequest &request);

// request: Exact mailbox identity admitted by takeCodexDebugScreen.
// result: Real rendering-worker application result, never a pixel observation.
// channel_count: Actual compiled channel catalog size, not a hard-coded guess.
// Exception: Rejects retired commands; the CCP worker sends the authenticated ACK.
esp_err_t completeCodexDebugScreen(const CodexDebugScreenRequest &request, esp_err_t result,
                                  uint8_t channel_count);
#endif

}  // namespace LUNASHELL
