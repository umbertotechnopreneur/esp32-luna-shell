#include "CODY_personality.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include "CODY_rig.hpp"
#include "cloud_rig_data.hpp"
#include "personality_icons.hpp"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_personality";
constexpr float kLagSeconds = .085f;
constexpr uint16_t kMaximumSpriteExtent = 64;

// cue: Stable symbol identity; integer offsets are used only for flash-table traversal.
const PersonalityAssets::Sprite *find_sprite(PersonalityCue cue)
{
    for (const auto &sprite : PersonalityAssets::sprites) {
        if (sprite.cue == cue) {
            return &sprite;
        }
    }
    return nullptr;
}

// first: One half-open rectangle in logical coordinates.
// second: Another half-open rectangle; touching edges do not overlap.
bool overlaps(DamageRect first, DamageRect second)
{
    return !first.empty() && !second.empty() && first.left < second.right &&
        second.left < first.right && first.top < second.bottom && second.top < first.bottom;
}

// left: Candidate left edge; floating geometry is rejected before integer conversion.
// top: Candidate upper edge, always above the current body crown.
// width: Positive bounded whole-pixel sprite width.
// height: Positive bounded whole-pixel sprite height.
// exclusions: Borrowed visible route rectangles.
// count: Readable rectangle count, bounded by preparation.
// bounds: Receives the admitted rectangle only when every occupied chord fits.
bool admit_bounds(float left, float top, uint16_t width, uint16_t height,
                   const DamageRect *exclusions, uint8_t count, DamageRect &bounds)
{
    const int x = static_cast<int>(std::round(left));
    const int y = static_cast<int>(std::round(top));
    if (x < 0 || y < 0 || x + width > 466 || y + height > 466) {
        return false;
    }
    // Circle chord width decreases monotonically with distance from its center.
    // The farther endpoint row is therefore the narrowest of EVERY occupied row.
    const float farthest_y = std::max(std::abs(y + .5f - 233.f),
        std::abs(y + height - .5f - 233.f));
    const float chord = std::sqrt(233.f * 233.f - farthest_y * farthest_y);
    if (x + .5f < 233.f - chord || x + width - .5f > 233.f + chord) {
        return false;
    }
    const DamageRect candidate{static_cast<uint16_t>(x), static_cast<uint16_t>(y),
        static_cast<uint16_t>(x + width), static_cast<uint16_t>(y + height)};
    for (uint8_t index = 0; index < count; ++index) {
        if (overlaps(candidate, exclusions[index])) {
            return false;
        }
    }
    bounds = candidate;
    return true;
}
} // namespace

// cue: Stable enum whose catalog membership is requested.
bool personality_cue_valid(PersonalityCue cue)
{
    return cue == PersonalityCue::None || find_sprite(cue) != nullptr;
}

// cue: Stable enum to classify without reading any device state.
bool personality_cue_is_device(PersonalityCue cue)
{
    return cue == PersonalityCue::LowBattery || cue == PersonalityCue::Charging ||
        cue == PersonalityCue::NoWifi;
}

