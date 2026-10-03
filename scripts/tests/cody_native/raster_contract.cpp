#include "CODY2D_paths.hpp"
#include "CODY_dirty_plan.hpp"
#include "CODY_rig.hpp"
#include "cloud_rig_data.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <iterator>
#include <limits>
#include <vector>

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
// paused: Whether the navigation exposes Resume instead of Pause.
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
                "disjoint compact tiles preserve artwork, skeleton, selection and paused navigation");
    }
}

// plan: Existing geometry damage to extend with a changed navigation control.
// control: Named control whose bounds come from the renderer's shared geometry table.
void include_control(CODY::DirtyPlan &plan, CODY::RigControl control)
{
    CODY::DamageRect bounds;
    const esp_err_t result = CODY::rig_control_damage_bounds(control, bounds);
    require(result == ESP_OK, "changed navigation control has valid bounds");
    if (result == ESP_OK) {
        require(CODY::include_damage(plan, bounds) == ESP_OK, "navigation damage is accumulated");
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
    // Navigation has its own owner, matching the firmware integration boundary.
    if (previous.debug != current.debug) {
        include_control(plan, CODY::RigControl::Overlay);
    }
    if (previous.paused != current.paused) {
        include_control(plan, CODY::RigControl::Pause);
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
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Native contract failed: %s\n", error.what());
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
