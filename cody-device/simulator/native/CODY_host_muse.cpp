#include "CODY_pixel_avatar.hpp"
#include "CODY_muse_showcase.hpp"
#include "CODY_pixel_demo.hpp"
#include "CODY_muse_themes.hpp"

#include <cstddef>

#if defined(_WIN32)
#define CODY_HOST_EXPORT extern "C" __declspec(dllexport)
#else
#define CODY_HOST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
CODY::MuseShowcase s_showcase{};
CODY::PixelDemoMotionState s_motion{};
uint8_t s_theme = 0;
}

// theme: Stable Studio theme identity; an invalid identity selects Classic.
// Exception: None; this serialized host preview never changes device settings.
CODY_HOST_EXPORT void cody_host_muse_theme(uint32_t theme)
{
    s_theme = theme < CODY::kMuseCharacterThemes.size() ? static_cast<uint8_t>(theme) : 0;
}

// seed: Artistic seed for a new scenario, with zero selecting startup defaults.
// The same preview owner must serialize reset and frame calls.
CODY_HOST_EXPORT void cody_host_muse_reset(uint32_t seed)
{
    CODY::reset_muse_animation(seed);
    s_showcase.reset(seed);
    s_motion = {};
}

// No parameters: discard synthetic motion while the mascot is hidden or in standby.
CODY_HOST_EXPORT void cody_host_muse_motion_reset()
{
    s_motion = {};
}

// acceleration_x: SIMULATED acceleration on board X in m/s2.
// acceleration_y: SIMULATED acceleration on board Y in m/s2.
// sampled_at_us: Host monotonic time at which this synthetic sample was generated.
// now_us: Host monotonic observation time, used for the same freshness gate as firmware.
// rendering_rotation_degrees: Applied logical display rotation.
// valid: Whether this synthetic observation is available.
// Exception: Invalid values are rejected by the shared motion owner.
CODY_HOST_EXPORT void cody_host_muse_motion(float acceleration_x, float acceleration_y,
    int64_t sampled_at_us, int64_t now_us, uint16_t rendering_rotation_degrees, bool valid)
{
    CODY::advance_pixel_demo_motion(s_motion,
        {acceleration_x, acceleration_y, sampled_at_us, rendering_rotation_degrees, valid}, now_us);
}

