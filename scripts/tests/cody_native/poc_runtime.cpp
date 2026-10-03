#include "CODY_benchmark.hpp"
#include "CODY_demo.hpp"
#include "CODY_response.hpp"
#include "CODY_damage.hpp"
#include "cloud_rig_data.hpp"
#include "poc_settings.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <vector>
#include <algorithm>
#include "CODY2D_paths.hpp"

namespace {
// condition: Contract that must hold for the actual portable firmware implementation.
// message: Concrete failure retained in the host check output.
void require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "Native PoC failure: %s\n", message);
        std::exit(1);
    }
}

// stamp: Source metadata in a host fixture; nothing here publishes to firmware snapshots.
// now_us: Successful sample time shared by this deterministic fixture bundle.
void current(CODY::SampleStamp &stamp, int64_t now_us)
{
    stamp.health = CODY::SampleHealth::Current;
    stamp.error = ESP_OK;
    stamp.sampled_at_us = now_us;
}

// Exercise real bounded statistics, rollover, overlay separation and error boundaries.
void benchmark_contracts()
{
    CODY::FrameBenchmark collector;
    CODY::FrameDistribution output{};
    require(collector.summarize(CODY::OverlayMode::Artwork, 1000, output) == ESP_ERR_NOT_FOUND,
            "empty statistics must remain unavailable");
    for (uint32_t duration : {100U, 200U, 300U, 400U}) {
        require(collector.record(CODY::OverlayMode::Artwork, {10, 20, 30, duration}) == ESP_OK,
                "valid measured frame rejected");
    }
    require(collector.summarize(CODY::OverlayMode::Artwork, 250, output) == ESP_OK,
            "statistics unavailable");
    require(output.count == 4 && output.mean.total_us == 250 && output.median_us == 200 &&
            output.p95_us == 400 && output.maximum_us == 400 && output.missed_deadlines == 2 &&
            std::abs(output.standard_deviation_us - std::sqrt(12500.0)) < .001,
            "measured distribution differs");
    require(collector.summarize(CODY::OverlayMode::Skeleton, 250, output) == ESP_ERR_NOT_FOUND,
            "overlay windows must remain independent");
    require(collector.record(CODY::OverlayMode::Artwork, {100, 100, 100, 1}) == ESP_ERR_INVALID_ARG,
            "invalid stages accepted");
    for (uint32_t index = 0; index < 129; ++index) {
        require(collector.record(CODY::OverlayMode::Skeleton, {0, 0, 0, 1000 + index}) == ESP_OK,
                "rolling record failed");
    }
    require(collector.summarize(CODY::OverlayMode::Skeleton, 2000, output) == ESP_OK &&
            output.count == 128 && output.maximum_us == 1128 && output.mean.total_us == 1064,
            "rolling window did not discard the oldest frame");
    collector.reset();
    require(collector.summarize(CODY::OverlayMode::Artwork, 250, output) == ESP_ERR_NOT_FOUND,
            "explicit reset must clear retained observations");
}

// surface: Guarded reference destination with ordinary RGB565 pixels.
// first_row: Canvas origin of this reference tile.
// from: Segment start, including deliberately off-screen endpoints.
// to: Segment end; identical endpoints exercise round markers.
// width: Positive stroke diameter shared with the production raster.
// color: Exact RGB565 output value.
void reference_stroke(CODY2D::Surface surface, uint16_t first_row,
                      CODY2D::Vec2 from, CODY2D::Vec2 to, float width, uint16_t color)
{
    // Keep the previous exhaustive algorithm independent of the optimized
    // scanline bounds. Comparing two calls to the new raster would prove little.
    const float radius = width * .5f;
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float squared_length = dx * dx + dy * dy;
    const int left = std::max(0, static_cast<int>(std::floor(std::min(from.x, to.x) - radius)));
    const int right = std::min(static_cast<int>(surface.width) - 1,
        static_cast<int>(std::ceil(std::max(from.x, to.x) + radius)));
    const int top = std::max(static_cast<int>(first_row),
        static_cast<int>(std::floor(std::min(from.y, to.y) - radius)));
    const int bottom = std::min(static_cast<int>(first_row + surface.height) - 1,
        static_cast<int>(std::ceil(std::max(from.y, to.y) + radius)));
    for (int row = top; row <= bottom; ++row) {
        for (int column = left; column <= right; ++column) {
            const float px = column + .5f - from.x;
            const float py = row + .5f - from.y;
            const float position = squared_length == 0 ? 0 :
                std::max(0.f, std::min(1.f, (px * dx + py * dy) / squared_length));
            const float ex = px - position * dx;
            const float ey = py - position * dy;
            if (ex * ex + ey * ey <= radius * radius) {
                surface.pixels[(row - first_row) * surface.stride + column] = color;
            }
        }
    }
}

