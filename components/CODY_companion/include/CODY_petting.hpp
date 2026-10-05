#pragma once

#include <cstdint>
#include "CODY_rig.hpp"
#include "CODY_petting_config.hpp"
#include "personality_catalog.hpp"

namespace CODY {

// A contact retains its first observed rejection; these are causes, not sensor errors.
enum class PettingRejection : uint8_t {
    None = 0,
    OutsideBody = 1,
    FaceBand = 2,
    Cooldown = 3,
    NonMonotonic = 4,
    SampleGap = 5,
    MaxDuration = 6,
    ZoneChanged = 7,
    TooFast = 8,
};

// Artistic gesture state; no additional sensor owner, allocation or event queue.
struct PettingState {
    PettingZone zone = PettingZone::None;
    CODY2D::Vec2 origin{};
    CODY2D::Vec2 accepted{};
    float contact_scale = 0;
    float path_pixels = 0;
    int64_t started_at_us = 0;
    int64_t sampled_at_us = 0;
    int64_t accepted_at_us = 0;
    int64_t last_triggered_at_us = 0;
    PettingRejection rejection = PettingRejection::None;
    int64_t rejected_at_us = 0;
    CODY2D::Vec2 rejected_point{};
    int64_t rejected_sample_gap_us = 0;
    float rejected_speed_pixels_per_second = 0;
    PettingZone rejected_zone = PettingZone::None;
    bool rejected_body_hit = false;
    // A time/geometry rejection occurs before speed is evaluated; zero is not a measurement then.
    bool rejected_speed_valid = false;
    bool contact_active = false;
    bool rejected = false;
    bool triggered = false;
};

// rejection: Typed first-contact cause; unknown values are explicitly named Invalid.
const char *petting_rejection_name(PettingRejection rejection);

// state: Gesture state to cancel while retaining the last accepted cooldown.
void cancel_petting(PettingState &state);

// scene: Last fully displayed geometry, in the logical touch coordinate system.
// point: One real acquired touch coordinate; release coordinates are not classified.
// pressed: Acquired contact state.
// pressed_edge: True only for the first acquired report of a contact.
// sampled_at_us: Original successful acquisition timestamp.
// state: Caller-owned gesture state, updated only after geometry validation.
// triggered_zone: Receives None or the single recognized caress for this contact.
// Exception: Returns invalid sample/geometry and logs the actual rejected input.
esp_err_t observe_petting(const RigScene &scene, CODY2D::Vec2 point, bool pressed,
                         bool pressed_edge, int64_t sampled_at_us, PettingState &state,
                         PettingZone &triggered_zone);

} // namespace CODY
