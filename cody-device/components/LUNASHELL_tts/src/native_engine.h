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
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// No parameters: initialize the synchronous engine under its allocator guard.
// Exception: Returns original eSpeak/errno status, without exiting or asserting.
int luna_tts_engine_initialize(void);
// voice_path: Exact manifest-owned language definition path.
// text: Validated null-terminated UTF-8 owned until synthesis returns.
// bytes: Exact input byte count including terminator.
// rate: Explicit bounded words-per-minute.
// pitch: Explicit 0..100 pitch.
// range: Explicit 0..100 pitch range.
// callback: Synchronous borrowed PCM16 sink; nonzero stops synthesis.
// Exception: Returns original eSpeak/errno status, including genuine stop/ENOMEM.
int luna_tts_engine_synthesize(const char *voice_path, const char *text, size_t bytes,
                              int rate, int pitch, int range,
                              int (*callback)(short *, int, void *));
// No parameters: sample rate from successfully loaded phonetic data.
int luna_tts_engine_sample_rate(void);
#ifdef __cplusplus
}
#endif
