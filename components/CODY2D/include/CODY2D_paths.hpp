#pragma once

#include "CODY2D.hpp"

namespace CODY2D {

struct Vec2 { float x = 0; float y = 0; };

// Paths use canvas coordinates. Only this explicit strip API clips to a tile.
constexpr uint16_t kMaxPathVertices = 256;
constexpr float kMaxStrokeWidth = 21;

// Coverage is opt-in: existing fills keep their exact pixel-center raster rule.
enum class FillAntialiasing : uint8_t { Disabled, Coverage };

// Default construction creates an invalid destination for prepare_stroke.
// Successful preparation is the only way to create raster-ready coefficients.
class PreparedStroke {
public:
    PreparedStroke() = default;

private:
    friend esp_err_t prepare_stroke(
        Vec2 from, Vec2 to, float width, uint16_t color, PreparedStroke &prepared
    );
    friend esp_err_t stroke_prepared_rows(
        Surface strip, uint16_t first_row, const PreparedStroke &prepared,
        uint16_t first_column
    );
    friend esp_err_t stroke_prepared_batch_rows(
        Surface strip, uint16_t first_row, const PreparedStroke *prepared,
        uint16_t count, uint16_t first_column
    );
    friend esp_err_t stroke_rows(
        Surface strip, uint16_t first_row, Vec2 from, Vec2 to,
        float width, uint16_t color, uint16_t first_column
    );

    // from: Validated segment start in canvas coordinates.
    // to: Validated segment end in canvas coordinates.
    // width: Validated positive stroke diameter.
    // color: Native RGB565 stroke color.
    PreparedStroke(Vec2 from, Vec2 to, float width, uint16_t color);

    // strip: Validated writable destination tile.
    // first_row: Validated canvas row represented by the tile.
    // first_column: Validated canvas column represented by the tile.
    esp_err_t raster_rows(Surface strip, uint16_t first_row, uint16_t first_column) const;

    bool valid_ = false;
    Vec2 from_{};
    uint16_t color_ = 0;
    float dx_ = 0;
    float dy_ = 0;
    float squared_length_ = 0;
    float squared_radius_ = 0;
    float cross_threshold_ = 0;
    float cross_guard_ = 0;
    bool bound_scanline_ = false;
    float band_half_width_ = 0;
    float slope_ = 0;
    float loose_left_ = 0;
    float loose_right_ = 0;
    float loose_top_ = 0;
    float loose_bottom_ = 0;
    int raster_left_ = 0;
    int raster_right_ = 0;
    int raster_top_ = 0;
    int raster_bottom_ = 0;
};

// strip: Validated caller-owned RGB565 destination with its own width and stride.
// first_row: Canvas row represented by the strip's first pixel row.
// vertices: Readable closed polygon vertices in canvas coordinates.
// count: Number of vertices, bounded by kMaxPathVertices.
// palette: Readable 256-entry native RGB565 palette.
// first_color: Top gradient slot, or the solid color slot.
// last_color: Bottom gradient slot, equal to first_color for a solid fill.
// opacity: Source coverage from transparent zero to opaque 255, composited in native RGB565.
// first_column: Canvas column represented by the strip's first pixel column.
// antialiasing: Disabled preserves pixel-center fills; Coverage smooths the polygon boundary.
// clip_vertices: Optional evaluated containing polygon; clipping shares the fill's sampling policy.
// clip_count: Zero without a clip; combined subject and clip vertices fit kMaxPathVertices.
// Exception: Returns the real surface/raster error or rejects invalid geometry and clip capacity.
esp_err_t fill_path_rows(Surface strip, uint16_t first_row,
                        const Vec2 *vertices, uint16_t count,
                        const uint16_t *palette, uint8_t first_color, uint8_t last_color,
                        uint8_t opacity = 255, uint16_t first_column = 0,
                        FillAntialiasing antialiasing = FillAntialiasing::Disabled,
                        const Vec2 *clip_vertices = nullptr, uint16_t clip_count = 0);

// from: Segment start in canvas coordinates.
// to: Segment end in canvas coordinates; an equal endpoint prepares a dot.
// width: Positive finite stroke diameter in pixels, at most kMaxStrokeWidth.
// color: Native RGB565 stroke color, with round caps.
// prepared: Receives validated geometry and reusable raster coefficients on success.
esp_err_t prepare_stroke(Vec2 from, Vec2 to, float width, uint16_t color,
                         PreparedStroke &prepared);

// strip: Caller-owned RGB565 destination.
// first_row: Global canvas row represented by this strip.
// prepared: Valid immutable stroke produced by prepare_stroke.
// first_column: Canvas column represented by the strip's first pixel column.
esp_err_t stroke_prepared_rows(Surface strip, uint16_t first_row,
                               const PreparedStroke &prepared,
                               uint16_t first_column = 0);

// strip: Caller-owned RGB565 destination, validated once for the complete batch.
// first_row: Global canvas row represented by this strip.
// prepared: Readable prepared strokes in their required paint order.
// count: Number of prepared strokes; zero performs validation without drawing.
// first_column: Canvas column represented by the strip's first pixel column.
esp_err_t stroke_prepared_batch_rows(Surface strip, uint16_t first_row,
                                     const PreparedStroke *prepared, uint16_t count,
                                     uint16_t first_column = 0);

// strip: Caller-owned RGB565 destination.
// first_row: Global canvas row represented by this strip.
// from: Segment start in canvas coordinates.
// to: Segment end in canvas coordinates; an equal endpoint draws a dot.
// width: Positive finite stroke diameter in pixels, at most kMaxStrokeWidth.
// color: Native RGB565 stroke color, with round caps.
// first_column: Canvas column represented by the strip's first pixel column.
esp_err_t stroke_rows(Surface strip, uint16_t first_row,
                      Vec2 from, Vec2 to, float width, uint16_t color,
                      uint16_t first_column = 0);

}  // namespace CODY2D