// Compare exact stroke pixels, round caps, row padding and guards across tiles.
// Exceptions: std::bad_alloc if a host fixture allocation fails.
void stroke_contracts()
{
    constexpr uint16_t size = 466;
    constexpr uint16_t stride = size + 3;
    struct Segment { CODY2D::Vec2 from; CODY2D::Vec2 to; };
    const Segment segments[] = {
        {{12.25f, 20.5f}, {451.75f, 445.25f}},
        {{451.75f, 20.5f}, {12.25f, 445.25f}},
        {{12.25f, 445.25f}, {451.75f, 20.5f}},
        {{451.75f, 445.25f}, {12.25f, 20.5f}},
        {{0.f, 233.f}, {466.f, 233.f}},
        {{233.f, 0.f}, {233.f, 466.f}},
        {{-.25f, -.75f}, {465.5f, 465.5f}},
        {{-2048.f, 210.f}, {2048.f, 212.f}},
        {{210.f, -2048.f}, {212.f, 2048.f}},
        {{-2048.f, -2048.f}, {2048.f, 2048.f}},
        {{1.f, 233.f}, {465.f, 233.0005f}},
        {{1.f, 233.f}, {465.f, 233.0015f}},
        {{232.5f, 233.5f}, {232.5f, 233.5f}},
        {{-5.f, -5.f}, {-2.f, -2.f}},
    };
    for (const Segment &segment : segments) {
        for (float width : {.001f, .5f, 1.f, 2.75f, 7.f, CODY2D::kMaxStrokeWidth}) {
            for (uint16_t rows : {uint16_t{8}, uint16_t{16}, uint16_t{64}}) {
                std::vector<uint16_t> expected(size * stride + 2, 0x35ad);
                auto actual = expected;
                for (uint16_t first = 0; first < size; first += rows) {
                    const uint16_t height = std::min<uint16_t>(rows, size - first);
                    const CODY2D::Surface reference{expected.data() + 1 + first * stride,
                        size, height, stride, static_cast<uint32_t>(height * stride)};
                    const CODY2D::Surface optimized{actual.data() + 1 + first * stride,
                        size, height, stride, static_cast<uint32_t>(height * stride)};
                    reference_stroke(reference, first, segment.from, segment.to, width, 0xffff);
                    require(CODY2D::stroke_rows(optimized, first, segment.from, segment.to,
                        width, 0xffff) == ESP_OK, "optimized stroke rejected valid geometry");
                }
                require(actual == expected, "scanline stroke changed reference pixels or guards");
            }
        }
    }
}

