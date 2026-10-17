#include "CODY_character_direction.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "cloud_rig_data.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr char kChat[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr char kOtherChat[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr char kTurn[] = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
constexpr char kOtherTurn[] = "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";

// condition: Observable outcome of the real shared native implementation.
// message: Concrete failure retained in stderr without changing runtime fixture JSON.
bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "Character direction contract failed: %s\n", message);
    }
    return condition;
}

// component: Explicit host fixture, never a published firmware sample.
void admit(LUNASHELL::ComponentSnapshot &component)
{
    component.health = LUNASHELL::DeviceHealth::Ready;
    component.data_valid = true;
    component.data_stale = false;
    component.last_error = ESP_OK;
}

// No parameters: genuine same-turn completion, one attention performance and protected native channels.
// Exception: Returns a nonzero host contract result without publishing synthetic firmware samples.
int check_codex_acting()
{
    using Reaction = CODY::CharacterReaction;
    CODY::CharacterDirectionInputs inputs{};
    inputs.enabled = true;
    inputs.activity.chat = kChat;
    inputs.activity.turn = kTurn;
    inputs.activity.current = inputs.activity.working = true;
    inputs.activity.work_kind = CODY::CharacterWorkKind::Editing;
    CODY::CharacterDirectionState working{};
    if (!expect(CODY::advance_character_direction(inputs, 1000000, working, working) == ESP_OK &&
        working.work > 0 && working.work_kind == CODY::CharacterWorkKind::Editing,
        "genuine editing did not contribute work acting")) { return 1; }
    inputs.activity.working = false;
    inputs.activity.work_kind = CODY::CharacterWorkKind::None;
    inputs.activity.ready = true;
    CODY::CharacterDirectionState ready{};
    if (!expect(CODY::advance_character_direction(inputs, 0, working, ready) == ESP_OK &&
        ready.reaction == Reaction::ReplyReady, "same-turn reply did not celebrate")) { return 1; }
    if (!expect(CODY::advance_character_direction(inputs, 900000, ready, ready) == ESP_OK &&
        CODY::character_direction_cue(ready) == CODY::PersonalityCue::Joy,
        "visible same-turn completion lost its manga reaction")) { return 1; }
    CODY::ResponseFrame frame{};
    frame.face = CODY::Data::ready;
    frame.face[CODY::FaceChannel::EyeLeft] = 0;
    frame.face[CODY::FaceChannel::MouthOpen] = .63f;
    frame.contour = CODY::contour_defaults();
    frame.transform = {251, 210, 130};
    const float smile = frame.face[CODY::FaceChannel::MouthCornerLeft];
    if (!expect(CODY::compose_character_direction(ready, frame) == ESP_OK &&
        frame.face[CODY::FaceChannel::MouthCornerLeft] > smile &&
        frame.face[CODY::FaceChannel::EyeLeft] == 0 && frame.face[CODY::FaceChannel::MouthOpen] == .63f &&
        frame.transform.x == 251 && frame.transform.y == 210 && frame.transform.scale == 130,
        "completion failed to smile or replaced protective/audio/root ownership")) { return 1; }
    if (!expect(CODY::advance_character_direction(inputs, 1800000, ready, ready) == ESP_OK &&
        ready.reaction == Reaction::None, "repeated ready frames restarted celebration")) { return 1; }
    CODY::CharacterDirectionState rejected{};
    inputs.activity.turn = kOtherTurn;
    if (!expect(CODY::advance_character_direction(inputs, 100000, working, rejected) == ESP_OK &&
        rejected.reaction == Reaction::None, "different turn became a completed task")) { return 1; }
    inputs.activity.turn = "";
    if (!expect(CODY::advance_character_direction(inputs, 100000, working, rejected) == ESP_OK &&
        rejected.reaction == Reaction::None, "missing turn became a completed task")) { return 1; }
    inputs.activity.turn = kTurn;
    inputs.activity.current = false;
    CODY::CharacterDirectionState gap{};
    if (!expect(CODY::advance_character_direction(inputs, 100000, working, gap) == ESP_OK,
        "source-gap contract rejected an unavailable observation")) { return 1; }
    inputs.activity.current = true;
    if (!expect(CODY::advance_character_direction(inputs, 100000, gap, rejected) == ESP_OK &&
        rejected.reaction == Reaction::None, "source recovery celebrated an unobserved end")) { return 1; }
    inputs.paused = true;
    if (!expect(CODY::advance_character_direction(inputs, 100000, working, gap) == ESP_OK,
        "pause could not consume a reply edge")) { return 1; }
    inputs.paused = false;
    if (!expect(CODY::advance_character_direction(inputs, 100000, gap, rejected) == ESP_OK &&
        rejected.reaction == Reaction::None, "paused reply replayed a celebration")) { return 1; }
    inputs.activity.ready = false;
    inputs.activity.attention = true;
    CODY::CharacterDirectionState attention{};
    if (!expect(CODY::advance_character_direction(inputs, 0, working, attention) == ESP_OK &&
        attention.reaction == Reaction::Attention, "attention edge did not start its performance")) { return 1; }
    if (!expect(CODY::advance_character_direction(inputs, 324000, attention, attention) == ESP_OK &&
        attention.reaction == Reaction::Attention, "persistent attention cancelled its double blink")) { return 1; }
    frame.face = CODY::Data::ready;
    const float open = frame.face[CODY::FaceChannel::EyeRight];
    if (!expect(CODY::compose_character_direction(attention, frame) == ESP_OK &&
        frame.face[CODY::FaceChannel::EyeRight] < open * .1f &&
        CODY::character_direction_cue(attention) == CODY::PersonalityCue::Curiosity,
        "attention failed to blink or lost its current cue")) { return 1; }
    if (!expect(CODY::advance_character_direction(inputs, 1800000, attention, attention) == ESP_OK &&
        attention.reaction == Reaction::None, "persistent attention replayed a transient blink")) { return 1; }
    return 0;
}
} // namespace

