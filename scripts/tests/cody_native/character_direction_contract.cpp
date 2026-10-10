#include "CODY_character_direction.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "cloud_rig_data.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr char kChat[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr char kOtherChat[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

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
} // namespace

// No parameters: exercise truthful completion, gaze/affect composition and route/pause isolation.
// Exception: Returns a nonzero contract result; no sensor or hardware service is executed.
int check_character_direction_contract()
{
    using Pose = CODY::CharacterCompactionPose;
    CODY::CharacterDirectionInputs inputs{};
    inputs.enabled = inputs.affection_allowed = true;
    inputs.activity.chat = kChat;
    inputs.activity.current = inputs.activity.compacting = true;
    inputs.activity.compaction_sequence = 7;
    CODY::CharacterDirectionState active{};
    if (!expect(CODY::advance_character_direction(inputs, 1000000, active, active) == ESP_OK &&
        active.pose == Pose::Concentrating && CODY::character_direction_cue(active) == CODY::PersonalityCue::Confusion,
        "current start did not enter concentration")) { return 1; }
    inputs.activity.compacting = false;
    inputs.activity.refreshed = true;
    CODY::CharacterDirectionState completed{};
    if (!expect(CODY::advance_character_direction(inputs, 100000, active, completed) == ESP_OK &&
        completed.pose == Pose::Release, "real same-sequence end did not release")) { return 1; }
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
    technical.data_stale = true;
    if (!expect(CODY::project_character_activity(link, technical, observation) == ESP_OK &&
        !observation.compacting, "stale technical source animated compaction")) { return 1; }
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
