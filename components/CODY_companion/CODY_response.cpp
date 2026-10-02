#include "CODY_response.hpp"

#include <cmath>
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

// stamp: Original observation status, including the last successful sample timestamp.
// now_us: Monotonic time at which this input is consumed.
// timeout_seconds: Reviewed maximum lifetime for the source.
// source: Stable identity named in rejected-metadata logs.
// effective: Receives the aged status without changing the original error.
esp_err_t effective_stamp(const SampleStamp &stamp, int64_t now_us,
                          float timeout_seconds, const char *source,
                          SampleStamp &effective)
{
    if (stamp.origin != InputOrigin::Hardware || stamp.sampled_at_us < 0 ||
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

// state: Previously committed state to inspect before it can affect geometry.
esp_err_t validate_response_state(const ResponseState &state)
{
    const CheckedParameter scalars[] = {
        {"state.stress", state.stress, 0, 1},
        {"state.fatigue", state.fatigue, 0, 1},
        {"state.fatigue_target", state.fatigue_target, 0, 1},
        {"state.breath_phase", state.breath_phase, 0, kTau},
        {"state.breath_rate_hz", state.breath_rate_hz, 0, 3},
        {"state.blink_elapsed_ms", state.blink_elapsed_ms, 0, 4800},
    };
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
    if (state.consumed_motion_at_us < 0) {
        return reject_value("state.consumed_motion_at_us", static_cast<double>(state.consumed_motion_at_us));
    }
    if (state.consumed_power_at_us < 0) {
        return reject_value("state.consumed_power_at_us", static_cast<double>(state.consumed_power_at_us));
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
    auto &mouth_width = result.face[FaceChannel::MouthWidth];
    const float mouth_minimum = Data::channel_min[to_index(FaceChannel::MouthWidth)];
    mouth_width -= (mouth_width - mouth_minimum) * state.stress * expression.stress_mouth_narrowing;
    result.face[FaceChannel::CheekOpacity] *= 1 - state.fatigue * expression.fatigue_cheek_fade;
    auto &amplitude = result.contour[ContourControl::Amplitude];
    amplitude += (1 - amplitude) * state.stress * expression.stress_wave_gain;
    auto &speed = result.contour[ContourControl::Speed];
    speed += (2 - speed) * state.stress * expression.stress_wave_speed_gain;
    speed *= 1 - state.fatigue * expression.fatigue_wave_slowdown;
    auto &fullness = result.contour[ContourControl::Fullness];
    fullness -= (fullness + 1) * state.fatigue * expression.fatigue_fullness;
    if (animate_rhythm) {
        const float breathing = std::sin(state.breath_phase);
        const float blink = state.blink_elapsed_ms < 280
            ? .5f - .5f * std::cos(kTau * state.blink_elapsed_ms / 280) : 0;
        for (const auto eye : {FaceChannel::EyeLeft, FaceChannel::EyeRight}) {
            result.face[eye] *= 1 - blink;
        }
        mouth += (1 - mouth) * .08f * (.5f + .5f * breathing);
        auto &breath = result.face[FaceChannel::Breath];
        breath += (1 - std::fabs(breath)) * breathing;
    }
    const auto &horizontal = state.body[to_index(Axis::X)];
    const auto &vertical = state.body[to_index(Axis::Y)];
    const float magnitude = std::hypot(horizontal.position, vertical.position);
    const float multiplier = magnitude > 0 ? soft_limit(magnitude, config.motion.translation_limit) / magnitude : 1;
    const CODY2D::Vec2 offset = logical_vector({horizontal.position * multiplier * placement.scale,
                                               vertical.position * multiplier * placement.scale}, rotation);
    result.spring_velocity_px_per_second = logical_vector(
        {horizontal.velocity * placement.scale, vertical.velocity * placement.scale}, rotation
    );
    result.depth_scale = 1 + soft_limit(state.body[to_index(Axis::Z)].position, config.motion.depth_limit);
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
