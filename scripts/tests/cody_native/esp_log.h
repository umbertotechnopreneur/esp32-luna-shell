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

// tag: Firmware component reporting a rejected input.
// format: Original format string from the portable firmware source.
// ...: Original log arguments; values are consumed so warnings remain meaningful.
inline void host_error_log(const char *tag, const char *format, ...)
{
    std::fprintf(stderr, "%s: ", tag);
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::fputc('\n', stderr);
}
#define ESP_LOGE(tag, ...) host_error_log(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) host_error_log(tag, __VA_ARGS__)
