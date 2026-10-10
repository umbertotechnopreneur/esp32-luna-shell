#include "CODY_character_direction.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "LUNASHELL_device_snapshot.hpp"
#include "cloud_rig_data.hpp"
#include "esp_log.h"

namespace CODY {
namespace {
constexpr char kTag[] = "CODY_direction";
constexpr uint32_t kAfterglowUs = 30000000;
constexpr uint32_t kReleaseUs = 2000000;

// field: Exact rejected contract field.
// value: Original invalid value, without substitution.
// Exception: Returns invalid argument with the actual cause logged.
esp_err_t reject(const char *field, double value)
{
    ESP_LOGE(kTag, "%s rejected: %.9g (%s)", field, value, esp_err_to_name(ESP_ERR_INVALID_ARG));
    return ESP_ERR_INVALID_ARG;
}

// value: Canonical component health; failed or old observations cannot drive acting.
bool current_component(const LUNASHELL::ComponentSnapshot &value)
{
    return value.health == LUNASHELL::DeviceHealth::Ready && value.data_valid &&
        !value.data_stale && value.last_error == ESP_OK;
}

// chat: Bounded canonical SHA-256 identity, read within its declared 65-byte contract.
bool valid_chat(const char *chat)
{
    if (chat == nullptr) {
        return false;
    }
    for (size_t index = 0; index < 64; ++index) {
        const char value = chat[index];
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) {
            return false;
        }
    }
    return chat[64] == '\0';
}

// point: Actual admitted logical information or acquired touch target.
bool valid_point(CODY2D::Vec2 point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) &&
        point.x >= 0 && point.x < 466 && point.y >= 0 && point.y < 466;
}

// state: Fixed artistic owner; invalid state is rejected rather than repaired.
// Exception: Returns the real invalid identity, timer or artistic value.
esp_err_t validate_state(const CharacterDirectionState &state)
{
    if (state.pose > CharacterCompactionPose::Release || state.afterglow_remaining_us > kAfterglowUs ||
        state.release_remaining_us > kReleaseUs || state.consumed_caress_at_us < 0 ||
        (state.current && !valid_chat(state.chat))) {
        return reject("state.identity_or_clock", static_cast<unsigned>(state.pose));
    }
    for (float value : {state.warmth, state.compaction}) {
        if (!std::isfinite(value) || value < 0 || value > 1) {
            return reject("state.weight", value);
        }
    }
    for (float value : {state.gaze_x, state.gaze_y}) {
        if (!std::isfinite(value) || value < -1 || value > 1) {
            return reject("state.gaze", value);
        }
    }
    return ESP_OK;
}

// previous: Bounded artistic value, never a measurement.
// target: Bounded artistic target.
// delta_us: Active animation duration; long frames settle without replaying gestures.
// seconds: Positive authored response duration.
float approach(float previous, float target, int64_t delta_us, float seconds)
{
    const float weight = static_cast<float>(-std::expm1(-static_cast<double>(delta_us) / (seconds * 1000000.0)));
    return previous + (target - previous) * weight;
}

// face: Already valid native face.
// channel: Semantic facial identity.
// amount: Signed artistic weight in [-1,1], approaching the existing authored boundary.
void offset_channel(Channels &face, FaceChannel channel, float amount)
{
    const auto index = to_index(channel);
    const float target = amount >= 0 ? Data::channel_max[index] : Data::channel_min[index];
    face[channel] += (target - face[channel]) * std::fabs(amount);
}
} // namespace

// link: Coherent canonical source retaining independent link/activity health.
// technical: Source freshness and exact activity ownership are checked separately.
// observation: Receives a borrowed semantic projection, without a sensor read or source mutation.
// Exception: Returns invalid current phase or compaction identity.
esp_err_t project_character_activity(const LUNASHELL::CodexLinkSnapshot &link,
    const LUNASHELL::CodexTechnicalSnapshot &technical, CharacterActivityObservation &observation)
{
    CharacterActivityObservation result{};
    if (!current_component(link) || link.state != LUNASHELL::CodexLinkState::Connected ||
        !current_component(link.activity)) {
        observation = result;
        return ESP_OK;
    }
    if (!valid_chat(link.activity.chat_id)) {
        return reject("activity.chat", 0);
    }
    using Phase = LUNASHELL::CodexActivityState;
    switch (link.activity.state) {
    case Phase::Working:
    case Phase::Editing:
    case Phase::Running: result.working = true; break;
    case Phase::Paused: result.attention = true; break;
    case Phase::None:
    case Phase::Active:
    case Phase::ResponseReady:
    case Phase::Interrupted:
    case Phase::Ended: break;
    case Phase::Unavailable:
        observation = result;
        return ESP_OK;
    default: return reject("activity.phase", static_cast<unsigned>(link.activity.state));
    }
    result.current = true;
    result.chat = link.activity.chat_id;
    const bool same_owner = current_component(technical) && technical.compaction_valid &&
        technical.revision == link.activity.revision &&
        std::strncmp(technical.chat_id, link.activity.chat_id, sizeof(technical.chat_id)) == 0;
    if (same_owner) {
        result.compaction_sequence = technical.compaction_sequence;
        using Compaction = LUNASHELL::CodexCompactionState;
        switch (technical.compaction) {
        case Compaction::Active: result.compacting = true; break;
        case Compaction::Refreshed: result.refreshed = true; break;
        case Compaction::None:
        case Compaction::Orphaned:
        case Compaction::Interrupted:
        case Compaction::Stale: break;
        default: return reject("activity.compaction", static_cast<unsigned>(technical.compaction));
        }
    }
    observation = result;
    return ESP_OK;
}

