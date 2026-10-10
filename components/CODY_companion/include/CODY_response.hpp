#pragma once

#include "CODY_rig.hpp"

#if defined(ESP_PLATFORM) && defined(CODY_HOST_SIMULATOR) && CODY_HOST_SIMULATOR
#error "The simulator input origin must never be compiled into firmware"
#endif

namespace LUNASHELL { struct ClockSnapshot; }

namespace CODY {

enum class StimulusId : uint8_t { MotionAcceleration, PowerCharge, MicrophoneLevel, SpeakerPlayback };
enum class Axis : uint8_t { X, Y, Z, Count };
enum class SampleHealth : uint8_t { Unavailable, Current, Stale, Error };
// Firmware accepts acquired hardware only. The PC driver labels its own inputs;
// its origin does not exist in the firmware build or snapshot service.
enum class InputOrigin : uint8_t {
    Hardware,
#if defined(CODY_HOST_SIMULATOR) && CODY_HOST_SIMULATOR
    Simulator,
#endif
};
enum class EyeId : uint8_t { None, Left, Right };
// Transient presentations within screen 16; these IDs never enter route history.
enum class IdleSceneId : uint32_t { None = 0, Yawn = 0xC002, Whistle = 0xC003, Greeting = 0xC004 };

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

struct MicrophoneSample {
    SampleStamp stamp{};
    uint8_t channel_count = 0;
    uint32_t mean_level[2] = {};
    uint32_t peak_level[2] = {};
};

struct PlaybackSample {
    SampleStamp stamp{};
    bool active = false;
    uint32_t mean_level = 0;
    uint32_t cue_id = 0;
};

struct BodyTouchSample {
    SampleStamp stamp{};
    bool pressed = false;
    // Acquired logical point; gaze uses this without reconstructing the contact from displacement.
    CODY2D::Vec2 point_px{};
    CODY2D::Vec2 displacement_px{};
    uint16_t rendering_rotation_degrees = 0;
    int64_t gesture_started_at_us = 0;
};

struct ResponseInputs {
    MotionSample motion{};
    // Navigation consumes measured peaks before artistic software smoothing.
    MotionSample navigation_motion{};
    PowerSample power{};
    uint16_t rendering_rotation_degrees = 0;
    MicrophoneSample microphone{};
    PlaybackSample playback{};
    BodyTouchSample body_touch{};
    int64_t last_interaction_at_us = 0;
    // Presentation policy supplied by the route owner, not a sensor reading.
    bool idle_scene_allowed = false;
    struct EyeTap {
        SampleStamp stamp{};
        EyeId eye = EyeId::None;
    } eye_tap;
};
static_assert(sizeof(ResponseInputs) <= 384, "Response projection exceeds the bounded renderer input budget");

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
    struct AudioConfig {
        float sample_timeout_seconds;
        float microphone_reference;
        float playback_reference;
        float attack_seconds;
        float release_seconds;
        float attention_brow;
        float playback_mouth;
    } audio;
    struct IdleConfig {
        float after_seconds;
        float duration_seconds;
        float cooldown_seconds;
        float activity_stress_threshold;
        float audio_activity_threshold;
        float mouth_open;
        float eye_closure;
    } idle;
    struct TouchConfig {
        float event_timeout_seconds;
        float eye_hit_radius;
        float eye_hold_seconds;
        float close_seconds;
        float reopen_seconds;
        float annoyance_release_seconds;
        float stress_gain;
        float brow_gain;
        float drag_gain;
        float drag_limit_px;
        float press_depth;
        float spring_hz;
        float damping_ratio;
    } touch;
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
    SampleStamp microphone_status{};
    SampleStamp playback_status{};
    int64_t consumed_microphone_at_us = 0;
    int64_t consumed_playback_at_us = 0;
    int64_t consumed_interaction_at_us = 0;
    float microphone_target = 0;
    float playback_target = 0;
    float attention = 0;
    float speaking = 0;
    float idle_seconds = 0;
    float yawn_seconds = 0;
    float yawn_cooldown_seconds = 0;
    float yawn_weight = 0;
    bool yawning = false;
    IdleSceneId idle_scene = IdleSceneId::None;
    IdleSceneId last_idle_scene = IdleSceneId::None;
    uint32_t idle_scene_generation = 0;
    uint32_t idle_random_state = 0x6D2B79F5U;
    float idle_wait_fraction = 0;
    float idle_scene_seconds = 0;
    float idle_scene_weight = 0;
    uint8_t idle_remaining_scenes = 0;
    SampleStamp eye_tap_status{};
    int64_t consumed_eye_tap_at_us = 0;
    float eye_hold_seconds = 0;
    float protective_eye_closure = 0;
    float annoyance = 0;
    SampleStamp body_touch_status{};
    int64_t consumed_body_touch_at_us = 0;
    int64_t consumed_body_press_at_us = 0;
    SpringState drag_x{};
    SpringState drag_y{};
    SpringState touch_pressure{};
};

// scene: Exactly the valid geometry currently displayed; no second pose is evaluated.
// point: Logical touch already mapped by the display owner for cardinal rotation.
// radius: Authored normalized hit radius scaled with the evaluated character.
// eye: Receives the hit semantic eye, or None when the touch misses both.
esp_err_t hit_test_eye(const RigScene &scene, CODY2D::Vec2 point, float radius, EyeId &eye);

// scene: Valid displayed body geometry, shared with raster and existing eye targets.
// point: Acquired logical coordinate; navigation is excluded by the caller.
// hit: Receives whether the point lies within the evaluated body on success.
esp_err_t hit_test_body(const RigScene &scene, CODY2D::Vec2 point, bool &hit);

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
// clock: Optional caller-owned routine observation from the same canonical read.
esp_err_t observe_response_inputs(ResponseInputs &inputs, LUNASHELL::ClockSnapshot *clock = nullptr);

} // namespace CODY
