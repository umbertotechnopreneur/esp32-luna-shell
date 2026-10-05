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
#ifdef __cplusplus
}
#endif
