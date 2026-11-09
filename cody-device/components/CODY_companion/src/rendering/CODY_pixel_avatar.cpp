/*
 * Copyright (c) 2026 Umberto Giacobbi
 * Original Cody pixel artwork and native renderer.
 */

#include "CODY_pixel_avatar.hpp"

#include <algorithm>
#include <cstddef>

#include "esp_log.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_pixel_avatar";
constexpr int kGridSize = 64;
constexpr uint32_t kCoordinateEnd = 65536;

enum class PixelTone : uint8_t {
    Transparent = 0,
    Outline = 1,
    Violet = 2,
    Lilac = 3,
    Blue = 4,
    Aqua = 5,
    Mint = 6,
    Pearl = 7,
    White = 8,
    Ink = 9,
    Cheek = 10,
    Heart = 11,
    Attention = 12,
    Listening = 13,
    Ready = 14,
    Shadow = 15,
};

// Fixed native RGB565 colors: changes in pose alter cells, never a hidden palette.
constexpr std::array<uint16_t, 16> kPalette = {
    0x0000, 0x5271, 0x939C, 0xC59F, 0x8DFF, 0x9F3E, 0xC7D9, 0xEFFF,
    0xFFFF, 0x18E7, 0xFCDC, 0xF2F5, 0xFBCB, 0x4E9F, 0x67B4, 0x39AD,
};

struct JellyLobe {
    int8_t x;
    int8_t y;
    uint8_t radius_x;
    uint8_t radius_y;
};

// Unequal lobes preserve Cody's cloud silhouette instead of a humanoid body.
constexpr std::array<JellyLobe, 8> kJellyLobes = {{
    {32, 39, 19, 14}, {28, 27, 11, 14}, {39, 28, 8, 10},
    {15, 38, 9, 10}, {48, 37, 8, 9}, {53, 46, 6, 7},
    {19, 48, 8, 6}, {42, 50, 9, 5},
}};

struct PixelSprite {
    uint8_t width;
    uint8_t height;
    std::array<uint8_t, 8> rows;
};

// Hand-authored bit masks; the highest bit within width is the leftmost cell.
constexpr PixelSprite kEye{4, 6, {0x6, 0xF, 0xF, 0xF, 0xF, 0x6}};
constexpr PixelSprite kWideEye{5, 7, {0xE, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0xE}};
constexpr PixelSprite kClosedEye{5, 2, {0x11, 0xE}};
constexpr PixelSprite kHappyEye{5, 3, {0x4, 0xA, 0x11}};
constexpr PixelSprite kSmile{5, 3, {0x11, 0xA, 0x4}};
constexpr PixelSprite kGrin{7, 4, {0x7F, 0x3E, 0x1C, 0x8}};
constexpr PixelSprite kRoundMouth{3, 4, {0x2, 0x5, 0x5, 0x2}};
constexpr PixelSprite kThinkingMouth{5, 2, {0x3, 0x1C}};
constexpr PixelSprite kFrown{5, 3, {0x4, 0xA, 0x11}};
constexpr PixelSprite kHeart{5, 5, {0xA, 0x1F, 0x1F, 0xE, 0x4}};
constexpr PixelSprite kShine{5, 5, {0x4, 0x4, 0x1F, 0x4, 0x4}};
constexpr PixelSprite kHighlight{8, 4, {0x0F, 0x3E, 0x78, 0x60}};
constexpr PixelSprite kWarning{3, 7, {0x7, 0x7, 0x7, 0x2, 0x2, 0x0, 0x2}};
constexpr PixelSprite kSleepMark{5, 5, {0x1F, 0x2, 0x4, 0x8, 0x1F}};
constexpr PixelSprite kReplyMark{7, 7, {0x7F, 0x41, 0x55, 0x41, 0x7E, 0x40, 0x20}};
constexpr PixelSprite kListenLeft{3, 7, {0x1, 0x2, 0x4, 0x4, 0x4, 0x2, 0x1}};
constexpr PixelSprite kListenRight{3, 7, {0x4, 0x2, 0x1, 0x1, 0x1, 0x2, 0x4}};

