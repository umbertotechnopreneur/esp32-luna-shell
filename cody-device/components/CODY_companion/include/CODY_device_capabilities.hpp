/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include <cstdint>

namespace CODY {

enum class DisplayShape : uint8_t { Rectangle, Circle };
enum class DisplayPixelFormat : uint8_t { Unavailable, Rgb565 };
enum class DisplayRotationSupport : uint8_t { Fixed, Orthogonal };

struct LogicalRect {
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t width = 0;
    uint16_t height = 0;

    // point_x: Acquired logical column.
    // point_y: Acquired logical row.
    constexpr bool contains(int32_t point_x, int32_t point_y) const
    {
        return point_x >= x && point_x < int32_t{x} + width &&
            point_y >= y && point_y < int32_t{y} + height;
    }
};

struct DisplayGeometry {
    uint16_t width = 0;
    uint16_t height = 0;
    DisplayShape shape = DisplayShape::Rectangle;
    LogicalRect safe_content{};

    // left: First logical column in the rectangular transfer.
    // top: First logical row in the rectangular transfer.
    // right: Exclusive column limit.
    // bottom: Exclusive row limit.
    constexpr bool admits_transfer(uint32_t left, uint32_t top, uint32_t right, uint32_t bottom) const
    {
        // A round panel still accepts transfers to the complete addressable rectangle.
        return left < right && top < bottom && right <= width && bottom <= height;
    }

    // bounds: Complete logical artwork or hit rectangle, with exclusive right/bottom edges.
    constexpr bool admits_content(LogicalRect bounds) const
    {
        const uint32_t right = uint32_t{bounds.x} + bounds.width;
        const uint32_t bottom = uint32_t{bounds.y} + bounds.height;
        if (!admits_transfer(bounds.x, bounds.y, right, bottom)) return false;
        if (shape == DisplayShape::Rectangle) return true;
        if (shape != DisplayShape::Circle) return false;

        // Compare doubled distances so odd canvas dimensions retain their half-pixel center.
        const int64_t left_distance = int64_t{width} - 2 * bounds.x;
        const int64_t right_distance = 2 * int64_t{right} - width;
        const int64_t top_distance = int64_t{height} - 2 * bounds.y;
        const int64_t bottom_distance = 2 * int64_t{bottom} - height;
        const int64_t dx = left_distance > right_distance ? left_distance : right_distance;
        const int64_t dy = top_distance > bottom_distance ? top_distance : bottom_distance;
        const int64_t diameter = width < height ? width : height;
        return dx * dx + dy * dy <= diameter * diameter;
    }

    // grid_side: Source grid width and height in cells.
    // preferred_scale: Maximum integer pixels per cell.
    // Returns an empty viewport when the admitted content area cannot fit a cell-for-cell grid.
    constexpr LogicalRect centered_grid(uint16_t grid_side, uint16_t preferred_scale) const
    {
        if (grid_side == 0 || preferred_scale == 0 ||
            !admits_content(safe_content)) {
            return {};
        }
        const uint16_t available = safe_content.width < safe_content.height
            ? safe_content.width : safe_content.height;
        const uint16_t fitting_scale = available / grid_side;
        const uint16_t scale = fitting_scale < preferred_scale ? fitting_scale : preferred_scale;
        if (scale == 0) return {};
        const uint16_t side = grid_side * scale;
        return {
            static_cast<uint16_t>(safe_content.x + (safe_content.width - side) / 2U),
            static_cast<uint16_t>(safe_content.y + (safe_content.height - side) / 2U),
            side,
            side,
        };
    }
};

// Selected capabilities only. Owners still report measured health, freshness,
// free space and actual failures. No memory capacity is implied by a RAM class.
struct DeviceCapabilities {
    DisplayGeometry geometry{};
    DisplayPixelFormat pixel_format = DisplayPixelFormat::Unavailable;
    DisplayRotationSupport rotation = DisplayRotationSupport::Fixed;
    bool touch = false;
    bool boot_button = false;
    bool power_button = false;
    bool speaker = false;
    uint8_t microphone_channels = 0;
    bool voice_recording = false;
    bool persistent_settings = false;
    bool removable_storage = false;
    bool internal_ram = false;
    bool external_ram_heap = false;
    bool battery_measurement = false;
    bool motion_sensor = false;
    bool external_rtc = false;
    bool gnss = false;
    bool distance = false;
};

} // namespace CODY
