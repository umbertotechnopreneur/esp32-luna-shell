#pragma once

#include <cstdint>
#include "CODY_damage.hpp"

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

struct PersonalityFrame {
    PersonalityCue cue = PersonalityCue::None;
    DamageRect bounds{};
};

// Fixed artistic state; no telemetry cache, heap ownership or frame-count clock.
struct PersonalityLag {
    PersonalityCue cue = PersonalityCue::None;
    CodySize size = CodySize::Micro;
    uint32_t route = 0;
    uint16_t rotation = 0;
    float center_x = 0;
    float center_y = 0;
    float width = 0;
    float height = 0;
    int64_t sampled_at_us = 0;
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

// scene: Evaluated pose owning the actual crown/upper-lobe anchors.
// cue: Single selected symbol after canonical device-state gating.
// size: Stable nominal character presentation, independent of animated scale.
// route: Stable route identity used to expire lag across navigation.
// rotation: Shared logical orientation; a change resets the lag without rotating it separately.
// now_us: Monotonic frame timestamp; stale/reversed intervals prime without catch-up.
// paused: Freeze lag time while still accepting an explicit changed selection.
// reset: Re-prime after wake or invalidated frame ownership.
// exclusions: Visible content, title and navigation rectangles to preserve.
// exclusion_count: Number of readable exclusions, at most six.
// lag: Caller-owned fixed artistic state, committed only on success.
// frame: Receives clipped, circle-safe sprite bounds; Micro always produces an empty frame.
// Exception: Returns invalid pose/identity/layout arguments with the real cause logged.
esp_err_t prepare_personality_frame(
    const RigScene &scene, PersonalityCue cue, CodySize size, uint32_t route,
    uint16_t rotation, int64_t now_us, bool paused, bool reset,
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
