#include "CODY_screensaver.hpp"

#include <algorithm>
#include <limits>
#include "LUNASHELL_device_snapshot.hpp"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr int16_t kOrigin = 73;
constexpr uint16_t kExtent = 320;
constexpr int kSubpixel = 16;
constexpr unsigned kScale = 5;
static_assert(kExtent == (ScreensaverPresentation::kGrid - 1U) * kScale,
    "The display extent must exactly repeat the grid cells");
constexpr std::array<int8_t, 32> kWave{
    0, 25, 49, 71, 90, 106, 117, 125, 127, 125, 117, 106, 90, 71, 49, 25,
    0, -25, -49, -71, -90, -106, -117, -125, -127, -125, -117, -106, -90, -71, -49, -25};

// No parameters: immutable smooth radial opacity, replacing any per-frame blur pass.
constexpr std::array<uint8_t, 256> make_falloff()
{
    std::array<uint8_t, 256> values{};
    for (uint32_t index = 0; index < values.size(); ++index) {
        const uint32_t remaining = 255U - index;
        values[index] = static_cast<uint8_t>((remaining * remaining + 127U) / 255U);
    }
    return values;
}
constexpr auto kFalloff = make_falloff();

// phase: Orbital phase in twenty-fifths of an 8-bit step, wrapping every cycle.
int wave(uint32_t phase)
{
    phase %= 256U * 25U;
    const auto index = phase / 200U;
    const int fraction = static_cast<int>(phase % 200U);
    return (kWave[index] * (200 - fraction) + kWave[(index + 1U) & 31U] * fraction) / 200;
}

// frame: Monotonic animation frame since the saver became visible.
unsigned glow_percent(uint32_t frame)
{
    constexpr uint32_t rest_frames = 2000000U / kScreensaverFrameUs;
    constexpr uint32_t pulse_frames = 6000000U / kScreensaverFrameUs;
    const uint32_t cycle_frame = frame % (rest_frames + pulse_frames);
    if (cycle_frame < rest_frames) return 50U;

    // Squaring the positive half-wave eases into and out of the pause at minimum intensity.
    const int amplitude = wave((cycle_frame - rest_frames) * 3200U / pulse_frames);
    return 50U + static_cast<unsigned>((50 * amplitude * amplitude + 127 * 127 / 2) / (127 * 127));
}

struct Lobe { int x; int y; int radius; uint8_t red; uint8_t green; uint8_t blue; };

// frame: Bounded animation phase; all operations are integer and all storage is persistent.
// pixels: Receives the small RGB565 field, later enlarged by repeating its cells.
void paint_grid(uint32_t frame, std::array<uint16_t, ScreensaverPresentation::kGrid * ScreensaverPresentation::kGrid> &pixels)
{
    const uint32_t phase = (frame % 800U) * 8U;
    const int drift_x = wave(phase) * (2 * kSubpixel) / 127;
    const int drift_y = wave(phase + 64U * 25U) * (2 * kSubpixel) / 127;
    const uint32_t orbit = phase * 2U;
    const unsigned glow = glow_percent(frame);
    const Lobe lobes[] = {
        {32 * kSubpixel + drift_x + wave(orbit) * (5 * kSubpixel) / 127,
         32 * kSubpixel + drift_y + wave(orbit + 64U * 25U) * (5 * kSubpixel) / 127, 25 * kSubpixel, 35, 145, 220},
        {32 * kSubpixel + drift_x + wave(orbit + 85U * 25U) * (5 * kSubpixel) / 127,
         32 * kSubpixel + drift_y + wave(orbit + 149U * 25U) * (5 * kSubpixel) / 127, 24 * kSubpixel, 160, 45, 180},
        {32 * kSubpixel + drift_x + wave(orbit + 170U * 25U) * (5 * kSubpixel) / 127,
         32 * kSubpixel + drift_y + wave(orbit + 234U * 25U) * (5 * kSubpixel) / 127, 23 * kSubpixel, 30, 155, 120},
    };
    for (int y = 0; y < ScreensaverPresentation::kGrid; ++y) {
        for (int x = 0; x < ScreensaverPresentation::kGrid; ++x) {
            uint32_t red = 0, green = 0, blue = 0;
            for (const auto &lobe : lobes) {
                const int dx = x * kSubpixel - lobe.x;
                const int dy = y * kSubpixel - lobe.y;
                const int radius_squared = lobe.radius * lobe.radius;
                const int distance = dx * dx + dy * dy;
                if (distance >= radius_squared) continue;
                const uint32_t alpha = kFalloff[static_cast<size_t>(distance * 255 / radius_squared)];
                red += lobe.red * alpha;
                green += lobe.green * alpha;
                blue += lobe.blue * alpha;
            }
            red = (std::min<uint32_t>(255U, red / 255U) * glow + 50U) / 100U;
            green = (std::min<uint32_t>(255U, green / 255U) * glow + 50U) / 100U;
            blue = (std::min<uint32_t>(255U, blue / 255U) * glow + 50U) / 100U;
            pixels[static_cast<size_t>(y) * ScreensaverPresentation::kGrid + x] =
                static_cast<uint16_t>(((red >> 3U) << 11U) | ((green >> 2U) << 5U) | (blue >> 3U));
        }
    }
}

