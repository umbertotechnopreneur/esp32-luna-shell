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

using BaseType_t = int32_t;
using UBaseType_t = uint32_t;
using StackType_t = uint32_t;
using TickType_t = uint32_t;
struct StaticTask_t { uint32_t unused; };
struct StaticSemaphore_t { bool available; };
using TaskHandle_t = StaticTask_t *;
using SemaphoreHandle_t = StaticSemaphore_t *;
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr BaseType_t pdPASS = 1;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(milliseconds) (static_cast<TickType_t>(milliseconds))
