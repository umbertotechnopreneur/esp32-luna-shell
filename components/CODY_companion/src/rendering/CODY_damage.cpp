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

#include "CODY_damage.hpp"

#include <algorithm>
#include <cstring>
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_damage";

// tile: Caller-owned rendered rows, with an even logical canvas width.
// first_row: Logical tile origin, inside the complete cache.
// previous: Complete RGB565 cache sharing the tile's logical width and byte order.
esp_err_t validate_rows(CODY2D::Surface tile, uint16_t first_row,
                        CODY2D::Surface previous)
{
    esp_err_t error = CODY2D::validate_surface(tile);
    if (error == ESP_OK) {
        error = CODY2D::validate_surface(previous);
    }
    if (error == ESP_OK && (tile.width != previous.width || tile.width % 2U != 0U ||
        static_cast<uint32_t>(first_row) + tile.height > previous.height ||
        tile.pixels == previous.pixels || tile.byte_order != previous.byte_order)) {
        error = ESP_ERR_INVALID_ARG;
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag,
            "Tile/cache contract rejected: row=%u tile=%ux%u order=%u cache=%ux%u order=%u (%s)",
            first_row, tile.width, tile.height, static_cast<unsigned>(tile.byte_order),
            previous.width, previous.height,
            static_cast<unsigned>(previous.byte_order),
            esp_err_to_name(error));
    }
    return error;
}
}  // namespace

