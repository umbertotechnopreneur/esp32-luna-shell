/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added the shared approval notice artwork and title viewport on 2026-10-16.
 * Human guidance: Umberto Giacobbi; approved the PC notice redesign, cyan chat title and lateral Back.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#pragma once

#include <algorithm>
#include <cstring>
#include "CODY2D_controls.hpp"
#include "CODY2D_paths.hpp"
#include "esp_log.h"

namespace CODY {

// Display-owned projection, retained outside embedded task stacks and canonical snapshots.
struct ApprovalNoticeState {
    char chat[256]{};
    CODY2D::TextLine chat_window{};
    CODY2D::PreparedStroke marks[9]{};
    uint16_t length = 0;
    uint16_t width = 0;
    uint16_t offset = 0;
    int64_t readable_at_us = -1;
    bool artwork_ready = false;
};
constexpr CODY2D::Rect kApprovalChatBounds{73, 210, 320, 28};
static_assert(sizeof(ApprovalNoticeState) <= 1536, "Approval art belongs in display-owned storage");

// state: Stable display owner whose complete chat text outlives the borrowed glyph window.
// font: Real shared native body font; its advance determines the pixel window.
// Exception: Returns the original native text preparation error.
inline esp_err_t prepare_approval_chat_window(ApprovalNoticeState &state, const CODY2D::MonoFont &font)
{
    const auto &bounds = kApprovalChatBounds;
    const bool overflow = state.width > bounds.width;
    const uint16_t first = overflow ? state.offset / font.advance : 0;
    const uint16_t fraction = overflow ? state.offset % font.advance : 0;
    const uint16_t capacity = (bounds.width + font.advance - 1U) / font.advance + 1U;
    const uint16_t count = std::min<uint16_t>(state.length - first, capacity);
    const int16_t left = overflow ? static_cast<int16_t>(bounds.x - fraction) :
        static_cast<int16_t>((466U - state.width) / 2U);
    return CODY2D::prepare_text_line(font, {state.chat + first, count},
        {left, static_cast<int16_t>(bounds.y)}, 0x3DFF, state.chat_window);
}

// state: Serialized presentation owner; a same-title refresh retains its scroll position.
// font: Immutable shared glyphs, validated before any advance/division or draw.
// chat: Complete originating hook title or explicit unavailability, never selected Home metadata.
// Exception: Returns precise invalid text/font/layout errors; rejected content is never truncated.
inline esp_err_t prepare_approval_notice(ApprovalNoticeState &state, const CODY2D::MonoFont &font,
                                         const char *chat)
{
    if (chat == nullptr) { return ESP_ERR_INVALID_ARG; }
    const size_t length = std::strlen(chat);
    if (length == 0 || length >= sizeof(state.chat) || font.height > kApprovalChatBounds.height) {
        ESP_LOGE("CODY_approval", "Chat title length/font rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const esp_err_t font_error = CODY2D::validate_mono_font(font);
    if (font_error != ESP_OK) { return font_error; }
    for (size_t index = 0; index < length; ++index) {
        if (chat[index] < 32 || chat[index] > 126) {
            ESP_LOGE("CODY_approval", "Chat title byte rejected at %u (%s)",
                static_cast<unsigned>(index), esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (!state.artwork_ready) {
        struct Mark { CODY2D::Vec2 from; CODY2D::Vec2 to; float width; uint16_t color; };
        // These marks are read-only art inside the monitor. No approval hit bounds exist.
        static constexpr Mark marks[] = {
            {{185, 303}, {194, 311}, 4, 0x7F55}, {{194, 311}, {211, 291}, 4, 0x7F55},
            {{254, 293}, {277, 315}, 4, 0xFAD3}, {{277, 293}, {254, 315}, 4, 0xFAD3},
            {{307, 255}, {307, 264}, 2.5f, 0xFE87}, {{307, 264}, {313, 267}, 2.5f, 0xFE87},
            {{176, 374}, {290, 374}, 2, 0x3C98}, {{180, 379}, {286, 379}, 2, 0x2B94},
            {{219, 382}, {247, 382}, 2, 0x5DBB},
        };
        for (uint8_t index = 0; index < 9; ++index) {
            const auto &mark = marks[index];
            const esp_err_t error = CODY2D::prepare_stroke(mark.from, mark.to, mark.width,
                mark.color, state.marks[index]);
            if (error != ESP_OK) { return error; }
        }
        state.artwork_ready = true;
    }
    if (state.length != length || std::strcmp(state.chat, chat) != 0) {
        std::memcpy(state.chat, chat, length + 1U);
        state.length = static_cast<uint16_t>(length);
        state.width = static_cast<uint16_t>((length - 1U) * font.advance + font.width);
        state.offset = 0;
        state.readable_at_us = -1;
    }
    return prepare_approval_chat_window(state, font);
}

// state: Prepared title projection, never a source of runtime/chat freshness.
// font: Same immutable native font used to measure this title.
// elapsed_us: Pausable visible scene clock; standby and hidden routes contribute no time.
// visible: True only after the complete title/narrative has been revealed.
// changed: Receives whether the bounded glyph window moved by a pixel.
// Exception: Returns invalid clock/state or original native preparation failure.
inline esp_err_t advance_approval_notice(ApprovalNoticeState &state, const CODY2D::MonoFont &font,
                                         int64_t elapsed_us, bool visible, bool &changed)
{
    changed = false;
    if (elapsed_us < 0 || !state.chat_window.valid) { return ESP_ERR_INVALID_STATE; }
    if (!visible) { return ESP_OK; }
    if (state.readable_at_us < 0) { state.readable_at_us = elapsed_us; }
    if (state.width <= kApprovalChatBounds.width) { return ESP_OK; }
    const uint16_t overflow = state.width - kApprovalChatBounds.width;
    constexpr int64_t step_us = 35000;
    constexpr int64_t start_hold_us = 1200000;
    constexpr int64_t end_hold_us = 1800000;
    const int64_t cycle = start_hold_us + overflow * step_us + end_hold_us;
    const int64_t elapsed = std::max<int64_t>(0, elapsed_us - state.readable_at_us) % cycle;
    const uint16_t offset = elapsed < start_hold_us ? 0 :
        static_cast<uint16_t>(std::min<int64_t>(overflow, (elapsed - start_hold_us) / step_us));
    if (offset == state.offset) { return ESP_OK; }
    state.offset = offset;
    const esp_err_t error = prepare_approval_chat_window(state, font);
    changed = error == ESP_OK;
    return error;
}

// state: Prepared immutable title and colorful computer artwork; no interactive decisions.
// strip: Borrowed RGB565 tile with its own stride and byte order.
// first_row: Logical tile y origin shared with native rotation and damage tracking.
// first_column: Logical tile x origin shared with clipping and the separate lateral Back.
// Exception: Returns the first genuine capsule, stroke or clipped text raster error.
inline esp_err_t draw_approval_notice_rows(const ApprovalNoticeState &state, CODY2D::Surface strip,
                                           uint16_t first_row, uint16_t first_column)
{
    if (!state.artwork_ready || !state.chat_window.valid) { return ESP_ERR_INVALID_STATE; }
    struct Panel { CODY2D::Rect bounds; uint16_t radius; uint16_t top; uint16_t bottom; uint16_t border; };
    // Every full rectangle is inside the circle's narrowest occupied chord, including the keyboard.
    static constexpr Panel panels[] = {
        {{224, 340, 18, 20}, 4, 0x1B32, 0x09AB, 0x2C15},
        {{205, 356, 56, 7}, 3, 0x3D39, 0x1A90, 0x3C97},
        {{151, 254, 164, 94}, 12, 0x65FF, 0x1B94, 0x867F},
        {{157, 260, 152, 78}, 8, 0x096A, 0x0045, 0x2BF6},
        {{174, 283, 49, 42}, 8, 0x11C9, 0x08C5, 0x2BCE},
        {{242, 283, 49, 42}, 8, 0x38C7, 0x2085, 0xA1CD},
        {{293, 249, 28, 28}, 0, 0x5144, 0x28C2, 0xFE07},
        {{164, 368, 138, 17}, 5, 0x1B52, 0x096A, 0x2C36},
    };
    for (const auto &panel : panels) {
        CODY2D::CapsuleControl control;
        control.bounds = panel.bounds;
        control.corner_radius = panel.radius;
        control.top_color = panel.top;
        control.bottom_color = panel.bottom;
        control.border_color = panel.border;
        const esp_err_t error = CODY2D::draw_capsule_rows(control, strip, first_row, first_column);
        if (error != ESP_OK) { return error; }
    }
    esp_err_t error = CODY2D::stroke_prepared_batch_rows(strip, first_row, state.marks, 9, first_column);
    if (error != ESP_OK) { return error; }
    const auto &bounds = kApprovalChatBounds;
    const uint16_t left = std::max(first_column, bounds.x);
    const uint16_t right = std::min<uint16_t>(first_column + strip.width, bounds.x + bounds.width);
    const uint16_t top = std::max(first_row, bounds.y);
    const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, bounds.y + bounds.height);
    if (left >= right || top >= bottom) { return ESP_OK; }
    CODY2D::Surface clipped = strip;
    const size_t skipped = (top - first_row) * strip.stride + left - first_column;
    clipped.pixels += skipped;
    clipped.pixel_capacity -= skipped;
    clipped.width = right - left;
    clipped.height = bottom - top;
    return CODY2D::draw_text_rows(state.chat_window, state.chat_window.text.length, clipped, top, left);
}

} // namespace CODY
