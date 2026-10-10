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

#include "LUNASHELL_mascot_asset.hpp"

#include <cstddef>

#include "esp_log.h"

namespace {

constexpr char kTag[] = "LUNASHELL_mascot";
constexpr size_t kExpectedBytes =
    static_cast<size_t>(LUNASHELL::MascotAsset::kWidth) *
    LUNASHELL::MascotAsset::kHeight * 4U;

extern const uint8_t mascot_idle_start[] asm("_binary_LUNASHELL_mascot_idle_start");
extern const uint8_t mascot_idle_end[] asm("_binary_LUNASHELL_mascot_idle_end");

}  // namespace

namespace LUNASHELL {

const lv_image_dsc_t *MascotAsset::idle_head_image()
{
    const size_t size = static_cast<size_t>(mascot_idle_end - mascot_idle_start);
    if (size != kExpectedBytes) {
        ESP_LOGE(kTag, "Idle head asset size invalid: %u, expected %u",
                 static_cast<unsigned>(size), static_cast<unsigned>(kExpectedBytes));
        return nullptr;
    }

    static const lv_image_dsc_t image = [] {
        lv_image_dsc_t descriptor{};
        descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
        descriptor.header.cf = LV_COLOR_FORMAT_ARGB8888;
        descriptor.header.flags = 0;
        descriptor.header.w = kWidth;
        descriptor.header.h = kHeight;
        descriptor.header.stride = kWidth * 4U;
        descriptor.data_size = kExpectedBytes;
        descriptor.data = mascot_idle_start;
        return descriptor;
    }();
    return &image;
}

}  // namespace LUNASHELL
