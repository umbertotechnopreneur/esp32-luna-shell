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

#include "lvgl.h"

namespace LUNASHELL {

class MascotAsset final {
public:
    static constexpr uint16_t kWidth = 170;
    static constexpr uint16_t kHeight = 198;

    [[nodiscard]] static const lv_image_dsc_t *idle_head_image();
};

}  // namespace LUNASHELL
