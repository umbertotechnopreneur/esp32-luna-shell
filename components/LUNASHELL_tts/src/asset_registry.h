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
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    const char *path;
    const uint8_t *data;
    size_t size;
    uint8_t sha256[32];
} luna_tts_asset_t;
extern luna_tts_asset_t luna_tts_assets[];
extern const size_t luna_tts_asset_count;
// path: Exact engine path inside the immutable package.
// size: Receives the manifest size only for an available asset.
void *espeak_mem_map(const char *path, int *size);
// path: Exact engine path or its virtual package directory.
// Exception: Returns original negative errno for absent assets.
int luna_espeak_asset_size(const char *path);
// pointer: Candidate data pointer; registry assets remain owned by their package.
int luna_espeak_borrowed(const void *pointer);
// path: Exact registry path; no consumer-side filesystem access is performed.
// mode: Read-only mode; writes are rejected.
// Exception: Returns NULL with original errno on an unavailable stream.
FILE *luna_espeak_fopen(const char *path, const char *mode);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
// stream: Registry-backed stream whose close failure must reach the engine guard.
// Exception: Retains the original errno cause instead of discarding it.
int luna_espeak_fclose(FILE *stream);
#ifdef __cplusplus
}
#endif
