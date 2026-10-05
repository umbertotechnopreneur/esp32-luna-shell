#include "mem_alloc.h"
#include "asset_registry.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include <errno.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

typedef union { max_align_t alignment; size_t size; } AllocationHeader;
static size_t allocated_bytes;
static jmp_buf allocation_boundary;
static int boundary_active;
static int poisoned;
static int terminal_cause;

// No parameters: retain a terminal allocator failure without aborting the device.
static void reject_allocation(void)
{
    terminal_cause = ENOMEM;
    poisoned = 1;
    if (boundary_active) longjmp(allocation_boundary, 1);
}

// size: Requested working bytes; never falls back to internal DMA memory.
// Exception: Escapes only to the serialized C guard on ENOMEM.
void *espeak_malloc(size_t size)
{
    if (size > SIZE_MAX - sizeof(AllocationHeader) ||
        size + sizeof(AllocationHeader) > CONFIG_LUNASHELL_TTS_ENGINE_HEAP_BYTES - allocated_bytes) {
        reject_allocation();
        return NULL;
    }
    AllocationHeader *header = heap_caps_malloc(sizeof(*header) + size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (header == NULL) {
        reject_allocation();
        return NULL;
    }
    header->size = size;
    allocated_bytes += sizeof(*header) + size;
    return header + 1;
}

// count: Number of elements.
// size: Element size; overflow is an allocation failure.
void *espeak_calloc(size_t count, size_t size)
{
    if (size != 0 && count > SIZE_MAX / size) {
        reject_allocation();
        return NULL;
    }
    const size_t bytes = count * size;
    void *pointer = espeak_malloc(bytes);
    if (pointer != NULL) memset(pointer, 0, bytes);
    return pointer;
}

// pointer: Owned allocation or exact borrowed asset pointer.
void espeak_free(void *pointer)
{
    if (pointer == NULL || luna_espeak_borrowed(pointer)) return;
    AllocationHeader *header = (AllocationHeader *)pointer - 1;
    allocated_bytes -= sizeof(*header) + header->size;
    heap_caps_free(header);
}

// pointer: Owned allocation, or null; assets cannot be resized.
// size: Requested new size.
void *espeak_realloc(void *pointer, size_t size)
{
    if (pointer == NULL) return espeak_malloc(size);
    if (size == 0) {
        espeak_free(pointer);
        return NULL;
    }
    if (luna_espeak_borrowed(pointer)) {
        reject_allocation();
        return NULL;
    }
    AllocationHeader *header = (AllocationHeader *)pointer - 1;
    const size_t previous_size = header->size;
    if (size > SIZE_MAX - sizeof(*header) ||
        size + sizeof(*header) > CONFIG_LUNASHELL_TTS_ENGINE_HEAP_BYTES -
            (allocated_bytes - previous_size - sizeof(*header))) {
        reject_allocation();
        return NULL;
    }
    AllocationHeader *replacement = heap_caps_realloc(header, sizeof(*header) + size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (replacement == NULL) {
        reject_allocation();
        return NULL;
    }
    replacement->size = size;
    allocated_bytes = allocated_bytes - previous_size + size;
    return replacement + 1;
}

// text: Engine-owned null-terminated string source.
char *luna_espeak_strdup(const char *text)
{
    const size_t bytes = strlen(text) + 1;
    char *copy = espeak_malloc(bytes);
    if (copy != NULL) memcpy(copy, text, bytes);
    return copy;
}

// operation: Native C engine call; serialized by the sole TTS worker.
// context: Trivial borrowed C context; no C++ destructors may be skipped.
// Exception: Returns the original retained engine/ENOMEM cause until restart.
int luna_espeak_guard(int (*operation)(void *), void *context)
{
    if (poisoned || boundary_active) return poisoned ? terminal_cause : EBUSY;
    boundary_active = 1;
    if (setjmp(allocation_boundary) != 0) {
        boundary_active = 0;
        return terminal_cause;
    }
    const int result = operation(context);
    boundary_active = 0;
    return result;
}

// No parameters: damaged engine state is never guessed safe to reuse.
int luna_espeak_poisoned(void) { return poisoned; }

// cause: Original failure from a C engine operation whose partial state cannot be reused.
// Exception: Skips only C frames to the sole worker's active engine boundary.
void luna_espeak_fail(int cause)
{
    terminal_cause = cause;
    poisoned = 1;
    if (boundary_active) longjmp(allocation_boundary, 1);
}