// state: Immutable persistent grid borrowed through frame retirement.
// strip: Driver-owned RGB565 tile with its actual stride and byte order.
// first_row: Logical top coordinate of this tile.
// first_column: Logical left coordinate of this tile.
// Exception: Returns invalid raster storage; the callback never allocates or submits DMA.
esp_err_t draw_rows(const void *state, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    if (state == nullptr || strip.pixels == nullptr || strip.stride < strip.width ||
        strip.height == 0 || static_cast<size_t>(strip.stride) * strip.height > strip.pixel_capacity ||
        !CODY2D::valid_rgb565_byte_order(strip.byte_order)) return ESP_ERR_INVALID_ARG;
    const auto &pixels = static_cast<const ScreensaverPresentation *>(state)->pixels;
    const int top = std::max<int>(first_row, kOrigin);
    const int bottom = std::min<int>(first_row + strip.height, kOrigin + kExtent);
    const int left = std::max<int>(first_column, kOrigin);
    const int right = std::min<int>(first_column + strip.width, kOrigin + kExtent);
    if (top >= bottom || left >= right) return ESP_OK;
    for (int y = top; y < bottom; ++y) {
        const unsigned dy = static_cast<unsigned>(y - kOrigin);
        const size_t source_row = (dy / kScale) * ScreensaverPresentation::kGrid;
        auto *destination = strip.pixels + static_cast<size_t>(y - first_row) * strip.stride + (left - first_column);
        // Reuse the expanded row when a tile contains multiple rows of one grid cell.
        if (y > top && dy % kScale != 0) {
            std::copy_n(destination - strip.stride, right - left, destination);
            continue;
        }
        for (int x = left; x < right;) {
            const unsigned dx = static_cast<unsigned>(x - kOrigin);
            const int run = std::min<int>(right - x, kScale - dx % kScale);
            const auto color = CODY2D::rgb565_native_to_stored(pixels[source_row + dx / kScale], strip.byte_order);
            std::fill_n(destination + (x - left), run, color);
            x += run;
        }
    }
    return ESP_OK;
}
} // namespace

// model: Persistent state only; dismissing never changes the retained dashboard route.
// now_us: Genuine input/lifecycle time from the caller.
void dismiss_screensaver(ScreensaverModel &model, int64_t now_us)
{
    model.phase = ScreensaverPhase::Waiting;
    model.eligible_since_us = now_us;
    model.started_at_us = 0;
}

// model: Device-owned state and optional preference failure.
// eligible: True only for a visible dashboard without a blocking overlay.
// last_interaction_us: Last real interaction, not the last repaint.
// now_us: Monotonic observation time.
// Exception: Logs changed original read errors and disables the optional saver on failure.
void update_screensaver(ScreensaverModel &model, bool eligible, int64_t last_interaction_us, int64_t now_us)
{
    LUNASHELL::CodyOptionsObservation observed{};
    auto error = LUNASHELL::DeviceSnapshotService::instance().readCodyOptionsObservation(observed);
    if (error == ESP_OK) error = observed.preferences.screensaver_error;
    const bool available = error == ESP_OK && observed.preferences.screensaver_valid;
    if (error != model.settings_error && error != ESP_OK) {
        ESP_LOGW("CODY_screensaver", "Screensaver preference unavailable (%s)", esp_err_to_name(error));
    }
    model.settings_error = error;
    if (!available || !observed.preferences.screensaver_enabled || !eligible || now_us < 0) {
        model.phase = available && observed.preferences.screensaver_enabled
            ? ScreensaverPhase::Waiting : ScreensaverPhase::Disabled;
        model.eligible_since_us = -1;
        return;
    }
    if (model.eligible_since_us < 0 || now_us < model.eligible_since_us) model.eligible_since_us = now_us;
    const int64_t idle_since = std::max(model.eligible_since_us, last_interaction_us);
    if (now_us < idle_since || now_us - idle_since < kScreensaverDelayUs) {
        model.phase = ScreensaverPhase::Waiting;
        return;
    }
    if (model.phase != ScreensaverPhase::Visible) {
        model.started_at_us = now_us;
        model.phase = ScreensaverPhase::Visible;
    }
    model.frame = static_cast<uint32_t>((now_us - model.started_at_us) / kScreensaverFrameUs);
}

// model: Immutable animation phase, supplied by the device model.
// presentation: Persistent small grid; unchanged ticks reuse it without raster preparation.
// content: Receives fixed globe bounds, including all previous and next animation positions.
// Exception: Returns invalid state or revision exhaustion, without dynamic allocation.
esp_err_t prepare_screensaver(const ScreensaverModel &model, ScreensaverPresentation &presentation, SceneContent &content)
{
    if (model.phase != ScreensaverPhase::Visible) return ESP_ERR_INVALID_STATE;
    if (!presentation.valid || presentation.frame != model.frame) {
        if (presentation.revision == std::numeric_limits<uint32_t>::max()) return ESP_ERR_INVALID_STATE;
        paint_grid(model.frame, presentation.pixels);
        presentation.frame = model.frame;
        presentation.valid = true;
        ++presentation.revision;
    }
    content = {};
    content.bounds = {kOrigin, kOrigin, kOrigin + kExtent, kOrigin + kExtent};
    content.state = &presentation;
    content.revision = presentation.revision;
    content.draw_rows = draw_rows;
    return ESP_OK;
}
} // namespace CODY
