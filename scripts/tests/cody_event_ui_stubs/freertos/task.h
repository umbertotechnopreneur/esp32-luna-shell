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

uint32_t ulTaskNotifyTake(bool clear_on_exit, uint32_t timeout);
void xTaskNotifyGive(TaskHandle_t task);
TaskHandle_t xTaskCreateStatic(void (*worker)(void *), const char *name,
    uint32_t stack_bytes, void *argument, uint32_t priority,
    StackType_t *stack, StaticTask_t *storage);
