#include "CODY2D_controls.hpp"
#include "CODY2D_paths.hpp"
#include "CODY_dirty_plan.hpp"
#include "CODY_rig.hpp"
#include "CODY_jelly_simd.hpp"
#include "cloud_rig_data.hpp"
#include "cody_jelly_data.hpp"
#include "host_settings.hpp"
#include "CODY_lifecycle.hpp"
#include "CODY_scene.hpp"
#include "CODY_navigation_visibility.hpp"
#include "LUNASHELL_button_hold.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <iterator>
#include <limits>
#include <vector>

#if CODY_JELLY_SIMD_AVAILABLE
namespace CODY {
extern bool corrupt_next_simd_model_output;
}
#endif

namespace {
int failures = 0;

// condition: Observable contract result, independent of debug assertions.
// message: Failed behavior to show in the manually requested check.
void require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

// Exercise the real integer alpha compositor, invalid geometry and stride guards.
void check_fill_contract()
{
    constexpr uint16_t width = 16;
    constexpr uint16_t stride = 18;
    constexpr uint16_t guard = 0x35ad;
    std::array<uint16_t, stride * 16> pixels;
    pixels.fill(guard);
    const CODY2D::Surface surface{pixels.data(), width, 16, stride,
                                static_cast<uint32_t>(pixels.size())};
    require(CODY2D::clear(surface, 0x001f) == ESP_OK, "surface clear succeeds");
    std::array<uint16_t, 256> palette{};
    palette[0] = 0xf800;
    const CODY2D::Vec2 rectangle[] = {{2, 2}, {12, 2}, {12, 12}, {2, 12}};
    require(CODY2D::fill_path_rows(surface, 0, rectangle, 4, palette.data(), 0, 0, 128) == ESP_OK,
            "native alpha polygon succeeds");
    require(pixels[4 * stride + 4] == 0x800f, "128 alpha rounds red/blue in native RGB565");
    for (uint16_t row = 0; row < 16; ++row) {
        require(pixels[row * stride + width] == guard && pixels[row * stride + width + 1] == guard,
                "raster preserves row padding");
    }
    const auto before = pixels;
    const CODY2D::Vec2 invalid[] = {{2, 2}, {12, 2}, {NAN, 12}};
    require(CODY2D::fill_path_rows(surface, 0, invalid, 3, palette.data(), 0, 0) == ESP_ERR_INVALID_ARG,
            "NaN polygon is rejected");
    require(pixels == before, "rejected polygon does not write plausible pixels");
    require(CODY2D::stroke_rows(surface, 0, {2, 2}, {12, 12}, 22, 0xffff) == ESP_ERR_INVALID_ARG,
            "oversized stroke is rejected");
    require(pixels == before, "rejected stroke preserves all pixels");
}

struct AnalyticRectangle {
    double left;
    double top;
    double right;
    double bottom;
};

// rectangle: Axis-aligned geometric region whose pixel intersection has an exact area.
// column: Absolute canvas pixel x coordinate.
// row: Absolute canvas pixel y coordinate.
double rectangle_pixel_area(AnalyticRectangle rectangle, uint16_t column, uint16_t row)
{
    const double width = std::max(0., std::min(column + 1., rectangle.right) -
                                     std::max(static_cast<double>(column), rectangle.left));
    const double height = std::max(0., std::min(row + 1., rectangle.bottom) -
                                      std::max(static_cast<double>(row), rectangle.top));
    return width * height;
}

// source: Native RGB565 color of the independently specified analytic shape.
// destination: Unmodified background pixel.
// opacity: Authored source opacity before geometric coverage.
// area: Exact fraction of the pixel occupied by the analytic region.
uint16_t analytic_coverage_color(uint16_t source, uint16_t destination,
                                 uint8_t opacity, double area)
{
    const long alpha = std::lround(opacity * area);
    const double source_weight = alpha / 255.;
    const double destination_weight = 1. - source_weight;
    const long red = std::lround(((source >> 11) & 31) * source_weight +
                                ((destination >> 11) & 31) * destination_weight);
    const long green = std::lround(((source >> 5) & 63) * source_weight +
                                  ((destination >> 5) & 63) * destination_weight);
    const long blue = std::lround((source & 31) * source_weight +
                                 (destination & 31) * destination_weight);
    return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

// No parameters: exact quarter-pixel rectangles expose coverage and gradient-cap mistakes.
void check_coverage_rectangle_contract()
{
    constexpr uint16_t width = 8;
    constexpr uint16_t height = 8;
    constexpr uint16_t stride = 11;
    constexpr uint16_t guard = 0x35ad;
    constexpr uint16_t background = 0x001f;
    constexpr size_t outer_guard = 4;
    std::array<uint16_t, stride * height + 2 * outer_guard> pixels;
    pixels.fill(guard);
    const CODY2D::Surface surface{pixels.data() + outer_guard, width, height, stride,
                                stride * height};
    require(CODY2D::clear(surface, background) == ESP_OK, "coverage rectangle clear succeeds");
    std::array<uint16_t, 256> palette{};
    palette[10] = 0xf800;
    palette[11] = 0x07e0;
    const CODY2D::Vec2 rectangle[] = {{2.25f, 2.25f}, {5.5f, 2.25f},
                                     {5.5f, 4.75f}, {2.25f, 4.75f}};
    require(CODY2D::fill_path_rows(surface, 0, rectangle, 4, palette.data(), 10, 10,
            128, 0, CODY2D::FillAntialiasing::Coverage) == ESP_OK,
            "fractional coverage rectangle succeeds");
    const AnalyticRectangle exact_rectangle{2.25, 2.25, 5.5, 4.75};
    for (uint16_t row = 0; row < height; ++row) {
        for (uint16_t column = 0; column < width; ++column) {
            const double area = rectangle_pixel_area(exact_rectangle, column, row);
            require(surface.pixels[row * stride + column] ==
                    analytic_coverage_color(palette[10], background, 128, area),
                    "coverage rectangle matches exact area including corners and opacity");
        }
        for (uint16_t column = width; column < stride; ++column) {
            require(surface.pixels[row * stride + column] == guard,
                    "coverage rectangle preserves padded row sentinels");
        }
    }
    require(surface.pixels[3 * stride + 3] == 0x800f,
            "full coverage retains the established 128-alpha RGB565 result");
    for (size_t index = 0; index < outer_guard; ++index) {
        require(pixels[index] == guard && pixels[pixels.size() - 1 - index] == guard,
                "coverage rectangle preserves both outer sentinels");
    }

    require(CODY2D::clear(surface, background) == ESP_OK, "gradient cap clear succeeds");
    const CODY2D::Vec2 shallow_caps[] = {{1.25f, 1.75f}, {4.75f, 1.75f},
                                        {4.75f, 2.25f}, {1.25f, 2.25f}};
    require(CODY2D::fill_path_rows(surface, 0, shallow_caps, 4, palette.data(), 10, 11,
            255, 0, CODY2D::FillAntialiasing::Coverage) == ESP_OK,
            "horizontal caps without a center-sample crossing remain visible");
    require(surface.pixels[1 * stride + 2] == 0x4017,
            "upper cap clamps gradient to the first slot with quarter coverage");
    require(surface.pixels[2 * stride + 2] == 0x0217,
            "lower cap clamps gradient to the last slot with quarter coverage");

    const auto before = pixels;
    require(CODY2D::fill_path_rows(surface, 0, rectangle, 4, palette.data(), 10, 10,
            0, 0, static_cast<CODY2D::FillAntialiasing>(255)) == ESP_ERR_INVALID_ARG,
            "unknown antialiasing policy is rejected even for transparent geometry");
    require(pixels == before, "rejected antialiasing policy leaves all pixels untouched");
}

// No parameters: an analytic U shape exercises disjoint spans and exact odd tile seams.
void check_coverage_concave_tiles_contract()
{
    constexpr uint16_t size = 16;
    constexpr uint16_t background = 0x39e7;
    constexpr uint16_t guard = 0x35ad;
    constexpr size_t outer_guard = 8;
    const CODY2D::Vec2 concave[] = {
        {1.25f, 1.75f}, {13.75f, 1.75f}, {13.75f, 12.25f}, {9.5f, 12.25f},
        {9.5f, 5.25f}, {5.5f, 5.25f}, {5.5f, 12.25f}, {1.25f, 12.25f},
    };
    const AnalyticRectangle pieces[] = {
        {1.25, 1.75, 13.75, 5.25},
        {1.25, 5.25, 5.5, 12.25},
        {9.5, 5.25, 13.75, 12.25},
    };
    std::array<uint16_t, 256> palette{};
    palette[10] = 0xf800;
    palette[11] = 0x07e0;
    constexpr uint16_t columns[] = {0, 1, 2, 5, 6, 9, 10, 13, 14, 16};
    constexpr uint16_t rows[] = {0, 1, 2, 5, 6, 12, 13, 16};
    for (const uint8_t opacity : {uint8_t{255}, uint8_t{173}}) {
        std::array<uint16_t, size * size> reference;
        reference.fill(background);
        const CODY2D::Surface full_surface{reference.data(), size, size, size,
                                          static_cast<uint32_t>(reference.size())};
        require(CODY2D::fill_path_rows(full_surface, 0, concave, std::size(concave),
                palette.data(), 10, 11, opacity, 0, CODY2D::FillAntialiasing::Coverage) == ESP_OK,
                "concave full-surface coverage fill succeeds");
        for (uint16_t row = 0; row < size; ++row) {
            // The two-slot ramp switches at its geometric midpoint y = 7.
            const uint16_t color = palette[row < 7 ? 10 : 11];
            for (uint16_t column = 0; column < size; ++column) {
                double exact_area = 0;
                for (const AnalyticRectangle piece : pieces) {
                    exact_area += rectangle_pixel_area(piece, column, row);
                }
                require(reference[row * size + column] ==
                        analytic_coverage_color(color, background, opacity, exact_area),
                        "concave spans composite each analytic pixel exactly once");
            }
        }

        for (size_t tile_row = 1; tile_row < std::size(rows); ++tile_row) {
            for (size_t tile_column = 1; tile_column < std::size(columns); ++tile_column) {
                const uint16_t first_row = rows[tile_row - 1];
                const uint16_t first_column = columns[tile_column - 1];
                const uint16_t width = columns[tile_column] - first_column;
                const uint16_t height = rows[tile_row] - first_row;
                const uint16_t stride = width + 3;
                const uint32_t capacity = stride * height;
                std::array<uint16_t, (size + 3) * size + 2 * outer_guard> storage;
                storage.fill(guard);
                const CODY2D::Surface tile{storage.data() + outer_guard, width, height, stride,
                                          capacity};
                require(CODY2D::clear(tile, background) == ESP_OK, "coverage tile clear succeeds");
                require(CODY2D::fill_path_rows(tile, first_row, concave, std::size(concave),
                        palette.data(), 10, 11, opacity, first_column,
                        CODY2D::FillAntialiasing::Coverage) == ESP_OK,
                        "odd compact coverage tile succeeds");
                for (uint16_t row = 0; row < height; ++row) {
                    for (uint16_t column = 0; column < width; ++column) {
                        require(tile.pixels[row * stride + column] ==
                                reference[(first_row + row) * size + first_column + column],
                                "coverage is identical across vertical and horizontal tile seams");
                    }
                    for (uint16_t column = width; column < stride; ++column) {
                        require(tile.pixels[row * stride + column] == guard,
                                "coverage compact tiles preserve padded row sentinels");
                    }
                }
                for (size_t index = 0; index < outer_guard; ++index) {
                    require(storage[index] == guard && storage[outer_guard + capacity + index] == guard,
                            "coverage compact tiles preserve both outer sentinels");
                }
            }
        }
    }
}

// scene: One immutable evaluated frame, reused by all destination partitions.
// strip_rows: Partition size, including sizes that leave an incomplete final strip.
// debug: Whether bone/point overlays and navigation are included.
// selected: Stable marker identity to highlight, or kNoPoint.
// paused: Current animation pause state; its authored navigation control stays hidden.
// Exceptions: std::bad_alloc if host framebuffer allocation fails.
std::vector<uint16_t> render(const CODY::RigScene &scene, uint16_t strip_rows, bool debug,
                             CODY::PointId selected = CODY::kNoPoint, bool paused = false)
{
    constexpr uint16_t size = 466;
    std::vector<uint16_t> pixels(size * size, 0x35ad);
    for (uint16_t first = 0; first < size; first += strip_rows) {
        const uint16_t rows = std::min<uint16_t>(strip_rows, size - first);
        const CODY2D::Surface strip{pixels.data() + first * size, size, rows, size,
                                  static_cast<uint32_t>(size * rows)};
        require(CODY::draw_rig_rows(scene, strip, first, debug, selected, nullptr, paused) == ESP_OK,
                "native rig strip succeeds");
    }
    return pixels;
}

struct FrameView {
    const CODY::RigScene &scene;
    bool debug = false;
    CODY::PointId selected = CODY::kNoPoint;
    bool paused = false;
};

// frame: Immutable geometry and navigation state shared across compact tiles.
// bounds: Nonempty half-open destination rectangle in the logical canvas.
// canvas: Full host framebuffer receiving only this tile's visible pixels.
// Exceptions: std::bad_alloc if guarded tile allocation fails.
void render_compact_tile(FrameView frame, const CODY::DamageRect &bounds,
                         std::vector<uint16_t> &canvas)
{
    constexpr uint16_t guard = 0x35ad;
    constexpr size_t outer_guard = 8;
    const uint16_t width = bounds.right - bounds.left;
    const uint16_t rows = bounds.bottom - bounds.top;
    const uint16_t stride = width + 3;
    const size_t storage_size = static_cast<size_t>(stride) * rows;
    std::vector<uint16_t> storage(storage_size + 2 * outer_guard, guard);
    const CODY2D::Surface tile{storage.data() + outer_guard, width, rows, stride,
                              static_cast<uint32_t>(storage_size)};
    const esp_err_t result = CODY::draw_rig_rows(frame.scene, tile, bounds.top,
        frame.debug, frame.selected, nullptr, frame.paused, bounds.left);
    require(result == ESP_OK, "compact rig tile succeeds");
    if (result != ESP_OK) {
        return;
    }
    bool sentinels_intact = true;
    for (size_t index = 0; index < outer_guard; ++index) {
        sentinels_intact = sentinels_intact && storage[index] == guard &&
            storage[outer_guard + storage_size + index] == guard;
    }
    for (uint16_t row = 0; row < rows; ++row) {
        for (uint16_t column = width; column < stride; ++column) {
            sentinels_intact = sentinels_intact && tile.pixels[row * stride + column] == guard;
        }
        std::copy_n(tile.pixels + row * stride, width,
                    canvas.data() + (bounds.top + row) * 466 + bounds.left);
    }
    require(sentinels_intact, "compact raster preserves outer guards and every row's padding");
}

// No parameters: odd seams and one-pixel edge tiles must not shift the raster.
// Exceptions: std::bad_alloc if host framebuffer allocation fails.
void check_compact_raster_contract()
{
    CODY::RigScene scene;
    require(CODY::evaluate_rig(CODY::Data::defaults, CODY::Data::placement, scene) == ESP_OK,
            "compact parity scene evaluates");
    constexpr uint16_t columns[] = {0, 1, 40, 73, 179, 233, 393, 465, 466};
    constexpr uint16_t rows[] = {0, 1, 29, 70, 203, 233, 263, 393, 433, 465, 466};
    const FrameView states[] = {
        {scene},
        {scene, true},
        {scene, true, CODY::PointId::Center},
        {scene, true, CODY::PointId::LobeCrownIn, true},
    };
    for (const FrameView frame : states) {
        const auto reference = render(frame.scene, 466, frame.debug, frame.selected, frame.paused);
        std::vector<uint16_t> compact(reference.size(), 0x35ad);
        for (size_t row = 1; row < std::size(rows); ++row) {
            for (size_t column = 1; column < std::size(columns); ++column) {
                const CODY::DamageRect bounds{columns[column - 1], rows[row - 1],
                                              columns[column], rows[row]};
                render_compact_tile(frame, bounds, compact);
            }
        }
        require(compact == reference,
                "disjoint compact tiles preserve artwork, skeleton and selection");
    }
}

// previous: State whose complete image is already present on the simulated panel.
// current: Next frame to reconstruct by updating only planner-supplied rectangles.
// message: Specific transition named if the reconstructed pixels diverge.
// Exceptions: std::bad_alloc if host framebuffer allocation fails.
void check_dirty_transition(FrameView previous, FrameView current, const char *message)
{
    CODY::DirtyPlan plan;
    const esp_err_t result = CODY::plan_rig_damage(previous.scene, current.scene,
        previous.debug, current.debug, previous.selected, current.selected, plan);
    require(result == ESP_OK, "valid frame transition produces a dirty plan");
    if (result != ESP_OK) {
        return;
    }
    const auto reference = render(current.scene, 466, current.debug, current.selected, current.paused);
    for (const uint16_t tile_rows : {uint16_t{2}, uint16_t{32}, uint16_t{64}}) {
        auto reconstructed = render(previous.scene, 466, previous.debug, previous.selected, previous.paused);
        for (uint16_t first_row = 0; first_row < 466; first_row += tile_rows) {
            const uint16_t row_count = std::min<uint16_t>(tile_rows, 466 - first_row);
            CODY::DamageRect bounds;
            const esp_err_t tile_result = CODY::dirty_tile_bounds(plan, first_row, row_count, bounds);
            require(tile_result == ESP_OK, "dirty tile query accepts even panel row partitions");
            if (tile_result != ESP_OK || bounds.empty()) {
                continue;
            }
            require(bounds.left % 2 == 0 && bounds.right % 2 == 0 &&
                    bounds.top % 2 == 0 && bounds.bottom % 2 == 0,
                    "every submitted compact rectangle has paired panel boundaries");
            render_compact_tile(current, bounds, reconstructed);
        }
        require(reconstructed == reference, message);
    }
}

// No parameters: exercise unchanged frames and every visual dirty-plan dependency.
// Exceptions: std::bad_alloc if host framebuffer allocation fails.
void check_dirty_plan_contract()
{
    CODY::RigScene base;
    require(CODY::evaluate_rig(CODY::Data::defaults, CODY::Data::placement, base) == ESP_OK,
            "dirty baseline scene evaluates");
    CODY::DirtyPlan unchanged;
    require(CODY::include_damage(unchanged, {0, 0, 466, 466}) == ESP_OK,
            "dirty plan accepts a full canvas invalidation");
    require(CODY::plan_rig_damage(base, base, true, true,
            CODY::PointId::Center, CODY::PointId::Center, unchanged) == ESP_OK,
            "identical scene planning succeeds");
    CODY::DamageRect bounds;
    require(CODY::dirty_tile_bounds(unchanged, 0, 466, bounds) == ESP_OK && bounds.empty(),
            "unchanged frame resets previous damage and plans no pixels");
    require(CODY::plan_rig_damage(base, base, false, false,
            CODY::kNoPoint, CODY::PointId::Center, unchanged) == ESP_OK,
            "hidden selection planning succeeds");
    require(CODY::dirty_tile_bounds(unchanged, 0, 466, bounds) == ESP_OK && bounds.empty(),
            "selection changes without overlay do not dirty pixels");

    check_dirty_transition({base}, {base, true}, "overlay enable reconstructs a full redraw");
    check_dirty_transition({base, true}, {base}, "overlay disable erases all old skeleton pixels");
    check_dirty_transition({base, true}, {base, true, CODY::PointId::Center},
                           "selected marker appearance reconstructs a full redraw");
    check_dirty_transition({base, true, CODY::PointId::Center},
                           {base, true, CODY::PointId::LobeCrownIn},
                           "selection movement erases the previous marker highlight");
    check_dirty_transition({base, true, CODY::PointId::LobeCrownIn}, {base, true},
                           "selection removal reconstructs a full redraw");
    check_dirty_transition({base}, {base, false, CODY::kNoPoint, true},
                           "pause navigation reconstructs a full redraw");
    check_dirty_transition({base, false, CODY::kNoPoint, true}, {base},
                           "resume navigation reconstructs a full redraw");

    CODY::RigScene changed;
    CODY::RigScene previous = base;
    auto channels = CODY::Data::defaults;
    for (float opacity : {0.f, .5f, 1.f}) {
        channels[CODY::FaceChannel::CheekOpacity] = opacity;
        require(CODY::evaluate_rig(channels, CODY::Data::placement, changed) == ESP_OK,
                "opacity transition scene evaluates");
        check_dirty_transition({previous}, {changed}, "opacity change recomposites overlapping artwork");
        previous = changed;
    }
    channels = CODY::Data::defaults;
    channels[CODY::FaceChannel::EyeLeft] = 0;
    require(CODY::evaluate_rig(channels, CODY::Data::placement, changed) == ESP_OK,
            "hidden eye scene evaluates");
    check_dirty_transition({base}, {changed}, "hidden paths erase their old footprint");
    check_dirty_transition({changed}, {base}, "visible paths restore their complete footprint");

    for (const CODY::Transform placement : {CODY::Transform{12, 18, 200}, CODY::Transform{450, 449, 20}}) {
        require(CODY::evaluate_rig(CODY::Data::defaults, placement, changed) == ESP_OK,
                "edge-clipped transform evaluates");
        check_dirty_transition({base, true}, {changed, true},
                               "transform changes redraw old and new clipped geometry");
        check_dirty_transition({changed, true}, {base, true},
                               "return from canvas edge clears every departing pixel");
    }
    auto contour = CODY::make_contour_state();
    contour.controls[CODY::ContourControl::Fullness] = .6f;
    contour.controls[CODY::ContourControl::Amplitude] = .8f;
    contour.phase = 2.f;
    require(CODY::evaluate_rig(CODY::Data::defaults, CODY::Data::placement, changed, &contour) == ESP_OK,
            "deformed contour scene evaluates");
    check_dirty_transition({base}, {changed}, "contour change preserves full polygon gradients");
    check_dirty_transition({base, true}, {changed, true}, "contour change preserves skeleton dependencies");
}

// Compare culling with an explicitly uncullable scene using the very same C++ raster.
// Exceptions: std::bad_alloc if host framebuffer allocation fails.
void check_rig_contract()
{
    CODY::RigScene scene;
    require(CODY::evaluate_rig(CODY::Data::defaults, CODY::Data::placement, scene) == ESP_OK,
            "generated ready geometry evaluates");
    require(scene.valid, "successful geometry publishes validity");
    // Emit a named fixture for the Python owner to compare against its evaluator.
    std::printf("{\"world\":{");
    for (size_t index = 0; index < std::size(CODY::Data::points); ++index) {
        const auto &point = CODY::Data::points[index];
        std::printf("%s\"%s\":[%.9g,%.9g]", index == 0 ? "" : ",", point.name,
                    scene.world[index].x, scene.world[index].y);
    }
    std::printf("}}\n");
    for (float scale : {20.f, CODY::Data::placement.scale, 200.f}) {
        auto placement = CODY::Data::placement;
        placement.scale = scale;
        require(CODY::evaluate_rig(CODY::Data::defaults, placement, scene) == ESP_OK,
                "bounded scale geometry evaluates");
        auto reference_scene = scene;
        for (auto &path : reference_scene.paths) {
            path.first_scan_y = -std::numeric_limits<float>::infinity();
            path.last_scan_y = std::numeric_limits<float>::infinity();
        }
        for (bool debug : {false, true}) {
            const auto reference = render(reference_scene, 466, debug);
            constexpr uint16_t strip_sizes[] = {3, 8, 17, 64};
            for (const uint16_t rows : strip_sizes) {
                require(render(scene, rows, debug) == reference,
                        "culling and strip partition preserve the full native framebuffer");
            }
        }
    }
    auto invalid = CODY::Data::defaults;
    invalid[CODY::FaceChannel::EyeLeft] = NAN;
    require(CODY::evaluate_rig(invalid, CODY::Data::placement, scene) == ESP_ERR_INVALID_ARG,
            "invalid face channel is rejected");
    require(!scene.valid, "failed evaluation invalidates the previous scene");
}
// texture: Immutable authored premultiplied RGBA8 samples.
// source_x: Q16 source coordinate, clipped to the texture footprint.
// source_y: Q16 source row, clipped to the texture footprint.
uint32_t reference_jelly_sample(const uint32_t *texture, int32_t source_x, int32_t source_y)
{
    const int32_t clipped_x = std::clamp<int32_t>(
        source_x, 0, (CODY::JellyData::width - 1) * 65536);
    const int32_t clipped_y = std::clamp<int32_t>(
        source_y, 0, (CODY::JellyData::height - 1) * 65536);
    const uint32_t column = static_cast<uint32_t>(clipped_x) / 65536U;
    const uint32_t row = static_cast<uint32_t>(clipped_y) / 65536U;
    const uint32_t next_column = std::min(column + 1U,
        static_cast<uint32_t>(CODY::JellyData::width - 1));
    const uint32_t next_row = std::min(row + 1U,
        static_cast<uint32_t>(CODY::JellyData::height - 1));
    const uint32_t horizontal_weight = static_cast<uint32_t>(clipped_x) / 256U % 256U;
    const uint32_t vertical_weight = static_cast<uint32_t>(clipped_y) / 256U % 256U;
    const uint32_t upper_first = texture[row * CODY::JellyData::width + column];
    const uint32_t upper_next = texture[row * CODY::JellyData::width + next_column];
    const uint32_t lower_first = texture[next_row * CODY::JellyData::width + column];
    const uint32_t lower_next = texture[next_row * CODY::JellyData::width + next_column];
    uint32_t result = 0;
    // The reference interpolates each channel separately, exposing packed-lane
    // carry errors and preserving both horizontal rounds before the vertical round.
    for (uint32_t shift : {0U, 8U, 16U, 24U}) {
        const uint32_t upper = (((upper_first >> shift) & 255U) * (256U - horizontal_weight) +
            ((upper_next >> shift) & 255U) * horizontal_weight + 128U) / 256U;
        const uint32_t lower = (((lower_first >> shift) & 255U) * (256U - horizontal_weight) +
            ((lower_next >> shift) & 255U) * horizontal_weight + 128U) / 256U;
        const uint32_t channel = (upper * (256U - vertical_weight) +
            lower * vertical_weight + 128U) / 256U;
        result |= channel << shift;
    }
    return result;
}

// source: Reference bilinear premultiplied RGBA8 sample.
// background: Native RGB565 color under this sample.
uint16_t reference_jelly_composite(uint32_t source, uint16_t background)
{
    const uint32_t alpha = source >> 24U;
    if (alpha == 0) {
        return background;
    }
    constexpr uint32_t channel_maximum[] = {31U, 63U, 31U};
    constexpr uint32_t native_shift[] = {11U, 5U, 0U};
    constexpr uint32_t source_shift[] = {0U, 8U, 16U};
    uint16_t result = 0;
    for (uint32_t channel = 0; channel < 3; ++channel) {
        const uint32_t maximum = channel_maximum[channel];
        const uint32_t background_channel =
            ((background >> native_shift[channel]) & maximum) * 255U / maximum;
        const uint32_t foreground_channel = (source >> source_shift[channel]) & 255U;
        const uint32_t composed_channel = std::min(255U, foreground_channel +
            (background_channel * (255U - alpha) + 127U) / 255U);
        const uint32_t quantized_channel = (composed_channel * maximum + 127U) / 255U;
        result |= static_cast<uint16_t>(quantized_channel << native_shift[channel]);
    }
    return result;
}

// skin: Authored identity selecting one real immutable packed texture.
// opaque_index: Existing real opaque sample whose row also crosses transparent padding.
void check_jelly_transparent_advance(const CODY::JellySkinDefinition &skin, uint32_t opaque_index)
{
    const uint32_t *texture = CODY::JellyData::pixels_for(skin.id);
    constexpr uint16_t guard = 0x5a5a;
    for (int32_t fraction : {0, 16384}) {
        CODY::JellyFrame frame;
        frame.valid = true;
        frame.skin_id = static_cast<uint16_t>(skin.id);
        frame.bounds = {10, 10, 13, 11};
        auto &row = frame.rows[10];
        row.left = 10;
        row.right = 13;
        row.source_x = fraction;
        row.source_y = static_cast<int32_t>(opaque_index / CODY::JellyData::width) * 65536 +
            fraction * 3;
        row.source_step = static_cast<int32_t>(std::max<uint32_t>(
            1U, opaque_index % CODY::JellyData::width)) * 65536;
        require((reference_jelly_sample(texture, row.source_x, row.source_y) >> 24U) == 0,
            "jelly advance fixture begins inside transparent padding");
        for (uint16_t background : {uint16_t{0}, uint16_t{0xffff}, uint16_t{0x001f}}) {
            for (const auto order : {
                    CODY2D::Rgb565ByteOrder::Native, CODY2D::Rgb565ByteOrder::ByteSwapped}) {
                std::array<uint16_t, 7> storage;
                storage.fill(guard);
                const uint16_t stored_background = CODY2D::rgb565_native_to_stored(background, order);
                std::fill(storage.begin() + 1, storage.begin() + 4, stored_background);
                const CODY2D::Surface tile{storage.data() + 1, 3, 1, 5, 5, order};
                require(CODY::draw_jelly_rows(frame, tile, 10, 10) == ESP_OK,
                    "jelly transparent-prefix row renders in both byte orders");
                for (int32_t column = 0; column < 3; ++column) {
                    const uint32_t sample = reference_jelly_sample(texture,
                        row.source_x + column * row.source_step, row.source_y);
                    const uint16_t expected = reference_jelly_composite(sample, background);
                    require(storage[column + 1] == CODY2D::rgb565_native_to_stored(expected, order),
                        "jelly skips transparent samples without shifting later pixels or interpolation");
                }
                require(storage[0] == guard && storage[4] == guard &&
                        storage[5] == guard && storage[6] == guard,
                    "jelly transparent-prefix rows preserve padding and both outer guards");
            }
        }
    }
}

int64_t profile_clock_ticks = 0;

// No parameters: deterministic host clock checks timer accounting, never device speed.
int64_t profile_clock()
{
    return ++profile_clock_ticks;
}

// skin: Real texture identity; every authored skin exercises both raster loops.
// opaque_index: Real body row used to cross transparent padding and visible artwork.
// kernel: Explicit scalar or host-modeled SIMD implementation; restored after the fixture.
void check_jelly_profile_parity(const CODY::JellySkinDefinition &skin, uint32_t opaque_index,
                               CODY::JellyRasterKernel kernel)
{
    require(CODY::select_jelly_raster_kernel(kernel) == ESP_OK, "select explicit parity backend");
    const uint32_t *texture = CODY::JellyData::pixels_for(skin.id);
    constexpr uint16_t guard = 0x5a5a;
    for (uint16_t width : {1, 15, 16, 17, 31, 33, 63, 64, 65, 127}) {
        for (int32_t fraction : {0, 16384, 49152}) {
            CODY::JellyFrame frame;
            frame.valid = true;
            frame.skin_id = static_cast<uint16_t>(skin.id);
            frame.bounds = {10, 10, static_cast<uint16_t>(10 + width), 11};
            auto &row = frame.rows[10];
            row.left = 10;
            row.right = static_cast<uint16_t>(10 + width);
            row.source_x = fraction - 32768;
            row.source_y = static_cast<int32_t>(opaque_index / CODY::JellyData::width) * 65536 + fraction;
            row.source_step = static_cast<int32_t>(CODY::JellyData::width) * 65536 /
                std::max<uint16_t>(1, width - 1);
            for (uint16_t inset : {0, 1}) {
                if (width <= inset * 2) {
                    continue;
                }
                const uint16_t visible = static_cast<uint16_t>(width - inset * 2);
                for (uint16_t background : {uint16_t{0}, uint16_t{0xffff}, uint16_t{0x001f}}) {
                    for (const auto order : {
                            CODY2D::Rgb565ByteOrder::Native, CODY2D::Rgb565ByteOrder::ByteSwapped}) {
                        std::vector<uint16_t> original(width + 2, guard);
                        std::fill(original.begin() + 1, original.begin() + 1 + visible,
                                  CODY2D::rgb565_native_to_stored(background, order));
                        auto expected = original;
                        for (uint16_t column = 0; column < visible; ++column) {
                            const uint32_t sample = reference_jelly_sample(texture,
                                row.source_x + (column + inset) * row.source_step, row.source_y);
                            expected[column + 1] = CODY2D::rgb565_native_to_stored(
                                reference_jelly_composite(sample, background), order);
                        }
                        for (int mode = 0; mode < 3; ++mode) {
                            auto actual = original;
                            const CODY2D::Surface tile{actual.data() + 1, visible, 1,
                                static_cast<uint16_t>(visible + 2), static_cast<uint32_t>(visible + 2), order};
                            CODY::JellyRasterProfile profile;
                            profile.now_us = profile_clock;
                            profile.blocks_enabled = mode == 2;
                            profile_clock_ticks = 0;
                            require(CODY::draw_jelly_rows(frame, tile, 10,
                                static_cast<uint16_t>(10 + inset), mode == 0 ? nullptr : &profile) == ESP_OK,
                                "unprofiled, coarse and block-profiled rows succeed");
                            require(actual == expected,
                                "profile modes preserve reference pixels, tile clipping, tails and guards");
                            const uint32_t drawn = visible - profile.coverage_skipped_pixels;
                            if (mode != 0 && kernel != CODY::JellyRasterKernel::Scalar) {
                                require(profile.vectorized_pixels + profile.scalar_tail_pixels +
                                        profile.zero_footprint_pixels + profile.coverage_skipped_pixels == visible,
                                        "SIMD counters account for pairs, tails and skipped coverage");
                                require(profile.scalar_tail_pixels == (drawn & 1U),
                                        "SIMD never reads beyond an odd visible span");
                                require(profile.verified_samples ==
                                        (kernel == CODY::JellyRasterKernel::SimdVerify ? visible : 0U),
                                        "comparison runs are explicit and counted");
                            }
                            if (mode == 2) {
                                const uint32_t block_size = 16U;
                                const uint32_t blocks = (drawn + block_size - 1U) / block_size;
                                require(profile.sampled_pixels == drawn &&
                                        profile.transparent_pixels + profile.blended_pixels == drawn &&
                                        profile.timed_blocks == blocks,
                                        "block counters cover each sample exactly once including the tail");
                                if (drawn == 0) {
                                    require(profile.interpolation_us == 0 && profile.blend_us == 0 &&
                                            profile.preparation_us == 0 && profile_clock_ticks == 3,
                                            "fully skipped row has no interpolation or composition clocks");
                                } else {
                                    const uint32_t row_preparation = kernel == CODY::JellyRasterKernel::Scalar ? 1 : 2;
                                    require(profile.interpolation_us == blocks && profile.blend_us == blocks &&
                                            profile.preparation_us == blocks + row_preparation &&
                                            profile_clock_ticks == 2 + 2 * row_preparation + 6 * blocks,
                                            "detail clocks operate per row/block, never per pixel");
                                }
                            } else {
                                require(profile.sampled_pixels == 0 && profile.timed_blocks == 0 &&
                                        profile_clock_ticks == (mode == 0 ? 0 : 2),
                                        "baseline has no hidden block timers or per-pixel counters");
                            }
                        }
                    }
                }
            }
        }
    }
    require(CODY::select_jelly_raster_kernel(CODY::JellyRasterKernel::Scalar) == ESP_OK,
            "parity fixture restores ordinary scalar rendering");
}

// No parameters: capability rejection and verifier faults cannot silently select scalar rendering.
void check_jelly_kernel_contract()
{
    require(CODY::select_jelly_raster_kernel(CODY::JellyRasterKernel::Scalar) == ESP_OK,
            "start kernel contract from scalar");
    require(CODY::select_jelly_raster_kernel(static_cast<CODY::JellyRasterKernel>(255)) == ESP_ERR_INVALID_ARG &&
            CODY::selected_jelly_raster_kernel() == CODY::JellyRasterKernel::Scalar,
            "unknown kernel is rejected without changing selection");
#if CODY_JELLY_SIMD_AVAILABLE
    require(CODY::verify_jelly_simd_kernel() == ESP_OK,
            "host model accepts all 65536 weight pairs and guarded pair layout");
    const auto &skin = CODY::JellySkinCatalog::definitions[0];
    const uint32_t *texture = CODY::JellyData::pixels_for(skin.id);
    uint32_t visible_index = 0;
    while (visible_index < CODY::JellyData::width * CODY::JellyData::height && texture[visible_index] == 0) {
        ++visible_index;
    }
    require(visible_index < CODY::JellyData::width * CODY::JellyData::height,
            "fault fixture has real visible texture data");
    if (visible_index == CODY::JellyData::width * CODY::JellyData::height) {
        return;
    }
    CODY::JellyFrame frame;
    frame.valid = true;
    frame.skin_id = static_cast<uint16_t>(skin.id);
    frame.bounds = {10, 10, 12, 11};
    frame.rows[10].left = 10;
    frame.rows[10].right = 12;
    frame.rows[10].source_x = static_cast<int32_t>(visible_index % CODY::JellyData::width) * 65536;
    frame.rows[10].source_y = static_cast<int32_t>(visible_index / CODY::JellyData::width) * 65536;
    frame.rows[10].source_step = 65536;
    std::array<uint16_t, 4> pixels{0x5a5a, 0x001f, 0x001f, 0x5a5a};
    const auto unchanged = pixels;
    const CODY2D::Surface tile{pixels.data() + 1, 2, 1, 2, 2};
    require(CODY::select_jelly_raster_kernel(CODY::JellyRasterKernel::SimdVerify) == ESP_OK,
            "select explicit compare mode");
    CODY::corrupt_next_simd_model_output = true;
    require(CODY::draw_jelly_rows(frame, tile, 10, 10) == ESP_ERR_INVALID_RESPONSE && pixels == unchanged &&
            !CODY::corrupt_next_simd_model_output,
            "first SIMD mismatch returns failure before painting the block");
    require(CODY::select_jelly_raster_kernel(CODY::JellyRasterKernel::Scalar) == ESP_OK,
            "fault fixture restores scalar mode");
#else
    require(CODY::select_jelly_raster_kernel(CODY::JellyRasterKernel::Simd) == ESP_ERR_NOT_SUPPORTED &&
            CODY::selected_jelly_raster_kernel() == CODY::JellyRasterKernel::Scalar,
            "missing SIMD kernel is rejected instead of silently substituted");
#endif
}

// skin: Authored identity selecting one real immutable packed texture.
void check_jelly_texture_samples(const CODY::JellySkinDefinition &skin)
{
    const uint32_t *texture = CODY::JellyData::pixels_for(skin.id);
    require(texture != nullptr, "every authored look has a real flash texture");
    if (texture == nullptr) {
        return;
    }
    std::array<uint32_t, 3> examples{};
    bool found_transparent = false;
    bool found_opaque = false;
    bool found_edge = false;
    bool valid_premultiplication = true;
    for (uint32_t index = 0; index < CODY::JellyData::width * CODY::JellyData::height; ++index) {
        const uint32_t pixel = texture[index];
        const uint32_t alpha = pixel >> 24U;
        valid_premultiplication = valid_premultiplication &&
            (pixel & 255U) <= alpha && ((pixel >> 8U) & 255U) <= alpha &&
            ((pixel >> 16U) & 255U) <= alpha;
        if (alpha == 0 && !found_transparent) {
            examples[0] = index;
            found_transparent = true;
        }
        if (alpha == 255 && !found_opaque) {
            examples[1] = index;
            found_opaque = true;
        }
        if (alpha >= 40 && alpha <= 215 && !found_edge) {
            examples[2] = index;
            found_edge = true;
        }
    }
    require(valid_premultiplication, "jelly texture is premultiplied RGBA, without a black matte");
    require(found_transparent && found_opaque && found_edge, "jelly contains transparent, opaque and AA edge samples");
    for (uint32_t index : examples) {
        CODY::JellyFrame frame;
        frame.valid = true;
        frame.skin_id = static_cast<uint16_t>(skin.id);
        frame.bounds = {10, 10, 11, 11};
        auto &row = frame.rows[10];
        row.left = 10;
        row.right = 11;
        row.source_x = static_cast<int32_t>(index % CODY::JellyData::width) * 65536;
        row.source_y = static_cast<int32_t>(index / CODY::JellyData::width) * 65536;
        row.source_step = 65536;
        constexpr uint16_t guard = 0x5a5a;
        std::array<uint16_t, 3> pixels{0x001f, guard, guard};
        const CODY2D::Surface tile{pixels.data(), 1, 1, 3, 3};
        require(CODY::draw_jelly_rows(frame, tile, 10, 10) == ESP_OK, "jelly alpha sample renders in a compact tile");
        const uint32_t source = texture[index];
        const double alpha = static_cast<double>(source >> 24U);
        const auto red = std::lround((source & 255U) * 31.0 / 255.0);
        const auto green = std::lround(((source >> 8U) & 255U) * 63.0 / 255.0);
        const auto blue = std::lround((((source >> 16U) & 255U) + 255.0 - alpha) * 31.0 / 255.0);
        const uint16_t expected = static_cast<uint16_t>((red << 11U) | (green << 5U) | blue);
        require(pixels[0] == expected, "jelly alpha composites correctly over native blue");
        require(pixels[1] == guard && pixels[2] == guard, "jelly preserves compact-tile stride guards");
        frame.valid = false;
        const auto unchanged = pixels;
        require(CODY::draw_jelly_rows(frame, tile, 10, 10) == ESP_ERR_INVALID_ARG,
                "jelly refuses an invalid prepared mapping");
        require(pixels == unchanged, "invalid jelly draw does not modify the destination");
    }
    if (found_opaque) {
        check_jelly_transparent_advance(skin, examples[1]);
        check_jelly_profile_parity(skin, examples[1], CODY::JellyRasterKernel::Scalar);
        if (CODY::jelly_simd_available()) {
            check_jelly_profile_parity(skin, examples[1], CODY::JellyRasterKernel::Simd);
            check_jelly_profile_parity(skin, examples[1], CODY::JellyRasterKernel::SimdVerify);
        }
    }
}

// No parameters: all authored textures retain alpha, tile guards and bounded geometry.
void check_jelly_texture_contract()
{
    for (uint16_t row = 0; row < CODY::JellyData::height; ++row) {
        const auto coverage = CODY::JellyData::row_coverage[row];
        bool within_bounds = coverage.first <= coverage.last && coverage.last <= CODY::JellyData::width;
        bool first_occupied = false;
        bool last_occupied = false;
        for (const auto &skin : CODY::JellySkinCatalog::definitions) {
            const uint32_t *pixels = CODY::JellyData::pixels_for(skin.id);
            for (uint16_t column = 0; column < CODY::JellyData::width; ++column) {
                const bool occupied = pixels[row * CODY::JellyData::width + column] != 0;
                within_bounds &= !occupied || (column >= coverage.first && column < coverage.last);
                first_occupied |= occupied && column == coverage.first;
                last_occupied |= occupied && column + 1 == coverage.last;
            }
        }
        require(within_bounds && (coverage.first == CODY::JellyData::width || first_occupied) &&
                (coverage.last == 0 || last_occupied),
                "generated row coverage bounds every skin without excluding texture data");
    }
    for (const auto &skin : CODY::JellySkinCatalog::definitions) {
        check_jelly_texture_samples(skin);
    }
    CODY::RigScene scene;
    require(CODY::evaluate_rig(CODY::Data::defaults, CODY::Data::placement, scene) == ESP_OK,
            "jelly controls evaluate with the original face rig");
    scene.world[CODY::to_index(CODY::PointId::LobeCrown)].y = NAN;
    require(CODY::prepare_jelly_frame(scene, scene.jelly) == ESP_ERR_INVALID_ARG && !scene.jelly.valid,
            "invalid lobe motion invalidates the complete jelly mapping");
    auto contour = CODY::make_contour_state();
    contour.controls[CODY::ContourControl::Amplitude] = 1.f;
    for (float fullness : {-1.f, 0.f, 1.f}) {
        contour.controls[CODY::ContourControl::Fullness] = fullness;
        for (uint16_t phase = 0; phase < 24; ++phase) {
            contour.phase = phase * 6.28318530718f / 24.f;
            require(CODY::evaluate_rig(CODY::Data::defaults, CODY::Data::placement, scene, &contour) == ESP_OK,
                    "maximum authored lobe sweep preserves ordered jelly scanlines");
        }
    }
}
// No parameters: capsule geometry, captions and compact tiles share logical bounds.
void check_capsule_contract()
{
    constexpr uint16_t width = 20;
    constexpr uint16_t height = 12;
    constexpr uint16_t guard = 0x35ad;
    CODY2D::CapsuleControl control{{4, 2, 12, 6}, {}, 0xf800, 0x001f, true};
    std::array<uint16_t, width * height> pixels{};
    const CODY2D::Surface full{pixels.data(), width, height, width,
        static_cast<uint32_t>(pixels.size())};
    require(CODY2D::draw_capsule_rows(control, full, 0, 0) == ESP_OK,
            "capsule renders without a caption");
    require(pixels[2 * width + 10] == 0xf800 && pixels[7 * width + 10] == 0x001f,
            "capsule uses the exact top and bottom gradient anchors");
    require(pixels[2 * width + 4] == 0 && pixels[4 * width + 10] != 0,
            "capsule rounds its corners and fills its middle");
    require(CODY2D::capsule_contains(control, {10, 4}) &&
            !CODY2D::capsule_contains(control, {4, 2}) &&
            !CODY2D::capsule_contains(control, {16, 4}),
            "capsule touch uses the same rounded half-open target");
    for (uint16_t row = 0; row < height; ++row) {
        for (uint16_t column = 0; column < width; ++column) {
            require((pixels[row * width + column] != 0) == CODY2D::capsule_contains(control,
                {static_cast<int16_t>(column), static_cast<int16_t>(row)}),
                "every painted capsule pixel belongs to its touch target");
        }
    }
    for (const auto order : {CODY2D::Rgb565ByteOrder::Native, CODY2D::Rgb565ByteOrder::ByteSwapped}) {
        std::array<uint16_t, 7 * 4> tile_pixels;
        tile_pixels.fill(guard);
        const CODY2D::Surface tile{tile_pixels.data(), 5, 4, 7,
            static_cast<uint32_t>(tile_pixels.size()), order};
        require(CODY2D::draw_capsule_rows(control, tile, 3, 8) == ESP_OK,
                "capsule clips to a compact tile in either storage order");
        for (uint16_t row = 0; row < 4; ++row) {
            for (uint16_t column = 0; column < 5; ++column) {
                require(tile_pixels[row * 7 + column] == CODY2D::rgb565_native_to_stored(
                    pixels[(row + 3) * width + column + 8], order),
                    "compact capsule matches the corresponding full-canvas pixels");
            }
            require(tile_pixels[row * 7 + 5] == guard && tile_pixels[row * 7 + 6] == guard,
                    "capsule preserves compact-tile stride guards");
        }
    }
    control.enabled = false;
    require(!CODY2D::capsule_contains(control, {10, 4}), "disabled capsule cannot admit an action");
    pixels.fill(0);
    require(CODY2D::draw_capsule_rows(control, full, 0, 0) == ESP_OK &&
            pixels[2 * width + 10] == CODY2D::fade_rgb565_to_black(0xf800, 128),
            "disabled capsule retains its shape with an explicit dim palette");
    constexpr uint8_t coverage[] = {0xc0};
    const CODY2D::MonoFont font{coverage, sizeof(coverage), 'A', 1, 1, 1, 1, 1};
    require(CODY2D::prepare_text_line(font, {"A", 1}, {10, 4}, 0xffff, control.caption) == ESP_OK,
            "capsule accepts a prepared bounded ASCII caption");
    require(CODY2D::draw_capsule_rows(control, full, 0, 0) == ESP_OK &&
            pixels[4 * width + 10] == 0xffff, "caption composites above the capsule fill");
    const auto unchanged = pixels;
    control.bounds.width = 5;
    require(CODY2D::draw_capsule_rows(control, full, 0, 0) == ESP_ERR_INVALID_ARG && pixels == unchanged,
            "invalid capsule rejects before modifying the destination");
}

// No parameters: hidden targets cannot activate; fade timing keeps running while paused.
void check_navigation_visibility_contract()
{
    require(CODY::rig_control_at({40, 233}) == CODY::RigControl::None,
            "Back remains defined but hidden and non-interactive by default");
    require(CODY::rig_control_at({160, 52}) == CODY::RigControl::None,
            "Pause remains authored but hidden and non-interactive");
    require(CODY::rig_control_at({233, 47}) == CODY::RigControl::None,
            "Screenshot is authored but hidden and non-interactive");
    require(CODY::rig_control_at({306, 52}) == CODY::RigControl::None,
            "Skeleton remains authored but hidden and non-interactive");
    require(CODY::rig_control_at({426, 233}) == CODY::RigControl::None,
            "old lateral skeleton target cannot activate");
    require(CODY::rig_control_at({158, 74}) == CODY::RigControl::None &&
            CODY::rig_control_at({308, 74}) == CODY::RigControl::None,
            "rounded Debug targets do not intrude into the safe square");
    require(CODY2D::blend_rgb565(0xAD55, 0xFFFF, 0) == 0xFFFF &&
            CODY2D::blend_rgb565(0xAD55, 0xFFFF, 255) == 0xAD55,
            "Debug icon fade respects the white scene background and opaque endpoint");
    CODY::NavigationVisibility visibility;
    require(visibility.opacity(1000000) == 0, "unprimed toolbar is hidden");
    visibility.interact(1000000);
    require(visibility.opacity(1165000) == 128 && visibility.opacity(1330000) == 255,
            "toolbar fades in over exactly 330 milliseconds");
    visibility.update(5999999);
    require(visibility.opacity(5999999) == 255, "toolbar remains until five seconds without touch");
    visibility.update(6165000);
    require(visibility.opacity(6165000) == 128 && visibility.opacity(6330000) == 0,
            "fade-out starts at the five-second deadline even across a delayed frame");
    visibility.interact(6330000);
    require(visibility.opacity(6660000) == 255, "new touch reveals without resetting the character");
    visibility.update(11495000);
    const uint8_t partially_hidden = visibility.opacity(11495000);
    visibility.interact(11495000);
    require(visibility.opacity(11495000) == partially_hidden && visibility.opacity(11825000) == 255,
            "touch during fade-out reverses from the existing opacity without a flash");
}
// No parameters: execute actual look persistence against explicit host fault injection.
void check_jelly_settings_contract()
{
    const auto original_entry = std::find_if(std::begin(CODY::JellySkinCatalog::definitions),
        std::end(CODY::JellySkinCatalog::definitions), [](const auto &skin) {
            return skin.id == CODY::JellySkinId::PearlMist;
        });
    const auto requested_entry = std::find_if(std::begin(CODY::JellySkinCatalog::definitions),
        std::end(CODY::JellySkinCatalog::definitions), [](const auto &skin) {
            return skin.id == CODY::JellySkinId::AuroraVeil;
        });
    require(original_entry != std::end(CODY::JellySkinCatalog::definitions) &&
            requested_entry != std::end(CODY::JellySkinCatalog::definitions),
            "the fixture's typed authored identities exist");
    if (original_entry == std::end(CODY::JellySkinCatalog::definitions) ||
        requested_entry == std::end(CODY::JellySkinCatalog::definitions)) {
        return;
    }
    const auto &original = *original_entry;
    const auto &requested = *requested_entry;
    require(CODY::select_jelly_skin(original.key) == ESP_OK, "fixture selects its initial look");
    HostSettings::reset();
    auto &storage = HostSettings::state();
    require(CODY::load_persisted_jelly_skin() == ESP_OK && storage.write_attempts == 1,
            "only an absent look key persists the first-boot default");

    storage.read_error = ESP_ERR_INVALID_RESPONSE;
    storage.write_attempts = 0;
    require(CODY::load_persisted_jelly_skin() == ESP_ERR_INVALID_RESPONSE &&
            storage.write_attempts == 0, "a failed settings read is not repaired or treated as missing");
    storage.read_error = ESP_OK;
    storage.saved_value = "unknown.look.id";
    require(CODY::load_persisted_jelly_skin() == ESP_ERR_INVALID_STATE &&
            storage.write_attempts == 0, "an invalid saved identity is never overwritten");

    storage.write_error = ESP_FAIL;
    require(CODY::persist_jelly_skin(requested.key) == ESP_FAIL,
            "failed look persistence returns the exact storage error");
    CODY::JellySkinSelection selection;
    require(CODY::selected_jelly_skin(selection) == ESP_OK &&
            std::string(selection.id) == original.key, "a failed save keeps the displayed look");
    storage.write_error = ESP_OK;
    require(CODY::persist_jelly_skin(requested.key) == ESP_OK &&
            storage.saved_value == requested.key, "retry persists the requested stable identity");
    require(CODY::selected_jelly_skin(selection) == ESP_OK &&
            std::string(selection.id) == original.key,
            "successful persistence alone cannot change renderer selection");
    require(CODY::select_jelly_skin(requested.key) == ESP_OK,
            "the display owner explicitly activates a confirmed save");
    require(CODY::selected_jelly_skin(selection) == ESP_OK &&
            std::string(selection.id) == requested.key, "confirmed activation selects the requested look");

    const unsigned writes_before_rejection = storage.write_attempts;
    require(CODY::persist_jelly_skin(nullptr) == ESP_ERR_INVALID_ARG &&
            CODY::persist_jelly_skin("unknown.look.id") == ESP_ERR_NOT_FOUND &&
            storage.write_attempts == writes_before_rejection,
            "invalid requests never reach the settings owner");
    require(CODY::select_jelly_skin(original.key) == ESP_OK, "restore the initial look for later contracts");
    HostSettings::reset();
}

// No parameters: exercise the same allocation-free hold detector used by PWR.
void check_button_hold_contract()
{
    LUNASHELL::ButtonHold hold;
    require(!hold.update(true, 1000000, true, 3999999, 3000000), "PWR cannot reset before 3 seconds");
    require(hold.update(true, 1000000, true, 4000000, 3000000), "PWR resets after a continuous 3-second hold");
    require(!hold.update(true, 1000000, true, 5000000, 3000000), "a held key fires only once");
    require(!hold.update(false, 0, true, 5000000, 3000000), "release rearms without firing");
    require(!hold.update(true, 5100000, true, 6000000, 3000000), "short second hold does not accumulate time");
    require(!hold.update(false, 0, true, 6000001, 3000000), "early release cancels reset");
    require(!hold.update(true, 0, true, 10000000, 3000000), "key held before acquisition cannot cause a reset loop");
    require(!hold.update(true, 7000000, false, 10000000, 3000000), "failed or stale sample cannot reset");
    require(!hold.update(true, 7000000, true, 10000001, 3000000), "recovery requires a new physical release");
    require(!hold.update(false, 0, true, 10000002, 3000000), "fresh release clears fault blocking");
    require(!hold.update(true, 20000000, true, 10000003, 3000000), "future press timestamp cannot reset");
    require(hold.update(true, 11000000, true, 14000000, 3000000), "new valid hold rearms after a release");
}

// No parameters: boot animation retains exact durations without inventing readiness.
void check_lifecycle_contract()
{
    using CODY::BootCheckpoint;
    using CODY::LifecyclePhase;
    CODY::LifecycleState state{};
    require(CODY::lifecycle_opacity(state) == 0, "sleeping character starts black");
    require(CODY::advance_lifecycle(state, 1000000, BootCheckpoint::Ready, state) == ESP_OK &&
            CODY::lifecycle_opacity(state) == 128, "sleeping fade midpoint at one second");
    require(CODY::advance_lifecycle(state, 1000000, BootCheckpoint::Sleeping, state) == ESP_OK &&
            state.phase == LifecyclePhase::BootSleeping && CODY::lifecycle_opacity(state) == 255,
            "sleeping fade finishes at two seconds but waits for local services");
    require(CODY::advance_lifecycle(state, 5000000, BootCheckpoint::Sleeping, state) == ESP_OK &&
            state.phase_elapsed_us == CODY::kBootSleepingUs, "startup waiting time does not skip later animation");
    require(CODY::advance_lifecycle(state, 0, BootCheckpoint::Waking, state) == ESP_OK &&
            state.phase == LifecyclePhase::BootWaking && state.phase_elapsed_us == 0, "local checkpoint releases waking phase");
    require(CODY::advance_lifecycle(state, 4000000, BootCheckpoint::Waking, state) == ESP_OK &&
            state.phase == LifecyclePhase::BootWaking && state.phase_elapsed_us == CODY::kBootWakingUs,
            "waking lasts four seconds and cannot imply complete startup");
    require(CODY::advance_lifecycle(state, 0, BootCheckpoint::Ready, state) == ESP_OK &&
            state.phase == LifecyclePhase::BootReady && state.phase_elapsed_us == 0, "ready starts after actual startup succeeds");
    require(CODY::advance_lifecycle(state, 3999999, BootCheckpoint::Ready, state) == ESP_OK &&
            state.phase == LifecyclePhase::BootReady, "ready remains visible for four seconds");
    require(CODY::advance_lifecycle(state, 1, BootCheckpoint::Ready, state) == ESP_OK &&
            state.phase == LifecyclePhase::Idle, "after ready first setup may enter Hello; otherwise settle at Idle16");
    state = {};
    require(CODY::advance_lifecycle(state, 10000000, BootCheckpoint::Ready, state) == ESP_OK &&
            state.phase == LifecyclePhase::Idle, "one large delta preserves the complete 2/4/4 timeline");
    state = {LifecyclePhase::BootSleeping, 0, CODY::StartupPresentation::PanelFade};
    require(CODY::lifecycle_opacity(state) == 255,
            "software wake does not stack a character fade over the panel fade");
    require(CODY::advance_lifecycle(state, 799999, BootCheckpoint::Ready, state) == ESP_OK &&
            state.phase == LifecyclePhase::BootSleeping, "software wake keeps Sleeping during the 800-ms panel fade");
    require(CODY::advance_lifecycle(state, 1, BootCheckpoint::Ready, state) == ESP_OK &&
            state.phase == LifecyclePhase::BootWaking, "waking begins after the shared panel fade");
    CODY::Channels face{};
    CODY::ContourControls contour{};
    require(CODY::lifecycle_targets({}, face, contour) == ESP_OK, "sleeping atlas expression resolves");
    require(face[CODY::FaceChannel::EyeLeft] == CODY::Data::sleep[CODY::FaceChannel::EyeLeft],
            "screen 01 begins with sleeping eyes, not Calm");
    const CODY::AtlasScreenDefinition *hello = nullptr;
    require(CODY::find_atlas_screen(CODY::AtlasScreenId::Greeting04, hello) == ESP_OK &&
            hello->expression == CODY::ExpressionId::Happy, "OOBE Hello selects the happy face");
    require(CODY::advance_lifecycle(state, -1, BootCheckpoint::Ready, state) == ESP_ERR_INVALID_ARG,
            "invalid lifecycle time fails fast");
}

// No parameters: exercise the real scene clocks without running a display or audio driver.
void check_scene_typewriter_contract()
{
    const CODY::SceneDefinition *definition = nullptr;
    CODY::SceneFrame scene;
    require(CODY::find_scene(CODY::SceneId::PairInvitation, definition) == ESP_OK &&
            CODY::begin_scene(*definition, scene) == ESP_OK, "prepare authored pairing invitation");
    require(CODY::advance_scene(scene, 1299999) == ESP_OK && scene.visible_characters == 0 &&
            CODY::scene_content_characters(scene, 7, scene.title.text.length) == 0,
            "entrance and 300-ms hold precede every typed character");
    require(CODY::advance_scene(scene, 1) == ESP_OK && scene.text_elapsed_us == 0,
            "exact text delay boundary contributes no typing time");
    require(CODY::advance_scene(scene, 45000) == ESP_OK && scene.visible_characters == 1,
            "first invitation glyph appears after its 45-ms cadence");
    const int64_t remaining_title_us = (scene.title.text.length - 1U) * 45000LL;
    require(CODY::advance_scene(scene, remaining_title_us) == ESP_OK &&
            scene.visible_characters == scene.title.text.length &&
            CODY::scene_content_characters(scene, 7, scene.title.text.length) == 0,
            "caption sequence starts after the complete title");
    require(CODY::advance_scene(scene, 45000) == ESP_OK &&
            CODY::scene_content_characters(scene, 7, scene.title.text.length) == 1,
            "body and footer use the same sequential typing clock");

    require(CODY::find_scene(CODY::SceneId::InstallApp, definition) == ESP_OK &&
            !definition->title_visible && definition->cloud.scale == 58 &&
            definition->standby_override_seconds == 60 &&
            CODY::begin_scene(*definition, scene) == ESP_OK,
            "installation scene enlarges Cody without a visible installation heading");
    require(CODY::advance_scene(scene, 45000) == ESP_OK &&
            CODY::scene_content_characters(scene, 9, 0) == 1,
            "a hidden title does not delay Pair Cody's typed caption");

    require(CODY::find_scene(CODY::SceneId::Pairing, definition) == ESP_OK &&
            CODY::begin_scene(*definition, scene) == ESP_OK, "prepare screen 07's reusable text layout");
    const int64_t long_running_us = static_cast<int64_t>(UINT32_MAX) + 1000000LL;
    require(CODY::advance_scene(scene, long_running_us) == ESP_OK &&
            scene.text_elapsed_us > UINT32_MAX, "typing clocks remain usable beyond 71 minutes");
    const uint32_t entrance_before = scene.entrance_elapsed_us;
    static constexpr char next_title[] = "Scan to pair";
    require(CODY::set_scene_title(scene, {next_title, sizeof(next_title) - 1U}) == ESP_OK &&
            scene.visible_characters == 0 && scene.text_elapsed_us == 0 &&
            scene.entrance_elapsed_us == entrance_before,
            "a new status restarts text without replaying Cody's entrance");
    require(CODY::advance_scene(scene, 45000) == ESP_OK && scene.visible_characters == 1 &&
            CODY::scene_content_characters(scene, 6, scene.title.text.length) == 0,
            "a replaced status cannot reveal its later caption instantly");
    scene.content.visible_characters = 3;
    scene.content.text_generation = 17;
    const CODY::SceneFrame retired = scene;
    scene.content.visible_characters = 5;
    scene.content.text_generation = 18;
    require(retired.content.visible_characters == 3 && retired.content.text_generation == 17,
            "retired audio baselines own their counts, not mutable content pointers");
}
}  // namespace

// No parameters: host-only portable contracts never touch a driver or serial port.
int main()
{
    try {
        check_fill_contract();
        check_coverage_rectangle_contract();
        check_coverage_concave_tiles_contract();
        check_rig_contract();
        check_compact_raster_contract();
        check_dirty_plan_contract();
        check_jelly_kernel_contract();
        check_jelly_texture_contract();
        check_jelly_settings_contract();
        check_capsule_contract();
        check_navigation_visibility_contract();
        check_button_hold_contract();
        check_lifecycle_contract();
        check_scene_typewriter_contract();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Native contract failed: %s\n", error.what());
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
