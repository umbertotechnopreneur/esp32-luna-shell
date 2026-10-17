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

#include "sdkconfig.h"

namespace LUNASHELL::task_affinity {

// Audio acquisition, ESP-SR inference and its internal AFE task stay on CPU0.
// The native Cody frame owner stays on CPU1, including wake-word variants.
constexpr int kVoiceCoreId = 0;
constexpr int kCodyRenderCoreId = 1;
// Activity acquisition keeps its 20 Hz lease away from CPU0's voice workers.
constexpr int kActivityAcquisitionCoreId = 1;
// CCP's CPU-bound verifier runs below acquisition workers, away from native frames.
constexpr int kCcpCoreId = 0;
static_assert(kCcpCoreId != kCodyRenderCoreId, "CCP crypto must not preempt the Cody renderer");

#if CONFIG_LUNASHELL_ACTIVITY_MODEL
#if CONFIG_FREERTOS_UNICORE
#error "Activity acquisition requires dual cores"
#endif
static_assert(kActivityAcquisitionCoreId != kVoiceCoreId,
              "Activity acquisition must not share the voice worker core");
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
#if CONFIG_FREERTOS_UNICORE
#error "Wake word requires a separate renderer core"
#endif
static_assert(kVoiceCoreId != kCodyRenderCoreId, "Voice must not share the Cody renderer core");
#endif

} // namespace LUNASHELL::task_affinity
