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
#include "FreeRTOS.h"

TaskHandle_t xTaskCreateStatic(void (*)(void *), const char *, uint32_t, void *,
                             UBaseType_t, StackType_t *, StaticTask_t *);
BaseType_t xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
