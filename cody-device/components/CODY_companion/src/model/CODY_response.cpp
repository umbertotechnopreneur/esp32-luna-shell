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

#include "CODY_response.hpp"

#include <cmath>
#include <algorithm>
#include <initializer_list>
#include "cloud_rig_data.hpp"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_response";
constexpr float kTau = 6.2831853071795864769f;
constexpr Axis kAxes[] = {Axis::X, Axis::Y, Axis::Z};

struct CheckedParameter {
    const char *name;
    float value;
    float minimum;
    float maximum;
};

// name: Rejected input or state field, retained in the component log.
// value: Actual rejected scalar.
esp_err_t reject_value(const char *name, double value)
{
    ESP_LOGE(kTag, "%s rejected: %.9g (%s)", name, value,
             esp_err_to_name(ESP_ERR_INVALID_ARG));
    return ESP_ERR_INVALID_ARG;
}

// axis: Semantic board/panel axis, never an authored list position.
const char *axis_name(Axis axis)
{
    switch (axis) {
    case Axis::X:
        return "x";
    case Axis::Y:
        return "y";
    case Axis::Z:
        return "z";
    default:
        return "invalid-axis";
    }
}

// field: Rejected motion/state quantity.
// axis: Named axis of that quantity.
// value: Actual rejected value retained in the component log.
esp_err_t reject_axis_value(const char *field, Axis axis, float value)
{
    ESP_LOGE(kTag, "%s.%s rejected: %.9g (%s)", field, axis_name(axis),
             static_cast<double>(value), esp_err_to_name(ESP_ERR_INVALID_ARG));
    return ESP_ERR_INVALID_ARG;
}

// origin: Classified acquisition source at the active platform's input boundary.
// No exceptions: Firmware never admits the simulator-only origin.
constexpr bool response_origin_supported(InputOrigin origin)
{
    if (origin == InputOrigin::Hardware) {
        return true;
    }
#if defined(CODY_HOST_SIMULATOR) && CODY_HOST_SIMULATOR
    return origin == InputOrigin::Simulator;
#else
    return false;
#endif
}

