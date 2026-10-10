/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#include "CODY_jelly.hpp"
#include "CODY_jelly_simd.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iterator>
#include <string>
#include "CODY_rig.hpp"
#include "LUNASHELL_settings.hpp"
#include "cody_jelly_data.hpp"
#include "cody_jelly_skins.hpp"
#include "cloud_rig_data.hpp"
#include "esp_log.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_jelly";
constexpr int32_t kFixedOne = 65536;
constexpr uint16_t kCanvasSize = 466;
constexpr uint16_t kProfileBlockPixels = 16;
constexpr uint16_t kSimdBlockPixels = 16;
constexpr char kSkinSettingKey[] = "cody_skin";
constexpr JellyRasterKernel kDefaultRasterKernel = CODY_JELLY_SIMD_AVAILABLE
    ? JellyRasterKernel::Simd : JellyRasterKernel::Scalar;
static_assert(std::size(JellyData::rows) == 33);
static_assert(JellyData::width > 1 && JellyData::height > 1);

static_assert(std::size(JellyFrame{}.controls) == std::size(JellyData::rows));
std::atomic<JellySkinId> s_selected_skin{JellySkinCatalog::default_skin};
std::atomic<JellyRasterKernel> s_raster_kernel{kDefaultRasterKernel};

// id: Exact stable authored ID, never a display name or catalog position.
const JellySkinDefinition *find_skin(const char *id)
{
    if (id == nullptr) {
        return nullptr;
    }
    for (const auto &definition : JellySkinCatalog::definitions) {
        if (std::strcmp(definition.key, id) == 0) {
            return &definition;
        }
    }
    return nullptr;
}

// identity: Typed authored identity, independent from catalog ordering.
const JellySkinDefinition *find_skin(JellySkinId identity)
{
    for (const auto &definition : JellySkinCatalog::definitions) {
        if (definition.id == identity) {
            return &definition;
        }
    }
    return nullptr;
}

// identity: Authored point whose ancestry determines whether it belongs to the face.
bool is_facial_point(PointId identity)
{
    for (size_t depth = 0; depth < std::size(Data::points) && identity != PointId::None; ++depth) {
        switch (identity) {
            case PointId::CheekLeft:
            case PointId::CheekRight:
            case PointId::EyeLeft:
            case PointId::EyeLeftShine:
            case PointId::EyeRight:
            case PointId::EyeRightShine:
            case PointId::BrowLeft:
            case PointId::BrowRight:
            case PointId::Nose:
            case PointId::Mouth:
                return true;
            default:
                identity = Data::points[to_index(identity)].parent;
                break;
        }
    }
    return false;
}

