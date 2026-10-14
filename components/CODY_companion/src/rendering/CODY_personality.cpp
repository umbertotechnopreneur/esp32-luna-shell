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

#include "CODY_personality.hpp"
#include "CODY_personality_config.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include "CODY_rig.hpp"
#include "cloud_rig_data.hpp"
#include "personality_icons.hpp"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_personality";
// Legacy bitmap masters have 256 pixels; compact native connection symbols use their own coverage.
constexpr uint16_t kMaximumSpriteExtent = 231;
static_assert(PersonalityConfig::position_window_us > 0 &&
    PersonalityConfig::position_window_us <= PersonalityConfig::scale_window_us &&
    PersonalityConfig::scale_window_us <= 400000, "Following must settle within 400 active milliseconds");
static_assert(PersonalityConfig::history_bucket_us > 0 &&
    PersonalityConfig::history_bucket_us <= PersonalityConfig::position_window_us &&
    PersonalityConfig::scale_window_us % PersonalityConfig::history_bucket_us == 0,
    "History buckets must divide the bounded scale horizon");
static_assert(PersonalityConfig::history_bucket_count ==
    PersonalityConfig::scale_window_us / PersonalityConfig::history_bucket_us + 1 &&
    PersonalityConfig::history_bucket_count <= UINT8_MAX,
    "History needs the full scale window and one partial bucket with a bounded index");
static_assert(sizeof(PersonalityFollowBucket) == 24, "Each fixed geometry history bucket uses 24 bytes");
static_assert(PersonalityConfig::cue_rule_count == 18, "Repeat storage follows the eighteen stable cue IDs");
static_assert(sizeof(PersonalityLag) <= 768, "Controller, geometry and repeat timestamps stay bounded");

// cue: Typed native artwork identity, separate from the fourteen bitmap masters.
// x: Quarter-pixel horizontal position on the immutable 64-pixel source canvas.
// y: Quarter-pixel vertical position on that same canvas.
constexpr bool connection_symbol_ink(PersonalityCue cue, int x, int y)
{
    if (cue == PersonalityCue::BatteryReady || cue == PersonalityCue::BatteryLow) {
        const bool body = x >= 44 && x <= 208 && y >= 84 && y <= 172 &&
            (x <= 54 || x >= 198 || y <= 94 || y >= 162);
        const bool terminal = x >= 208 && x <= 224 && y >= 108 && y <= 148;
        const bool mark = cue == PersonalityCue::BatteryLow
            ? (x >= 120 && x <= 132 && ((y >= 104 && y <= 138) || (y >= 148 && y <= 154)))
            : ((x >= 96 && x <= 124 && y - x >= 10 && y - x <= 22) ||
               (x >= 118 && x <= 160 && x + y >= 254 && x + y <= 266));
        return body || terminal || mark;
    }
    if (cue == PersonalityCue::WifiConnected) {
        const int dx = x - 128;
        const int dy = y - 196;
        const int distance = dx * dx + dy * dy;
        const bool arc_sector = dy < 0 && dx >= -100 && dx <= 100 && dx >= dy * 2 && dx <= -dy * 2;
        const bool arcs = arc_sector &&
            ((distance >= 150 * 150 && distance <= 162 * 162) ||
             (distance >= 100 * 100 && distance <= 112 * 112) ||
             (distance >= 50 * 50 && distance <= 62 * 62));
        const bool dot = distance <= 10 * 10;
        const bool check_start = x >= 148 && x <= 174 && y - x >= 32 && y - x <= 44;
        const bool check_finish = x >= 166 && x <= 214 && x + y >= 366 && x + y <= 378;
        return arcs || dot || check_start || check_finish;
    }
    const int dx = x - 108;
    const int dy = y - 108;
    const int distance = dx * dx + dy * dy;
    const bool rim = distance >= 48 * 48 && distance <= 62 * 62;
    const bool handle = x >= 150 && x <= 214 && y >= 150 && y <= 214 && x - y >= -10 && x - y <= 10;
    const bool sparkle = (x >= 202 && x <= 210 && y >= 42 && y <= 74) ||
        (x >= 190 && x <= 222 && y >= 54 && y <= 62);
    return rim || handle || sparkle;
}

// cue: Stable native symbol; coverage is sampled at four subpixel positions at compile time.
constexpr std::array<uint8_t, 4096> connection_symbol_coverage(PersonalityCue cue)
{
    std::array<uint8_t, 4096> coverage{};
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            unsigned samples = 0;
            for (int sub_y = 1; sub_y <= 3; sub_y += 2) {
                for (int sub_x = 1; sub_x <= 3; sub_x += 2) {
                    samples += connection_symbol_ink(cue, x * 4 + sub_x, y * 4 + sub_y) ? 1U : 0U;
                }
            }
            coverage[y * 64 + x] = static_cast<uint8_t>((samples * 255U + 2U) / 4U);
        }
    }
    return coverage;
}

constexpr auto kWifiConnectedCoverage = connection_symbol_coverage(PersonalityCue::WifiConnected);
constexpr auto kSearchCoverage = connection_symbol_coverage(PersonalityCue::Search);
constexpr auto kBatteryReadyCoverage = connection_symbol_coverage(PersonalityCue::BatteryReady);
constexpr auto kBatteryLowCoverage = connection_symbol_coverage(PersonalityCue::BatteryLow);
// Native symbols use a single semantic color and own no palette-index plane.
constexpr PersonalityAssets::Sprite kWifiConnectedSprite{PersonalityCue::WifiConnected, 64, 64,
    nullptr, kWifiConnectedCoverage.data()};
constexpr PersonalityAssets::Sprite kSearchSprite{PersonalityCue::Search, 64, 64,
    nullptr, kSearchCoverage.data()};
constexpr PersonalityAssets::Sprite kBatteryReadySprite{PersonalityCue::BatteryReady, 64, 64,
    nullptr, kBatteryReadyCoverage.data()};