// Check every authored phase boundary using the same generated settings used on the device.
void demo_contracts()
{
    CODY::Channels face{};
    CODY::ContourControls contour{};
    CODY::DemoPhase phase{};
    int64_t elapsed_us = 0;
    for (const auto &step : CODY::PocData::steps) {
        require(CODY::demo_targets(elapsed_us, face, contour, phase) == ESP_OK && phase == step.identity,
                "demo phase identity or boundary differs");
        require(face[CODY::FaceChannel::EyeLeft] == step.face[CODY::FaceChannel::EyeLeft] &&
                contour[CODY::ContourControl::Speed] == step.speed, "demo target differs");
        elapsed_us += step.duration_ms * 1000LL;
    }
    require(CODY::demo_targets(elapsed_us, face, contour, phase) == ESP_OK &&
            phase == CODY::PocData::steps[0].identity, "demo cycle did not wrap");
    require(CODY::demo_targets(-1, face, contour, phase) == ESP_ERR_INVALID_ARG,
            "negative demo time accepted");
}
// Cover cache invalidation, paired columns, overlapping packing and pixel parity.
// Exceptions: std::bad_alloc if a host framebuffer allocation fails.
void damage_contracts()
{
    uint16_t cached_storage[40]{};
    uint16_t tile_storage[20]{};
    const CODY2D::Surface cache{cached_storage, 8, 4, 10, 40};
    const CODY2D::Surface tile{tile_storage, 8, 2, 10, 20};
    CODY::DamageRect damage;
    require(CODY::measure_damage_rows(tile, 1, cache, true, damage) == ESP_OK && damage.empty(),
            "unchanged tile submitted pixels");
    tile_storage[3] = 0x1234;
    tile_storage[17] = 0xabcd;
    require(CODY::measure_damage_rows(tile, 1, cache, true, damage) == ESP_OK &&
            damage.left == 2 && damage.right == 8 && damage.top == 1 && damage.bottom == 3,
            "paired-column damage bounds differ");
    CODY2D::Surface packed;
    require(CODY::prepare_damage_rows(tile, 1, cache, damage, packed) == ESP_OK &&
            packed.width == 6 && packed.height == 2 && packed.stride == 6 &&
            packed.pixels[1] == 0x1234 && packed.pixels[11] == 0xabcd &&
            cached_storage[13] == 0x1234 && cached_storage[27] == 0xabcd &&
            cached_storage[18] == 0 && cached_storage[28] == 0,
            "packing corrupted candidate cache, cropped rows or stride padding");
    require(CODY::measure_damage_rows(tile, 3, cache, true, damage) == ESP_ERR_INVALID_ARG,
            "out-of-canvas tile accepted");
    require(CODY::measure_damage_rows(tile, 1, cache, false, damage) == ESP_OK &&
            damage.left == 0 && damage.right == 8 && damage.top == 1 && damage.bottom == 3,
            "invalid cache did not force the complete tile");

    constexpr uint16_t panel_width = 466;
    constexpr uint16_t panel_height = 466;
    constexpr uint16_t tile_rows = 16;
    constexpr uint32_t frame_pixels = static_cast<uint32_t>(panel_width) * panel_height;
    for (const bool debug : {false, true}) {
        std::vector<uint16_t> previous(frame_pixels);
        std::vector<uint16_t> host_panel(frame_pixels, 0xf81f);
        std::vector<uint16_t> expected(frame_pixels);
        std::vector<uint16_t> rendered_tile(panel_width * tile_rows);
        const CODY2D::Surface previous_frame{previous.data(), panel_width, panel_height, panel_width, frame_pixels};
        const CODY2D::Surface expected_frame{expected.data(), panel_width, panel_height, panel_width, frame_pixels};
        bool cache_valid = false;
        enum class Scenario { Initial, Unchanged, MouthChange, Invalidate };
        for (const auto scenario : {Scenario::Initial, Scenario::Unchanged, Scenario::MouthChange, Scenario::Invalidate}) {
            auto channels = CODY::Data::ready;
            if (scenario == Scenario::MouthChange) {
                channels[CODY::FaceChannel::MouthOpen] = .9f;
            }
            CODY::RigScene scene;
            require(CODY::evaluate_rig(channels, CODY::Data::placement, scene) == ESP_OK &&
                    CODY::draw_rig_rows(scene, expected_frame, 0, debug, CODY::kNoPoint) == ESP_OK,
                    "reference geometry/raster failed");
            uint32_t submitted_pixels = 0;
            const bool compare_previous = cache_valid && scenario != Scenario::Invalidate;
            for (uint16_t row = 0; row < panel_height; row += tile_rows) {
                const uint16_t height = panel_height - row < tile_rows ? panel_height - row : tile_rows;
                const CODY2D::Surface rendered{rendered_tile.data(), panel_width, height, panel_width,
                    static_cast<uint32_t>(rendered_tile.size())};
                require(CODY::draw_rig_rows(scene, rendered, row, debug, CODY::kNoPoint) == ESP_OK &&
                        CODY::measure_damage_rows(rendered, row, previous_frame, compare_previous, damage) == ESP_OK,
                        "rendered tile damage failed");
                if (damage.empty()) {
                    continue;
                }
                require(CODY::prepare_damage_rows(rendered, row, previous_frame, damage, packed) == ESP_OK,
                        "actual rig payload packing failed");
                for (uint16_t packed_row = 0; packed_row < packed.height; ++packed_row) {
                    for (uint16_t column = 0; column < packed.width; ++column) {
                        host_panel[(damage.top + packed_row) * panel_width + damage.left + column] =
                            packed.pixels[packed_row * packed.stride + column];
                    }
                }
                submitted_pixels += static_cast<uint32_t>(packed.width) * packed.height;
            }
            require(host_panel == expected && previous == expected,
                    "partial updates differ from the complete actual rig framebuffer");
            if (scenario == Scenario::Unchanged) {
                require(submitted_pixels == 0, "unchanged frame submitted a rectangle");
            } else if (scenario == Scenario::Initial || scenario == Scenario::Invalidate) {
                require(submitted_pixels == frame_pixels, "invalid cache skipped canvas pixels");
            } else {
                require(submitted_pixels > 0 && submitted_pixels < frame_pixels,
                        "face update did not use actual changed regions");
            }
            cache_valid = true;
        }
    }
}
} // namespace

