#include "CODY2D_paths.hpp"
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

// scene: One immutable evaluated frame, reused by all destination partitions.
// strip_rows: Partition size, including sizes that leave an incomplete final strip.
// debug: Whether bone/point overlays and navigation are included.
// Exceptions: std::bad_alloc if host framebuffer allocation fails.
std::vector<uint16_t> render(const CODY::RigScene &scene, uint16_t strip_rows, bool debug)
{
    constexpr uint16_t size = 466;
    std::vector<uint16_t> pixels(size * size, 0x35ad);
    for (uint16_t first = 0; first < size; first += strip_rows) {
        const uint16_t rows = std::min<uint16_t>(strip_rows, size - first);
        const CODY2D::Surface strip{pixels.data() + first * size, size, rows, size,
                                  static_cast<uint32_t>(size * rows)};
        require(CODY::draw_rig_rows(scene, strip, first, debug, CODY::kNoPoint) == ESP_OK,
                "native rig strip succeeds");
    }
    return pixels;
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

// Host-only portable contracts; this executable never touches a driver or serial port.
int main()
{
    try {
        check_fill_contract();
        check_rig_contract();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Native contract failed: %s\n", error.what());
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