// No parameters: exercise truthful completion, gaze/affect composition and route/pause isolation.
// Exception: Returns a nonzero contract result; no sensor or hardware service is executed.
int check_character_direction_contract()
{
    if (check_codex_acting() != 0) { return 1; }
    using Pose = CODY::CharacterCompactionPose;
    CODY::CharacterDirectionInputs inputs{};
    inputs.enabled = inputs.affection_allowed = true;
    inputs.activity.chat = kChat;
    inputs.activity.turn = kTurn;
    inputs.activity.current = inputs.activity.compacting = true;
    inputs.activity.compaction_sequence = 7;
    CODY::CharacterDirectionState active{};
    if (!expect(CODY::advance_character_direction(inputs, 1000000, active, active) == ESP_OK &&
        active.pose == Pose::Concentrating && CODY::character_direction_cue(active) == CODY::PersonalityCue::Confusion,
        "current start did not enter concentration")) { return 1; }
    if (!expect(CODY::prioritize_compaction_cue(active, CODY::PersonalityCue::Joy) ==
        CODY::PersonalityCue::Confusion, "authored working cue masked genuine compaction")) { return 1; }
    if (!expect(CODY::prioritize_compaction_cue(active, CODY::PersonalityCue::LowBattery) ==
        CODY::PersonalityCue::LowBattery && CODY::prioritize_compaction_cue(active, CODY::PersonalityCue::Sleep) ==
        CODY::PersonalityCue::Sleep, "compaction replaced device or sleep priority")) { return 1; }
    CODY::CompactionSoundLatch sound_latch{};
    if (!expect(!CODY::claim_compaction_sound(active, false, CODY::PersonalityCue::Confusion, sound_latch) &&
        CODY::claim_compaction_sound(active, true, CODY::PersonalityCue::Confusion, sound_latch) &&
        !CODY::claim_compaction_sound(active, true, CODY::PersonalityCue::Confusion, sound_latch),
        "compaction sound must first retire visibly and admit exactly once")) { return 1; }
    auto resumed = active;
    resumed.attention = true;
    if (!expect(!CODY::claim_compaction_sound(resumed, true, CODY::PersonalityCue::Curiosity, sound_latch),
        "attention originated a compaction sound")) { return 1; }
    resumed.attention = false;
    if (!expect(!CODY::claim_compaction_sound(resumed, true, CODY::PersonalityCue::Confusion, sound_latch),
        "attention/resume replayed an admitted compaction sound")) { return 1; }
    ++resumed.compaction_sequence;
    if (!expect(CODY::claim_compaction_sound(resumed, true, CODY::PersonalityCue::Confusion, sound_latch),
        "second genuine sequence was lost while the same symbol remained visible")) { return 1; }
    std::strcpy(resumed.turn, kOtherTurn);
    if (!expect(CODY::claim_compaction_sound(resumed, true, CODY::PersonalityCue::Confusion, sound_latch),
        "new turn reusing a compaction sequence lost its sound admission")) { return 1; }
    ++resumed.compaction_sequence;
    resumed.current = false;
    if (!expect(!CODY::claim_compaction_sound(resumed, true, CODY::PersonalityCue::Confusion, sound_latch),
        "stale lifecycle originated a compaction sound")) { return 1; }
    auto attention = active;
    attention.attention = true;
    if (!expect(CODY::prioritize_compaction_cue(attention, CODY::PersonalityCue::Curiosity) ==
        CODY::PersonalityCue::Curiosity, "compaction replaced question priority")) { return 1; }
    auto stale = active;
    stale.current = false;
    if (!expect(CODY::prioritize_compaction_cue(stale, CODY::PersonalityCue::Joy) ==
        CODY::PersonalityCue::Joy, "stale compaction replaced the current scene")) { return 1; }
    inputs.activity.compacting = false;
    inputs.activity.refreshed = true;
    CODY::CharacterDirectionState completed{};
    if (!expect(CODY::advance_character_direction(inputs, 100000, active, completed) == ESP_OK &&
        completed.pose == Pose::Release &&
        CODY::prioritize_compaction_cue(completed, CODY::PersonalityCue::Joy) == CODY::PersonalityCue::Joy,
        "real same-sequence end did not release")) { return 1; }
    CODY::CharacterDirectionState other_turn{};
    inputs.activity.turn = kOtherTurn;
    if (!expect(CODY::advance_character_direction(inputs, 100000, active, other_turn) == ESP_OK &&
        other_turn.pose != Pose::Release, "new turn reusing a sequence invented prior completion")) { return 1; }
    inputs.activity.turn = kTurn;
    inputs.activity.compaction_sequence = 8;
    CODY::CharacterDirectionState mismatched{};
    if (!expect(CODY::advance_character_direction(inputs, 100000, active, mismatched) == ESP_OK &&
        mismatched.pose != Pose::Release, "different sequence became completion")) { return 1; }
    inputs.activity.compaction_sequence = 7;
    inputs.activity.chat = kOtherChat;
    if (!expect(CODY::advance_character_direction(inputs, 100000, active, mismatched) == ESP_OK &&
        mismatched.pose != Pose::Release, "different chat became completion")) { return 1; }
    inputs.activity.chat = kChat;
    inputs.rebase = true;
    if (!expect(CODY::advance_character_direction(inputs, 0, active, mismatched) == ESP_OK &&
        mismatched.pose != Pose::Release, "wake replayed a completion")) { return 1; }
    inputs.rebase = false;
    inputs.activity.current = false;
    CODY::CharacterDirectionState gap{};
    if (!expect(CODY::advance_character_direction(inputs, 100000, active, gap) == ESP_OK &&
        CODY::character_direction_cue(gap) == CODY::PersonalityCue::None, "source gap retained manga truth")) { return 1; }
    inputs.activity.current = true;
    if (!expect(CODY::advance_character_direction(inputs, 100000, gap, mismatched) == ESP_OK &&
        mismatched.pose != Pose::Release, "source recovery inferred a missing end")) { return 1; }
    inputs.paused = true;
    if (!expect(CODY::advance_character_direction(inputs, 1000000, active, gap) == ESP_OK &&
        gap.compaction == active.compaction && gap.pose == active.pose, "pause moved the visible pose")) { return 1; }
    inputs.paused = false;
    if (!expect(CODY::advance_character_direction(inputs, 100000, gap, mismatched) == ESP_OK &&
        mismatched.pose == Pose::Neutral, "paused source edge replayed a success gesture")) { return 1; }

    // Canonical source admission rejects a fresh-looking technical packet from another activity revision.
    LUNASHELL::CodexLinkSnapshot link{};
    LUNASHELL::CodexTechnicalSnapshot technical{};
    admit(link);
    admit(link.activity);
    admit(technical);
    link.state = LUNASHELL::CodexLinkState::Connected;
    link.activity.state = LUNASHELL::CodexActivityState::Working;
    link.activity.revision = 3;
    std::strcpy(link.activity.chat_id, kChat);
    std::strcpy(technical.chat_id, kChat);
    technical.revision = 2;
    technical.compaction_valid = true;
    technical.compaction = LUNASHELL::CodexCompactionState::Active;
    technical.compaction_sequence = 7;
    CODY::CharacterActivityObservation observation{};
    if (!expect(CODY::project_character_activity(link, technical, observation) == ESP_OK &&
        observation.current && !observation.compacting, "revision mismatch animated current compaction")) { return 1; }
    technical.revision = 3;
    std::strcpy(technical.turn_id, kTurn);
    if (!expect(CODY::project_character_activity(link, technical, observation) == ESP_OK &&
        std::strcmp(observation.turn, kTurn) == 0 && observation.work_kind == CODY::CharacterWorkKind::Thinking,
        "coherent turn/work kind did not reach the director")) { return 1; }
    technical.compaction = LUNASHELL::CodexCompactionState::Orphaned;
    if (!expect(CODY::project_character_activity(link, technical, observation) == ESP_OK &&
        !observation.compacting && !observation.refreshed,
        "orphaned compaction invented an active or completed pose")) { return 1; }
    technical.compaction = LUNASHELL::CodexCompactionState::Active;
    technical.data_stale = true;
    if (!expect(CODY::project_character_activity(link, technical, observation) == ESP_OK &&
        !observation.compacting && observation.turn[0] == '\0',
        "stale technical source retained compaction/turn acting")) { return 1; }
    link.data_stale = true;
    if (!expect(CODY::project_character_activity(link, technical, observation) == ESP_OK &&
        !observation.current, "stale link retained current acting")) { return 1; }

    CODY::HappinessConfig mood{0, 100, 50, 10, 1, 60000000};
    float affect = -1;
    if (!expect(CODY::positive_character_affect(mood, {0, 0}, affect) == ESP_OK && affect == 0,
        "unattended mood produced a negative facial effect")) { return 1; }
    if (!expect(CODY::positive_character_affect(mood, {60, 0}, affect) == ESP_OK && std::fabs(affect - .2f) < .0001f,
        "existing happiness did not project positive warmth")) { return 1; }
    inputs.activity = {};
    inputs.gaze_visible = true;
    inputs.gaze_point = {360, 180};
    inputs.caress_at_us = 1000000;
    CODY::CharacterDirectionState warm{};
    if (!expect(CODY::advance_character_direction(inputs, 5000000, warm, warm) == ESP_OK &&
        warm.warmth > 0 && warm.gaze_x > 0 && warm.gaze_y < 0,
        "accepted stroke/gaze did not affect the character")) { return 1; }
    CODY::ResponseFrame frame{};
    frame.face = CODY::Data::ready;
    frame.face[CODY::FaceChannel::EyeLeft] = 0;
    frame.face[CODY::FaceChannel::MouthOpen] = .6f;
    frame.contour = CODY::contour_defaults();
    frame.transform = {251, 210, 130};
    if (!expect(CODY::compose_character_direction(warm, frame) == ESP_OK &&
        frame.face[CODY::FaceChannel::EyeLeft] == 0 && frame.face[CODY::FaceChannel::MouthOpen] == .6f &&
        frame.transform.x == 251 && frame.transform.y == 210 && frame.transform.scale == 130,
        "direction replaced protective closure, audio mouth or acquired motion")) { return 1; }
    inputs.enabled = false;
    if (!expect(CODY::advance_character_direction(inputs, 100000, warm, warm) == ESP_OK && !warm.enabled &&
        warm.afterglow_remaining_us == 0, "Debug/setup retained a pending caress performance")) { return 1; }
    inputs.enabled = true;
    inputs.positive_affect = 2;
    const auto prior = warm;
    if (!expect(CODY::advance_character_direction(inputs, 100000, warm, warm) == ESP_ERR_INVALID_ARG &&
        warm.consumed_caress_at_us == prior.consumed_caress_at_us && warm.warmth == prior.warmth,
        "invalid input partially changed artistic state")) { return 1; }
    return 0;
}
