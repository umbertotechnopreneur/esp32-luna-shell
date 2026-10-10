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

#include "asset_registry.h"
#include "mem_alloc.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

static const char root[] = "/luna-tts/espeak-ng-data";

// path: Exact engine path, never a host/card path.
static luna_tts_asset_t *find_asset(const char *path)
{
    if (path == NULL || strncmp(path, root, sizeof(root) - 1) != 0 ||
        path[sizeof(root) - 1] != '/') return NULL;
    const char *relative = path + sizeof(root);
    for (size_t index = 0; index < luna_tts_asset_count; ++index) {
        if (strcmp(relative, luna_tts_assets[index].path) == 0)
            return &luna_tts_assets[index];
    }
    return NULL;
}

// path: Requested immutable package entry.
// size: Receives its length only when the entry is loaded.
void *espeak_mem_map(const char *path, int *size)
{
    luna_tts_asset_t *asset = find_asset(path);
    if (asset == NULL || asset->data == NULL || asset->size > INT_MAX) {
        errno = ENOENT;
        return NULL;
    }
    *size = (int)asset->size;
    return (void *)asset->data;
}

// path: Asset or virtual root directory path.
int luna_espeak_asset_size(const char *path)
{
    if (path != NULL && strcmp(path, root) == 0) return -EISDIR;
    int size = 0;
    if (espeak_mem_map(path, &size) == NULL) return -errno;
    return size;
}

// pointer: Exact registry pointer, independent of storage backend.
int luna_espeak_borrowed(const void *pointer)
{
    for (size_t index = 0; index < luna_tts_asset_count; ++index) {
        if (pointer == luna_tts_assets[index].data) return 1;
    }
    return 0;
}

// path: Registry-owned file; no direct SD or sensor read is performed.
// mode: Read-only stream mode.
// Exception: Preserves libc stream-allocation failure and rejects writes.
FILE *luna_espeak_fopen(const char *path, const char *mode)
{
    if (mode == NULL || (strcmp(mode, "r") != 0 && strcmp(mode, "rb") != 0)) {
        errno = EROFS;
        return NULL;
    }
    int size = 0;
    void *data = espeak_mem_map(path, &size);
    if (data == NULL) return NULL;
    errno = 0;
    FILE *stream = fmemopen(data, (size_t)size, "rb");
    if (stream == NULL) luna_espeak_fail(errno == 0 ? EIO : errno);
    return stream;
}

// stream: Engine-owned registry stream, closed synchronously in the active C boundary.
// Exception: Returns through the native guard with the real stream-close errno.
int luna_espeak_fclose(FILE *stream)
{
    errno = 0;
    const int result = fclose(stream);
    if (result != 0) luna_espeak_fail(errno == 0 ? EIO : errno);
    return result;
}
