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

// Process-lifetime RGB565 background generated with the canonical ordered Bayer
// pattern. The pixels live in PSRAM so the shared background remains an image,
// not a renderer-dependent gradient.

#include <atomic>
#include <cstdint>

#include "lvgl.h"

namespace LUNASHELL {

class BackgroundAsset final {
public:
    static constexpr uint16_t kWidth = 466;
    static constexpr uint16_t kHeight = 466;

    static BackgroundAsset &instance();

    // Start the generator once; the GUI thread only observes the published image.
    void request();

    [[nodiscard]] const lv_image_dsc_t *image() const;

private:
    enum class State : uint8_t {
        Idle,
        Working,
        Ready,
        Failed,
    };

    BackgroundAsset() = default;

    static void worker_entry(void *argument);
    void run_worker();
    void publish(uint8_t *pixels);

    std::atomic<State> state_{State::Idle};
    lv_image_dsc_t image_{};
};

}  // namespace LUNASHELL
