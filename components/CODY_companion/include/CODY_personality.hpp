#pragma once

#include <cstdint>
#include <array>
#include "CODY_damage.hpp"
#include "CODY_personality_config.hpp"

namespace CODY {

struct RigScene;

// Explicit IDs survive catalog reordering. None consumes no sprite work.
enum class PersonalityCue : uint8_t {
    None = 0, Sleep = 1, Fear = 2, Joy = 3, Hunger = 4, Thirst = 5,
    Heat = 6, Cold = 7, LowBattery = 8, Charging = 9, NoWifi = 10,
    Curiosity = 11, Confusion = 12, Affection = 13, Frustration = 14,
};

// Names identify the existing authored presentations, not an array position.
enum class CodySize : uint8_t {
    Micro = 1, Tiny = 2, Preferences = 3, Look = 4, Pairing = 5,
    Small = 6, Invitation = 7, Medium = 8, Greeting = 9, Full = 10,
};

enum class PersonalityPresentation : uint8_t { Disabled = 0, Main = 1, Sleep = 2 };
enum class PersonalityPresentationStatus : uint8_t { Hidden = 0, Visible = 1, BlockedByLayout = 2 };
enum class PersonalityPhase : uint8_t { Hidden = 0, FadeIn = 1, Visible = 2, FadeOut = 3 };

struct PersonalityContext {
    PersonalityPresentation presentation = PersonalityPresentation::Disabled;
    float nominal_scale = 145;
    uint32_t route = 0;
    uint16_t rotation = 0;
    bool release_immediately = false;
    bool invalidate_current = false;
};

struct PersonalityFrame {
    PersonalityCue cue = PersonalityCue::None;
    DamageRect bounds{};
    uint8_t opacity = 0;
    PersonalityPresentationStatus status = PersonalityPresentationStatus::Hidden;
};

struct PersonalityFollowTarget {
    float center_x = 0;
    float center_y = 0;
    float width = 0;
    float height = 0;
};

// Time buckets hold artistic geometry only; cue requests still have one latest target.
struct PersonalityFollowBucket {
    float x_integral = 0;
    float y_integral = 0;
    float width_integral = 0;
    float height_integral = 0;
    uint32_t duration_us = 0;
    uint32_t age_us = 0;
};

// Fixed artistic state; no telemetry cache, heap ownership or frame-count clock.
struct PersonalityLag {
    PersonalityCue cue = PersonalityCue::None;
    PersonalityCue desired = PersonalityCue::None;
    PersonalityPhase phase = PersonalityPhase::Hidden;
    int64_t desired_age_us = 0;
    int64_t phase_elapsed_us = 0;
    float opacity = 0;
    float fade_out_opacity = 0;
    CodySize size = CodySize::Micro;
    uint32_t route = 0;
    uint16_t rotation = 0;
    float center_x = 0;
    float center_y = 0;
    float width = 0;
    float height = 0;
    std::array<PersonalityFollowBucket, PersonalityConfig::history_bucket_count> follow_history{};
    PersonalityFollowTarget previous_target{};
    uint32_t position_stable_us = 0;
    uint32_t scale_stable_us = 0;
    uint8_t current_bucket = 0;
    bool primed = false;
};

// cue: Typed identity whose catalog membership is requested.
bool personality_cue_valid(PersonalityCue cue);

// cue: Typed identity to classify as a device observation rather than narrative.
bool personality_cue_is_device(PersonalityCue cue);

// nominal_scale: Finite authored scale before breathing, motion and entrance interpolation.
// size: Receives the nearest existing presentation; Micro applies only at scale <= 22.
// Exception: Returns invalid argument for a scale outside the rig's [20,200] contract.
esp_err_t cody_size_for_scale(float nominal_scale, CodySize &size);

// cue: None or a narrative symbol; device symbols must use the separate canonical selector.
// Exception: Returns invalid argument or the real display lock/consumer-state error.
esp_err_t set_personality_cue(PersonalityCue cue);

// cue: None, LowBattery, Charging or NoWifi; paint is gated by fresh canonical state.
// Exception: Returns invalid argument or the real display lock/consumer-state error.
esp_err_t set_personality_device_cue(PersonalityCue cue);

// cue: Receives the selected request, which may currently be hidden by validity/layout.
// Exception: Returns the real display lock/consumer-state error.
esp_err_t get_personality_cue(PersonalityCue &cue);

// frame: Receives the last retired presentation, including layout admission status.
// Exception: Returns the real display lock/consumer-state error.
esp_err_t get_personality_frame(PersonalityFrame &frame);

// scene: Evaluated pose owning the face and upper-lobe anchors.
// cue: Single selected symbol after canonical device-state gating.
// size: Stable nominal character presentation, independent of animated scale.
// context: Scene permission, authored scale and explicit smooth-release policy.
// active_delta_us: Elapsed active animation time; never includes paused/standby time.
// paused: Freeze channel and follow time while still accepting an updated desired cue.
// reset: Re-prime geometric following only; fade and hold retain their progress.
// exclusions: Permanently reserved chrome and navigation rectangles to preserve.
// exclusion_count: Number of readable exclusions, at most six.
// lag: Caller-owned fixed artistic state, committed only on success.
// frame: Receives a single admitted sprite or explicit Hidden/BlockedByLayout outcome.
// Exception: Returns invalid geometry/identity arguments; ordinary admission rejection is a status.
esp_err_t prepare_personality_frame(
    const RigScene &scene, PersonalityCue cue, CodySize size, const PersonalityContext &context,
    int64_t active_delta_us, bool paused, bool reset,
    const DamageRect *exclusions, uint8_t exclusion_count,
    PersonalityLag &lag, PersonalityFrame &frame);

// frame: Prepared single-sprite identity and circle-safe logical bounds.
// strip: Borrowed compact RGB565 tile with explicit byte order.
// first_row: Logical tile y origin; no sprite sampling depends on tile boundaries.
// first_column: Logical tile x origin before shared display rotation.
// Exception: Returns the real surface or sprite identity error.
esp_err_t draw_personality_rows(const PersonalityFrame &frame, CODY2D::Surface strip,
                                uint16_t first_row, uint16_t first_column);

// previous: Last completely displayed sprite footprint and identity.
// current: Prepared next sprite footprint and identity.
bool same_personality_frame(const PersonalityFrame &previous, const PersonalityFrame &current);

} // namespace CODY
