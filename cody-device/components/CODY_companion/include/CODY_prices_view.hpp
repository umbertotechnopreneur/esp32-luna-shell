#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "CODY_scene.hpp"
#include "CODY2D_controls.hpp"
#include "LUNASHELL_openrouter_catalog.hpp"

namespace CODY {

// Serialized display owner: bounded page and labels survive until frame retirement.
struct PricesView {
    static constexpr size_t kRows = 3;
    std::array<LUNASHELL::OpenRouterModelQuote, kRows> quotes{};
    std::array<CODY2D::CapsuleControl, kRows> rows{};
    std::array<bool, kRows> row_old{};
    std::array<CODY2D::TextLine, 9> lines{};
    char row_labels[kRows][65]{};
    char labels[9][48]{};
    LUNASHELL::OpenRouterCatalogStatus status{};
    const CODY2D::MonoFont *font = nullptr;
    uint32_t revision = 0;
    uint16_t page = 0;
    uint8_t row_count = 0;
    uint8_t selected = 0;
    bool detail = false;
    bool prepared = false;
    bool old_quote = false;
};

// view: Serialized owner; caller changes it only after the previous frame retires.
// font: Immutable native body font.
// now_us: Current monotonic time; used only to distinguish an old quote.
// content: Receives the borrowed SceneContent callback and heading.
// Exception: Returns the original snapshot, page or typography error.
esp_err_t prepare_prices_view(PricesView &view, const CODY2D::MonoFont &font,
    int64_t now_us, SceneContent &content);

// view: Current retired catalog view.
// direction: Minus one for previous, plus one for next page.
// Exception: Returns false at a boundary or outside the listing.
bool prices_move_page(PricesView &view, int direction);

// view: Current retired listing with controls admitted by the renderer.
// point: Logical touch coordinate from the same retired revision.
// Exception: Returns false if no model row was selected.
bool prices_select_at(PricesView &view, CODY2D::Point point);

// view: Current retired catalog view.
// Exception: Returns false if already showing the model list.
bool prices_back(PricesView &view);

} // namespace CODY