// nominal_scale: Unanimated authored rig scale.
// size: Receives a stable named presentation on success only.
// Exception: Returns invalid argument for nonfinite or unsupported rig scale.
esp_err_t cody_size_for_scale(float nominal_scale, CodySize &size)
{
    if (!std::isfinite(nominal_scale) || nominal_scale < 20 || nominal_scale > 200) {
        ESP_LOGE(kTag, "Size scale=%f rejected (%s)", nominal_scale, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (nominal_scale <= 22) {
        size = CodySize::Micro;
        return ESP_OK;
    }
    constexpr struct { CodySize identity; float scale; } presentations[] = {
        {CodySize::Tiny, 30}, {CodySize::Preferences, 42}, {CodySize::Look, 44},
        {CodySize::Pairing, 46}, {CodySize::Small, 58}, {CodySize::Invitation, 65},
        {CodySize::Medium, 105}, {CodySize::Greeting, 130}, {CodySize::Full, 145},
    };
    float distance = 1000;
    for (const auto &presentation : presentations) {
        const float candidate = std::abs(nominal_scale - presentation.scale);
        if (candidate < distance || (candidate == distance && presentation.identity < size)) {
            size = presentation.identity;
            distance = candidate;
        }
    }
    return ESP_OK;
}

// scene: Successfully evaluated crown and neighboring lobe anchors.
// cue: Single admitted narrative or canonical device symbol.
// size: Nominal semantic presentation; never derived from breathing.
// route: Stable route identity, including the ordinary rig route.
// rotation: Shared cardinal display orientation.
// now_us: Current monotonic sample, with no catch-up loops after stale gaps.
// paused: Freeze the lag while the evaluated pose is frozen.
// reset: Wake or invalidated frame ownership requests a fresh prime.
// exclusions: Existing content/navigation footprints.
// exclusion_count: Readable exclusions, bounded to six.
// lag: Fixed caller-owned artistic filter state.
// frame: Receives the one bounded destination sprite footprint.
// Exception: Returns invalid pose, clock, enum or exclusion arguments.
esp_err_t prepare_personality_frame(
    const RigScene &scene, PersonalityCue cue, CodySize size, uint32_t route,
    uint16_t rotation, int64_t now_us, bool paused, bool reset,
    const DamageRect *exclusions, uint8_t exclusion_count,
    PersonalityLag &lag, PersonalityFrame &frame)
{
    frame = {};
    if (!scene.valid || !std::isfinite(scene.scale) || scene.scale <= 0 || !personality_cue_valid(cue) ||
        now_us < 0 || rotation % 90 != 0 || rotation > 270 || exclusion_count > 6 ||
        (exclusion_count != 0 && exclusions == nullptr) ||
        size < CodySize::Micro || size > CodySize::Full) {
        ESP_LOGE(kTag, "Prepare rejected: valid=%u scale=%f cue=%u size=%u clock=%lld rotation=%u exclusions=%u (%s)",
            scene.valid, scene.scale, static_cast<unsigned>(cue), static_cast<unsigned>(size),
            static_cast<long long>(now_us), rotation, exclusion_count, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (cue == PersonalityCue::None || size == CodySize::Micro) {
        lag = {};
        return ESP_OK;
    }
    const auto crown = scene.world[to_index(PointId::LobeCrown)];
    const auto upper_left = scene.world[to_index(PointId::LobeUpperLeft)];
    const auto upper_right = scene.world[to_index(PointId::LobeUpperRight)];
    if (!std::isfinite(crown.x) || !std::isfinite(crown.y) ||
        !std::isfinite(upper_left.x) || !std::isfinite(upper_left.y) ||
        !std::isfinite(upper_right.x) || !std::isfinite(upper_right.y)) {
        ESP_LOGE(kTag, "Nonfinite upper-lobe anchor rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    // The crown drives motion, but clearance belongs to the visible silhouette.
    // Lobe anchors can sit below the highest point of their Bezier curve.
    float body_top = 466.f;
#if CODY_ENABLE_PEARL_JELLY
    if (!scene.jelly.valid || scene.jelly.bounds.empty()) {
        ESP_LOGE(kTag, "Visible jelly body bounds unavailable (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    body_top = scene.jelly.bounds.top;
#else
    const auto &body = scene.paths[to_index(PathId::Body)];
    if (!body.visible || body.count == 0 ||
        static_cast<uint32_t>(body.first_vertex) + body.count > scene.vertices.size()) {
        ESP_LOGE(kTag, "Visible vector body extent rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    for (uint16_t offset = 0; offset < body.count; ++offset) {
        body_top = std::min(body_top, scene.vertices[body.first_vertex + offset].y);
    }
#endif
    const float extent = std::clamp(scene.scale * .42f, 12.f, 58.f);
    // A cheap artistic squash follows the existing lobe span; no second mesh.
    const float rest_span = (Data::points[to_index(PointId::LobeUpperRight)].x -
        Data::points[to_index(PointId::LobeUpperLeft)].x) * scene.scale;
    if (!std::isfinite(rest_span) || rest_span <= 0) {
        ESP_LOGE(kTag, "Upper-lobe rest span=%f rejected (%s)", rest_span, esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const float squash = std::clamp((upper_right.x - upper_left.x) / rest_span, .9f, 1.1f);
    const float target_width = extent * squash;
    const float target_height = extent / squash;
    const float target_y = crown.y - (crown.y - body_top) - 4.f - target_height * .5f;
    if (!std::isfinite(body_top) || (lag.primed &&
        (!std::isfinite(lag.center_x) || !std::isfinite(lag.center_y) ||
         !std::isfinite(lag.width) || !std::isfinite(lag.height) || lag.sampled_at_us < 0))) {
        ESP_LOGE(kTag, "Body extent/filter state rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    PersonalityLag next = lag;
    const int64_t elapsed_us = now_us >= lag.sampled_at_us ? now_us - lag.sampled_at_us : -1;
    const bool prime = reset || !lag.primed || lag.cue != cue || lag.size != size ||
        lag.route != route || lag.rotation != rotation || elapsed_us < 0 || elapsed_us > 250000;
    if (prime) {
        next.center_x = crown.x;
        next.center_y = target_y;
        next.width = target_width;
        next.height = target_height;
    } else if (!paused && elapsed_us > 0) {
        const float blend = -std::expm1(-static_cast<float>(elapsed_us) / (1000000.f * kLagSeconds));
        next.center_x += (crown.x - next.center_x) * blend;
        next.center_y += (target_y - next.center_y) * blend;
        next.width += (target_width - next.width) * blend;
        next.height += (target_height - next.height) * blend;
    }
    next.cue = cue;
    next.size = size;
    next.route = route;
    next.rotation = rotation;
    next.sampled_at_us = now_us;
    next.primed = true;
    uint16_t width = static_cast<uint16_t>(std::clamp(std::round(next.width), 12.f, 64.f));
    uint16_t height = static_cast<uint16_t>(std::clamp(std::round(next.height), 12.f, 64.f));
    float clearance_top = 0;
    // Chrome can occupy the band above a large, upward-deformed body. Scale the
    // cue into the real gap below that band, keeping it available on Full Cody.
    for (uint8_t index = 0; index < exclusion_count; ++index) {
        const auto &excluded = exclusions[index];
        const bool crosses_x = next.center_x - width * .5f < excluded.right &&
            next.center_x + width * .5f > excluded.left;
        if (!excluded.empty() && crosses_x && excluded.bottom < body_top - 4.f) {
            const float available = body_top - 4.f - excluded.bottom;
            if (available >= 12.f) {
                clearance_top = std::max(clearance_top, static_cast<float>(excluded.bottom));
                if (available < height) {
                    width = static_cast<uint16_t>(std::max(12.f, std::floor(width * available / height)));
                    height = static_cast<uint16_t>(std::floor(available));
                }
            }
        }
    }
    // Preserve the filtered bottom anchor when admission reduces the extent.
    // Position into the available gap instead of preserving an obsolete center.
    const float wanted_top = std::max(clearance_top,
        std::min(next.center_y + next.height * .5f - height, body_top - height - 4.f));
    // Search a fixed small set above Cody. Whole artwork bounds, including
    // antialias coverage, must fit every chord and preserve QR quiet zones.
    for (uint8_t step = 0; step < 8 && frame.bounds.empty(); ++step) {
        const float top = wanted_top - step * (height * .5f + 2.f);
        for (float shift : {0.f, -static_cast<float>(width), static_cast<float>(width)}) {
            if (admit_bounds(next.center_x + shift - width * .5f, top,
                             width, height, exclusions, exclusion_count, frame.bounds)) {
                frame.cue = cue;
                break;
            }
        }
    }
    if (frame.bounds.empty()) {
        ESP_LOGE(kTag, "Cue placement unavailable: cue=%u size=%u route=%lu body_top=%f extent=%ux%u (%s)",
            static_cast<unsigned>(cue), static_cast<unsigned>(size), static_cast<unsigned long>(route),
            body_top, width, height, esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    lag = next;
    return ESP_OK;
}

// frame: Prepared immutable sprite identity and logical footprint.
// strip: Borrowed compact RGB565 destination.
// first_row: Logical y origin of this tile.
// first_column: Logical x origin of this tile.
// Exception: Returns the original surface or invalid sprite-bounds error.
esp_err_t draw_personality_rows(const PersonalityFrame &frame, CODY2D::Surface strip,
                                uint16_t first_row, uint16_t first_column)
{
    if (frame.bounds.empty()) {
        return ESP_OK;
    }
    const esp_err_t surface_error = CODY2D::validate_surface(strip);
    if (surface_error != ESP_OK) {
        ESP_LOGE(kTag, "Sprite surface rejected (%s)", esp_err_to_name(surface_error));
        return surface_error;
    }
    const auto *sprite = find_sprite(frame.cue);
    const auto &bounds = frame.bounds;
    const uint16_t width = bounds.right - bounds.left;
    const uint16_t height = bounds.bottom - bounds.top;
    if (sprite == nullptr || bounds.left >= bounds.right || bounds.top >= bounds.bottom ||
        bounds.right > 466 || bounds.bottom > 466 || width > kMaximumSpriteExtent ||
        height > kMaximumSpriteExtent || first_row + strip.height > 466 ||
        first_column + strip.width > 466) {
        ESP_LOGE(kTag, "Sprite bounds/identity rejected cue=%u (%s)",
            static_cast<unsigned>(frame.cue), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const uint16_t top = std::max(bounds.top, first_row);
    const uint16_t bottom = std::min<uint16_t>(bounds.bottom, first_row + strip.height);
    const uint16_t left = std::max(bounds.left, first_column);
    const uint16_t right = std::min<uint16_t>(bounds.right, first_column + strip.width);
    for (uint16_t y = top; y < bottom; ++y) {
        const uint16_t source_y = (y - bounds.top) * sprite->height / height;
        auto *destination = strip.pixels + (y - first_row) * strip.stride;
        for (uint16_t x = left; x < right; ++x) {
            const uint16_t source_x = (x - bounds.left) * sprite->width / width;
            const uint32_t source = source_y * sprite->width + source_x;
            const uint8_t coverage = sprite->coverage[source];
            if (coverage == 0) {
                continue;
            }
            const uint16_t foreground = PersonalityAssets::palette[sprite->indices[source]];
            auto &pixel = destination[x - first_column];
            const uint16_t background = CODY2D::rgb565_stored_to_native(pixel, strip.byte_order);
            pixel = CODY2D::rgb565_native_to_stored(
                CODY2D::blend_rgb565(foreground, background, coverage), strip.byte_order);
        }
    }
    return ESP_OK;
}

// previous: Last completely displayed cue and bounds.
// current: Next immutable cue and bounds.
bool same_personality_frame(const PersonalityFrame &previous, const PersonalityFrame &current)
{
    return previous.cue == current.cue && previous.bounds.left == current.bounds.left &&
        previous.bounds.top == current.bounds.top && previous.bounds.right == current.bounds.right &&
        previous.bounds.bottom == current.bounds.bottom;
}
} // namespace CODY
