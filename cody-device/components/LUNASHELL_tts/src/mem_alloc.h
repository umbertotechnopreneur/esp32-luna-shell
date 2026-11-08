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
// size: Requested working bytes, charged against the dedicated external-memory budget.
// Exception: The guarded C engine boundary returns ENOMEM on an exhausted budget.
void *espeak_malloc(size_t size);
// count: Number of zeroed elements.
// size: Bytes per element; multiplication overflow is rejected.
void *espeak_calloc(size_t count, size_t size);
// pointer: Engine-owned allocation, or null.
// size: New byte count; existing allocation is retained on failure.
void *espeak_realloc(void *pointer, size_t size);
// pointer: Engine-owned allocation or borrowed registry data; borrowed data is never freed.
void espeak_free(void *pointer);
// text: Null-terminated engine string copied using the same bounded allocator.
char *luna_espeak_strdup(const char *text);
// operation: One serialized native C operation, including all engine allocations.
// context: Caller-owned operation context; C++ lifetimes must not cross allocation longjmp.
// Exception: Returns ENOMEM and poisons the engine after allocation failure.
int luna_espeak_guard(int (*operation)(void *), void *context);
// No parameters: reports whether an allocation failure requires a device restart.
int luna_espeak_poisoned(void);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
// cause: Original native engine error; stops the serialized C call without unsafe recovery.
// Exception: Poisons the engine and escapes only to its active C guard.
void luna_espeak_fail(int cause);
// destination: Caller-owned native text or phoneme buffer.
// capacity: Actual destination capacity, including its terminator.
// format: Constant printf format checked at the call site.
// ...: Format arguments whose types must match the format.
// Exception: Stops the guarded engine with the real formatting cause; never truncates silently.
void luna_espeak_format(char *destination, size_t capacity, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
#ifdef __cplusplus
}
#endif
