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

// Declaration-only SDK boundary for the portable event-layout fixture. No task
// execution is emulated: the linked owner boundary aborts if a task is invoked.
using StackType_t = uint32_t;
using TaskHandle_t = void *;
struct StaticTask_t { uint32_t unused; };
constexpr bool pdTRUE = true;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