// tile: Fully rendered typed RGB565 rows, before any in-place packing.
// first_row: Logical origin of the tile in the complete canvas.
// previous: PSRAM-owned candidate frame in the same byte order, updated only for changes.
// compare_previous: False after startup, failed frames, rotation or panel invalidation.
// regions: Old/new artwork and controls that can change, in logical coordinates.
// damage: Receives the paired-column bounds of actual changed pixels.
esp_err_t measure_and_cache_damage_rows(CODY2D::Surface tile, uint16_t first_row,
                                        CODY2D::Surface previous, bool compare_previous,
                                        const DamageRegions &regions,
                                        DamageRect &damage)
{
    damage = {};
    const esp_err_t error = validate_rows(tile, first_row, previous);
    if (error != ESP_OK) {
        return error;
    }
    if (regions.count > DamageRegions::capacity) {
        ESP_LOGE(kTag, "Candidate region count rejected: %u (%s)", regions.count,
            esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    for (uint16_t index = 0; index < regions.count; ++index) {
        const auto &region = regions.rectangles[index];
        if (region.left > region.right || region.top > region.bottom ||
            region.right > previous.width || region.bottom > previous.height) {
            ESP_LOGE(kTag, "Candidate region %u rejected: [%u,%u,%u,%u] (%s)", index,
                region.left, region.top, region.right, region.bottom, esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (!compare_previous) {
        // An invalid cache has no reusable pixels. Stage the complete rendered
        // tile now; the frame owner still keeps the cache invalid until every
        // tile and panel transfer completes successfully.
        for (uint16_t row = 0; row < tile.height; ++row) {
            std::memcpy(previous.pixels + (first_row + row) * previous.stride,
                        tile.pixels + row * tile.stride,
                        tile.width * sizeof(uint16_t));
        }
        damage = {0, first_row, tile.width, static_cast<uint16_t>(first_row + tile.height)};
        return ESP_OK;
    }
    uint16_t left = tile.width;
    uint16_t right = 0;
    uint16_t top = tile.height;
    uint16_t bottom = 0;
    for (uint16_t row = 0; row < tile.height; ++row) {
        uint16_t candidate_left = tile.width;
        uint16_t candidate_right = 0;
        const uint16_t canvas_row = first_row + row;
        for (uint16_t index = 0; index < regions.count; ++index) {
            const auto &region = regions.rectangles[index];
            if (!region.empty() && canvas_row >= region.top && canvas_row < region.bottom) {
                candidate_left = std::min(candidate_left, region.left);
                candidate_right = std::max(candidate_right, region.right);
            }
        }
        if (candidate_left >= candidate_right) {
            continue;
        }
        const uint16_t *current_row = tile.pixels + row * tile.stride;
        const uint16_t *cached_row = previous.pixels + (first_row + row) * previous.stride;
        // Search RGB565 pixels directly. The physical-device profile showed
        // that repeated 64-byte memcmp calls cost more than this scan; keep
        // the independently useful rejection of static tiles in the caller.
        uint16_t first_changed = candidate_left;
        while (first_changed < candidate_right && current_row[first_changed] == cached_row[first_changed]) {
            ++first_changed;
        }
        if (first_changed == candidate_right) {
            continue;
        }
        uint16_t after_last_changed = candidate_right;
        while (current_row[after_last_changed - 1U] == cached_row[after_last_changed - 1U]) {
            --after_last_changed;
        }
        // Stage only this row's paired-pixel span. The aligned edge pixels are
        // already equal, and keeping the ordinary runtime buffers word-aligned
        // avoids turning the reduced PSRAM write into an unaligned copy.
        const uint16_t cache_left = static_cast<uint16_t>(first_changed & ~1U);
        const uint16_t cache_right = static_cast<uint16_t>((after_last_changed + 1U) & ~1U);
        std::memcpy(previous.pixels + canvas_row * previous.stride + cache_left,
                    current_row + cache_left,
                    (cache_right - cache_left) * sizeof(uint16_t));
        left = std::min(left, first_changed);
        right = std::max(right, after_last_changed);
        top = std::min(top, row);
        bottom = static_cast<uint16_t>(row + 1U);
    }
    if (top != tile.height) {
        // Match Luna's paired-column panel boundaries, including unchanged edge pixels.
        damage = {static_cast<uint16_t>(left & ~1U),
            static_cast<uint16_t>(first_row + top),
            static_cast<uint16_t>((right + 1U) & ~1U),
            static_cast<uint16_t>(first_row + bottom)};
    }
    return ESP_OK;
}

// tile: Fully rendered tile whose pixels become the candidate cache contents.
// first_row: Logical row offset into the complete cache.
// previous: Same-order frame cache already staged above; retained for bounds validation.
// damage: Changed region measured while staging the candidate cache.
// packed: Receives a compact transfer view preserving the tile byte order.
esp_err_t pack_damage_rows(CODY2D::Surface tile, uint16_t first_row,
                           CODY2D::Surface previous, const DamageRect &damage,
                           CODY2D::Surface &packed)
{
    packed = {};
    const esp_err_t error = validate_rows(tile, first_row, previous);
    if (error != ESP_OK) {
        return error;
    }
    if (damage.empty() || damage.left >= damage.right || damage.top >= damage.bottom ||
        damage.right > tile.width || damage.top < first_row ||
        damage.bottom > first_row + tile.height || damage.left % 2U != 0U ||
        damage.right % 2U != 0U) {
        ESP_LOGE(kTag, "Damage packing bounds rejected: [%u,%u,%u,%u] (%s)",
            damage.left, damage.top, damage.right, damage.bottom,
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const uint16_t width = damage.right - damage.left;
    const uint16_t height = damage.bottom - damage.top;
    const uint16_t local_top = damage.top - first_row;
    // measure_and_cache_damage_rows staged the candidate cache before this in-place
    // compaction. A failed transfer keeps that whole cache invalid in the frame
    // owner, so a later frame will rebuild it rather than trust partial data.
    for (uint16_t row = 0; row < height; ++row) {
        // Destination precedes this source and every future row. memmove also
        // handles the first row's overlapping left-edge crop safely.
        std::memmove(tile.pixels + row * width,
                     tile.pixels + (local_top + row) * tile.stride + damage.left,
                     width * sizeof(uint16_t));
    }
    packed = {
        tile.pixels, width, height, width, tile.pixel_capacity, tile.byte_order
    };
    return ESP_OK;
}
}  // namespace CODY
