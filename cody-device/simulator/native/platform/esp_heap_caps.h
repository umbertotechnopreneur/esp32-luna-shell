#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

// Capability bits are host admission identities, not physical RAM measurements.
inline constexpr uint32_t MALLOC_CAP_INTERNAL = 1U << 0;
inline constexpr uint32_t MALLOC_CAP_8BIT = 1U << 1;
inline constexpr uint32_t MALLOC_CAP_SPIRAM = 1U << 2;

// size: Bytes requested by the original settings executor.
// capabilities: Host-supported request for internal byte-addressable storage.
// Exception: Returns nullptr for unsupported capabilities or allocation failure.
inline void *heap_caps_malloc(size_t size, uint32_t capabilities)
{
    constexpr uint32_t supported = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM;
    if ((capabilities & MALLOC_CAP_INTERNAL) && (capabilities & MALLOC_CAP_SPIRAM)) return nullptr;
    if ((capabilities & ~supported) != 0) return nullptr;
    return std::malloc(size);
}

// pointer: Allocation returned by heap_caps_malloc; nullptr is allowed.
// Exception: None.
inline void heap_caps_free(void *pointer) { std::free(pointer); }