// pose: Numeric PixelAvatarPose value; the host ABI rejects unknown values.
// time_ms: Nonnegative active animation time.
// pose_ms: Elapsed time since the selected pose began.
// reaction_ms: Elapsed tap reaction time; a value of 1600 ms or more is inactive.
// audio_level: Optional normalized artistic input; no host audio is acquired.
// working: Zero for normal pose or one for the active-work Thinking variant.
// side: Requested viewport side, in pixels, as a positive multiple of 64.
// pixels: Exclusive native RGB565 output.
// count: Exact writable output length in pixels.
// style: Validated artwork configuration.
// Exception: Returns the original draw error or rejects invalid input before composition.
static esp_err_t render_muse_pose(uint32_t pose, uint32_t time_ms,
    uint32_t pose_ms, uint32_t reaction_ms, float audio_level, uint32_t working,
    uint32_t side, uint16_t *pixels, size_t count, CODY::MuseVisualStyle style)
{
    if (pose > static_cast<uint32_t>(CODY::PixelAvatarPose::Sleep) || working > 1 ||
        pixels == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (side == 0 || side > CODY2D::kMaxDimension || side % CODY::kPixelAvatarGridSize != 0 ||
        count != static_cast<size_t>(side) * side) {
        return ESP_ERR_INVALID_SIZE;
    }
    CODY::PixelAvatarFrame frame{};
    const auto status = CODY::compose_muse_frame(frame, {
        static_cast<CODY::PixelAvatarPose>(pose), time_ms, pose_ms, reaction_ms, audio_level, working == 1,
        style,
    });
    if (status != CODY::PixelAvatarComposeStatus::Ok) {
        return ESP_ERR_INVALID_ARG;
    }
    const auto width = static_cast<uint16_t>(side);
    CODY2D::Surface surface{pixels, width, width, width, static_cast<uint32_t>(count)};
    return CODY::draw_pixel_avatar_rows(frame, {0, 0, width, width}, surface, 0, 0);
}

// pose: Numeric presentation state.
// time_ms: Active animation time.
// pose_ms: Time since the current pose started.
// reaction_ms: Time since the last touch reaction.
// audio_level: Optional normalized input energy.
// working: Whether thinking represents active work.
// side: Requested output width and height.
// pixels: Exclusive output surface.
// count: Surface capacity in pixels.
// Exception: Preserves the native renderer's validation result.
CODY_HOST_EXPORT esp_err_t cody_host_muse_pose_frame(uint32_t pose, uint32_t time_ms,
    uint32_t pose_ms, uint32_t reaction_ms, float audio_level, uint32_t working,
    uint32_t side, uint16_t *pixels, size_t count)
{
    CODY::MuseVisualStyle style{};
    style.character_theme = s_theme;
    return render_muse_pose(pose, time_ms, pose_ms, reaction_ms, audio_level, working,
        side, pixels, count, style);
}

// pose: Numeric PixelAvatarPose value.
// time_ms: Active animation time.
// pose_ms: Time since the current pose started.
// reaction_ms: Time since the accepted tap, or an inactive value.
// audio_level: Optional normalized input energy.
// working: Zero or one for the active-work Thinking variant.
// side: Requested output width and height.
// pixels: Exclusive output surface.
// count: Surface capacity in pixels.
// left_eye: Numeric MuseEyeStyle for the left eye.
// right_eye: Numeric MuseEyeStyle for the right eye.
// mouth: Numeric MuseMouthStyle independent of the eyes.
// talking_openness: Explicit Talking mouth opening in the range 0..1.
// symbol: Numeric MuseSymbol independent of the selected face.
// body_x: Horizontal body offset in Muse grid pixels.
// body_y: Vertical body offset in Muse grid pixels.
// left_hand_x: Horizontal left-hand offset in Muse grid pixels.
// left_hand_y: Vertical left-hand offset in Muse grid pixels.
// right_hand_x: Horizontal right-hand offset in Muse grid pixels.
// right_hand_y: Vertical right-hand offset in Muse grid pixels.
// gaze_x: Authored horizontal gaze from -1 to 1.
// gaze_y: Authored vertical gaze from -1 to 1.
// eye_openness: Explicit eye openness from zero to one.
// Exception: Rejects unknown selectors and preserves native composition errors.
CODY_HOST_EXPORT esp_err_t cody_host_muse_face_frame(uint32_t pose, uint32_t time_ms,
    uint32_t pose_ms, uint32_t reaction_ms, float audio_level, uint32_t working,
    uint32_t side, uint16_t *pixels, size_t count, uint32_t left_eye, uint32_t right_eye,
    uint32_t mouth, float talking_openness, uint32_t symbol, float body_x, float body_y,
    float left_hand_x, float left_hand_y, float right_hand_x, float right_hand_y,
    float gaze_x, float gaze_y, float eye_openness)
{
    if (left_eye >= static_cast<uint32_t>(CODY::MuseEyeStyle::Count) ||
        right_eye >= static_cast<uint32_t>(CODY::MuseEyeStyle::Count) ||
        mouth >= static_cast<uint32_t>(CODY::MuseMouthStyle::Count) ||
        symbol >= static_cast<uint32_t>(CODY::MuseSymbol::Count)) {
        return ESP_ERR_INVALID_ARG;
    }
    CODY::MuseVisualStyle style{};
    style.character_theme = s_theme;
    style.face.left_eye = static_cast<CODY::MuseEyeStyle>(left_eye);
    style.face.right_eye = static_cast<CODY::MuseEyeStyle>(right_eye);
    style.face.mouth = static_cast<CODY::MuseMouthStyle>(mouth);
    style.face.talking_openness = talking_openness;
    style.symbol.symbol = static_cast<CODY::MuseSymbol>(symbol);
    style.pose.body_x = body_x;
    style.pose.body_y = body_y;
    style.pose.left_hand_x = left_hand_x;
    style.pose.left_hand_y = left_hand_y;
    style.pose.right_hand_x = right_hand_x;
    style.pose.right_hand_y = right_hand_y;
    style.pose.gaze_x = gaze_x;
    style.pose.gaze_y = gaze_y;
    style.pose.eye_openness = eye_openness;
    return render_muse_pose(pose, time_ms, pose_ms, reaction_ms, audio_level, working,
        side, pixels, count, style);
}

// time_ms: Nonnegative active time since the preview starts.
// reaction_ms: Time since the accepted pet, or UINT32_MAX if there is no active pet.
// pixels: Exclusive native RGB565 output for the Studio preview.
// count: Writable output length in pixels.
// Exception: Returns the native surface validation error if drawing fails.
CODY_HOST_EXPORT esp_err_t cody_host_muse_frame(uint32_t time_ms, uint32_t reaction_ms,
    uint16_t *pixels, size_t count)
{
    constexpr uint16_t side = 320;
    if (pixels == nullptr || count != side * side) {
        return ESP_ERR_INVALID_ARG;
    }
    const auto pose = s_showcase.sample(time_ms);
    CODY::MuseVisualStyle style{};
    style.character_theme = s_theme;
    style.symbol.symbol = pose.symbol;
    CODY::apply_pixel_demo_motion(style, s_motion);
    return render_muse_pose(static_cast<uint32_t>(pose.pose), time_ms,
        pose.pose_ms, reaction_ms, 0.0f, pose.working ? 1U : 0U, side, pixels, count, style);
}