constexpr PersonalityAssets::Sprite kBatteryLowSprite{PersonalityCue::BatteryLow, 64, 64,
    nullptr, kBatteryLowCoverage.data()};
constexpr int8_t kSearchOffsetX[]{0, 1, 2, 1, 0, -1, -2, -1};
constexpr int8_t kSearchOffsetY[]{-1, 0, 1, 2, 1, 0, -1, -2};

// state: Fixed geometry history whose internal invariants must remain coherent.
bool follow_history_valid(const PersonalityLag &state)
{
    if (state.current_bucket >= state.follow_history.size() ||
        state.position_stable_us > PersonalityConfig::scale_window_us ||
        state.scale_stable_us > PersonalityConfig::scale_window_us ||
        !std::isfinite(state.previous_target.center_x) || !std::isfinite(state.previous_target.center_y) ||
        !std::isfinite(state.previous_target.width) || !std::isfinite(state.previous_target.height) ||
        state.previous_target.width < 1 || state.previous_target.width > kMaximumSpriteExtent ||
        state.previous_target.height < 1 || state.previous_target.height > kMaximumSpriteExtent) {
        return false;
    }
    const uint32_t current_duration_us = state.follow_history[state.current_bucket].duration_us;
    if (current_duration_us >= PersonalityConfig::history_bucket_us) {
        return false;
    }
    for (size_t index = 0; index < state.follow_history.size(); ++index) {
        const auto &bucket = state.follow_history[index];
        const size_t intervals_back = (state.current_bucket + state.follow_history.size() - index) %
            state.follow_history.size();
        const uint32_t expected_age_us = current_duration_us +
            static_cast<uint32_t>(intervals_back * PersonalityConfig::history_bucket_us);
        if (bucket.duration_us > PersonalityConfig::history_bucket_us || bucket.age_us < bucket.duration_us ||
            bucket.age_us > PersonalityConfig::scale_window_us + PersonalityConfig::history_bucket_us + 1 ||
            bucket.age_us != expected_age_us ||
            (index != state.current_bucket && bucket.duration_us != PersonalityConfig::history_bucket_us) ||
            !std::isfinite(bucket.x_integral) || !std::isfinite(bucket.y_integral) ||
            !std::isfinite(bucket.width_integral) || !std::isfinite(bucket.height_integral) ||
            (bucket.duration_us != 0 &&
             (bucket.width_integral < bucket.duration_us || bucket.height_integral < bucket.duration_us ||
              bucket.width_integral > kMaximumSpriteExtent * bucket.duration_us ||
              bucket.height_integral > kMaximumSpriteExtent * bucket.duration_us)) ||
            (bucket.duration_us == 0 && (bucket.x_integral != 0 || bucket.y_integral != 0 ||
             bucket.width_integral != 0 || bucket.height_integral != 0))) {
            return false;
        }
    }
    return true;
}

// state: Geometry state to prime; the cue phase, opacity and hold are deliberately untouched.
// target: Current finite authored position and bounded sprite dimensions.
void prime_follow(PersonalityLag &state, const PersonalityFollowTarget &target)
{
    state.center_x = target.center_x;
    state.center_y = target.center_y;
    state.width = target.width;
    state.height = target.height;
    state.previous_target = target;
    state.position_stable_us = 0;
    state.scale_stable_us = 0;
    state.current_bucket = static_cast<uint8_t>(state.follow_history.size() - 1);
    for (size_t index = 0; index < state.follow_history.size(); ++index) {
        auto &bucket = state.follow_history[index];
        if (index == state.current_bucket) {
            bucket = {};
            continue;
        }
        bucket.duration_us = PersonalityConfig::history_bucket_us;
        bucket.age_us = static_cast<uint32_t>((state.follow_history.size() - 1 - index) *
            PersonalityConfig::history_bucket_us);
        bucket.x_integral = target.center_x * bucket.duration_us;
        bucket.y_integral = target.center_y * bucket.duration_us;
        bucket.width_integral = target.width * bucket.duration_us;
        bucket.height_integral = target.height * bucket.duration_us;
    }
}

