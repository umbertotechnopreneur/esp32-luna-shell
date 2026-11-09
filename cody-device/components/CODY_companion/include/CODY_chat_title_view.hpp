#pragma once

#include <algorithm>
#include <cstring>

#include "CODY2D_text.hpp"
#include "LUNASHELL_device_snapshot.hpp"

namespace CODY {

inline constexpr CODY2D::Rect kChatTitleBounds{73, 129, 320, 25};

// Persistent display storage; native text windows borrow the full bounded title.
struct ChatTitleView {
    char title[sizeof(LUNASHELL::CodexActivitySnapshot::title)]{};
    char chat[sizeof(LUNASHELL::CodexActivitySnapshot::chat_id)]{};
    CODY2D::TextLine line{};
    int64_t started_at_us = 0;
    uint16_t offset = 0;
};
static_assert(sizeof(ChatTitleView) <= 416, "Chat title storage belongs to the view, outside the frame stack");

// view: Persistent text and scroll state for one visible chat heading.
// title: Canonical title or its explicit availability caption.
// chat: Opaque identity, used to restart scrolling without displaying the ID.
// font: Immutable native glyphs for the viewport.
// now_us: Monotonic time sampled by the display owner.
// reset: True when this view is entered from another route.
// changed: Receives whether the visible heading needs repainting.
// Exception: Propagates original font and text preparation errors.
inline esp_err_t prepare_chat_title(ChatTitleView &view, const char *title, const char *chat,
    const CODY2D::MonoFont &font, int64_t now_us, bool reset, bool &changed)
{
    const auto length = std::strlen(title);
    const auto chat_length = std::strlen(chat);
    if (length == 0 || length >= sizeof(view.title) || chat_length >= sizeof(view.chat) ||
        font.height > kChatTitleBounds.height) return ESP_ERR_INVALID_SIZE;
    const bool new_title = reset || !view.line.valid || std::strcmp(view.title, title) != 0 ||
        std::strcmp(view.chat, chat) != 0;
    uint32_t width = 0;
    auto error = CODY2D::measure_text_width(font, {title, static_cast<uint16_t>(length)}, width);
    if (error != ESP_OK) return error;
    if (new_title) {
        std::memcpy(view.title, title, length + 1U);
        std::memcpy(view.chat, chat, chat_length + 1U);
        view.started_at_us = now_us;
    }
    uint16_t offset = 0;
    if (width > kChatTitleBounds.width) {
        constexpr int64_t step_us = 16000;
        constexpr int64_t start_hold_us = 1200000;
        constexpr int64_t end_hold_us = 1600000;
        const int64_t travel = width - kChatTitleBounds.width;
        const int64_t cycle = start_hold_us + travel * step_us + end_hold_us;
        const auto elapsed = std::max<int64_t>(0, now_us - view.started_at_us) % cycle;
        offset = static_cast<uint16_t>(std::clamp<int64_t>((elapsed - start_hold_us) / step_us, 0, travel));
    }
    changed = new_title || offset != view.offset;
    if (!changed) return ESP_OK;
    view.line.valid = false;
    view.offset = offset;
    CODY2D::TextSpan span{view.title, static_cast<uint16_t>(length)};
    int16_t x = kChatTitleBounds.x;
    if (width > kChatTitleBounds.width) {
        CODY2D::TextSpan window{};
        int16_t origin = 0;
        error = CODY2D::select_text_window(font, span, offset, kChatTitleBounds.width, window, origin);
        if (error != ESP_OK) return error;
        span = window;
        x += origin;
    } else {
        x += (kChatTitleBounds.width - width) / 2U;
    }
    return CODY2D::prepare_text_line(font, span,
        {x, static_cast<int16_t>(kChatTitleBounds.y + (kChatTitleBounds.height - font.height) / 2U)},
        0x6F9E, view.line);
}

// view: Prepared title borrowed through frame retirement.
// strip: Native tile with original stride and byte order.
// row: Tile's logical top edge.
// column: Tile's logical left edge.
// Exception: Returns the original text raster error.
inline esp_err_t draw_chat_title(const ChatTitleView &view, CODY2D::Surface strip, uint16_t row, uint16_t column)
{
    const auto &box = kChatTitleBounds;
    const auto left = std::max<int>(column, box.x);
    const auto right = std::min<int>(column + strip.width, box.x + box.width);
    if (!view.line.valid || row >= box.y + box.height || row + strip.height <= box.y || left >= right) return ESP_OK;
    const size_t skip = left - column;
    strip.pixels += skip;
    strip.pixel_capacity -= skip;
    strip.width = static_cast<uint16_t>(right - left);
    return CODY2D::draw_text_rows(view.line, view.line.text.length, strip, row, left);
}

} // namespace CODY
