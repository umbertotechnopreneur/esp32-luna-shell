#pragma once

#include "CODY_rig.hpp"

namespace CODY {

enum class StimulusId : uint8_t { MotionAcceleration, PowerCharge };
enum class Axis : uint8_t { X, Y, Z, Count };
enum class SampleHealth : uint8_t { Unavailable, Current, Stale, Error };
// The firmware input boundary deliberately has no synthetic source.
enum class InputOrigin : uint8_t { Hardware };

struct AxisValues {
    float values[to_index(Axis::Count)] = {};

    // axis: Semantic axis, converted to storage only at this boundary.
    float &operator[](Axis axis)
    {
        return values[to_index(axis)];
    }
    // axis: Semantic axis to read from immutable input or animation state.
    const float &operator[](Axis axis) const
    {
        return values[to_index(axis)];
    }
};

struct SampleStamp {
    SampleHealth health = SampleHealth::Unavailable;
    InputOrigin origin = InputOrigin::Hardware;
    int64_t sampled_at_us = 0;
    esp_err_t error = ESP_ERR_INVALID_STATE;
};

struct MotionSample {
    SampleStamp stamp{};
    AxisValues acceleration_mps2{};
};

struct PowerSample {
    SampleStamp stamp{};
    float level_percent = 0;
    bool charging = false;
};

struct ResponseInputs {
    MotionSample motion{};
    PowerSample power{};
    uint16_t rendering_rotation_degrees = 0;
};

struct AxisMapping {
    Axis axis = Axis::X;
    float sign = 1;
};

struct MotionResponseConfig {
    AxisMapping board_to_panel[to_index(Axis::Count)]{};
    float sensitivity;
    float baseline_seconds;
    float sample_timeout_seconds;
    float translation_gain;
    float translation_limit;
    float depth_gain;
    float depth_limit;
    float spring_hz;
    float damping_ratio;
    float stress_reference_mps2;
};

struct PowerResponseConfig {
    float sample_timeout_seconds;
    float fatigue_curve;
};

struct InternalStateConfig {
    float stress_attack_seconds;
    float stress_release_seconds;
    float fatigue_attack_seconds;
    float fatigue_release_seconds;
};

struct ExpressionResponseConfig {
    float stress_brow;
    float fatigue_eye_closure;
    float fatigue_cheek_fade;
    float stress_mouth_open;
    float stress_mouth_narrowing;
    float stress_wave_gain;
    float stress_wave_speed_gain;
    float fatigue_wave_slowdown;
    float fatigue_fullness;
    float base_breath_hz;
    float stress_breath_gain;
    float fatigue_breath_slowdown;
    float rhythm_blend_seconds;
};

struct ResponseConfig {
    MotionResponseConfig motion;
    PowerResponseConfig power;
    InternalStateConfig state;
    ExpressionResponseConfig expression;
};

struct SpringState {
    float position = 0;
    float velocity = 0;
};

struct ResponseState {
    AxisValues baseline_mps2{};
    AxisValues transient_mps2{};
    SpringState body[to_index(Axis::Count)]{};
    SampleStamp motion_status{};
    SampleStamp power_status{};
    int64_t consumed_motion_at_us = 0;
    int64_t consumed_power_at_us = 0;
    bool motion_primed = false;
    bool fatigue_known = false;
    float stress = 0;
    float fatigue = 0;
    float fatigue_target = 0;
    float breath_phase = 0;
    float breath_rate_hz = 0;
    float blink_elapsed_ms = 0;
};

struct ResponseFrame {
    Channels face{};
    ContourControls contour{};
    Transform transform{};
    CODY2D::Vec2 resting_root{};
    CODY2D::Vec2 current_root{};
    CODY2D::Vec2 spring_velocity_px_per_second{};
    float depth_scale = 1;
};

// config: Strict, YAML-generated model parameters; invalid values are never repaired.
esp_err_t validate_response_config(const ResponseConfig &config);
// config: Validated model whose initial breath rate belongs to this character skin.
ResponseState make_response_state(const ResponseConfig &config);
// state: Existing artistic state; resume discards input history, not fatigue or phases.
void rebase_response_motion(ResponseState &state);
// config: Validated immutable response settings.
// state: Previous model state, unchanged on a rejected sample or failed update.
// inputs: Published observations with real timestamps, units and original errors.
// delta_ms: Active elapsed time; zero freezes dynamics but can prime fresh input.
// now_us: Monotonic observation time used for sample freshness, independent of animation.
// animate_rhythm: Whether breath/blink clocks advance; contour has its own clock.
// next: Complete next model state, written only on success and allowed to alias state.
esp_err_t advance_response(const ResponseConfig &config, const ResponseState &state,
                          const ResponseInputs &inputs, float delta_ms, int64_t now_us,
                          bool animate_rhythm, ResponseState &next);
// config: Validated gains for independent expression contributions.
// state: Updated model, including phases and source health.
// base_face: Independently authored or selected facial controls.
// base_contour: Independently selected contour targets; its phase is not recomputed.
// placement: Resting root and scale in logical display pixels.
// rotation: Clockwise cardinal display orientation, applied once by the display renderer.
// animate_rhythm: Whether the composed face includes breath and blink.
// frame: Composed controls and one root transform shared by artwork and debug points.
esp_err_t compose_response(const ResponseConfig &config, const ResponseState &state,
                          const Channels &base_face, const ContourControls &base_contour,
                          Transform placement, uint16_t rotation, bool animate_rhythm,
                          ResponseFrame &frame);
// inputs: Receives hardware-only observations; no driver refresh occurs here.
esp_err_t observe_response_inputs(ResponseInputs &inputs);

} // namespace CODY