// state: Fixed bucket history and resulting artistic transform.
// target: Latest evaluated geometry, held over the active frame interval.
// elapsed_us: Positive active interval; intervals covering the whole horizon prime directly.
void advance_follow(PersonalityLag &state, const PersonalityFollowTarget &target, int64_t elapsed_us)
{
    if (elapsed_us >= PersonalityConfig::scale_window_us) {
        // No history before this long frame gap can contribute to the bounded horizon.
        prime_follow(state, target);
        return;
    }
    const uint32_t delta_us = static_cast<uint32_t>(elapsed_us);
    const bool same_position = target.center_x == state.previous_target.center_x &&
        target.center_y == state.previous_target.center_y;
    const bool same_scale = target.width == state.previous_target.width && target.height == state.previous_target.height;
    state.position_stable_us = same_position ? std::min<uint32_t>(
        state.position_stable_us + delta_us, PersonalityConfig::scale_window_us) : 0;
    state.scale_stable_us = same_scale ? std::min<uint32_t>(
        state.scale_stable_us + delta_us, PersonalityConfig::scale_window_us) : 0;
    state.previous_target = target;
    for (auto &bucket : state.follow_history) {
        bucket.age_us = std::min<uint32_t>(bucket.age_us + delta_us,
            PersonalityConfig::scale_window_us + PersonalityConfig::history_bucket_us + 1);
    }
    uint32_t remaining_us = delta_us;
    while (remaining_us != 0) {
        auto &bucket = state.follow_history[state.current_bucket];
        const uint32_t accepted_us = std::min<uint32_t>(remaining_us,
            PersonalityConfig::history_bucket_us - bucket.duration_us);
        bucket.x_integral += target.center_x * accepted_us;
        bucket.y_integral += target.center_y * accepted_us;
        bucket.width_integral += target.width * accepted_us;
        bucket.height_integral += target.height * accepted_us;
        bucket.duration_us += accepted_us;
        remaining_us -= accepted_us;
        if (bucket.duration_us == PersonalityConfig::history_bucket_us) {
            state.current_bucket = static_cast<uint8_t>((state.current_bucket + 1) % state.follow_history.size());
            state.follow_history[state.current_bucket] = {};
            state.follow_history[state.current_bucket].age_us = remaining_us;
        }
    }
    float x_integral = 0;
    float y_integral = 0;
    float width_integral = 0;
    float height_integral = 0;
    uint32_t position_duration_us = 0;
    uint32_t scale_duration_us = 0;
    for (const auto &bucket : state.follow_history) {
        // Drop the entire crossing bucket: its average may contain older samples.
        // Keeping a partial old mean would extend a 400 ms horizon to 425 ms.
        if (bucket.age_us <= PersonalityConfig::position_window_us) {
            x_integral += bucket.x_integral;
            y_integral += bucket.y_integral;
            position_duration_us += bucket.duration_us;
        }
        if (bucket.age_us <= PersonalityConfig::scale_window_us) {
            width_integral += bucket.width_integral;
            height_integral += bucket.height_integral;
            scale_duration_us += bucket.duration_us;
        }
    }
    state.center_x = x_integral / position_duration_us;
    state.center_y = y_integral / position_duration_us;
    state.width = width_integral / scale_duration_us;
    state.height = height_integral / scale_duration_us;
    // Exact assignment at the deadline removes floating-point summation residue.
    if (state.position_stable_us >= PersonalityConfig::position_window_us) {
        state.center_x = target.center_x;
        state.center_y = target.center_y;
    }
    if (state.scale_stable_us >= PersonalityConfig::scale_window_us) {
        state.width = target.width;
        state.height = target.height;
    }
    const float lag_x = state.center_x - target.center_x;
    const float lag_y = state.center_y - target.center_y;
    const float distance_squared = lag_x * lag_x + lag_y * lag_y;
    const float maximum_lag = PersonalityConfig::maximum_position_lag_pixels;
    if (distance_squared > maximum_lag * maximum_lag) {
        const float reduction = maximum_lag / std::sqrt(distance_squared);
        state.center_x = target.center_x + lag_x * reduction;
        state.center_y = target.center_y + lag_y * reduction;
    }
}

// elapsed_us: Nonnegative progress within a bounded transition.
// duration_us: Positive transition duration.
float smooth_progress(int64_t elapsed_us, int64_t duration_us)
{
    const float progress = std::clamp(static_cast<float>(elapsed_us) / duration_us, 0.f, 1.f);
    return progress * progress * (3.f - 2.f * progress);
}

// state: Channel to hide without discarding the latest desired target or geometric following.
void hide_current(PersonalityLag &state)
{
    state.cue = PersonalityCue::None;
    state.phase = PersonalityPhase::Hidden;
    state.phase_elapsed_us = 0;
    state.opacity = 0;
    state.fade_out_opacity = 0;
}

// state: Current display state whose opacity becomes the fade-out starting point.
void begin_fade_out(PersonalityLag &state)
{
    state.phase = PersonalityPhase::FadeOut;
    state.phase_elapsed_us = 0;
    state.fade_out_opacity = state.opacity;
}

// cue: Typed identity in the generated configuration catalog; None has no authored rule.
const PersonalityConfig::CueRule *cue_rule(PersonalityCue cue)
{
    for (const auto &rule : PersonalityConfig::cue_rules) {
        if (rule.cue == cue) {
            return &rule;
        }
    }
    return nullptr;
}

