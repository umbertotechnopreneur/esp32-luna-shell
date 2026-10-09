#pragma once
#include <cstddef>
#include <cstdint>

constexpr uint32_t MALLOC_CAP_INTERNAL = 1U;
constexpr uint32_t MALLOC_CAP_8BIT = 2U;
void *heap_caps_malloc(size_t, uint32_t);
void heap_caps_free(void *);