// config: Authored happiness domain and neutral initial value.
// happiness: Existing session-only owner, validated without advancing its decay clock.
// affect: Receives positive warmth only; unattended values never become an automatic frown.
// Exception: Returns the original mood configuration/state error.
esp_err_t positive_character_affect(const HappinessConfig &config,
    const HappinessState &happiness, float &affect)
{
    HappinessState checked = happiness;
    const esp_err_t error = advance_happiness(config, 0, checked);
    if (error != ESP_OK) {
        return error;
    }
    affect = happiness.happiness <= config.initial ? 0.f :
        static_cast<float>(happiness.happiness - config.initial) / (config.maximum - config.initial);
    return ESP_OK;
}

// inputs: Canonical or explicitly synthetic observations and one actual displayed gaze target.
// active_delta_us: Animation duration excluding standby and pause.
// state: Previous bounded artistic owner, unchanged on rejection.
// next: Receives a complete update only on success; may alias state.
// Exception: Returns actual invalid clocks, geometry, identities or artistic values.
esp_err_t advance_character_direction(const CharacterDirectionInputs &inputs, int64_t active_delta_us,
    const CharacterDirectionState &state, CharacterDirectionState &next)
{
    esp_err_t error = validate_state(state);
    if (error != ESP_OK) {
        return error;
    }
    if (active_delta_us < 0 || inputs.caress_at_us < 0) {
        return reject("input.clock", static_cast<double>(active_delta_us));
    }
    if (!std::isfinite(inputs.positive_affect) || inputs.positive_affect < 0 || inputs.positive_affect > 1) {
        return reject("input.affect", inputs.positive_affect);
    }
    if (!valid_point({inputs.placement.x, inputs.placement.y}) || !std::isfinite(inputs.placement.scale) ||
        inputs.placement.scale < 20 || inputs.placement.scale > 200 ||
        (inputs.gaze_visible && !valid_point(inputs.gaze_point))) {
        return reject("input.geometry", inputs.placement.scale);
    }
    if ((inputs.activity.current && !valid_chat(inputs.activity.chat)) ||
        (inputs.activity.compacting && inputs.activity.refreshed)) {
        return reject("input.activity", 0);
    }
    CharacterDirectionState result = state;
    const bool same_chat = !inputs.rebase && state.current && inputs.activity.current &&
        std::strcmp(state.chat, inputs.activity.chat) == 0;
    const bool compacting = inputs.activity.current && inputs.activity.compacting;
    // Success requires observing the real end of the same sequence, without a source gap or wake.
    const bool refreshed = state.enabled && same_chat && state.compacting && inputs.activity.refreshed &&
        state.compaction_sequence == inputs.activity.compaction_sequence;
    const bool caress = !inputs.rebase && inputs.caress_at_us > state.consumed_caress_at_us;
    result.current = inputs.activity.current;
    result.compacting = compacting;
    result.attention = inputs.activity.current && inputs.activity.attention;
    result.compaction_sequence = inputs.activity.current ? inputs.activity.compaction_sequence : 0;
    result.consumed_caress_at_us = std::max(state.consumed_caress_at_us, inputs.caress_at_us);
    if (inputs.activity.current) {
        std::memcpy(result.chat, inputs.activity.chat, sizeof(result.chat));
    } else {
        result.chat[0] = '\0';
    }
    if (!inputs.enabled) {
        // Exact Debug/preset/setup poses keep ownership; hidden events cannot replay on exit.
        result.enabled = false;
        result.pose = CharacterCompactionPose::Neutral;
        result.afterglow_remaining_us = result.release_remaining_us = 0;
        result.gaze_x = result.gaze_y = result.warmth = result.compaction = 0;
        next = result;
        return ESP_OK;
    }
    if (inputs.paused) {
        // Freeze the pose while consuming edges; pause never creates a deferred success gesture.
        // A changed/absent source breaks continuity without changing the frozen artistic weights.
        result.current = same_chat;
        next = result;
        return ESP_OK;
    }
    result.enabled = true;
    if (!same_chat || (state.compacting && !compacting && !refreshed)) {
        result.pose = CharacterCompactionPose::Neutral;
        result.release_remaining_us = 0;
    }
    if (compacting) {
        result.pose = CharacterCompactionPose::Concentrating;
        result.release_remaining_us = 0;
    } else if (refreshed && !result.attention) {
        result.pose = CharacterCompactionPose::Release;
        result.release_remaining_us = kReleaseUs;
    } else if (result.pose == CharacterCompactionPose::Concentrating) {
        result.pose = CharacterCompactionPose::Neutral;
    } else if (result.pose == CharacterCompactionPose::Release) {
        result.release_remaining_us -= static_cast<uint32_t>(
            std::min<int64_t>(active_delta_us, result.release_remaining_us));
        if (result.release_remaining_us == 0) {
            result.pose = CharacterCompactionPose::Neutral;
        }
    }
    result.afterglow_remaining_us -= static_cast<uint32_t>(
        std::min<int64_t>(active_delta_us, result.afterglow_remaining_us));
    const bool affection = inputs.affection_allowed && !compacting && !result.attention && !inputs.activity.working;
    if (caress && affection) {
        // Thirty seconds of warmth belong to this accepted stroke, not to a new mood counter.
        result.afterglow_remaining_us = kAfterglowUs;
    }
    const float warmth = affection ? std::max(inputs.positive_affect,
        .35f * result.afterglow_remaining_us / kAfterglowUs) : 0;
    result.warmth = approach(state.warmth, warmth, active_delta_us, .8f);
    result.compaction = approach(state.compaction, compacting ? 1.f : 0.f, active_delta_us, .35f);
    const float gaze_x = inputs.gaze_visible ? .75f * std::tanh(
        (inputs.gaze_point.x - inputs.placement.x) / inputs.placement.scale) : 0;
    const float gaze_y = inputs.gaze_visible ? .65f * std::tanh(
        (inputs.gaze_point.y - inputs.placement.y) / inputs.placement.scale) : 0;
    result.gaze_x = approach(state.gaze_x, gaze_x, active_delta_us, .18f);
    result.gaze_y = approach(state.gaze_y, gaze_y, active_delta_us, .22f);
    error = validate_state(result);
    if (error == ESP_OK) {
        next = result;
    }
    return error;
}