// state: Fixed clock/repeat history whose timestamps use admission time plus one.
// elapsed_us: Nonnegative unpaused active interval to accumulate exactly once.
// Exception: Returns invalid history or active-clock overflow without changing state.
esp_err_t advance_active_clock(PersonalityLag &state, int64_t elapsed_us)
{
    constexpr int64_t maximum_clock = std::numeric_limits<int64_t>::max() - 1;
    if (state.active_elapsed_us < 0 || state.active_elapsed_us > maximum_clock || elapsed_us < 0 ||
        elapsed_us > maximum_clock - state.active_elapsed_us) {
        ESP_LOGE(kTag, "Repeat clock rejected: clock_us=%lld delta_us=%lld (%s)",
                 static_cast<long long>(state.active_elapsed_us), static_cast<long long>(elapsed_us),
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    for (const auto timestamp : state.last_admitted_at_us) {
        if (timestamp < 0 || timestamp > state.active_elapsed_us + 1) {
            ESP_LOGE(kTag, "Repeat timestamp=%lld exceeds active clock=%lld (%s)",
                     static_cast<long long>(timestamp), static_cast<long long>(state.active_elapsed_us),
                     esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
    }
    state.active_elapsed_us += elapsed_us;
    return ESP_OK;
}

// state: One presentation plus fixed per-cue repeat timestamps.
// cue: Valid generated cue identity requested for a new admission.
bool repeat_ready(const PersonalityLag &state, PersonalityCue cue)
{
    const auto *rule = cue_rule(cue);
    if (rule == nullptr) {
        return cue == PersonalityCue::None;
    }
    const auto slot = static_cast<size_t>(cue) - 1U;
    const int64_t previous = state.last_admitted_at_us[slot];
    return previous == 0 || state.active_elapsed_us - (previous - 1) >= rule->repeat_cooldown_us;
}

// state: Channel ready to enter its latest eligible target, with no deferred event queue.
void begin_fade_in(PersonalityLag &state)
{
    state.cue = state.desired;
    state.phase = PersonalityPhase::FadeIn;
    state.phase_elapsed_us = 0;
    state.last_admitted_at_us[static_cast<size_t>(state.cue) - 1U] = state.active_elapsed_us + 1;
}

// state: Fixed single-target controller; no history of intermediate requests is retained.
// context: Explicit release policy, including wake from Sleep and canonical invalidity.
// elapsed_us: Active elapsed time only; phase entry starts at zero on its current frame.
void advance_channel(PersonalityLag &state, const PersonalityContext &context, int64_t elapsed_us)
{
    const bool semantic_sleep = context.presentation == PersonalityPresentation::Sleep &&
        state.desired == PersonalityCue::Sleep;
    const auto *desired_rule = cue_rule(state.desired);
    const auto *current_rule = cue_rule(state.cue);
    const bool interrupts = desired_rule != nullptr && desired_rule->interrupts &&
        (current_rule == nullptr || desired_rule->priority > current_rule->priority);
    const bool desired_ready = semantic_sleep || interrupts ||
        state.desired_age_us >= PersonalityConfig::stability_us;
    // Semantic Sleep and an authored higher-priority interruption release an artistic hold.
    // They retain the exact current opacity and never overlap the departing sprite.
    if ((semantic_sleep || interrupts) && state.cue != state.desired &&
        state.phase != PersonalityPhase::Hidden && state.phase != PersonalityPhase::FadeOut) {
        if (state.opacity == 0) {
            hide_current(state);
        } else {
            begin_fade_out(state);
            return;
        }
    }
    if (context.release_immediately && state.cue == PersonalityCue::Sleep &&
        state.phase != PersonalityPhase::Hidden && state.phase != PersonalityPhase::FadeOut) {
        begin_fade_out(state);
        return;
    }
    if (state.phase == PersonalityPhase::Hidden) {
        if (state.desired != PersonalityCue::None && desired_ready && repeat_ready(state, state.desired)) {
            begin_fade_in(state);
        }
        return;
    }
    const int64_t limit = state.phase == PersonalityPhase::FadeIn ? PersonalityConfig::fade_in_us :
        state.phase == PersonalityPhase::Visible ? PersonalityConfig::minimum_visible_us :
        PersonalityConfig::fade_out_us;
    state.phase_elapsed_us += std::min(elapsed_us, limit - state.phase_elapsed_us);
    if (state.phase == PersonalityPhase::FadeIn) {
        state.opacity = smooth_progress(state.phase_elapsed_us, PersonalityConfig::fade_in_us);
        if (state.phase_elapsed_us == PersonalityConfig::fade_in_us) {
            state.phase = PersonalityPhase::Visible;
            state.phase_elapsed_us = 0;
        }
    } else if (state.phase == PersonalityPhase::Visible) {
        if (state.desired != state.cue && desired_ready &&
            state.phase_elapsed_us == PersonalityConfig::minimum_visible_us) {
            begin_fade_out(state);
        }
    } else {
        state.opacity = state.fade_out_opacity *
            (1.f - smooth_progress(state.phase_elapsed_us, PersonalityConfig::fade_out_us));
        if (state.phase_elapsed_us == PersonalityConfig::fade_out_us) {
            hide_current(state);
            if (state.desired != PersonalityCue::None && desired_ready && repeat_ready(state, state.desired)) {
                begin_fade_in(state);
            }
        }
    }
}

// cue: Stable symbol identity; integer offsets are used only for flash-table traversal.
const PersonalityAssets::Sprite *find_sprite(PersonalityCue cue)
{
    if (cue == PersonalityCue::WifiConnected) {
        return &kWifiConnectedSprite;
    }
    if (cue == PersonalityCue::Search) {
        return &kSearchSprite;
    }
    if (cue == PersonalityCue::BatteryReady) {
        return &kBatteryReadySprite;
    }
    if (cue == PersonalityCue::BatteryLow) {
        return &kBatteryLowSprite;
    }
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
// top: Candidate upper edge in the same logical coordinates as the character.
// width: Positive bounded whole-pixel sprite width.
// height: Positive bounded whole-pixel sprite height.
// exclusions: Borrowed visible route rectangles.
// count: Readable rectangle count, bounded by preparation.
// bounds: Receives the admitted rectangle only when every occupied chord fits.
bool admit_bounds(float left, float top, uint16_t width, uint16_t height,
                   const DamageRect *exclusions, uint8_t count, DamageRect &bounds)
{
    if (!std::isfinite(left) || !std::isfinite(top) || left < -.5f || top < -.5f ||
        left > 466.f || top > 466.f) {
        return false;
    }
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

// cue: Typed visible identity; unknown cues remain subject to audio-owner validation.
bool personality_cue_is_silent(PersonalityCue cue)
{
    const auto *rule = cue_rule(cue);
    return rule != nullptr && !rule->sfx_enabled;
}

// cue: Stable enum whose catalog membership is requested.
bool personality_cue_valid(PersonalityCue cue)
{
    return cue == PersonalityCue::None || find_sprite(cue) != nullptr;
}

// cue: Stable enum to classify without reading any device state.
bool personality_cue_is_device(PersonalityCue cue)
{
    return cue == PersonalityCue::LowBattery || cue == PersonalityCue::Charging ||
        cue == PersonalityCue::NoWifi || cue == PersonalityCue::WifiConnected ||
        cue == PersonalityCue::BatteryReady || cue == PersonalityCue::BatteryLow;
}

// observation: Original current battery sample; absence, stale data and charging suppress the prelude.
// working: True only for the actual resumed Working route.
// delta_us: Active elapsed time, excluding hidden or unretired presentation time.
// state: Single-use display state armed by a real standby wake.
// Exception: Returns the actual timing/percentage contract error without mutation.
esp_err_t advance_wake_battery_prelude(const PersonalityPowerObservation &observation,
    bool working, int64_t delta_us, WakeBatteryPrelude &state)
{
    constexpr int64_t visible_duration_us = 4000000;
    if (delta_us < 0 || (observation.current && observation.battery_present && observation.percentage_valid &&
        (!std::isfinite(observation.level_percent) || observation.level_percent < 0 || observation.level_percent > 100))) {
        ESP_LOGE(kTag, "Wake battery prelude rejected: delta=%lld percentage=%f (%s)",
            static_cast<long long>(delta_us), observation.level_percent, esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (!working || !observation.current || !observation.battery_present ||
        !observation.percentage_valid || observation.charging) {
        state = {};
        return ESP_OK;
    }
    if (state.pending) {
        state.pending = false;
        state.active = true;
        state.retired = false;
        state.visible_us = 0;
    } else if (state.active && state.retired) {
        state.visible_us += std::min(delta_us, visible_duration_us - state.visible_us);
        if (state.visible_us == visible_duration_us) {
            state = {};
            return ESP_OK;
        }
    }
    if (state.active) {
        state.cue = observation.level_percent <= PersonalityConfig::low_battery_enter_percent
            ? PersonalityCue::BatteryLow : PersonalityCue::BatteryReady;
    }
    return ESP_OK;
}

// lag: Current presentation whose artistic repeat history must survive route/geometry resets.
void clear_personality_presentation(PersonalityLag &lag)
{
    const auto repeats = lag.last_admitted_at_us;
    const int64_t active_elapsed_us = lag.active_elapsed_us;
    lag = {};
    lag.last_admitted_at_us = repeats;
    lag.active_elapsed_us = active_elapsed_us;
}

// current_percentage: True only for a current canonical percentage on a present battery.
// percentage: Original measured value, not a substitute for an unavailable reading.
// latch: Single caller-owned threshold latch.
// alert: Receives current low-battery state on success only.
// Exception: Returns invalid authored thresholds or measured percentage without repairing either.
esp_err_t update_personality_battery_latch(bool current_percentage, float percentage,
                                         PersonalityBatteryLatch &latch, bool &alert)
{
    if (PersonalityConfig::low_battery_enter_percent >= PersonalityConfig::low_battery_exit_percent ||
        PersonalityConfig::low_battery_exit_percent > 100) {
        ESP_LOGE(kTag, "Battery thresholds rejected: enter=%u exit=%u (%s)",
                 static_cast<unsigned>(PersonalityConfig::low_battery_enter_percent),
                 static_cast<unsigned>(PersonalityConfig::low_battery_exit_percent),
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (!current_percentage) {
        latch.low_battery_active = false;
        alert = false;
        return ESP_OK;
    }
    if (!std::isfinite(percentage) || percentage < 0 || percentage > 100) {
        ESP_LOGE(kTag, "Measured battery percentage=%f rejected (%s)", percentage,
                 esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (percentage <= PersonalityConfig::low_battery_enter_percent) {
        latch.low_battery_active = true;
    } else if (percentage >= PersonalityConfig::low_battery_exit_percent) {
        latch.low_battery_active = false;
    }
    alert = latch.low_battery_active;
    return ESP_OK;
}

// observation: Current Cody battery state; unavailable numbers are never interpreted.
// latch: One existing hysteresis owner, committed together with the selected identity.
// cue: Current power symbol; connecting a charger does not claim the battery has recovered.
// Exception: Returns the original percentage/threshold error without changing either output.
esp_err_t select_personality_power_cue(const PersonalityPowerObservation &observation,
    PersonalityBatteryLatch &latch, PersonalityCue &cue)
{
    auto next = latch;
    bool alert = false;
    const bool present = observation.current && observation.battery_present;
    const esp_err_t error = update_personality_battery_latch(present && observation.percentage_valid,
        observation.level_percent, next, alert);
    if (error != ESP_OK) {
        return error;
    }
    // Charging remains truthful even when the owner cannot supply a percentage.
    cue = present && observation.charging ? PersonalityCue::Charging :
        alert ? PersonalityCue::LowBattery : PersonalityCue::None;
    latch = next;
    return ESP_OK;
}

// observation: Canonical power projection already checked by the caller.
// idle: Only an unobstructed Home pause permits a notice to appear.
// delta_us: Awake elapsed time, bounded by the existing animation owner.
// state: Caller-owned transition memory; at most one latest event is retained.
void advance_battery_notice(const PersonalityPowerObservation &observation, bool idle,
                            int64_t delta_us, BatteryNoticeState &state)
{
    const int64_t elapsed = std::max<int64_t>(0, delta_us);
    if (!observation.current || !observation.battery_present) {
        state.pending = BatteryNotice::None;
        state.active = BatteryNotice::None;
        state.idle_us = 0;
        state.visible_us = 0;
        return;
    }
    const bool full = observation.percentage_valid && observation.level_percent == 100;
    BatteryWarningBand band = BatteryWarningBand::None;
    if (observation.percentage_valid && !observation.charging && !full) {
        if (observation.level_percent <= 10) {
            band = BatteryWarningBand::Critical;
        } else if (observation.level_percent <= 20) {
            band = BatteryWarningBand::Low;
        }
    }
    state.reminder_us = std::min<int64_t>(300000000, state.reminder_us + elapsed);
    if (full && (!state.observed || !state.was_full)) {
        state.pending = BatteryNotice::Full;
    } else if (observation.charging && (!state.observed || !state.was_charging)) {
        state.pending = BatteryNotice::Charging;
    } else if (band > state.warned_band ||
               (band != BatteryWarningBand::None && state.reminder_us >=
                (band == BatteryWarningBand::Critical ? 60000000 : 300000000))) {
        state.pending = band == BatteryWarningBand::Critical ? BatteryNotice::Critical : BatteryNotice::Low;
    }
    // Hysteresis re-arms warnings after recovery, without clamping a measurement.
    if (observation.charging || (observation.percentage_valid && observation.level_percent >= 25)) {
        state.warned_band = BatteryWarningBand::None;
    } else if (observation.percentage_valid && observation.level_percent >= 15 &&
               state.warned_band == BatteryWarningBand::Critical) {
        state.warned_band = BatteryWarningBand::Low;
    }
    state.observed = true;
    state.was_charging = observation.charging;
    state.was_full = full;
    // notice: Pending or active event whose original device condition must still hold.
    const auto supported = [&](BatteryNotice notice) {
        switch (notice) {
        case BatteryNotice::Low: return band == BatteryWarningBand::Low;
        case BatteryNotice::Critical: return band == BatteryWarningBand::Critical;
        case BatteryNotice::Charging: return observation.charging && !full;
        case BatteryNotice::Full: return full;
        case BatteryNotice::None: return true;
        }
        return false;
    };
    if (!supported(state.pending)) {
        state.pending = BatteryNotice::None;
    }
    if (!supported(state.active)) {
        state.active = BatteryNotice::None;
        state.visible_us = 0;
    }
    if (!idle) {
        // A real conversation or touch retires the notice; it never replaces that route.
        if (state.active != BatteryNotice::None && state.pending == BatteryNotice::None) {
            state.pending = state.active;
        }
        state.active = BatteryNotice::None;
        state.visible_us = 0;
        state.idle_us = 0;
        return;
    }
    state.idle_us = std::min<int64_t>(10000000, state.idle_us + elapsed);
    if (state.active != BatteryNotice::None) {
        state.visible_us += elapsed;
        if (state.visible_us >= 4000000) {
            state.active = BatteryNotice::None;
            state.visible_us = 0;
        }
    } else if (state.idle_us >= 10000000 && state.pending != BatteryNotice::None) {
        state.active = state.pending;
        state.pending = BatteryNotice::None;
        state.visible_us = 0;
        state.reminder_us = 0;
        state.warned_band = band;
    }
}

// notice: Valid domain identity; no percentage or electrical status is exposed in prose.
const char *battery_notice_text(BatteryNotice notice)
{
    switch (notice) {
    case BatteryNotice::Low: return "A little tired. Charge me?";
    case BatteryNotice::Critical: return "Please charge me soon.";
    case BatteryNotice::Charging: return "Thanks! Recharging.";
    case BatteryNotice::Full: return "All charged. Happy!";
    case BatteryNotice::None: return "";
    }
    return "";
}

// notice: Power event reusing the stable LowBattery, Charging and Joy sprite identities.
PersonalityCue battery_notice_cue(BatteryNotice notice)
{
    switch (notice) {
    case BatteryNotice::Low:
    case BatteryNotice::Critical: return PersonalityCue::LowBattery;
    case BatteryNotice::Charging: return PersonalityCue::Charging;
    case BatteryNotice::Full: return PersonalityCue::Joy;
    case BatteryNotice::None: return PersonalityCue::None;
    }
    return PersonalityCue::None;
}

// admitted_request: Canonical-admitted baseline; this helper never samples a driver or snapshot.
// semantic_sleep: Authored Sleep presentation, above device and artistic selections.
// affection_active: Current automatic caress lifetime, independent of an explicit Affection request.
// desired: Receives the one selected target on success.
// release_affection: Receives true when the auto request must stop requesting its heart.
// eye_reaction: Latest acquired eye-contact symbol; only None, Fear and Frustration are accepted.
// Exception: Returns invalid cue without changing outputs and logs the actual rejected identity.
esp_err_t select_personality_target(PersonalityCue admitted_request, bool semantic_sleep,
                                   bool affection_active, PersonalityCue &desired, bool &release_affection,
                                   PersonalityCue eye_reaction)
{
    if (!personality_cue_valid(admitted_request) ||
        (eye_reaction != PersonalityCue::None && eye_reaction != PersonalityCue::Fear &&
         eye_reaction != PersonalityCue::Frustration)) {
        ESP_LOGE(kTag, "Target selection cue=%u eye=%u rejected (%s)",
                 static_cast<unsigned>(admitted_request), static_cast<unsigned>(eye_reaction),
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    PersonalityCue selected = admitted_request;
    bool release = false;
    if (semantic_sleep) {
        selected = PersonalityCue::Sleep;
        release = true;
    } else {
        const auto *baseline_rule = cue_rule(admitted_request);
        const auto *affection_rule = cue_rule(PersonalityCue::Affection);
        if (affection_rule == nullptr || (admitted_request != PersonalityCue::None && baseline_rule == nullptr)) {
            ESP_LOGE(kTag, "Target selection authored rule missing: cue=%u (%s)",
                     static_cast<unsigned>(admitted_request), esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        uint16_t baseline_priority = baseline_rule == nullptr ? 0 : baseline_rule->priority;
        const auto *eye_rule = cue_rule(eye_reaction);
        if (eye_reaction != PersonalityCue::None && eye_rule == nullptr) {
            ESP_LOGE(kTag, "Eye reaction authored rule missing (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        if (eye_rule != nullptr && eye_rule->priority > baseline_priority) {
            selected = eye_reaction;
            baseline_priority = eye_rule->priority;
        }
        if (affection_active && affection_rule->priority > baseline_priority) {
            selected = PersonalityCue::Affection;
        } else if (selected != PersonalityCue::None) {
            release = true;
        }
    }
    desired = selected;
    release_affection = release;
    return ESP_OK;
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

// scene: Evaluated pose with finite face and silhouette geometry.
// cue: Latest narrative or canonically admitted device target.
// size: Nominal presentation identity, independent of scene permission.
// context: Scene permission, authored layout/scale, orientation and release policy.
// active_delta_us: Nonnegative elapsed active animation time only.
// paused: Freeze channel and following; updates to the desired identity remain accepted.
// reset: Re-prime geometric following without restarting the current channel phase.
// exclusions: Permanently reserved chrome/navigation rectangles.
// exclusion_count: Number of readable exclusions, at most six.
// lag: Fixed caller-owned controller and geometric state, committed on success.
// frame: Receives an admitted sprite or an explicit ordinary admission outcome.
// Exception: Returns malformed pose/context/filter inputs with the original cause logged.
esp_err_t prepare_personality_frame(
    const RigScene &scene, PersonalityCue cue, CodySize size, const PersonalityContext &context,
    int64_t active_delta_us, bool paused, bool reset,
    const DamageRect *exclusions, uint8_t exclusion_count,
    PersonalityLag &lag, PersonalityFrame &frame)
{
    frame = {};
    if (!scene.valid || !std::isfinite(scene.scale) || scene.scale <= 0 ||
        !std::isfinite(context.nominal_scale) || context.nominal_scale < 20 || context.nominal_scale > 200 ||
        !personality_cue_valid(cue) || active_delta_us < 0 || context.rotation % 90 != 0 ||
        context.rotation > 270 || exclusion_count > 6 ||
        (exclusion_count != 0 && exclusions == nullptr) || size < CodySize::Micro || size > CodySize::Full ||
        context.presentation > PersonalityPresentation::Sleep || context.layout > PersonalityLayout::HomeDashboard) {
        ESP_LOGE(kTag, "Prepare rejected: valid=%u scale=%f nominal=%f cue=%u size=%u delta=%lld rotation=%u exclusions=%u presentation=%u layout=%u (%s)",
            scene.valid, scene.scale, context.nominal_scale, static_cast<unsigned>(cue), static_cast<unsigned>(size),
            static_cast<long long>(active_delta_us), context.rotation, exclusion_count,
            static_cast<unsigned>(context.presentation), static_cast<unsigned>(context.layout),
            esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (context.presentation == PersonalityPresentation::Disabled || size == CodySize::Micro) {
        PersonalityLag cleared = lag;
        const esp_err_t clock_error = advance_active_clock(cleared, paused ? 0 : active_delta_us);
        if (clock_error != ESP_OK) {
            return clock_error;
        }
        clear_personality_presentation(cleared);
        lag = cleared;
        return ESP_OK;
    }
    const auto nose = scene.world[to_index(PointId::Nose)];
    const auto upper_left = scene.world[to_index(PointId::LobeUpperLeft)];
    const auto upper_right = scene.world[to_index(PointId::LobeUpperRight)];
    if (!std::isfinite(nose.x) || !std::isfinite(nose.y) ||
        !std::isfinite(upper_left.x) || !std::isfinite(upper_left.y) ||
        !std::isfinite(upper_right.x) || !std::isfinite(upper_right.y)) {
        ESP_LOGE(kTag, "Nonfinite face/body anchor rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
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
        const float vertex_y = scene.vertices[body.first_vertex + offset].y;
        if (!std::isfinite(vertex_y)) {
            ESP_LOGE(kTag, "Nonfinite body vertex rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        body_top = std::min(body_top, vertex_y);
    }
#endif
    const auto &profile = context.presentation == PersonalityPresentation::Sleep ?
        PersonalityConfig::sleep : PersonalityConfig::main;
    const float extent = std::clamp(context.nominal_scale * profile.extent_per_scale,
        PersonalityConfig::minimum_extent, PersonalityConfig::maximum_base_extent);
    const float rest_span = (Data::points[to_index(PointId::LobeUpperRight)].x -
        Data::points[to_index(PointId::LobeUpperLeft)].x) * scene.scale;
    if (!std::isfinite(rest_span) || rest_span <= 0 || !std::isfinite(profile.offset_x) ||
        !std::isfinite(profile.offset_y) || !std::isfinite(profile.extent_per_scale) || profile.extent_per_scale <= 0 ||
        !std::isfinite(profile.pixel_offset_x) || std::abs(profile.pixel_offset_x) > 466 ||
        !std::isfinite(profile.pixel_offset_y) || std::abs(profile.pixel_offset_y) > 466 ||
        !std::isfinite(profile.size_multiplier) || profile.size_multiplier < 1 || profile.size_multiplier > 3.6f) {
        ESP_LOGE(kTag, "Anchor profile/rest span rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const float deformation_limit = extent * PersonalityConfig::maximum_deformation_pixels /
        PersonalityConfig::maximum_base_extent;
    const float deformation = std::clamp(((upper_right.x - upper_left.x) / rest_span - 1.f) * extent,
        -deformation_limit, deformation_limit);
    const float target_width = (extent + deformation) * profile.size_multiplier;
    const float target_height = (extent - deformation) * profile.size_multiplier;
    // Pixel translation is independent of character scale. Enlarging around the
    // center preserves the exact requested movement of that center on every pose.
    const float target_x = nose.x + profile.offset_x * scene.scale + profile.pixel_offset_x;
    // The compact Home slot needs twelve authored pixels below the status strip.
    // This translates every cue equally; scale, collision admission and other layouts stay exact.
    const float home_offset_y = context.layout == PersonalityLayout::HomeDashboard ? 12.f : 0.f;
    const float target_y = nose.y + profile.offset_y * scene.scale + profile.pixel_offset_y + home_offset_y;
    if (!std::isfinite(target_x) || !std::isfinite(target_y) || !std::isfinite(body_top) ||
        !std::isfinite(target_x * PersonalityConfig::scale_window_us) ||
        !std::isfinite(target_y * PersonalityConfig::scale_window_us) ||
        (lag.primed && (!std::isfinite(lag.center_x) || !std::isfinite(lag.center_y) ||
         !std::isfinite(lag.width) || !std::isfinite(lag.height) || lag.width < 1 || lag.height < 1 ||
         lag.width > kMaximumSpriteExtent || lag.height > kMaximumSpriteExtent || !follow_history_valid(lag))) ||
        !std::isfinite(lag.opacity) || lag.opacity < 0 || lag.opacity > 1 ||
        !std::isfinite(lag.fade_out_opacity) || lag.fade_out_opacity < 0 || lag.fade_out_opacity > 1 ||
        !personality_cue_valid(lag.cue) || !personality_cue_valid(lag.desired) ||
        lag.desired_age_us < 0 || lag.desired_age_us > PersonalityConfig::stability_us ||
        lag.phase_elapsed_us < 0 || lag.phase > PersonalityPhase::FadeOut ||
        lag.layout > PersonalityLayout::HomeDashboard) {
        ESP_LOGE(kTag, "Anchor/controller state rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const int64_t phase_limit = lag.phase == PersonalityPhase::FadeIn ? PersonalityConfig::fade_in_us :
        lag.phase == PersonalityPhase::Visible ? PersonalityConfig::minimum_visible_us :
        lag.phase == PersonalityPhase::FadeOut ? PersonalityConfig::fade_out_us : 0;
    if (lag.phase_elapsed_us > phase_limit) {
        ESP_LOGE(kTag, "Channel phase elapsed=%lld rejected (%s)",
            static_cast<long long>(lag.phase_elapsed_us), esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    PersonalityLag next = lag;
    const int64_t elapsed_us = paused ? 0 : active_delta_us;
    const esp_err_t clock_error = advance_active_clock(next, elapsed_us);
    if (clock_error != ESP_OK) {
        return clock_error;
    }
    if (next.desired != cue) {
        next.desired = cue;
        next.desired_age_us = 0;
    } else {
        next.desired_age_us += std::min(elapsed_us, PersonalityConfig::stability_us - next.desired_age_us);
    }
    if (context.invalidate_current) {
        // An invalid canonical fact cannot remain visible for an artistic minimum hold.
        hide_current(next);
    }
    // A changed reserved layout cannot inherit a center that collides with its chrome,
    // even when the new frame is presented while animation time is paused.
    const bool prime = !next.primed || next.layout != context.layout || (!paused &&
        (reset || next.size != size || next.route != context.route || next.rotation != context.rotation));
    const PersonalityFollowTarget target{target_x, target_y, target_width, target_height};
    if (prime) {
        prime_follow(next, target);
    } else if (elapsed_us > 0) {
        advance_follow(next, target, elapsed_us);
    }
    next.size = size;
    next.layout = context.layout;
    next.route = context.route;
    next.rotation = context.rotation;
    next.primed = true;
    // No relocation or adaptive shrinking: the authored center and size are the contract.
    const uint16_t width = static_cast<uint16_t>(std::round(next.width));
    const uint16_t height = static_cast<uint16_t>(std::round(next.height));
    if (width == 0 || height == 0 || width > kMaximumSpriteExtent || height > kMaximumSpriteExtent) {
        ESP_LOGE(kTag, "Filtered sprite extent=%ux%u rejected (%s)", width, height, esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (next.desired == PersonalityCue::None && next.phase == PersonalityPhase::Hidden) {
        lag = next;
        return ESP_OK;
    }
    DamageRect admitted{};
    const float left = next.center_x - width * .5f;
    const float top = next.center_y - height * .5f;
    const bool overlaps_body = std::round(top) + height + PersonalityConfig::body_clearance_pixels > body_top;
    // Main/Sleep explicitly permit the icon over the upper body. Circle and
    // navigation/status exclusions still apply to the full enlarged rectangle.
    if ((!profile.allow_body_overlap && overlaps_body) ||
        !admit_bounds(left, top, width, height, exclusions, exclusion_count, admitted)) {
        // Layout admission is an expected presentation outcome, not a failed component operation.
        hide_current(next);
        frame.cue = next.desired;
        frame.status = PersonalityPresentationStatus::BlockedByLayout;
        lag = next;
        return ESP_OK;
    }
    if (!paused) {
        advance_channel(next, context, elapsed_us);
    }
    frame.opacity = static_cast<uint8_t>(std::round(next.opacity * 255.f));
    if (frame.opacity != 0) {
        frame.cue = next.cue;
        frame.bounds = admitted;
        frame.status = PersonalityPresentationStatus::Visible;
        if (next.cue == PersonalityCue::Search) {
            frame.animation_phase = static_cast<uint8_t>((next.active_elapsed_us / 125000) % 8);
        }
    } else if (next.phase == PersonalityPhase::Hidden && next.desired != PersonalityCue::None &&
               !repeat_ready(next, next.desired)) {
        frame.cue = next.desired;
        frame.status = PersonalityPresentationStatus::SuppressedByCooldown;
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
    if (frame.bounds.empty() || frame.opacity == 0) {
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
    // Flash descriptors are immutable; load their geometry before destination
    // stores, which could otherwise conservatively alias the uint16_t fields.
    const int inset = sprite->stored_inset;
    const int stored_width = sprite->width - 2 * inset;
    const int stored_right = sprite->width - inset;
    const int stored_bottom = sprite->height - inset;
    for (uint16_t y = top; y < bottom; ++y) {
        const uint16_t source_y = (y - bounds.top) * sprite->height / height;
        auto *destination = strip.pixels + (y - first_row) * strip.stride;
        for (uint16_t x = left; x < right; ++x) {
            int source_x = (x - bounds.left) * sprite->width / width;
            int animated_y = source_y;
            if (frame.cue == PersonalityCue::Search) {
                source_x -= kSearchOffsetX[frame.animation_phase % 8];
                animated_y -= kSearchOffsetY[frame.animation_phase % 8];
                if (source_x < 0 || source_x >= sprite->width || animated_y < 0 || animated_y >= sprite->height) {
                    continue;
                }
            }
            // Logical dimensions include the generator-verified transparent
            // border. Native procedural symbols retain their uncropped planes.
            if (source_x < inset || animated_y < inset ||
                source_x >= stored_right || animated_y >= stored_bottom) {
                continue;
            }
            const uint32_t source = (animated_y - inset) * stored_width + source_x - inset;
            const uint8_t coverage = (static_cast<uint16_t>(sprite->coverage[source]) * frame.opacity + 127) / 255;
            if (coverage == 0) {
                continue;
            }
            const uint16_t foreground = frame.cue == PersonalityCue::BatteryLow ? 0xF986 :
                frame.cue == PersonalityCue::BatteryReady ? 0x07E0 :
                frame.cue == PersonalityCue::WifiConnected || frame.cue == PersonalityCue::Search
                    ? 0x5EDD : PersonalityAssets::palette[sprite->indices[source]];
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
    return previous.cue == current.cue && previous.opacity == current.opacity &&
        previous.status == current.status && previous.animation_phase == current.animation_phase &&
        previous.bounds.left == current.bounds.left &&
        previous.bounds.top == current.bounds.top && previous.bounds.right == current.bounds.right &&
        previous.bounds.bottom == current.bounds.bottom;
}
} // namespace CODY
