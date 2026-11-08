#pragma once

#include "CODY_response.hpp"
#include "CODY_happiness.hpp"
#include "personality_catalog.hpp"

namespace LUNASHELL {
struct CodexLinkSnapshot;
struct CodexTechnicalSnapshot;
}

namespace CODY {

enum class CharacterCompactionPose : uint8_t { Neutral, Concentrating, Release };
enum class CharacterWorkKind : uint8_t { None, Thinking, Editing, Running };
enum class CharacterReaction : uint8_t { None, Attention, ReplyReady, Interrupted };

// Identity is borrowed for one synchronous update, then copied into the bounded artistic owner.
struct CharacterActivityObservation {
    const char *chat = "";
    uint64_t compaction_sequence = 0;
    bool current = false;
    bool working = false;
    bool attention = false;
    bool compacting = false;
    bool refreshed = false;
    const char *turn = "";
    CharacterWorkKind work_kind = CharacterWorkKind::None;
    bool ready = false;
    bool interrupted = false;
};

struct CharacterDirectionInputs {
    CharacterActivityObservation activity{};
    Transform placement{};
    CODY2D::Vec2 gaze_point{};
    int64_t caress_at_us = 0;
    float positive_affect = 0;
    bool enabled = false;
    bool paused = false;
    bool rebase = false;
    bool gaze_visible = false;
    bool affection_allowed = false;
};
static_assert(sizeof(CharacterDirectionInputs) <= 96, "Character inputs must remain a bounded projection");

// One small artistic owner; no telemetry history, event queue or second happiness value.
struct CharacterDirectionState {
    char chat[65]{};
    char turn[65]{};
    uint64_t compaction_sequence = 0;
    int64_t consumed_caress_at_us = 0;
    uint32_t afterglow_remaining_us = 0;
    uint32_t release_remaining_us = 0;
    uint32_t reaction_remaining_us = 0;
    uint32_t work_elapsed_us = 0;
    float gaze_x = 0;
    float gaze_y = 0;
    float warmth = 0;
    float compaction = 0;
    float work = 0;
    CharacterCompactionPose pose = CharacterCompactionPose::Neutral;
    CharacterWorkKind work_kind = CharacterWorkKind::None;
    CharacterReaction reaction = CharacterReaction::None;
    bool current = false;
    bool compacting = false;
    bool attention = false;
    bool enabled = false;
    bool working = false;
    bool ready = false;
    bool interrupted = false;
};
static_assert(sizeof(CharacterDirectionState) <= 208,
    "Character acting and exact turn identity must remain in the display-owned bounded state");

// link: Canonical link/activity with their original independent health.
// technical: Canonical technical source with exact ACTIVITY ownership and source freshness.
// observation: Receives a borrowed semantic projection; stale data cannot imply completion.
// Exception: Returns invalid current identities instead of guessing a phase.
esp_err_t project_character_activity(const LUNASHELL::CodexLinkSnapshot &link,
    const LUNASHELL::CodexTechnicalSnapshot &technical, CharacterActivityObservation &observation);

// config: Authored happiness bounds and neutral initial value.
// happiness: Existing artistic state, retaining its awake decay clock and derived sadness.
// affect: Receives positive warmth above the initial value; unattended values never cause a frown.
// Exception: Returns the original invalid mood configuration/state error.
esp_err_t positive_character_affect(const HappinessConfig &config,
    const HappinessState &happiness, float &affect);

// inputs: Canonical observations and actual displayed targets, or labelled synthetic host inputs.
// active_delta_us: Animation duration excluding pause and standby.
// state: Previous serialized artistic state, unchanged on failure.
// next: Receives a complete update on success; may alias state.
// Exception: Returns invalid clocks, identities, geometry or artistic values with the cause logged.
esp_err_t advance_character_direction(const CharacterDirectionInputs &inputs, int64_t active_delta_us,
    const CharacterDirectionState &state, CharacterDirectionState &next);

// state: Current bounded gaze, warmth, Codex work/reaction and genuine compaction presentation.
// frame: Native response; acquired motion, audio and protective eye closure retain ownership.
// Exception: Returns original rig/contour errors without mutating a rejected frame.
esp_err_t compose_character_direction(const CharacterDirectionState &state, ResponseFrame &frame);

// state: Current source admission and acting; unavailable facts never retain their manga request.
PersonalityCue character_direction_cue(const CharacterDirectionState &state);

// state: Canonical current actor state; question attention suppresses compaction priority.
// fallback: Existing scene/request cue, before sleep/wake and device-safety arbitration.
PersonalityCue prioritize_compaction_cue(const CharacterDirectionState &state, PersonalityCue fallback);

// Display-owned identity only: the PC restarts sequence numbering for each turn.
// No canonical snapshot, audio buffer, dynamic allocation or additional task is retained.
struct CompactionSoundLatch {
    char chat[65]{};
    char turn[65]{};
    uint64_t sequence = 0;
};
static_assert(sizeof(CompactionSoundLatch) <= 144, "Compaction sound chat/turn/sequence remains within 144 display-owned bytes");

// state: Genuine actor facts admitted from the matching current canonical owner.
// retired_visible: A complete ordinary Home frame displayed this cue, without pause or shutdown.
// retired_cue: Cue actually visible after raster retirement, never merely requested.
// latch: Last admitted chat/turn/sequence; temporary attention or source loss never resets it.
// Returns whether this observed lifecycle may enter the existing bounded audio admission path.
bool claim_compaction_sound(const CharacterDirectionState &state, bool retired_visible,
    PersonalityCue retired_cue, CompactionSoundLatch &latch);

} // namespace CODY