// frame: Borrowed indexed output.
// x: Grid column; cells outside the grid are clipped.
// y: Grid row; cells outside the grid are clipped.
// tone: Typed palette entry to write.
void put_cell(PixelAvatarFrame &frame, int x, int y, PixelTone tone)
{
    if (x >= 0 && x < kGridSize && y >= 0 && y < kGridSize) {
        frame.cells[static_cast<size_t>(y * kGridSize + x)] = static_cast<uint8_t>(tone);
    }
}

// frame: Borrowed body silhouette before facial details are composited.
// x: Grid column, with out-of-grid coordinates treated as transparent.
// y: Grid row, with out-of-grid coordinates treated as transparent.
bool occupied(const PixelAvatarFrame &frame, int x, int y)
{
    if (x < 0 || x >= kGridSize || y < 0 || y >= kGridSize) {
        return false;
    }
    return frame.cells[static_cast<size_t>(y * kGridSize + x)] != 0;
}

// frame: Borrowed indexed output.
// sprite: Fixed mask no larger than eight by eight cells.
// x: Leftmost destination column.
// y: Top destination row.
// tone: Typed palette entry for the mask's set bits.
void stamp_sprite(PixelAvatarFrame &frame, const PixelSprite &sprite,
    int x, int y, PixelTone tone)
{
    for (int row = 0; row < sprite.height; ++row) {
        for (int column = 0; column < sprite.width; ++column) {
            const uint8_t bit = static_cast<uint8_t>(1U << (sprite.width - 1 - column));
            if ((sprite.rows[static_cast<size_t>(row)] & bit) != 0) {
                put_cell(frame, x + column, y + row, tone);
            }
        }
    }
}

// frame: Borrowed indexed output receiving one part of the jelly silhouette.
// center_x: Grid column at the ellipse center.
// center_y: Grid row at the ellipse center.
// radius_x: Positive horizontal radius, at most nineteen cells.
// radius_y: Positive vertical radius, at most fourteen cells.
void fill_lobe(PixelAvatarFrame &frame, int center_x, int center_y,
    int radius_x, int radius_y)
{
    const int radius_x_squared = radius_x * radius_x;
    const int radius_y_squared = radius_y * radius_y;
    const int limit = radius_x_squared * radius_y_squared;
    for (int y = -radius_y; y <= radius_y; ++y) {
        for (int x = -radius_x; x <= radius_x; ++x) {
            if (x * x * radius_y_squared + y * y * radius_x_squared <= limit) {
                put_cell(frame, center_x + x, center_y + y, PixelTone::Aqua);
            }
        }
    }
}

// y: Unscaled artwork row.
// offset_y: Vertical artistic displacement in grid cells.
// height_percent: Positive body-height percentage, anchored at row fifty-four.
int body_row(int y, int offset_y, int height_percent)
{
    return 54 + (y - 54) * height_percent / 100 + offset_y;
}

// frame: Borrowed silhouette; shading preserves every occupied cell.
// offset_x: Horizontal artistic displacement in grid cells.
// offset_y: Vertical artistic displacement in grid cells.
// height_percent: Positive body-height percentage, used to align color bands.
void shade_body(PixelAvatarFrame &frame, int offset_x, int offset_y, int height_percent)
{
    for (int y = 0; y < kGridSize; ++y) {
        const int artwork_y = 54 + (y - offset_y - 54) * 100 / height_percent;
        for (int x = 0; x < kGridSize; ++x) {
            if (!occupied(frame, x, y)) {
                continue;
            }
            const bool edge = !occupied(frame, x - 1, y) || !occupied(frame, x + 1, y) ||
                !occupied(frame, x, y - 1) || !occupied(frame, x, y + 1);
            PixelTone tone = PixelTone::Aqua;
            if (edge) {
                tone = artwork_y < 32 ? PixelTone::Pearl : PixelTone::Violet;
            } else if (artwork_y < 23) {
                tone = PixelTone::Mint;
            } else if (artwork_y > 47) {
                tone = (x + y) % 3 == 0 ? PixelTone::Violet : PixelTone::Lilac;
            } else if (artwork_y > 41) {
                tone = (x + y) % 4 == 0 ? PixelTone::Lilac : PixelTone::Blue;
            } else if (x - offset_x > 23 && x - offset_x < 40 && artwork_y > 30) {
                tone = (x + y) % 4 == 0 ? PixelTone::Aqua : PixelTone::Pearl;
            } else if ((x + 2 * y) % 7 == 0) {
                tone = PixelTone::Mint;
            }
            // Nonzero colors allow the same frame to remain its own silhouette mask.
            put_cell(frame, x, y, tone);
        }
    }
}

