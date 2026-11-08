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

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "lvgl.h"

namespace LUNASHELL {

// Owns one reusable grayscale image for both Espressif app and device QR payloads.
class PairingQrAsset final {
public:
    static constexpr uint16_t kSize = 140;

    PairingQrAsset() = default;
    ~PairingQrAsset();

    PairingQrAsset(const PairingQrAsset &) = delete;
    PairingQrAsset &operator=(const PairingQrAsset &) = delete;

    esp_err_t generate(const char *payload);
    void reset();
    [[nodiscard]] const lv_image_dsc_t *image() const;

private:
    static void render_callback(const uint8_t *qrcode);
    void render(const uint8_t *qrcode);

    static PairingQrAsset *rendering_;
    uint8_t *pixels_ = nullptr;
    size_t stride_ = 0;
    size_t data_size_ = 0;
    lv_image_dsc_t image_{};
};

}  // namespace LUNASHELL