// scene: Rig with an already prepared, monotonic sprite row mapping.
// point: Unwarped logical position, replaced only after a finite forward mapping succeeds.
esp_err_t warp_face_point(const RigScene &scene, CODY2D::Vec2 &point)
{
    if (!scene.jelly.valid || !std::isfinite(point.x) || !std::isfinite(point.y)) {
        ESP_LOGE(kTag, "Face warp input rejected: valid=%u point=%f,%f (%s)",
                 scene.jelly.valid, point.x, point.y, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const auto center = scene.world[to_index(PointId::Center)];
    const float local_y = (point.y - center.y) / scene.scale;
    size_t support = 0;
    while (support + 1 < std::size(JellyData::rows) - 1 &&
           local_y >= JellyData::rows[support + 1].local_y) {
        ++support;
    }
    const auto &source_upper = JellyData::rows[support];
    const auto &source_lower = JellyData::rows[support + 1];
    const auto &upper = scene.jelly.controls[support];
    const auto &lower = scene.jelly.controls[support + 1];
    // Endpoint segments extend linearly for authored facial handles outside the
    // sprite rectangle. No expression is silently clamped to the body boundary.
    const float fraction = (local_y - source_upper.local_y) /
                           (source_lower.local_y - source_upper.local_y);
    const float left = upper.left + (lower.left - upper.left) * fraction;
    const float right = upper.right + (lower.right - upper.right) * fraction;
    const float local_x = (point.x - center.x) / scene.scale;
    const float horizontal = (local_x - JellyData::local_left) /
                             (JellyData::local_right - JellyData::local_left);
    const CODY2D::Vec2 warped{
        left + (right - left) * horizontal,
        upper.y + (lower.y - upper.y) * fraction
    };
    if (!std::isfinite(warped.x) || !std::isfinite(warped.y) ||
        std::abs(warped.x) > 2048.f || std::abs(warped.y) > 2048.f) {
        ESP_LOGE(kTag, "Face warp output rejected: %f,%f (%s)",
                 warped.x, warped.y, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    point = warped;
    return ESP_OK;
}

// first: Packed premultiplied RGBA8 texel at the lower interpolation endpoint.
// second: Packed premultiplied RGBA8 texel at the upper interpolation endpoint.
// weight: Eight-bit fraction in [0,255]; 256 would select the second endpoint.
uint32_t lerp_rgba(uint32_t first, uint32_t second, uint32_t weight)
{
    // Two independent 16-bit lanes keep the full multiply in 32-bit arithmetic.
    constexpr uint32_t lanes = 0x00ff00ffU;
    const uint32_t inverse = 256U - weight;
    const uint32_t red_blue = (((first & lanes) * inverse + (second & lanes) * weight +
                               0x00800080U) >> 8U) & lanes;
    const uint32_t green_alpha = ((((first >> 8U) & lanes) * inverse +
                                   ((second >> 8U) & lanes) * weight +
                                   0x00800080U) >> 8U) & lanes;
    return red_blue | (green_alpha << 8U);
}

// source_x: Signed Q16 source coordinate at the destination pixel's center.
// upper: First source scanline selected once for the destination row.
// lower: Adjacent source scanline, or upper at the texture's lower edge.
// vertical_weight: Eight-bit source-row interpolation fraction.
uint32_t sample_rgba(int32_t source_x, const uint32_t *upper,
                     const uint32_t *lower, uint32_t vertical_weight)
{
    // This clamps the sampler footprint, not input state. The master has real
    // transparent padding, so extending its edge never manufactures visible ink.
    const int32_t clipped = std::clamp<int32_t>(source_x, 0, (JellyData::width - 1) * kFixedOne);
    const uint32_t column = static_cast<uint32_t>(clipped) >> 16U;
    const uint32_t next = std::min(column + 1U, static_cast<uint32_t>(JellyData::width - 1));
    const uint32_t horizontal_weight = (static_cast<uint32_t>(clipped) >> 8U) & 255U;
    const uint32_t upper_first = upper[column];
    const uint32_t upper_next = upper[next];
    const uint32_t lower_first = lower[column];
    const uint32_t lower_next = lower[next];
    // Transparent padding has an exact zero result at every sampling weight.
    // Keep the original three rounded lerps for every nonzero footprint.
    if ((upper_first | upper_next | lower_first | lower_next) == 0) {
        return 0;
    }
    const uint32_t above = lerp_rgba(upper_first, upper_next, horizontal_weight);
    const uint32_t below = lerp_rgba(lower_first, lower_next, horizontal_weight);
    return lerp_rgba(above, below, vertical_weight);
}

// source: Bilinearly filtered premultiplied RGBA8 sample.
// destination: Existing native RGB565 canvas pixel under the sprite.
uint16_t blend_rgba(uint32_t source, uint16_t destination)
{
    const uint32_t alpha = source >> 24U;
    if (alpha == 0) {
        return destination;
    }
    uint32_t red = source & 255U;
    uint32_t green = (source >> 8U) & 255U;
    uint32_t blue = (source >> 16U) & 255U;
    if (alpha != 255U && destination != 0) {
        const uint32_t inverse = 255U - alpha;
        red += (((destination >> 11U) * 255U / 31U) * inverse + 127U) / 255U;
        green += ((((destination >> 5U) & 63U) * 255U / 63U) * inverse + 127U) / 255U;
        blue += (((destination & 31U) * 255U / 31U) * inverse + 127U) / 255U;
    }
    // Quantize only the composed sample. Premultiplied interpolation keeps the
    // translucent contour clean; no black matte is baked into the source colors.
    const uint32_t native_red = (std::min<uint32_t>(red, 255U) * 31U + 127U) / 255U;
    const uint32_t native_green = (std::min<uint32_t>(green, 255U) * 63U + 127U) / 255U;
    const uint32_t native_blue = (std::min<uint32_t>(blue, 255U) * 31U + 127U) / 255U;
    return static_cast<uint16_t>((native_red << 11U) | (native_green << 5U) | native_blue);
}

#if CODY_JELLY_SIMD_AVAILABLE
// upper: Immutable RGBA8 source row; only clamped texels are read.
// lower: Next source row, possibly the same at the texture boundary.
// source_x: First Q16 coordinate, advanced by the exact number of packed pixels.
// step: Prepared Q16 source increment, unchanged across compact tile splits.
// pair_count: Complete pairs in one 16-pixel block; no speculative tail reads.
// pairs: Aligned scratch filled for every lane the assembly will consume.
uint32_t prepare_simd_pairs(const uint32_t *upper, const uint32_t *lower, int32_t &source_x,
                            int32_t step, uint16_t pair_count, JellySimdPair *pairs)
{
    uint32_t footprint = 0;
    for (uint16_t pair_index = 0; pair_index < pair_count; ++pair_index) {
        auto &pair = pairs[pair_index];
        for (uint16_t pixel = 0; pixel < 2; ++pixel) {
            const int32_t clipped = std::clamp<int32_t>(source_x, 0, (JellyData::width - 1) * kFixedOne);
            const uint32_t column = static_cast<uint32_t>(clipped) >> 16U;
            const uint32_t next = std::min(column + 1U, static_cast<uint32_t>(JellyData::width - 1));
            pair.first[pixel] = upper[column];
            pair.first[pixel + 2] = lower[column];
            pair.second[pixel] = upper[next];
            pair.second[pixel + 2] = lower[next];
            footprint |= pair.first[pixel] | pair.first[pixel + 2] |
                         pair.second[pixel] | pair.second[pixel + 2];
            const uint16_t weight = static_cast<uint16_t>((static_cast<uint32_t>(clipped) >> 8U) & 255U);
            std::fill_n(pair.horizontal + pixel * 4, 4, weight);
            source_x += step;
        }
    }
    return footprint;
}

// upper: First immutable source scanline.
// lower: Adjacent source scanline selected by the existing sampler clamp.
// source_x: Exact Q16 coordinate of this clipped destination span.
// step: Existing Q16 horizontal increment.
// vertical_weight: Existing Q8 row fraction in [0,255].
// destination: Writable span with exactly pixel_count valid pixels.
// byte_order: Storage order; composition still uses the existing RGB565 arithmetic.
// pixel_count: Visible span length, including an optional odd final pixel.
// kernel: SIMD performance or explicit C/SIMD verification mode.
// profile: Optional counters/clock; preparing aligned data is inside whole-sprite time.
// Exception: Returns ESP_ERR_INVALID_RESPONSE on the first RGBA mismatch, before painting that block.
esp_err_t draw_simd_row(const uint32_t *upper, const uint32_t *lower, int32_t source_x,
                        int32_t step, uint32_t vertical_weight, uint16_t *destination,
                        CODY2D::Rgb565ByteOrder byte_order, uint16_t pixel_count,
                        JellyRasterKernel kernel, JellyRasterProfile *profile)
{
    const bool detail = profile != nullptr && profile->blocks_enabled;
    const int64_t constants_started = detail ? profile->now_us() : 0;
    JellySimdConstants constants;
    std::fill_n(constants.vertical, 8, static_cast<uint16_t>(vertical_weight));
    alignas(16) JellySimdPair pairs[kSimdBlockPixels / 2];
    alignas(16) uint32_t samples[kSimdBlockPixels];
    if (detail) {
        profile->preparation_us += profile->now_us() - constants_started;
    }
    for (uint16_t painted = 0; painted < pixel_count;) {
        const uint16_t count = std::min<uint16_t>(kSimdBlockPixels, pixel_count - painted);
        const uint16_t pair_count = count / 2;
        const uint16_t pair_pixels = pair_count * 2;
        const int32_t block_source_x = source_x;
        const int64_t preparation_started = detail ? profile->now_us() : 0;
        const uint32_t footprint = prepare_simd_pairs(upper, lower, source_x, step, pair_count, pairs);
        if (detail) {
            profile->preparation_us += profile->now_us() - preparation_started;
        }
        const int64_t interpolation_started = detail ? profile->now_us() : 0;
        if (pair_count != 0 && footprint != 0) {
            cody_jelly_bilinear_s3(pairs, samples, pair_count, &constants);
        } else if (kernel == JellyRasterKernel::SimdVerify) {
            // Verification reads every sample; timing mode never reads the skipped pairs.
            std::fill_n(samples, pair_pixels, 0U);
        }
        if ((count & 1U) != 0) {
            samples[count - 1] = sample_rgba(source_x, upper, lower, vertical_weight);
            source_x += step;
        }
        if (detail) {
            profile->interpolation_us += profile->now_us() - interpolation_started;
        }
        if (kernel == JellyRasterKernel::SimdVerify) {
            int32_t reference_x = block_source_x;
            for (uint16_t offset = 0; offset < count; ++offset) {
                const uint32_t expected = sample_rgba(reference_x, upper, lower, vertical_weight);
                if (samples[offset] != expected) {
                    ESP_LOGE(kTag, "SIMD mismatch x_q16=%ld y_weight=%lu expected=%08lx actual=%08lx (%s)",
                        static_cast<long>(reference_x), static_cast<unsigned long>(vertical_weight),
                        static_cast<unsigned long>(expected), static_cast<unsigned long>(samples[offset]),
                        esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
                    return ESP_ERR_INVALID_RESPONSE;
                }
                reference_x += step;
            }
            if (profile != nullptr) {
                profile->verified_samples += count;
            }
        }
        const int64_t composition_started = detail ? profile->now_us() : 0;
        uint16_t first_composed = 0;
        if (footprint == 0) {
            // All four source texels of every packed pair are zero. Their alpha is
            // exactly zero at any weight, so leave the destination span untouched.
            destination += pair_pixels;
            first_composed = pair_pixels;
            if (detail) {
                profile->transparent_pixels += pair_pixels;
            }
        }
        for (uint16_t offset = first_composed; offset < count; ++offset) {
            const uint32_t sample = samples[offset];
            if ((sample >> 24U) == 0) {
                if (detail) {
                    ++profile->transparent_pixels;
                }
                ++destination;
                continue;
            }
            const uint16_t native = CODY2D::rgb565_stored_to_native(*destination, byte_order);
            *destination++ = CODY2D::rgb565_native_to_stored(blend_rgba(sample, native), byte_order);
            if (detail) {
                ++profile->blended_pixels;
            }
        }
        if (profile != nullptr) {
            profile->vectorized_pixels += footprint != 0 ? pair_pixels : 0;
            profile->zero_footprint_pixels += footprint == 0 ? pair_pixels : 0;
            profile->scalar_tail_pixels += count & 1U;
            if (detail) {
                profile->blend_us += profile->now_us() - composition_started;
                profile->sampled_pixels += count;
                ++profile->timed_blocks;
            }
        }
        painted = static_cast<uint16_t>(painted + count);
    }
    return ESP_OK;
}
#endif

}  // namespace

// No parameters: explicit capability, never an automatic scalar substitution.
bool jelly_simd_available()
{
    return CODY_JELLY_SIMD_AVAILABLE != 0;
}

// kernel: Typed identity copied into measurement evidence.
const char *jelly_raster_kernel_name(JellyRasterKernel kernel)
{
    switch (kernel) {
        case JellyRasterKernel::Scalar: return "scalar";
        case JellyRasterKernel::Simd: return "simd";
        case JellyRasterKernel::SimdVerify: return "simd_verify";
    }
    return "invalid";
}

// kernel: Explicit implementation selected by the display owner between frames.
// Exception: Rejects invalid identities or SIMD in a build without that kernel.
esp_err_t select_jelly_raster_kernel(JellyRasterKernel kernel)
{
    if (kernel != JellyRasterKernel::Scalar && kernel != JellyRasterKernel::Simd &&
        kernel != JellyRasterKernel::SimdVerify) {
        ESP_LOGE(kTag, "Raster kernel rejected: %u (%s)", static_cast<unsigned>(kernel),
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (kernel != JellyRasterKernel::Scalar && !jelly_simd_available()) {
        ESP_LOGE(kTag, "Raster kernel unavailable: %s (%s)", jelly_raster_kernel_name(kernel),
                 esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_raster_kernel.store(kernel, std::memory_order_release);
    ESP_LOGI(kTag, "Raster kernel selected: %s", jelly_raster_kernel_name(kernel));
    return ESP_OK;
}

// No parameters: per-draw snapshot avoids mixing implementations inside one sprite pass.
JellyRasterKernel selected_jelly_raster_kernel()
{
    return s_raster_kernel.load(std::memory_order_acquire);
}

// No parameters: expose bounded catalog size without treating position as identity.
size_t jelly_skin_count()
{
    return std::size(JellySkinCatalog::definitions);
}

// index: Traversal offset at this API boundary, never persisted or used as identity.
// selection: Receives immutable catalog data only after a valid bounds check.
esp_err_t jelly_skin_at(size_t index, JellySkinSelection &selection)
{
    if (index >= std::size(JellySkinCatalog::definitions)) {
        ESP_LOGE(kTag, "Skin catalog index rejected: %u (%s)",
                 static_cast<unsigned>(index), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const auto &definition = JellySkinCatalog::definitions[index];
    selection = {
        definition.key,
        definition.display_name,
        definition.id == JellySkinCatalog::default_skin,
    };
    return ESP_OK;
}

// id: Exact authored identity selected for subsequent evaluated frames.
// Exception: Null and unknown IDs are rejected without changing the current skin.
esp_err_t select_jelly_skin(const char *id)
{
    if (id == nullptr) {
        ESP_LOGE(kTag, "Skin selection rejected: id=<null> (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const JellySkinDefinition *definition = find_skin(id);
    if (definition == nullptr) {
        ESP_LOGE(kTag, "Skin selection rejected: id=%s (%s)",
                 id, esp_err_to_name(ESP_ERR_NOT_FOUND));
        return ESP_ERR_NOT_FOUND;
    }
    s_selected_skin.store(definition->id, std::memory_order_release);
    ESP_LOGI(kTag, "Skin selected for subsequent frames: id=%s name=%s%s",
             definition->key, definition->display_name,
             definition->id == JellySkinCatalog::default_skin ? " default" : "");
    return ESP_OK;
}

// selection: Receives the current typed selection after validating the catalog identity.
esp_err_t selected_jelly_skin(JellySkinSelection &selection)
{
    const JellySkinId identity = s_selected_skin.load(std::memory_order_acquire);
    const JellySkinDefinition *definition = find_skin(identity);
    if (definition == nullptr) {
        ESP_LOGE(kTag, "Selected skin identity rejected: value=%u (%s)",
                 static_cast<unsigned>(identity), esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    selection = {
        definition->key,
        definition->display_name,
        definition->id == JellySkinCatalog::default_skin,
    };
    return ESP_OK;
}

// No parameters: load the saved stable identity after the shared settings owner is ready.
// Exception: Missing is the only state that creates the documented Pearl Mist default.
esp_err_t load_persisted_jelly_skin()
{
    auto &settings = LUNASHELL::UserSettingsService::instance();
    esp_err_t error = settings.isInitialized() ? ESP_OK : settings.initialize();
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Look settings initialization failed (%s)", esp_err_to_name(error));
        return error;
    }
    std::string saved_id;
    error = settings.getString(kSkinSettingKey, &saved_id);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        const JellySkinDefinition *definition = find_skin(JellySkinCatalog::default_skin);
        if (definition == nullptr) {
            ESP_LOGE(kTag, "Default look identity is not in the catalog (%s)",
                     esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        ESP_LOGI(kTag, "Look key '%s' absent; persisting default id=%s name=%s",
                 kSkinSettingKey, definition->key, definition->display_name);
        error = settings.setString(kSkinSettingKey, definition->key);
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "Default look persistence failed: key=%s id=%s (%s)",
                     kSkinSettingKey, definition->key, esp_err_to_name(error));
            return error;
        }
        s_selected_skin.store(definition->id, std::memory_order_release);
        return ESP_OK;
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Look preference read failed: key=%s (%s)",
                 kSkinSettingKey, esp_err_to_name(error));
        return error;
    }
    const JellySkinDefinition *definition = find_skin(saved_id.c_str());
    if (definition == nullptr) {
        ESP_LOGE(kTag, "Saved look rejected: key=%s id=%s (%s)", kSkinSettingKey,
                 saved_id.c_str(), esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    s_selected_skin.store(definition->id, std::memory_order_release);
    ESP_LOGI(kTag, "Saved look loaded: id=%s name=%s%s", definition->key,
             definition->display_name,
             definition->id == JellySkinCatalog::default_skin ? " default" : "");
    return ESP_OK;
}

// id: Exact authored identity chosen from screen 11, never its visible row position.
// Exception: Returns the original NVS error; activation belongs to the display owner.
esp_err_t persist_jelly_skin(const char *id)
{
    if (id == nullptr) {
        ESP_LOGE(kTag, "Look persistence rejected: id=<null> (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const JellySkinDefinition *definition = find_skin(id);
    if (definition == nullptr) {
        ESP_LOGE(kTag, "Look persistence rejected: id=%s (%s)", id,
                 esp_err_to_name(ESP_ERR_NOT_FOUND));
        return ESP_ERR_NOT_FOUND;
    }
    const esp_err_t error = LUNASHELL::UserSettingsService::instance().setString(
        kSkinSettingKey, definition->key);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Look persistence failed: key=%s id=%s (%s)",
                 kSkinSettingKey, definition->key, esp_err_to_name(error));
        return error;
    }
    // The action worker must not mutate renderer selection. Its completion is
    // applied by the display owner between frames via select_jelly_skin.
    ESP_LOGI(kTag, "Look persisted: id=%s name=%s%s", definition->key,
             definition->display_name,
             definition->id == JellySkinCatalog::default_skin ? " default" : "");
    return ESP_OK;
}

// identity: Semantic vector surface path replaced by the approved sprite.
bool jelly_replaces_path(PathId identity)
{
    switch (identity) {
        case PathId::Body:
        case PathId::BodyShadowLower:
        case PathId::BodyShadowCrown:
        case PathId::BodyShadowLeft:
        case PathId::BodyShadowRight:
            return true;
        default:
            return false;
    }
}

// scene: Evaluated world points and scale, before the enclosing scene becomes valid.
// frame: Caller-owned mapping, made valid only after every visible row is prepared.
esp_err_t prepare_jelly_frame(const RigScene &scene, JellyFrame &frame)
{
    frame.valid = false;
    frame.bounds = {};
    frame.skin_id = static_cast<uint16_t>(s_selected_skin.load(std::memory_order_acquire));
    if (!std::isfinite(scene.scale) || scene.scale <= 0 || scene.scale > 204.f) {
        ESP_LOGE(kTag, "Prepare scale rejected: %f (%s)", scene.scale,
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const auto center = scene.world[to_index(PointId::Center)];
    std::array<CODY2D::Vec2, std::size(JellyData::anchors)> offsets{};
    for (size_t index = 0; index < std::size(JellyData::anchors); ++index) {
        const auto &anchor = JellyData::anchors[index];
        const auto position = scene.world[to_index(anchor.point)];
        offsets[index] = {position.x - center.x - anchor.rest.x * scene.scale,
                          position.y - center.y - anchor.rest.y * scene.scale};
    }
    auto &controls = frame.controls;
    for (size_t index = 0; index < controls.size(); ++index) {
        const auto &binding = JellyData::rows[index];
        auto &control = controls[index];
        control = {center.y + binding.local_y * scene.scale,
                   center.x + JellyData::local_left * scene.scale,
                   center.x + JellyData::local_right * scene.scale};
        for (size_t anchor = 0; anchor < offsets.size(); ++anchor) {
            control.left += offsets[anchor].x * binding.left[anchor];
            control.right += offsets[anchor].x * binding.right[anchor];
            control.y += offsets[anchor].y * binding.vertical[anchor];
        }
        if (!std::isfinite(control.y) || !std::isfinite(control.left) || !std::isfinite(control.right) ||
            std::abs(control.y) > 2048.f || std::abs(control.left) > 2048.f ||
            std::abs(control.right) > 2048.f || control.right - control.left <= .001f ||
            (index > 0 && control.y - controls[index - 1].y <= .001f)) {
            ESP_LOGE(kTag, "Prepare row %u rejected: y=%f left=%f right=%f (%s)",
                     static_cast<unsigned>(binding.id), control.y, control.left, control.right,
                     esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
    }
    const int first = std::clamp(static_cast<int>(std::ceil(controls.front().y - .5f)), 0, 466);
    const int last = std::clamp(static_cast<int>(std::ceil(controls.back().y - .5f)), 0, 466);
    size_t support = 0;
    for (int row = first; row < last; ++row) {
        const float sample_y = row + .5f;
        while (support + 1 < controls.size() - 1 && sample_y >= controls[support + 1].y) {
            ++support;
        }
        const auto &upper = controls[support];
        const auto &lower = controls[support + 1];
        const float fraction = (sample_y - upper.y) / (lower.y - upper.y);
        const float left = upper.left + (lower.left - upper.left) * fraction;
        const float right = upper.right + (lower.right - upper.right) * fraction;
        auto &mapping = frame.rows[row];
        mapping = {};
        mapping.left = static_cast<uint16_t>(std::clamp(static_cast<int>(std::floor(left)), 0, 466));
        mapping.right = static_cast<uint16_t>(std::clamp(static_cast<int>(std::ceil(right)), 0, 466));
        if (mapping.left >= mapping.right) {
            continue;
        }
        const float step = JellyData::width / (right - left);
        // Bound the fixed-point step before conversion. This is rejection of a
        // degenerate mapping, not an implicit change to authored motion controls.
        if (!std::isfinite(step) || step <= 0 || step > 1024.f) {
            ESP_LOGE(kTag, "Prepare row %d sampling step rejected: %f (%s)", row, step,
                     esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
        const auto &source_upper = JellyData::rows[support];
        const auto &source_lower = JellyData::rows[support + 1];
        const float source_y = source_upper.source_y +
            (source_lower.source_y - source_upper.source_y) * fraction - .5f;
        mapping.source_x = static_cast<int32_t>(std::lround(((mapping.left + .5f - left) * step - .5f) * kFixedOne));
        mapping.source_step = static_cast<int32_t>(std::lround(step * kFixedOne));
        mapping.source_y = static_cast<int32_t>(std::lround(source_y * kFixedOne));
        if (frame.bounds.empty()) {
            frame.bounds = {mapping.left, static_cast<uint16_t>(row), mapping.right,
                            static_cast<uint16_t>(row + 1)};
        } else {
            frame.bounds.left = std::min(frame.bounds.left, mapping.left);
            frame.bounds.right = std::max(frame.bounds.right, mapping.right);
            frame.bounds.bottom = static_cast<uint16_t>(row + 1);
        }
    }
    frame.valid = true;
    return ESP_OK;
}

// scene: Uncommitted rig with a valid sprite mapping; keeps authored local coordinates intact.
esp_err_t deform_jelly_face(RigScene &scene)
{
    for (size_t index = 0; index < std::size(Data::paths); ++index) {
        const auto &definition = Data::paths[index];
        if (jelly_replaces_path(definition.id)) {
            continue;
        }
        auto &path = scene.paths[index];
        const float coverage = definition.antialiasing == CODY2D::FillAntialiasing::Coverage ? .5f : 0.f;
        const float margin = definition.stroke * scene.scale * .5f + coverage;
        for (uint16_t offset = 0; offset < path.count; ++offset) {
            auto &point = scene.vertices[path.first_vertex + offset];
            const esp_err_t error = warp_face_point(scene, point);
            if (error != ESP_OK) {
                ESP_LOGE(kTag, "Face path %s warp failed: %s", definition.name, esp_err_to_name(error));
                return error;
            }
            if (offset == 0) {
                path.first_scan_y = point.y - margin;
                path.last_scan_y = point.y + margin;
            } else {
                path.first_scan_y = std::min(path.first_scan_y, point.y - margin);
                path.last_scan_y = std::max(path.last_scan_y, point.y + margin);
            }
        }
    }
    // Cubics are sampled before the nonlinear warp. Warping only their control
    // points would follow a different curve from the painted texture between rows.
    for (const auto &definition : Data::points) {
        if (!is_facial_point(definition.id)) {
            continue;
        }
        const esp_err_t error = warp_face_point(scene, scene.world[to_index(definition.id)]);
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "Face point %s warp failed: %s", definition.name, esp_err_to_name(error));
            return error;
        }
    }
    return ESP_OK;
}

// frame: Prepared logical rows; no floating-point work or PNG decode occurs here.
// strip: RGB565 output with caller-owned writable storage and explicit byte order.
// first_row: Logical tile y origin, independent of the framebuffer's row stride.
// first_column: Logical tile x origin, preserving identical samples across tile splits.
// Exception: Returns the original surface error or rejects invalid geometry and skin identity.
// profile: Optional execution clock and block-level accounting for this sprite pass.
esp_err_t draw_jelly_rows(const JellyFrame &frame, CODY2D::Surface strip,
                         uint16_t first_row, uint16_t first_column,
                         JellyRasterProfile *profile)
{
    if (profile != nullptr && profile->now_us == nullptr) {
        ESP_LOGE(kTag, "Jelly profile clock rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const int64_t jelly_started_us = profile != nullptr ? profile->now_us() : 0;
    const bool profile_blocks = profile != nullptr && profile->blocks_enabled;
    const JellyRasterKernel kernel = selected_jelly_raster_kernel();
    if (profile != nullptr) {
        profile->kernel = kernel;
    }
    const esp_err_t surface_error = CODY2D::validate_surface(strip);
    if (surface_error != ESP_OK) {
        return surface_error;
    }
    if (!frame.valid || first_row + strip.height > kCanvasSize || first_column + strip.width > kCanvasSize) {
        ESP_LOGE(kTag, "Draw rejected: valid=%u tile=%u,%u %ux%u (%s)", frame.valid,
                 first_column, first_row, strip.width, strip.height, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const auto skin = static_cast<JellySkinId>(frame.skin_id);
    const uint32_t *pixels = JellyData::pixels_for(skin);
    if (pixels == nullptr || find_skin(skin) == nullptr) {
        ESP_LOGE(kTag, "Draw skin rejected: value=%u (%s)", frame.skin_id,
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const uint16_t first = std::max(first_row, frame.bounds.top);
    const uint16_t last = std::min(static_cast<uint16_t>(first_row + strip.height), frame.bounds.bottom);
    for (uint16_t row = first; row < last; ++row) {
        const auto &mapping = frame.rows[row];
        uint16_t left = std::max(first_column, mapping.left);
        uint16_t right = std::min(static_cast<uint16_t>(first_column + strip.width), mapping.right);
        if (left >= right) {
            continue;
        }
        const int64_t preparation_started_us = profile_blocks ? profile->now_us() : 0;
        const int32_t source_y = std::clamp<int32_t>(mapping.source_y, 0, (JellyData::height - 1) * kFixedOne);
        const uint32_t source_row = static_cast<uint32_t>(source_y) >> 16U;
        const uint32_t next_row = std::min(source_row + 1U, static_cast<uint32_t>(JellyData::height - 1));
        const uint32_t vertical_weight = (static_cast<uint32_t>(source_y) >> 8U) & 255U;
        const auto *upper = pixels + source_row * JellyData::width;
        const auto *lower = pixels + next_row * JellyData::width;
        int32_t source_x = mapping.source_x + (left - mapping.left) * mapping.source_step;
        auto *destination = strip.pixels + (row - first_row) * strip.stride + left - first_column;
#if CODY_JELLY_SIMD_AVAILABLE
        if (kernel != JellyRasterKernel::Scalar) {
            const auto &upper_coverage = JellyData::row_coverage[source_row];
            const auto &lower_coverage = JellyData::row_coverage[next_row];
            const uint16_t first_texel = std::min(upper_coverage.first, lower_coverage.first);
            const uint16_t last_texel = std::max(upper_coverage.last, lower_coverage.last);
            if (last_texel == 0 && kernel == JellyRasterKernel::Simd) {
                if (profile != nullptr) {
                    profile->coverage_skipped_pixels += right - left;
                }
                continue;
            }
            if (kernel == JellyRasterKernel::Simd && mapping.source_step > 0) {
                const uint16_t untrimmed_pixels = right - left;
                // Bilinear sampling can reach the first nonzero texel from its
                // preceding column. The bounds cover every authored skin.
                const int32_t first_possible_x = (first_texel - 1) * kFixedOne;
                const int32_t first_impossible_x = last_texel * kFixedOne;
                while (left < right && first_texel > 0 && source_x < first_possible_x) {
                    ++left;
                    ++destination;
                    source_x += mapping.source_step;
                }
                while (left < right && last_texel < JellyData::width &&
                       static_cast<int64_t>(source_x) +
                           static_cast<int64_t>(right - left - 1) * mapping.source_step >= first_impossible_x) {
                    --right;
                }
                if (profile != nullptr) {
                    profile->coverage_skipped_pixels += untrimmed_pixels - (right - left);
                }
                if (left == right) {
                    continue;
                }
            }
            if (profile_blocks) {
                profile->preparation_us += profile->now_us() - preparation_started_us;
            }
            const esp_err_t error = draw_simd_row(upper, lower, source_x, mapping.source_step,
                vertical_weight, destination, strip.byte_order, right - left, kernel, profile);
            if (error != ESP_OK) {
                return error;
            }
            continue;
        }
#endif
        if (!profile_blocks) {
            for (uint16_t column = left; column < right; ++column) {
                const uint32_t sample = sample_rgba(source_x, upper, lower, vertical_weight);
                // Zero alpha is the identity over every RGB565 background. Advance
                // both cursors without reading or rewriting that destination pixel.
                if ((sample >> 24U) != 0) {
                    const uint16_t native_destination = CODY2D::rgb565_stored_to_native(
                        *destination, strip.byte_order
                    );
                    const uint16_t blended_color = blend_rgba(sample, native_destination);
                    *destination = CODY2D::rgb565_native_to_stored(blended_color, strip.byte_order);
                }
                ++destination;
                source_x += mapping.source_step;
            }
            continue;
        }
        profile->preparation_us += profile->now_us() - preparation_started_us;
        for (uint16_t column = left; column < right;) {
            const uint16_t block_pixels = std::min<uint16_t>(kProfileBlockPixels, right - column);
            const int64_t block_preparation_started_us = profile->now_us();
            std::array<uint32_t, kProfileBlockPixels> samples{};
            profile->preparation_us += profile->now_us() - block_preparation_started_us;
            const int64_t interpolation_started_us = profile->now_us();
            for (uint16_t offset = 0; offset < block_pixels; ++offset) {
                samples[offset] = sample_rgba(source_x, upper, lower, vertical_weight);
                source_x += mapping.source_step;
            }
            profile->interpolation_us += profile->now_us() - interpolation_started_us;
            const int64_t blend_started_us = profile->now_us();
            for (uint16_t offset = 0; offset < block_pixels; ++offset) {
                const uint32_t sample = samples[offset];
                if ((sample >> 24U) == 0) {
                    ++profile->transparent_pixels;
                    ++destination;
                    continue;
                }
                const uint16_t native_destination = CODY2D::rgb565_stored_to_native(
                    *destination, strip.byte_order
                );
                const uint16_t blended_color = blend_rgba(sample, native_destination);
                *destination = CODY2D::rgb565_native_to_stored(blended_color, strip.byte_order);
                ++profile->blended_pixels;
                ++destination;
            }
            profile->blend_us += profile->now_us() - blend_started_us;
            profile->sampled_pixels += block_pixels;
            ++profile->timed_blocks;
            column = static_cast<uint16_t>(column + block_pixels);
        }
    }
    if (profile != nullptr) {
        profile->total_us += profile->now_us() - jelly_started_us;
    }
    return ESP_OK;
}

// previous: Last completed scene's immutable row mapping.
// current: Next evaluated row mapping, including both texture and alpha geometry.
bool same_jelly_frame(const JellyFrame &previous, const JellyFrame &current)
{
    if (!previous.valid || !current.valid || previous.skin_id != current.skin_id ||
        previous.bounds.left != current.bounds.left ||
        previous.bounds.top != current.bounds.top || previous.bounds.right != current.bounds.right ||
        previous.bounds.bottom != current.bounds.bottom) {
        return false;
    }
    for (uint16_t row = current.bounds.top; row < current.bounds.bottom; ++row) {
        const auto &old_row = previous.rows[row];
        const auto &new_row = current.rows[row];
        if (old_row.left != new_row.left || old_row.right != new_row.right ||
            old_row.source_x != new_row.source_x || old_row.source_step != new_row.source_step ||
            old_row.source_y != new_row.source_y) {
            return false;
        }
    }
    return true;
}

}  // namespace CODY
