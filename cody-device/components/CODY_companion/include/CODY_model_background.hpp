#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "CODY2D.hpp"
#include "CODY2D_text.hpp"
#include "../../../assets/cody/model-identities/generated/terra.hpp"
#include "../../../assets/cody/model-identities/generated/sol.hpp"
#include "../../../assets/cody/model-identities/generated/luna.hpp"
#include "../../../assets/cody/model-identities/generated/astra.hpp"

namespace CODY {

enum class ModelBackground : uint8_t { None, Terra, Sol, Luna, Astra };

// model: Counted authenticated identity.
// literal: Exact firmware-owned spelling.
// Exception: None; absent and incomplete names do not match.
template<std::size_t N>
inline bool model_background_equals(CODY2D::TextSpan model, const char (&literal)[N])
{
    return model.characters != nullptr && model.length == N - 1U &&
        std::memcmp(model.characters, literal, N - 1U) == 0;
}

// model: Exact counted model identity from the current authenticated observation.
// current: Whether that identity is valid and fresh.
// Exception: Unknown, stale, absent and unsupported models have no artwork.
inline ModelBackground model_background_for(CODY2D::TextSpan model, bool current)
{
    if (!current) return ModelBackground::None;
    if (model_background_equals(model, "gpt-6.1-terra")) return ModelBackground::Terra;
    if (model_background_equals(model, "gpt-6.1-sol") || model_background_equals(model, "gpt-6-sol"))
        return ModelBackground::Sol;
    if (model_background_equals(model, "gpt-6-luna")) return ModelBackground::Luna;
    if (model_background_equals(model, "gpt-6-astra")) return ModelBackground::Astra;
    return ModelBackground::None;
}

// background: Exact recognized model artwork or the explicit blank fallback.
// viewport: Square logical art extent, drawn below the foreground and status.
// strip: Borrowed destination tile, including stride, capacity and byte order.
// first_row: Logical y coordinate of the tile's first row.
// first_column: Logical x coordinate of the tile's first column.
// Exception: Returns a surface error or invalid geometry without drawing.
inline esp_err_t draw_model_background_rows(ModelBackground background, CODY2D::Rect viewport,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    const esp_err_t error = CODY2D::validate_surface(strip);
    if (error != ESP_OK) return error;
    const uint32_t right_edge = static_cast<uint32_t>(viewport.x) + viewport.width;
    const uint32_t bottom_edge = static_cast<uint32_t>(viewport.y) + viewport.height;
    const uint32_t tile_right = static_cast<uint32_t>(first_column) + strip.width;
    const uint32_t tile_bottom = static_cast<uint32_t>(first_row) + strip.height;
    if (viewport.width < 64 || viewport.width != viewport.height ||
        viewport.width > CODY2D::kMaxDimension || right_edge > 65536U || bottom_edge > 65536U ||
        tile_right > 65536U || tile_bottom > 65536U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (background == ModelBackground::None) return ESP_OK;
    const uint8_t *cells = nullptr;
    const uint16_t *palette = nullptr;
    switch (background) {
    case ModelBackground::Terra: cells = ModelIdentityData::terra_cells; palette = ModelIdentityData::terra_palette; break;
    case ModelBackground::Sol: cells = ModelIdentityData::sol_cells; palette = ModelIdentityData::sol_palette; break;
    case ModelBackground::Luna: cells = ModelIdentityData::luna_cells; palette = ModelIdentityData::luna_palette; break;
    case ModelBackground::Astra: cells = ModelIdentityData::astra_cells; palette = ModelIdentityData::astra_palette; break;
    default: return ESP_ERR_INVALID_ARG;
    }

    const uint32_t left = std::max<uint32_t>(viewport.x, first_column);
    const uint32_t top = std::max<uint32_t>(viewport.y, first_row);
    const uint32_t right = std::min(right_edge, tile_right);
    const uint32_t bottom = std::min(bottom_edge, tile_bottom);
    // At most 19% foreground coverage keeps bright copy legible over black.
    constexpr uint8_t opacity = 48;
    for (uint32_t y = top; y < bottom; ++y) {
        const uint32_t source_y = (y - viewport.y) * 64U / viewport.height;
        uint16_t *destination = strip.pixels + (y - first_row) * strip.stride;
        for (uint32_t x = left; x < right; ++x) {
            const uint32_t source_x = (x - viewport.x) * 64U / viewport.width;
            const uint32_t source_index = source_y * 64U + source_x;
            const uint8_t packed = cells[source_index / 2U];
            const uint8_t index = source_index % 2U == 0 ? packed >> 4U : packed & 0x0fU;
            if (index == 0) continue;
            auto &pixel = destination[x - first_column];
            const uint16_t native = CODY2D::rgb565_stored_to_native(pixel, strip.byte_order);
            pixel = CODY2D::rgb565_native_to_stored(
                CODY2D::blend_rgb565(palette[index], native, opacity), strip.byte_order);
        }
    }
    return ESP_OK;
}

} // namespace CODY