// pose: Public semantic pose whose underlying value is checked explicitly.
bool valid_pose(PixelAvatarPose pose)
{
    switch (pose) {
        case PixelAvatarPose::Waking:
        case PixelAvatarPose::Idle:
        case PixelAvatarPose::Listening:
        case PixelAvatarPose::Thinking:
        case PixelAvatarPose::Reply:
        case PixelAvatarPose::Error:
        case PixelAvatarPose::Sleep:
            return true;
    }
    return false;
}

// viewport: Logical square scaled from the 64-cell grid without fractional pixels.
// Exception: Invalid scale or coordinate extent is logged and returned.
esp_err_t validate_viewport(CODY2D::Rect viewport)
{
    const uint32_t right = static_cast<uint32_t>(viewport.x) + viewport.width;
    const uint32_t bottom = static_cast<uint32_t>(viewport.y) + viewport.height;
    if (viewport.width == 0 || viewport.width != viewport.height ||
        viewport.width % kGridSize != 0 || viewport.width > CODY2D::kMaxDimension ||
        right > kCoordinateEnd || bottom > kCoordinateEnd) {
        ESP_LOGE(kTag, "Viewport rejected: x=%u y=%u width=%u height=%u (%s)",
            viewport.x, viewport.y, viewport.width, viewport.height,
            esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

// frame: Borrowed indexed output with its finished jelly body.
// pose: Valid semantic state, never replaced by the touch reaction.
// time_ms: Active artistic time for blinking and gaze.
// pose_ms: Elapsed state time for opening the eyes on wake.
// reacting: Whether a permitted touch reaction is active.
// offset_x: Horizontal displacement shared with the body.
// offset_y: Vertical displacement shared with the body.
// height_percent: Positive body-height percentage shared with the body.
void draw_face(PixelAvatarFrame &frame, PixelAvatarPose pose,
    uint32_t time_ms, uint32_t pose_ms, bool reacting,
    int offset_x, int offset_y, int height_percent)
{
    const uint32_t blink_phase = time_ms % 4700U;
    const bool blink = blink_phase >= 4500U && blink_phase < 4620U;
    const bool closed = pose == PixelAvatarPose::Sleep || blink ||
        (pose == PixelAvatarPose::Waking && pose_ms < 350U);
    int gaze = 0;
    if (pose == PixelAvatarPose::Idle) {
        gaze = static_cast<int>((time_ms / 1900U) % 3U) - 1;
    } else if (pose == PixelAvatarPose::Thinking) {
        gaze = 1;
    }
    const int left_eye = 23 + offset_x + gaze;
    const int right_eye = 37 + offset_x + gaze;
    const int eye_y = body_row(34, offset_y, height_percent);
    const PixelSprite *eye = &kEye;
    if (closed) {
        eye = &kClosedEye;
    } else if (reacting) {
        eye = &kHappyEye;
    } else if (pose == PixelAvatarPose::Listening) {
        eye = &kWideEye;
    }
    const int eye_top = eye_y + (closed ? 3 : 0);
    stamp_sprite(frame, *eye, left_eye, eye_top, PixelTone::Ink);
    stamp_sprite(frame, *eye, right_eye, eye_top, PixelTone::Ink);
    if (!closed && !reacting) {
        put_cell(frame, left_eye + 1, eye_y + 1, PixelTone::White);
        put_cell(frame, right_eye + 1, eye_y + 1, PixelTone::White);
    }

    const int mouth_y = body_row(43, offset_y, height_percent);
    const PixelSprite *mouth = &kSmile;
    if (pose == PixelAvatarPose::Error) {
        mouth = &kFrown;
        put_cell(frame, left_eye, eye_y - 2, PixelTone::Ink);
        put_cell(frame, left_eye + 1, eye_y - 1, PixelTone::Ink);
        put_cell(frame, right_eye + 2, eye_y - 1, PixelTone::Ink);
        put_cell(frame, right_eye + 3, eye_y - 2, PixelTone::Ink);
    } else if (reacting || pose == PixelAvatarPose::Reply) {
        mouth = &kGrin;
    } else if (pose == PixelAvatarPose::Listening) {
        mouth = &kRoundMouth;
    } else if (pose == PixelAvatarPose::Thinking) {
        mouth = &kThinkingMouth;
        put_cell(frame, right_eye + 1, eye_y - 3, PixelTone::Ink);
        put_cell(frame, right_eye + 2, eye_y - 3, PixelTone::Ink);
    }
    stamp_sprite(frame, *mouth, 32 + offset_x - mouth->width / 2, mouth_y, PixelTone::Ink);
    if (pose != PixelAvatarPose::Error && pose != PixelAvatarPose::Sleep) {
        put_cell(frame, 20 + offset_x, eye_y + 6, PixelTone::Cheek);
        put_cell(frame, 21 + offset_x, eye_y + 6, PixelTone::Cheek);
        put_cell(frame, 43 + offset_x, eye_y + 6, PixelTone::Cheek);
        put_cell(frame, 44 + offset_x, eye_y + 6, PixelTone::Cheek);
    }
}

// frame: Borrowed output with the body and face already composed.
// pose: Current semantic state whose indicator remains visible during reactions.
// time_ms: Active artistic time controlling small indicator motion.
// offset_y: Vertical displacement of the body.
void draw_pose_detail(PixelAvatarFrame &frame, PixelAvatarPose pose,
    uint32_t time_ms, int offset_y)
{
    switch (pose) {
        case PixelAvatarPose::Waking:
            stamp_sprite(frame, kShine, 43, 12 + offset_y, PixelTone::White);
            break;
        case PixelAvatarPose::Idle:
            break;
        case PixelAvatarPose::Listening: {
            const int spread = static_cast<int>((time_ms / 360U) % 2U);
            stamp_sprite(frame, kListenLeft, 3 - spread, 31 + offset_y, PixelTone::Listening);
            stamp_sprite(frame, kListenRight, 59 + spread, 31 + offset_y, PixelTone::Listening);
            break;
        }
        case PixelAvatarPose::Thinking: {
            const int raised = static_cast<int>((time_ms / 320U) % 3U);
            for (int dot = 0; dot < 3; ++dot) {
                const int y = 17 - (dot == raised ? 2 : 0);
                put_cell(frame, 44 + dot * 4, y, PixelTone::Lilac);
                put_cell(frame, 45 + dot * 4, y, PixelTone::Lilac);
                put_cell(frame, 44 + dot * 4, y + 1, PixelTone::Lilac);
                put_cell(frame, 45 + dot * 4, y + 1, PixelTone::Lilac);
            }
            break;
        }
        case PixelAvatarPose::Reply:
            stamp_sprite(frame, kReplyMark, 48, 14, PixelTone::Ready);
            stamp_sprite(frame, kShine, 9, 19 + static_cast<int>((time_ms / 500U) % 2U), PixelTone::White);
            break;
        case PixelAvatarPose::Error:
            stamp_sprite(frame, kWarning, 50, 15, PixelTone::Attention);
            break;
        case PixelAvatarPose::Sleep:
            stamp_sprite(frame, kSleepMark, 44, 20 - static_cast<int>((time_ms / 700U) % 3U), PixelTone::Lilac);
            break;
    }
}

// frame: Borrowed output retaining its semantic state and indicator.
// reaction_ms: Active reaction elapsed time, strictly below 1600 milliseconds.
void draw_reaction(PixelAvatarFrame &frame, uint32_t reaction_ms)
{
    for (uint32_t heart = 0; heart < 2; ++heart) {
        const uint32_t delay = heart * 220U;
        if (reaction_ms < delay) {
            continue;
        }
        const uint32_t age = reaction_ms - delay;
        const int rise = static_cast<int>(age * 14U / kPixelAvatarReactionMs);
        const int x = heart == 0 ? 11 : 49;
        const PixelTone tone = reaction_ms < 1250U ? PixelTone::Heart : PixelTone::Cheek;
        stamp_sprite(frame, kHeart, x, 20 - rise, tone);
    }
}

}  // namespace

// frame: Caller-owned complete indexed output, reused without temporary frame copies.
// pose: Semantic state; Error and Sleep suppress touch happiness.
// time_ms: Active artistic clock independent of the number of rendered frames.
// pose_ms: State-local clock for the wake and initial error motion.
// reaction_ms: Touch elapsed time, with values at or above 1600 inactive.
// Exception: Invalid pose values are logged and leave a transparent output.
void compose_pixel_avatar(PixelAvatarFrame &frame, PixelAvatarPose pose,
    uint32_t time_ms, uint32_t pose_ms, uint32_t reaction_ms)
{
    frame.cells.fill(0);
    if (!valid_pose(pose)) {
        ESP_LOGE(kTag, "Compose rejected pose=%u (%s)",
            static_cast<unsigned>(pose), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return;
    }
    const bool reacting = reaction_ms < kPixelAvatarReactionMs &&
        pose != PixelAvatarPose::Error && pose != PixelAvatarPose::Sleep;
    int offset_x = 0;
    int offset_y = time_ms % 3200U < 1600U ? 0 : 1;
    int height_percent = 100;
    if (pose == PixelAvatarPose::Waking) {
        const uint32_t progress = std::min(pose_ms, uint32_t{800});
        height_percent = 55 + static_cast<int>(progress * 45U / 800U);
    } else if (pose == PixelAvatarPose::Sleep) {
        height_percent = 72;
        offset_y += 3;
    } else if (pose == PixelAvatarPose::Thinking) {
        offset_x = static_cast<int>((time_ms / 1400U) % 3U) - 1;
    } else if (pose == PixelAvatarPose::Error && pose_ms < 480U) {
        offset_x = static_cast<int>((pose_ms / 80U) % 3U) - 1;
    }
    if (reacting) {
        const uint32_t phase = reaction_ms % 400U;
        const uint32_t amplitude = 6U - reaction_ms / 400U;
        const uint32_t hop = 4U * phase * (400U - phase) * amplitude / 160000U;
        offset_y -= static_cast<int>(hop);
    }

    for (const JellyLobe &lobe : kJellyLobes) {
        const int radius_y = std::max(1, lobe.radius_y * height_percent / 100);
        fill_lobe(frame, lobe.x + offset_x, body_row(lobe.y, offset_y, height_percent),
            lobe.radius_x, radius_y);
    }
    shade_body(frame, offset_x, offset_y, height_percent);
    stamp_sprite(frame, kHighlight, 21 + offset_x,
        body_row(19, offset_y, height_percent), PixelTone::White);
    put_cell(frame, 47 + offset_x, body_row(32, offset_y, height_percent), PixelTone::White);
    draw_face(frame, pose, time_ms, pose_ms, reacting, offset_x, offset_y, height_percent);
    draw_pose_detail(frame, pose, time_ms, offset_y);
    if (reacting) {
        draw_reaction(frame, reaction_ms);
    }
}

// previous: Borrowed previous frame; no local 4096-byte copy is made.
// current: Borrowed newly composed frame.
// viewport: Integer-scaled logical square, independent of display orientation.
// Exception: Invalid viewport returns an empty rectangle after logging its values.
CODY2D::Rect pixel_avatar_damage(const PixelAvatarFrame &previous,
    const PixelAvatarFrame &current, CODY2D::Rect viewport)
{
    if (validate_viewport(viewport) != ESP_OK) {
        return {};
    }
    int left = kGridSize;
    int top = kGridSize;
    int right = 0;
    int bottom = 0;
    for (int y = 0; y < kGridSize; ++y) {
        for (int x = 0; x < kGridSize; ++x) {
            const size_t index = static_cast<size_t>(y * kGridSize + x);
            if (previous.cells[index] != current.cells[index]) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x + 1);
                bottom = std::max(bottom, y + 1);
            }
        }
    }
    if (left == kGridSize) {
        return {};
    }
    const uint32_t scale = viewport.width / kGridSize;
    return {
        static_cast<uint16_t>(viewport.x + static_cast<uint32_t>(left) * scale),
        static_cast<uint16_t>(viewport.y + static_cast<uint32_t>(top) * scale),
        static_cast<uint16_t>(static_cast<uint32_t>(right - left) * scale),
        static_cast<uint16_t>(static_cast<uint32_t>(bottom - top) * scale),
    };
}

// frame: Borrowed indexed artwork; visible palette entries are validated before writes.
// viewport: Integer-scaled logical square containing the avatar.
// strip: Borrowed RGB565 tile, preserving padding and storage byte order.
// first_row: Logical row of the tile's first pixel.
// first_column: Logical column of the tile's first pixel.
// Exception: Returns original surface errors or rejected bounds/palette indices.
esp_err_t draw_pixel_avatar_rows(const PixelAvatarFrame &frame,
    CODY2D::Rect viewport, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    const esp_err_t surface_error = CODY2D::validate_surface(strip);
    if (surface_error != ESP_OK) {
        return surface_error;
    }
    const esp_err_t viewport_error = validate_viewport(viewport);
    if (viewport_error != ESP_OK) {
        return viewport_error;
    }
    const uint32_t tile_right = static_cast<uint32_t>(first_column) + strip.width;
    const uint32_t tile_bottom = static_cast<uint32_t>(first_row) + strip.height;
    if (tile_right > kCoordinateEnd || tile_bottom > kCoordinateEnd) {
        ESP_LOGE(kTag, "Draw rejected tile: x=%u y=%u width=%u height=%u (%s)",
            first_column, first_row, strip.width, strip.height,
            esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const uint32_t left = std::max<uint32_t>(viewport.x, first_column);
    const uint32_t top = std::max<uint32_t>(viewport.y, first_row);
    const uint32_t right = std::min<uint32_t>(viewport.x + viewport.width, tile_right);
    const uint32_t bottom = std::min<uint32_t>(viewport.y + viewport.height, tile_bottom);
    if (left >= right || top >= bottom) {
        return ESP_OK;
    }
    const uint32_t scale = viewport.width / kGridSize;
    const uint32_t source_left = (left - viewport.x) / scale;
    const uint32_t source_top = (top - viewport.y) / scale;
    const uint32_t source_right = (right - 1U - viewport.x) / scale;
    const uint32_t source_bottom = (bottom - 1U - viewport.y) / scale;
    for (uint32_t y = source_top; y <= source_bottom; ++y) {
        for (uint32_t x = source_left; x <= source_right; ++x) {
            const uint8_t index = frame.cells[y * kGridSize + x];
            if (index >= kPalette.size()) {
                ESP_LOGE(kTag, "Draw rejected palette index=%u at cell=%lu,%lu (%s)",
                    static_cast<unsigned>(index), static_cast<unsigned long>(x),
                    static_cast<unsigned long>(y), esp_err_to_name(ESP_ERR_INVALID_ARG));
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    for (uint32_t y = top; y < bottom; ++y) {
        const uint32_t source_y = (y - viewport.y) / scale;
        uint16_t *destination = strip.pixels + (y - first_row) * strip.stride;
        for (uint32_t x = left; x < right; ++x) {
            const uint32_t source_x = (x - viewport.x) / scale;
            const uint8_t index = frame.cells[source_y * kGridSize + source_x];
            if (index != 0) {
                destination[x - first_column] = CODY2D::rgb565_native_to_stored(
                    kPalette[index], strip.byte_order);
            }
        }
    }
    return ESP_OK;
}

}  // namespace CODY
