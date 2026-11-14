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
#include <cstdarg>
#include <cstdio>
#include <cstring>

// tag: Original firmware component name.
// format: Original firmware message format.
// ...: Original arguments, including real operation failure causes.
inline void cody_host_log(const char *tag, const char *format, ...)
{
    // Device FreeRTOS stack high-water marks have no host equivalent. Do not
    // publish the host shim's sentinel as if it were a measured byte count.
    if (std::strstr(format, "minimum free stack") != nullptr) {
        std::fprintf(stderr, "%s: stack headroom unavailable (SIMULATED)\n", tag);
        return;
    }
    std::fprintf(stderr, "%s: ", tag);
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::fputc('\n', stderr);
}
#define ESP_LOGE(tag, ...) cody_host_log(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) cody_host_log(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) cody_host_log(tag, __VA_ARGS__)
