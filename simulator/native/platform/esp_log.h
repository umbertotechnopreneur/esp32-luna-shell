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

// tag: Original firmware component name.
// format: Original firmware message format.
// ...: Original arguments, including real operation failure causes.
inline void cody_host_log(const char *tag, const char *format, ...)
{
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