// state: Valid artistic gaze, warmth and compaction presentation.
// frame: Native composition; closed eyes, measured audio mouth and acquired root motion survive.
// Exception: Returns original rig/contour errors without committing a partial frame.
esp_err_t compose_character_direction(const CharacterDirectionState &state, ResponseFrame &frame)
{
    esp_err_t error = validate_state(state);
    if (error != ESP_OK || !state.enabled) {
        return error;
    }
    Channels face{};
    error = advance_face(frame.face, frame.face, 0, face);
    ContourState contour{};
    if (error == ESP_OK) {
        error = advance_contour({frame.contour, 0}, frame.contour, 0, contour);
    }
    if (error != ESP_OK) {
        return error;
    }
    face[FaceChannel::GazeX] += (state.gaze_x - face[FaceChannel::GazeX]) * .85f;
    face[FaceChannel::GazeY] += (state.gaze_y - face[FaceChannel::GazeY]) * .85f;
    offset_channel(face, FaceChannel::BrowHeightLeft, .14f * state.compaction);
    offset_channel(face, FaceChannel::EyeCurveLeft, -.1f * state.compaction);
    offset_channel(face, FaceChannel::EyeCurveRight, -.1f * state.compaction);
    face[FaceChannel::MouthWidth] += (.92f - face[FaceChannel::MouthWidth]) * .22f * state.compaction;
    const float release_phase = 1.f - static_cast<float>(state.release_remaining_us) / kReleaseUs;
    const float sine = std::sin(3.14159265358979323846f * release_phase);
    const float stretch = state.pose == CharacterCompactionPose::Release ? .18f * sine * sine : 0;
    const float smile = .28f * state.warmth + stretch;
    offset_channel(face, FaceChannel::MouthCornerLeft, smile);
    offset_channel(face, FaceChannel::MouthCornerRight, smile * .85f);
    offset_channel(face, FaceChannel::CheekOpacity, .35f * smile);
    offset_channel(face, FaceChannel::CheekRaiseLeft, .3f * smile);
    offset_channel(face, FaceChannel::CheekRaiseRight, .3f * smile);
    auto &fullness = contour.controls[ContourControl::Fullness];
    fullness += (-.18f - fullness) * .35f * state.compaction;
    fullness += (1.f - fullness) * stretch;
    // Existing breathing, body springs and manga fading retain their own clocks and parameters.
    frame.face = face;
    frame.contour = contour.controls;
    return ESP_OK;
}

// state: Semantic admission rather than the slowly decaying artistic compaction weight.
PersonalityCue character_direction_cue(const CharacterDirectionState &state)
{
    return state.enabled && state.current && state.compacting && !state.attention
        ? PersonalityCue::Confusion : PersonalityCue::None;
}
} // namespace CODY