// stamp: Original status and timestamps, explicitly labelled synthetic on the PC.
// now_us: Monotonic time at which this input is consumed.
// timeout_seconds: Reviewed maximum lifetime for the source.
// source: Stable identity named in rejected-metadata logs.
// effective: Receives the aged status without changing the original error.
esp_err_t effective_stamp(const SampleStamp &stamp, int64_t now_us,
                          float timeout_seconds, const char *source,
                          SampleStamp &effective)
{
    if (!response_origin_supported(stamp.origin) || stamp.sampled_at_us < 0 ||
        stamp.sampled_at_us > now_us ||
        (stamp.health != SampleHealth::Unavailable && stamp.health != SampleHealth::Current &&
         stamp.health != SampleHealth::Stale && stamp.health != SampleHealth::Error)) {
        ESP_LOGE(kTag, "%s metadata rejected: origin=%u health=%u sample=%lldus now=%lldus (%s)",
                 source, static_cast<unsigned>(to_index(stamp.origin)),
                 static_cast<unsigned>(to_index(stamp.health)),
                 static_cast<long long>(stamp.sampled_at_us), static_cast<long long>(now_us),
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if ((stamp.health == SampleHealth::Error && stamp.error == ESP_OK) ||
        (stamp.health == SampleHealth::Current &&
         (stamp.error != ESP_OK || stamp.sampled_at_us == 0))) {
        return reject_value(source, stamp.error);
    }
    effective = stamp;
    if (stamp.health == SampleHealth::Current &&
        static_cast<double>(now_us - stamp.sampled_at_us) >= timeout_seconds * 1000000.0) {
        effective.health = SampleHealth::Stale;
    }
    return ESP_OK;
}

// state: One independent body's spring displacement and velocity.
// target: Bounded artistic equilibrium for this derived motion cue.
// seconds: Active elapsed time, integrated analytically with bounded work per frame.
// frequency_hz: Authored natural spring frequency.
// damping_ratio: Authored damping ratio; all three damping cases are supported.
SpringState advance_spring(SpringState state, float target, float seconds,
                           float frequency_hz, float damping_ratio)
{
    if (seconds == 0) {
        return state;
    }
    // Double intermediates keep near-critical damping and long frames well behaved.
    const double omega = kTau * static_cast<double>(frequency_hz);
    const double displacement = state.position - target;
    const double velocity = state.velocity;
    const double decay_rate = damping_ratio * omega;
    double position;
    double speed;
    if (damping_ratio < 1) {
        const double damped_frequency = omega * std::sqrt(1.0 - damping_ratio * static_cast<double>(damping_ratio));
        const double decay = std::exp(-decay_rate * seconds);
        const double cosine = std::cos(damped_frequency * seconds);
        const double sine = std::sin(damped_frequency * seconds);
        const double companion = (velocity + decay_rate * displacement) / damped_frequency;
        position = target + decay * (displacement * cosine + companion * sine);
        speed = decay * (velocity * cosine - (decay_rate * velocity + omega * omega * displacement) / damped_frequency * sine);
    } else if (damping_ratio == 1) {
        const double decay = std::exp(-omega * seconds);
        const double companion = velocity + omega * displacement;
        position = target + decay * (displacement + companion * seconds);
        speed = decay * (velocity - omega * companion * seconds);
    } else {
        const double root = std::sqrt(damping_ratio * static_cast<double>(damping_ratio) - 1.0);
        const double slow_rate = -omega / (damping_ratio + root);
        const double fast_rate = -omega * (damping_ratio + root);
        const double slow_weight = (velocity - fast_rate * displacement) / (slow_rate - fast_rate);
        const double fast_weight = displacement - slow_weight;
        const double slow_decay = std::exp(slow_rate * seconds);
        const double fast_decay = std::exp(fast_rate * seconds);
        position = target + slow_weight * slow_decay + fast_weight * fast_decay;
        speed = slow_rate * slow_weight * slow_decay + fast_rate * fast_weight * fast_decay;
    }
    return {static_cast<float>(position), static_cast<float>(speed)};
}

// value: Integrated artistic clock before reduction to one cycle.
// period: Positive cycle length; the representational endpoint wraps to zero.
float wrap_cycle(double value, float period)
{
    const float phase = static_cast<float>(std::fmod(value, static_cast<double>(period)));
    // A double remainder just below the endpoint can round to it as a float.
    return phase >= period ? 0 : phase;
}

// previous: Previous normalized artistic state.
// target: Requested normalized artistic state.
// seconds: Active elapsed time.
// attack: Time constant when the target increases.
// release: Time constant when the target decreases.
float blend_state(float previous, float target, float seconds, float attack, float release)
{
    const float time_constant = target > previous ? attack : release;
    return previous + (target - previous) * -std::expm1(-seconds / time_constant);
}

// value: Desired artistic excursion, not measured telemetry.
// limit: Reviewed output bound; zero disables the effect.
float soft_limit(float value, float limit)
{
    return limit > 0 ? limit * std::tanh(value / limit) : 0;
}

// vector: Physical panel-frame vector, independent of a pixel origin.
// rotation: Clockwise cardinal display orientation; the display will rotate artwork once.
CODY2D::Vec2 logical_vector(CODY2D::Vec2 vector, uint16_t rotation)
{
    switch (rotation) {
    case 90:
        return {vector.y, -vector.x};
    case 180:
        return {-vector.x, -vector.y};
    case 270:
        return {-vector.y, vector.x};
    default:
        // The public boundary rejects unsupported rotations before reaching this helper.
        return vector;
    }
}

// state: Transaction-local PRNG, seeded once by the runtime rather than per frame.
uint32_t next_idle_random(ResponseState &state)
{
    uint32_t value = state.idle_random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    state.idle_random_state = value;
    return value;
}

// state: Session shuffle bag; all three named scenes occur before another bag starts.
// random: One random draw, consumed only when a scene is admitted.
IdleSceneId select_idle_scene(ResponseState &state, uint32_t random)
{
    constexpr struct { IdleSceneId scene; uint8_t bit; } scenes[] = {
        {IdleSceneId::Yawn, 1}, {IdleSceneId::Whistle, 2}, {IdleSceneId::Greeting, 4}
    };
    if (state.idle_remaining_scenes == 0) {
        state.idle_remaining_scenes = 7;
    }
    uint32_t available = 0;
    for (const auto &entry : scenes) {
        if ((state.idle_remaining_scenes & entry.bit) != 0 && entry.scene != state.last_idle_scene) {
            ++available;
        }
    }
    // A valid bag never leaves only the scene that was just consumed.
    if (available == 0) {
        return IdleSceneId::None;
    }
    uint32_t choice = random % available;
    for (const auto &entry : scenes) {
        if ((state.idle_remaining_scenes & entry.bit) != 0 && entry.scene != state.last_idle_scene) {
            if (choice == 0) {
                state.idle_remaining_scenes &= static_cast<uint8_t>(~entry.bit);
                return entry.scene;
            }
            --choice;
        }
    }
    return IdleSceneId::None;
}

// config: Authored delay/duration/cooldown shared by the three idle presentations.
// busy: Route, interaction, microphone, stress or unrelated playback takes priority.
// seconds: Active elapsed time; paused/parked clocks cannot admit a scene.
// next: Transaction-local state; no route, setting or inactivity timer is changed.
void advance_idle_scene(const ResponseConfig &config, bool busy, float seconds, ResponseState &next)
{
    if (busy) {
        if (next.idle_scene != IdleSceneId::None) {
            next.yawn_cooldown_seconds = config.idle.cooldown_seconds;
        }
        next.idle_scene = IdleSceneId::None;
        next.idle_seconds = 0;
        next.idle_scene_seconds = 0;
        next.idle_scene_weight = 0;
        next.yawning = false;
        next.yawn_seconds = 0;
        next.yawn_weight = 0;
        return;
    }
    if (seconds == 0) {
        return;
    }
    next.yawn_cooldown_seconds = std::max(0.f, next.yawn_cooldown_seconds - seconds);
    if (next.idle_scene == IdleSceneId::None) {
        // Wait 1-2 authored intervals after the cooldown, rather than accumulating
        // eligibility while another scene or the device's standby policy owns time.
        if (next.yawn_cooldown_seconds > 0) {
            return;
        }
        if (next.idle_seconds == 0) {
            next.idle_wait_fraction = static_cast<float>(next_idle_random(next) & 0xFFFFU) / 65535.f;
        }
        const float delay = config.idle.after_seconds * (1 + next.idle_wait_fraction);
        next.idle_seconds = std::min(delay, next.idle_seconds + seconds);
        if (next.idle_seconds < delay) {
            return;
        }
        const uint32_t random = next_idle_random(next);
        next.idle_scene = select_idle_scene(next, random);
        next.last_idle_scene = next.idle_scene;
        ++next.idle_scene_generation;
        next.idle_scene_seconds = 0;
        next.idle_scene_weight = 0;
        return; // The entrance owns a complete new active-time interval.
    }
    next.idle_scene_seconds += seconds;
    if (next.idle_scene_seconds >= config.idle.duration_seconds) {
        next.idle_scene = IdleSceneId::None;
        next.idle_seconds = 0;
        next.idle_scene_seconds = 0;
        next.idle_scene_weight = 0;
        next.yawn_cooldown_seconds = config.idle.cooldown_seconds;
    } else {
        const float shape = std::sin(kTau * .5f * next.idle_scene_seconds / config.idle.duration_seconds);
        next.idle_scene_weight = shape * shape;
    }
    next.yawning = next.idle_scene == IdleSceneId::Yawn;
    next.yawn_seconds = next.yawning ? next.idle_scene_seconds : 0;
    next.yawn_weight = next.yawning ? next.idle_scene_weight : 0;
}

// config: Authored audio/idle gains, validated with the rest of the skin.
// previous: Previous committed targets and consumed publication timestamps.
// inputs: One canonical observation bundle; current PCM levels are never guessed.
// seconds: Active animation time, excluding standby.
// now_us: Current observation clock used for freshness and interaction timestamps.
// next: Transaction-local next model, committed by the public boundary on success.
// Exception: Returns actual observation, level or timestamp rejection errors.
esp_err_t advance_audio_and_idle(const ResponseConfig &config, const ResponseState &previous,
    const ResponseInputs &inputs, float seconds, int64_t now_us, ResponseState &next)
{
    esp_err_t error = effective_stamp(inputs.microphone.stamp, now_us,
        config.audio.sample_timeout_seconds, "audio.microphone", next.microphone_status);
    if (error != ESP_OK) {
        return error;
    }
    error = effective_stamp(inputs.playback.stamp, now_us,
        config.audio.sample_timeout_seconds, "audio.playback", next.playback_status);
    if (error != ESP_OK) {
        return error;
    }
    if (next.microphone_status.health == SampleHealth::Current) {
        const auto &microphone = inputs.microphone;
        if (microphone.channel_count == 0 || microphone.channel_count > 2) {
            return reject_value("audio.microphone channel_count", microphone.channel_count);
        }
        uint32_t sum = 0;
        for (uint8_t channel = 0; channel < microphone.channel_count; ++channel) {
            if (microphone.peak_level[channel] > 32768U ||
                microphone.mean_level[channel] > microphone.peak_level[channel]) {
                return reject_value("audio.microphone PCM level", microphone.mean_level[channel]);
            }
            sum += microphone.mean_level[channel];
        }
        if (microphone.stamp.sampled_at_us < previous.consumed_microphone_at_us) {
            return reject_value("audio.microphone reversed timestamp", static_cast<double>(microphone.stamp.sampled_at_us));
        }
        if (microphone.stamp.sampled_at_us > previous.consumed_microphone_at_us) {
            const float normalized = sum / (32768.f * microphone.channel_count);
            next.microphone_target = -std::expm1(-normalized / config.audio.microphone_reference);
            next.consumed_microphone_at_us = microphone.stamp.sampled_at_us;
        }
    } else {
        next.microphone_target = 0; // Decay an artistic contribution; do not invent a silent sample.
    }
    if (next.playback_status.health == SampleHealth::Current) {
        const auto &playback = inputs.playback;
        if (playback.mean_level > 32768U) {
            return reject_value("audio.playback PCM level", playback.mean_level);
        }
        if (playback.stamp.sampled_at_us < previous.consumed_playback_at_us) {
            return reject_value("audio.playback reversed timestamp", static_cast<double>(playback.stamp.sampled_at_us));
        }
        if (playback.stamp.sampled_at_us > previous.consumed_playback_at_us) {
            next.playback_target = playback.active
                ? -std::expm1(-static_cast<float>(playback.mean_level) / (32768.f * config.audio.playback_reference)) : 0;
            next.consumed_playback_at_us = playback.stamp.sampled_at_us;
        }
    } else {
        next.playback_target = 0;
    }
    const bool playing = next.playback_status.health == SampleHealth::Current && inputs.playback.active;
    // Playback may be heard by the microphones. Keep those measurements intact,
    // but avoid treating our own cue as an independent attention stimulus.
    const bool own_whistle = playing &&
        inputs.playback.cue_id == static_cast<uint32_t>(IdleSceneId::Whistle);
    const float attention_target = playing ? 0 : next.microphone_target;
    next.attention = blend_state(previous.attention, attention_target, seconds,
                                 config.audio.attack_seconds, config.audio.release_seconds);
    next.speaking = blend_state(previous.speaking, own_whistle ? 0 : next.playback_target, seconds,
                                config.audio.attack_seconds, config.audio.release_seconds);
    if (inputs.last_interaction_at_us < previous.consumed_interaction_at_us ||
        inputs.last_interaction_at_us > now_us) {
        return reject_value("interaction timestamp", static_cast<double>(inputs.last_interaction_at_us));
    }
    const bool interaction = inputs.last_interaction_at_us > previous.consumed_interaction_at_us;
    next.consumed_interaction_at_us = inputs.last_interaction_at_us;
    const bool touching = next.body_touch_status.health == SampleHealth::Current && inputs.body_touch.pressed;
    const bool busy = !inputs.idle_scene_allowed || interaction || touching ||
        next.stress >= config.idle.activity_stress_threshold ||
        (!own_whistle && (next.microphone_target >= config.idle.audio_activity_threshold || playing));
    advance_idle_scene(config, busy, seconds, next);
    return ESP_OK;
}

// state: Previously committed state to inspect before it can affect geometry.
// Exception: Returns a logged invalid scalar, identity or dynamics error without repair.
esp_err_t validate_response_state(const ResponseState &state)
{
    const CheckedParameter scalars[] = {
        {"state.stress", state.stress, 0, 1},
        {"state.fatigue", state.fatigue, 0, 1},
        {"state.fatigue_target", state.fatigue_target, 0, 1},
        {"state.breath_phase", state.breath_phase, 0, kTau},
        {"state.breath_rate_hz", state.breath_rate_hz, 0, 3},
        {"state.blink_elapsed_ms", state.blink_elapsed_ms, 0, 4800},
        {"state.attention", state.attention, 0, 1},
        {"state.speaking", state.speaking, 0, 1},
        {"state.microphone_target", state.microphone_target, 0, 1},
        {"state.playback_target", state.playback_target, 0, 1},
        {"state.idle_seconds", state.idle_seconds, 0, 1200},
        {"state.idle_wait_fraction", state.idle_wait_fraction, 0, 1},
        {"state.idle_scene_seconds", state.idle_scene_seconds, 0, 10},
        {"state.idle_scene_weight", state.idle_scene_weight, 0, 1},
        {"state.yawn_seconds", state.yawn_seconds, 0, 10},
        {"state.yawn_cooldown_seconds", state.yawn_cooldown_seconds, 0, 600},
        {"state.yawn_weight", state.yawn_weight, 0, 1},
        {"state.eye_hold_seconds", state.eye_hold_seconds, 0, 3},
        {"state.protective_eye_closure", state.protective_eye_closure, 0, 1},
        {"state.annoyance", state.annoyance, 0, 1},
    };
    for (const auto identity : {state.idle_scene, state.last_idle_scene}) {
        if (identity != IdleSceneId::None && identity != IdleSceneId::Yawn &&
            identity != IdleSceneId::Whistle && identity != IdleSceneId::Greeting) {
            return reject_value("state.idle_scene identity", static_cast<uint32_t>(identity));
        }
    }
    if (state.idle_random_state == 0) {
        return reject_value("state.idle_random_state", state.idle_random_state);
    }
    if (state.idle_remaining_scenes > 7 ||
        (state.last_idle_scene == IdleSceneId::Yawn && (state.idle_remaining_scenes & 1U) != 0) ||
        (state.last_idle_scene == IdleSceneId::Whistle && (state.idle_remaining_scenes & 2U) != 0) ||
        (state.last_idle_scene == IdleSceneId::Greeting && (state.idle_remaining_scenes & 4U) != 0)) {
        return reject_value("state.idle_remaining_scenes", state.idle_remaining_scenes);
    }
    for (const auto &parameter : scalars) {
        if (!std::isfinite(parameter.value) || parameter.value < parameter.minimum ||
            parameter.value > parameter.maximum) {
            return reject_value(parameter.name, parameter.value);
        }
    }
    for (const auto axis : kAxes) {
        const auto &spring = state.body[to_index(axis)];
        if (!std::isfinite(spring.position)) {
            return reject_axis_value("state.position", axis, spring.position);
        }
        if (!std::isfinite(spring.velocity)) {
            return reject_axis_value("state.velocity", axis, spring.velocity);
        }
        if (!std::isfinite(state.baseline_mps2[axis])) {
            return reject_axis_value("state.baseline", axis, state.baseline_mps2[axis]);
        }
        if (!std::isfinite(state.transient_mps2[axis])) {
            return reject_axis_value("state.transient", axis, state.transient_mps2[axis]);
        }
    }
    for (const auto &spring : {state.drag_x, state.drag_y, state.touch_pressure}) {
        if (!std::isfinite(spring.position) || !std::isfinite(spring.velocity)) {
            return reject_value("state.body_touch spring", spring.position);
        }
    }
    if (state.consumed_motion_at_us < 0) {
        return reject_value("state.consumed_motion_at_us", static_cast<double>(state.consumed_motion_at_us));
    }
    if (state.consumed_power_at_us < 0) {
        return reject_value("state.consumed_power_at_us", static_cast<double>(state.consumed_power_at_us));
    }
    if (state.consumed_microphone_at_us < 0 || state.consumed_playback_at_us < 0 ||
        state.consumed_interaction_at_us < 0 || state.consumed_eye_tap_at_us < 0 ||
        state.consumed_body_touch_at_us < 0 || state.consumed_body_press_at_us < 0) {
        return reject_value("state.audio/interaction timestamp", -1);
    }
    if (state.breath_phase >= kTau) {
        return reject_value("state.breath_phase", state.breath_phase);
    }
    if (state.blink_elapsed_ms >= 4800) {
        return reject_value("state.blink_elapsed_ms", state.blink_elapsed_ms);
    }
    return ESP_OK;
}
} // namespace

// scene: Shared evaluated artwork geometry, including any inertial translation or scale.
// point: Logical touch in the exact coordinate system used to draw that geometry.
// radius: Strictly authored normalized touch radius; it stays usable while eyes close.
// eye: Stable eye identity; no list position becomes an eye identifier.
esp_err_t hit_test_eye(const RigScene &scene, CODY2D::Vec2 point, float radius, EyeId &eye)
{
    if (!scene.valid || !std::isfinite(scene.scale) || scene.scale <= 0 ||
        !std::isfinite(point.x) || !std::isfinite(point.y) ||
        !std::isfinite(radius) || radius < .05f || radius > .25f) {
        ESP_LOGE(kTag, "Eye hit geometry rejected: %s", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    const float limit = radius * scene.scale;
    const struct { EyeId eye; PointId point; } targets[] = {
        {EyeId::Left, PointId::EyeLeft}, {EyeId::Right, PointId::EyeRight}
    };
    EyeId nearest = EyeId::None;
    float nearest_squared = limit * limit;
    for (const auto &target : targets) {
        const auto anchor = scene.world[to_index(target.point)];
        const float dx = point.x - anchor.x;
        const float dy = point.y - anchor.y;
        const float squared = dx * dx + dy * dy;
        if (!std::isfinite(squared)) {
            return reject_value("eye hit anchor", squared);
        }
        if (squared <= nearest_squared) {
            nearest = target.eye;
            nearest_squared = squared;
        }
    }
    eye = nearest;
    return ESP_OK;
}

// scene: Exactly the valid displayed rig; no second pose or texture is decoded.
// point: Logical touch coordinate already mapped through the display owner.
// hit: Updated only after the polygon extent and input have been accepted.
esp_err_t hit_test_body(const RigScene &scene, CODY2D::Vec2 point, bool &hit)
{
    const auto &body = scene.paths[to_index(PathId::Body)];
    if (!scene.valid || !std::isfinite(point.x) || !std::isfinite(point.y) ||
        point.x < 0 || point.x >= 466 || point.y < 0 || point.y >= 466 ||
        body.count < 3 || body.first_vertex + body.count > scene.vertices.size()) {
        ESP_LOGE(kTag, "Body hit geometry rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    bool inside = false;
    uint16_t previous = body.count - 1U;
    for (uint16_t current = 0; current < body.count; ++current) {
        const auto first = scene.vertices[body.first_vertex + previous];
        const auto second = scene.vertices[body.first_vertex + current];
        if ((first.y > point.y) != (second.y > point.y)) {
            const float crossing_x = first.x + (point.y - first.y) * (second.x - first.x) / (second.y - first.y);
            if (point.x < crossing_x) {
                inside = !inside;
            }
        }
        previous = current;
    }
    hit = inside;
    return ESP_OK;
}

// config: Complete authored settings, checked without silent corrections or defaults.
esp_err_t validate_response_config(const ResponseConfig &config)
{
    // The generated limits share their authoring source with the host validator.
    const auto parameters = Data::response_parameters(config);
    for (const auto &parameter : parameters) {
        if (!std::isfinite(parameter.value) || parameter.value < parameter.minimum ||
            parameter.value > parameter.maximum) {
            return reject_value(parameter.name, parameter.value);
        }
    }
    bool used_axes[to_index(Axis::Count)] = {};
    for (const auto panel_axis : kAxes) {
        const auto &mapping = config.motion.board_to_panel[to_index(panel_axis)];
        if (to_index(mapping.axis) >= to_index(Axis::Count)) {
            return reject_value("motion.board_to_panel invalid axis", to_index(mapping.axis));
        }
        if (mapping.sign != -1 && mapping.sign != 1) {
            return reject_axis_value("motion.board_to_panel sign", panel_axis, mapping.sign);
        }
        if (used_axes[to_index(mapping.axis)]) {
            ESP_LOGE(kTag, "motion.board_to_panel duplicate sensor axis '%s' for panel '%s' (%s)",
                     axis_name(mapping.axis), axis_name(panel_axis), esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
        used_axes[to_index(mapping.axis)] = true;
    }
    return ESP_OK;
}

// config: Validated configuration whose base breath rate initializes this character.
ResponseState make_response_state(const ResponseConfig &config)
{
    ResponseState state{};
    state.breath_rate_hz = config.expression.base_breath_hz;
    return state;
}

// state: Committed state; wake discards sample history while preserving artistic continuity.
void rebase_response_motion(ResponseState &state)
{
    state.motion_primed = false;
    state.transient_mps2 = {};
}

// config: Validated immutable response settings.
// state: Previous state, unchanged if any boundary rejects the update.
// inputs: Hardware-only observations projected from the single canonical snapshot.
// delta_ms: Active elapsed time; the analytical springs need no catch-up loop.
// now_us: Monotonic time that determines freshness independently of active animation.
// animate_rhythm: Advances breath/blink only when the facial clock is running.
// next: Receives a complete next state only after successful processing.
esp_err_t advance_response(const ResponseConfig &config, const ResponseState &state,
                          const ResponseInputs &inputs, float delta_ms, int64_t now_us,
                          bool animate_rhythm, ResponseState &next)
{
    if (!std::isfinite(delta_ms) || delta_ms < 0) {
        return reject_value("response.delta_ms", delta_ms);
    }
    if (now_us <= 0) {
        return reject_value("response.now_us", static_cast<double>(now_us));
    }
    if (inputs.rendering_rotation_degrees != 0 && inputs.rendering_rotation_degrees != 90 &&
        inputs.rendering_rotation_degrees != 180 && inputs.rendering_rotation_degrees != 270) {
        return reject_value("response.rotation", inputs.rendering_rotation_degrees);
    }
    const esp_err_t state_error = validate_response_state(state);
    if (state_error != ESP_OK) {
        return state_error;
    }
    const float seconds = delta_ms / 1000;
    ResponseState result = state;
    esp_err_t error = effective_stamp(inputs.motion.stamp, now_us, config.motion.sample_timeout_seconds,
                                     "motion.acceleration", result.motion_status);
    if (error != ESP_OK) {
        return error;
    }
    error = effective_stamp(inputs.power.stamp, now_us, config.power.sample_timeout_seconds,
                            "power.charge", result.power_status);
    if (error != ESP_OK) {
        return error;
    }
    if (result.motion_status.health == SampleHealth::Current) {
        for (const auto axis : kAxes) {
            const float value = inputs.motion.acceleration_mps2[axis];
            if (!std::isfinite(value) || std::fabs(value) > 256) {
                return reject_axis_value("motion.acceleration", axis, value);
            }
        }
        const int64_t sampled_at_us = result.motion_status.sampled_at_us;
        if (sampled_at_us < state.consumed_motion_at_us) {
            return reject_value("motion.acceleration reversed timestamp", static_cast<double>(sampled_at_us));
        }
        if (sampled_at_us > state.consumed_motion_at_us) {
            const float sample_seconds = static_cast<float>(sampled_at_us - state.consumed_motion_at_us) / 1000000.f;
            if (!state.motion_primed || sample_seconds >= config.motion.sample_timeout_seconds) {
                // Baseline priming prevents gravity and wake from manufacturing a kick.
                result.baseline_mps2 = inputs.motion.acceleration_mps2;
                result.transient_mps2 = {};
            } else {
                const float blend = -std::expm1(-sample_seconds / config.motion.baseline_seconds);
                for (const auto axis : kAxes) {
                    const float raw = inputs.motion.acceleration_mps2[axis];
                    const float baseline = state.baseline_mps2[axis] + (raw - state.baseline_mps2[axis]) * blend;
                    result.baseline_mps2[axis] = baseline;
                    result.transient_mps2[axis] = raw - baseline;
                }
            }
            result.motion_primed = true;
            result.consumed_motion_at_us = sampled_at_us;
        }
    } else {
        result.motion_primed = false;
        result.transient_mps2 = {};
    }

    float magnitude_squared = 0;
    for (const auto panel_axis : kAxes) {
        const auto &mapping = config.motion.board_to_panel[to_index(panel_axis)];
        const float cue = result.transient_mps2[mapping.axis] * mapping.sign;
        const bool depth_axis = panel_axis == Axis::Z;
        const float gain = depth_axis ? config.motion.depth_gain : config.motion.translation_gain;
        const float limit = depth_axis ? config.motion.depth_limit : config.motion.translation_limit;
        const float target = soft_limit(-cue * config.motion.sensitivity * gain, limit);
        result.body[to_index(panel_axis)] = advance_spring(state.body[to_index(panel_axis)], target, seconds,
                                                         config.motion.spring_hz, config.motion.damping_ratio);
        magnitude_squared += cue * cue;
    }
    const float normalized_motion = config.motion.sensitivity * std::sqrt(magnitude_squared) /
                                    config.motion.stress_reference_mps2;
    const float stress_target = -std::expm1(-normalized_motion * normalized_motion);
    result.stress = blend_state(state.stress, stress_target, seconds,
                               config.state.stress_attack_seconds, config.state.stress_release_seconds);

    if (result.power_status.health == SampleHealth::Current) {
        const float percentage = inputs.power.level_percent;
        if (!std::isfinite(percentage) || percentage < 0 || percentage > 100) {
            return reject_value("power.charge level_percent", percentage);
        }
        const int64_t sampled_at_us = result.power_status.sampled_at_us;
        if (sampled_at_us < state.consumed_power_at_us) {
            return reject_value("power.charge reversed timestamp", static_cast<double>(sampled_at_us));
        }
        if (sampled_at_us > state.consumed_power_at_us) {
            result.fatigue_target = std::pow(1 - percentage / 100, config.power.fatigue_curve);
            result.fatigue_known = true;
            result.consumed_power_at_us = sampled_at_us;
        }
        result.fatigue = blend_state(state.fatigue, result.fatigue_target, seconds,
                                    config.state.fatigue_attack_seconds, config.state.fatigue_release_seconds);
    }
    // Unknown charge holds artistic state. It is never substituted with "battery full".
    error = effective_stamp(inputs.eye_tap.stamp, now_us, config.touch.event_timeout_seconds,
                            "touch.eye", result.eye_tap_status);
    if (error != ESP_OK) {
        return error;
    }
    const float active_seconds = animate_rhythm ? seconds : 0;
    result.eye_hold_seconds = std::max(0.f, state.eye_hold_seconds - active_seconds);
    result.annoyance = blend_state(state.annoyance, 0, active_seconds,
        config.touch.annoyance_release_seconds, config.touch.annoyance_release_seconds);
    if (result.eye_tap_status.health == SampleHealth::Current) {
        if (inputs.eye_tap.eye != EyeId::Left && inputs.eye_tap.eye != EyeId::Right) {
            return reject_value("touch.eye identity", to_index(inputs.eye_tap.eye));
        }
        if (inputs.eye_tap.stamp.sampled_at_us < state.consumed_eye_tap_at_us) {
            return reject_value("touch.eye reversed timestamp", static_cast<double>(inputs.eye_tap.stamp.sampled_at_us));
        }
        if (inputs.eye_tap.stamp.sampled_at_us > state.consumed_eye_tap_at_us) {
            result.consumed_eye_tap_at_us = inputs.eye_tap.stamp.sampled_at_us;
            result.eye_hold_seconds = config.touch.eye_hold_seconds;
            result.annoyance = 1;
            result.stress = std::max(result.stress, config.touch.stress_gain);
        }
    }
    const float closure_target = result.eye_hold_seconds > 0 ? 1.f : 0.f;
    result.protective_eye_closure = blend_state(state.protective_eye_closure, closure_target,
        active_seconds, config.touch.close_seconds, config.touch.reopen_seconds);
    error = effective_stamp(inputs.body_touch.stamp, now_us, config.touch.event_timeout_seconds,
                            "touch.body", result.body_touch_status);
    if (error != ESP_OK) {
        return error;
    }
    float drag_x_target = 0;
    float drag_y_target = 0;
    bool body_pressed = false;
    if (result.body_touch_status.health == SampleHealth::Current) {
        const auto &touch = inputs.body_touch;
        if (!std::isfinite(touch.displacement_px.x) || !std::isfinite(touch.displacement_px.y) ||
            std::fabs(touch.displacement_px.x) > 465 || std::fabs(touch.displacement_px.y) > 465 ||
            touch.gesture_started_at_us <= 0 || touch.gesture_started_at_us > touch.stamp.sampled_at_us ||
            (touch.rendering_rotation_degrees != 0 && touch.rendering_rotation_degrees != 90 &&
             touch.rendering_rotation_degrees != 180 && touch.rendering_rotation_degrees != 270)) {
            return reject_value("touch.body coordinates/basis", touch.displacement_px.x);
        }
        if (touch.stamp.sampled_at_us < state.consumed_body_touch_at_us) {
            return reject_value("touch.body reversed timestamp", static_cast<double>(touch.stamp.sampled_at_us));
        }
        result.consumed_body_touch_at_us = touch.stamp.sampled_at_us;
        if (touch.rendering_rotation_degrees == inputs.rendering_rotation_degrees) {
            body_pressed = touch.pressed;
            if (body_pressed) {
                drag_x_target = soft_limit(touch.displacement_px.x * config.touch.drag_gain, config.touch.drag_limit_px);
                drag_y_target = soft_limit(touch.displacement_px.y * config.touch.drag_gain, config.touch.drag_limit_px);
            }
            if (touch.gesture_started_at_us > state.consumed_body_press_at_us) {
                result.consumed_body_press_at_us = touch.gesture_started_at_us;
                // A press/release within one render frame still leaves a tiny
                // artistic impulse; no sensor sample or release is invented.
                result.touch_pressure.velocity += config.touch.press_depth * 12.f;
            }
        } else {
            // An older logical basis cannot be interpreted after panel rotation.
            // Preserve the timestamp but let the artistic springs settle.
            result.body_touch_status.health = SampleHealth::Stale;
        }
    }
    result.drag_x = advance_spring(state.drag_x, drag_x_target, active_seconds,
        config.touch.spring_hz, config.touch.damping_ratio);
    result.drag_y = advance_spring(state.drag_y, drag_y_target, active_seconds,
        config.touch.spring_hz, config.touch.damping_ratio);
    result.touch_pressure = advance_spring(result.touch_pressure,
        body_pressed ? config.touch.press_depth : 0.f, active_seconds,
        config.touch.spring_hz, config.touch.damping_ratio);
    error = advance_audio_and_idle(config, state, inputs, active_seconds, now_us, result);
    if (error != ESP_OK) {
        return error;
    }
    if (animate_rhythm) {
        const auto &expression = config.expression;
        const float target_rate = expression.base_breath_hz * (1 + expression.stress_breath_gain * result.stress) *
                                  (1 - expression.fatigue_breath_slowdown * result.fatigue);
        const float blend = -std::expm1(-seconds / expression.rhythm_blend_seconds);
        const double integrated_rate = target_rate * static_cast<double>(seconds) +
            (state.breath_rate_hz - target_rate) * expression.rhythm_blend_seconds * blend;
        result.breath_phase = wrap_cycle(state.breath_phase + kTau * integrated_rate, kTau);
        result.breath_rate_hz = state.breath_rate_hz + (target_rate - state.breath_rate_hz) * blend;
        result.blink_elapsed_ms = wrap_cycle(state.blink_elapsed_ms + static_cast<double>(delta_ms), 4800);
    }
    error = validate_response_state(result);
    if (error != ESP_OK) {
        return error;
    }
    next = result;
    return ESP_OK;
}

// config: Validated response gains for the current character.
// state: Continuous response state and independent rhythmic phases.
// base_face: Authored facial controls before response contributions.
// base_contour: Authored independent contour targets.
// placement: Resting logical root and uniform scale.
// rotation: Clockwise cardinal orientation of the physical presentation.
// animate_rhythm: Whether the composed face includes breathing and blinking.
// frame: Receives controls and the same root transform used by artwork and overlays.
// Exception: Returns the real state, channel, contour or placement validation error.
esp_err_t compose_response(const ResponseConfig &config, const ResponseState &state,
                          const Channels &base_face, const ContourControls &base_contour,
                          Transform placement, uint16_t rotation, bool animate_rhythm,
                          ResponseFrame &frame)
{
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
        return reject_value("compose.rotation", rotation);
    }
    if (!std::isfinite(placement.x) || placement.x < 0 || placement.x >= 466) {
        return reject_value("compose.placement.x", placement.x);
    }
    if (!std::isfinite(placement.y) || placement.y < 0 || placement.y >= 466) {
        return reject_value("compose.placement.y", placement.y);
    }
    if (!std::isfinite(placement.scale) || placement.scale < 20 || placement.scale > 200) {
        return reject_value("compose.placement.scale", placement.scale);
    }
    esp_err_t error = validate_response_state(state);
    if (error != ESP_OK) {
        return error;
    }
    ResponseFrame result{};
    // Reuse the rig's semantic boundary instead of maintaining a second list of ranges.
    error = advance_face(base_face, base_face, 0, result.face);
    if (error != ESP_OK) {
        return error;
    }
    const ContourState base_shape{base_contour, 0};
    ContourState checked_shape{};
    error = advance_contour(base_shape, base_contour, 0, checked_shape);
    if (error != ESP_OK) {
        return error;
    }
    result.contour = checked_shape.controls;
    const auto &expression = config.expression;
    for (const auto eye : {FaceChannel::EyeLeft, FaceChannel::EyeRight}) {
        result.face[eye] *= 1 - state.fatigue * expression.fatigue_eye_closure;
    }
    for (const auto brow : {FaceChannel::BrowLeft, FaceChannel::BrowRight}) {
        result.face[brow] += (1 - result.face[brow]) * state.stress * expression.stress_brow;
    }
    auto &mouth = result.face[FaceChannel::MouthOpen];
    mouth += (1 - mouth) * state.stress * expression.stress_mouth_open;
    mouth += (1 - mouth) * state.speaking * config.audio.playback_mouth;
    mouth += (1 - mouth) * state.yawn_weight * config.idle.mouth_open;
    for (const auto eye : {FaceChannel::EyeLeft, FaceChannel::EyeRight}) {
        result.face[eye] *= 1 - state.yawn_weight * config.idle.eye_closure;
        result.face[eye] *= 1 - state.protective_eye_closure;
    }
    for (const auto brow : {FaceChannel::BrowLeft, FaceChannel::BrowRight}) {
        result.face[brow] += (1 - result.face[brow]) * state.attention * config.audio.attention_brow;
        result.face[brow] += (1 - result.face[brow]) * state.annoyance * config.touch.brow_gain;
    }
    auto &mouth_width = result.face[FaceChannel::MouthWidth];
    const float mouth_minimum = Data::channel_min[to_index(FaceChannel::MouthWidth)];
    mouth_width -= (mouth_width - mouth_minimum) * state.stress * expression.stress_mouth_narrowing;
    result.face[FaceChannel::CheekOpacity] *= 1 - state.fatigue * expression.fatigue_cheek_fade;
    auto &amplitude = result.contour[ContourControl::Amplitude];
    amplitude += (1 - amplitude) * state.stress * expression.stress_wave_gain;
    // Excite the same lobes/face warp as IMU motion, not just root translation.
    // Spring positions keep deforming after release without inventing samples.
    const float touch_drag = config.touch.drag_limit_px > 0
        ? std::hypot(state.drag_x.position, state.drag_y.position) / config.touch.drag_limit_px : 0;
    const float touch_activity = soft_limit(std::fabs(state.touch_pressure.position) * 6.f + touch_drag, 1.f);
    amplitude += (1 - amplitude) * touch_activity;
    auto &speed = result.contour[ContourControl::Speed];
    speed += (2 - speed) * state.stress * expression.stress_wave_speed_gain;
    speed += (2 - speed) * touch_activity * .5f;
    speed *= 1 - state.fatigue * expression.fatigue_wave_slowdown;
    auto &fullness = result.contour[ContourControl::Fullness];
    fullness -= (fullness + 1) * state.fatigue * expression.fatigue_fullness;
    fullness -= (fullness + 1) * soft_limit(std::fabs(state.touch_pressure.position), .15f);
    if (animate_rhythm) {
        const float breathing = std::sin(state.breath_phase);
        const float blink = state.blink_elapsed_ms < 280
            ? .5f - .5f * std::cos(kTau * state.blink_elapsed_ms / 280) : 0;
        for (const auto eye : {FaceChannel::EyeLeft, FaceChannel::EyeRight}) {
            result.face[eye] *= 1 - blink;
        }
        apply_mouth_breathing(result.face, breathing);
        auto &breath = result.face[FaceChannel::Breath];
        breath += (1 - std::fabs(breath)) * breathing;
    }
    // Idle accents compose after breathing so a whistle stays pursed. They use
    // existing face controls and never add a second pose or navigation owner.
    const float gesture = state.idle_scene_weight;
    if (state.idle_scene == IdleSceneId::Whistle) {
        mouth += (.24f - mouth) * gesture;
        // The pursed pose targets the authored minimum, remaining inside rig bounds.
        mouth_width += (mouth_minimum - mouth_width) * gesture;
    } else if (state.idle_scene == IdleSceneId::Greeting) {
        mouth += (.65f - mouth) * gesture;
        mouth_width += (1.1f - mouth_width) * gesture;
        result.face[FaceChannel::EyeRight] *= 1 - .65f * gesture;
        auto &cheek = result.face[FaceChannel::CheekOpacity];
        cheek += (1 - cheek) * .25f * gesture;
    }
    const auto &horizontal = state.body[to_index(Axis::X)];
    const auto &vertical = state.body[to_index(Axis::Y)];
    const float magnitude = std::hypot(horizontal.position, vertical.position);
    const float multiplier = magnitude > 0 ? soft_limit(magnitude, config.motion.translation_limit) / magnitude : 1;
    CODY2D::Vec2 offset = logical_vector({horizontal.position * multiplier * placement.scale,
                                               vertical.position * multiplier * placement.scale}, rotation);
    // Touch has already been transformed by the display owner. Never rotate
    // it as an IMU vector a second time, particularly at 90/270 degrees.
    const float drag_magnitude = std::hypot(state.drag_x.position, state.drag_y.position);
    const float drag_multiplier = drag_magnitude > 0
        ? soft_limit(drag_magnitude, config.touch.drag_limit_px) / drag_magnitude : 1.f;
    offset.x += state.drag_x.position * drag_multiplier;
    offset.y += state.drag_y.position * drag_multiplier;
    result.spring_velocity_px_per_second = logical_vector(
        {horizontal.velocity * placement.scale, vertical.velocity * placement.scale}, rotation
    );
    result.spring_velocity_px_per_second.x += state.drag_x.velocity;
    result.spring_velocity_px_per_second.y += state.drag_y.velocity;
    result.depth_scale = (1 + soft_limit(state.body[to_index(Axis::Z)].position, config.motion.depth_limit)) *
        (1 - soft_limit(state.touch_pressure.position, .15f));
    result.transform = {placement.x + offset.x, placement.y + offset.y, placement.scale * result.depth_scale};
    result.resting_root = {placement.x, placement.y};
    result.current_root = {result.transform.x, result.transform.y};
    if (result.transform.x < 0 || result.transform.x >= 466) {
        return reject_value("compose.root.x", result.transform.x);
    }
    if (result.transform.y < 0 || result.transform.y >= 466) {
        return reject_value("compose.root.y", result.transform.y);
    }
    if (result.transform.scale < 20 || result.transform.scale > 200) {
        return reject_value("compose.root.scale", result.transform.scale);
    }
    frame = result;
    return ESP_OK;
}
} // namespace CODY