// Emit a fifty-second fixture from the actual C++ model for independent Python parity.
int main()
{
    benchmark_contracts();
    demo_contracts();
    damage_contracts();
    stroke_contracts();
    require(CODY::validate_response_config(CODY::Data::response) == ESP_OK, "generated response rejected");
    auto state = CODY::make_response_state(CODY::Data::response);
    CODY::RigScene scene;
    require(CODY::evaluate_rig(CODY::Data::ready, {245, 221, 135}, scene) == ESP_OK,
            "shared interaction geometry unavailable");
    CODY::EyeId eye = CODY::EyeId::None;
    require(CODY::hit_test_eye(scene, scene.world[CODY::to_index(CODY::PointId::EyeLeft)],
            CODY::Data::response.touch.eye_hit_radius, eye) == ESP_OK && eye == CODY::EyeId::Left,
            "translated/scaled left eye hit differs");
    require(CODY::hit_test_eye(scene, {20, 20}, CODY::Data::response.touch.eye_hit_radius, eye) == ESP_OK &&
            eye == CODY::EyeId::None, "eye miss became an eye interaction");
    CODY::ResponseInputs rejected{};
    current(rejected.microphone.stamp, 1000000LL);
    rejected.microphone.channel_count = 3;
    auto unchanged = state;
    require(CODY::advance_response(CODY::Data::response, state, rejected, 100, 1000000LL, true, unchanged) == ESP_ERR_INVALID_ARG &&
            unchanged.idle_seconds == state.idle_seconds && unchanged.breath_phase == state.breath_phase,
            "invalid microphone must reject the entire model transaction");
    std::puts("[");
    for (int tick = 0; tick < 500; ++tick) {
        const int64_t now_us = 1000000LL + tick * 100000LL;
        CODY::ResponseInputs inputs{};
        current(inputs.power.stamp, now_us);
        inputs.power.level_percent = tick < 150 ? 100.f : 12.f;
        if (tick >= 400) {
            inputs.power.stamp.health = CODY::SampleHealth::Unavailable;
            inputs.power.stamp.error = ESP_ERR_INVALID_STATE;
        }
        current(inputs.microphone.stamp, now_us);
        inputs.microphone.channel_count = 2;
        const bool heard = tick >= 80 && tick < 100;
        inputs.microphone.mean_level[0] = inputs.microphone.peak_level[0] = heard ? 6000 : 0;
        inputs.microphone.mean_level[1] = inputs.microphone.peak_level[1] = heard ? 3000 : 0;
        current(inputs.playback.stamp, now_us);
        inputs.playback.active = tick >= 110 && tick < 130;
        inputs.playback.mean_level = inputs.playback.active ? 5000 : 0;
        inputs.last_interaction_at_us = tick >= 344 ? 35400000LL : 0;
        if (tick >= 450) {
            current(inputs.eye_tap.stamp, 46000000LL);
            inputs.eye_tap.eye = CODY::EyeId::Left;
            inputs.last_interaction_at_us = 46000000LL;
        }
        require(CODY::advance_response(CODY::Data::response, state, inputs, 100, now_us, true, state) == ESP_OK,
                "response fixture update rejected");
        CODY::ResponseFrame frame{};
        require(CODY::compose_response(CODY::Data::response, state, CODY::Data::ready,
            CODY::contour_defaults(), CODY::Data::placement, 0, true, frame) == ESP_OK,
            "response fixture composition rejected");
        std::printf("%s{\"tick\":%d,\"attention\":%.9g,\"speaking\":%.9g,\"fatigue\":%.9g,"
            "\"idle\":%.9g,\"yawn\":%.9g,\"mouth\":%.9g,\"eye\":%.9g,\"annoyance\":%.9g,\"closure\":%.9g}",
            tick == 0 ? "" : ",\n", tick, state.attention, state.speaking, state.fatigue,
            state.idle_seconds, state.yawn_weight, frame.face[CODY::FaceChannel::MouthOpen],
            frame.face[CODY::FaceChannel::EyeLeft], state.annoyance, state.protective_eye_closure);
    }
    std::puts("\n]");
    return 0;
}
