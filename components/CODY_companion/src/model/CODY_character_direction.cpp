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
constexpr uint32_t kReactionUs = 1800000;
constexpr uint32_t kWorkCycleUs = 12000000;
constexpr float kPi = 3.14159265358979323846f;

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

// turn: Optional exact SHA-256 identity; empty means no coherent turn was observed.
bool valid_turn(const char *turn)
{
    return turn != nullptr && (turn[0] == '\0' || valid_chat(turn));
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
    if (state.pose > CharacterCompactionPose::Release || state.reaction > CharacterReaction::Interrupted ||
        state.work_kind > CharacterWorkKind::Running || state.work_elapsed_us >= kWorkCycleUs ||
        state.reaction_remaining_us > kReactionUs || !valid_turn(state.turn) ||
        state.afterglow_remaining_us > kAfterglowUs ||
        state.release_remaining_us > kReleaseUs || state.consumed_caress_at_us < 0 ||
        (state.current && !valid_chat(state.chat))) {
        return reject("state.identity_or_clock", static_cast<unsigned>(state.pose));
    }
    for (float value : {state.warmth, state.compaction, state.work}) {
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

// state: Current reaction and current observed phase after this frame's projection.
// same_chat: Continuous visible observation of the same selected Codex chat.
// same_turn: Exact continuous turn identity, required for task outcomes.
// No exceptions: This admission helper owns neither observations nor animation time.
bool reaction_supported(const CharacterDirectionState &state, bool same_chat, bool same_turn)
{
    if (!same_chat) {
        return false;
    }
    switch (state.reaction) {
    case CharacterReaction::Attention: return state.attention;
    case CharacterReaction::ReplyReady: return same_turn && state.ready && !state.attention && !state.compacting;
    case CharacterReaction::Interrupted: return same_turn && state.interrupted && !state.attention && !state.compacting;
    case CharacterReaction::None: return false;
    }
    return false;
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
        result.working = true;
        result.work_kind = CharacterWorkKind::Thinking;
        break;
    case Phase::Editing:
        result.working = true;
        result.work_kind = CharacterWorkKind::Editing;
        break;
    case Phase::Running:
        result.working = true;
        result.work_kind = CharacterWorkKind::Running;
        break;
    case Phase::Paused: result.attention = true; break;
    case Phase::ResponseReady: result.ready = true; break;
    case Phase::Interrupted: result.interrupted = true; break;
    case Phase::None:
    case Phase::Active:
    case Phase::Ended: break;
    case Phase::Unavailable:
        observation = result;
        return ESP_OK;
    default: return reject("activity.phase", static_cast<unsigned>(link.activity.state));
    }
    result.current = true;
    result.chat = link.activity.chat_id;
    const bool same_owner = current_component(technical) &&
        technical.revision == link.activity.revision &&
        std::strncmp(technical.chat_id, link.activity.chat_id, sizeof(technical.chat_id)) == 0;
    if (same_owner) {
        if (!valid_turn(technical.turn_id)) {
            return reject("activity.turn", 0);
        }
        // An all-zero wire identity denotes absence, not a turn that can complete.
        if (technical.turn_id[0] != '\0' && std::strspn(technical.turn_id, "0") != 64) {
            result.turn = technical.turn_id;
        }
    }
    if (same_owner && technical.compaction_valid) {
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
    if ((inputs.activity.current && (!valid_chat(inputs.activity.chat) || !valid_turn(inputs.activity.turn))) ||
        inputs.activity.work_kind > CharacterWorkKind::Running ||
        (inputs.activity.compacting && inputs.activity.refreshed) ||
        (inputs.activity.ready && (inputs.activity.working || inputs.activity.attention || inputs.activity.interrupted))) {
        return reject("input.activity", 0);
    }
    CharacterDirectionState result = state;
    const bool same_chat = !inputs.rebase && state.current && inputs.activity.current &&
        std::strcmp(state.chat, inputs.activity.chat) == 0;
    const bool same_turn = same_chat && state.turn[0] != '\0' && inputs.activity.turn[0] != '\0' &&
        std::strcmp(state.turn, inputs.activity.turn) == 0;
    const bool compacting = inputs.activity.current && inputs.activity.compacting;
    // Sequence numbers restart per turn; success requires that exact turn and sequence, without a source gap or wake.
    const bool refreshed = state.enabled && same_turn && state.compacting && inputs.activity.refreshed &&
        state.compaction_sequence == inputs.activity.compaction_sequence;
    const bool caress = !inputs.rebase && inputs.caress_at_us > state.consumed_caress_at_us;
    const bool attention_edge = state.enabled && same_chat && !state.attention && inputs.activity.attention;
    const bool reply_edge = state.enabled && same_turn && state.working && inputs.activity.ready &&
        !state.interrupted && !compacting;
    const bool interrupted_edge = state.enabled && same_turn && state.working &&
        inputs.activity.interrupted && !compacting;
    result.current = inputs.activity.current;
    result.compacting = compacting;
    result.attention = inputs.activity.current && inputs.activity.attention;
    result.working = inputs.activity.current && inputs.activity.working;
    result.ready = inputs.activity.current && inputs.activity.ready;
    result.interrupted = inputs.activity.current && inputs.activity.interrupted;
    result.work_kind = result.working ? inputs.activity.work_kind : CharacterWorkKind::None;
    result.compaction_sequence = inputs.activity.current ? inputs.activity.compaction_sequence : 0;
    result.consumed_caress_at_us = std::max(state.consumed_caress_at_us, inputs.caress_at_us);
    if (inputs.activity.current) {
        std::memcpy(result.chat, inputs.activity.chat, sizeof(result.chat));
        if (inputs.activity.turn[0] != '\0') {
            std::memcpy(result.turn, inputs.activity.turn, sizeof(result.turn));
        } else {
            result.turn[0] = '\0';
        }
    } else {
        result.chat[0] = '\0';
        result.turn[0] = '\0';
    }
    if (!inputs.enabled) {
        // Exact Debug/preset/setup poses keep ownership; hidden events cannot replay on exit.
        result.enabled = false;
        result.pose = CharacterCompactionPose::Neutral;
        result.afterglow_remaining_us = result.release_remaining_us = 0;
        result.reaction = CharacterReaction::None;
        result.reaction_remaining_us = result.work_elapsed_us = 0;
        result.gaze_x = result.gaze_y = result.warmth = result.compaction = result.work = 0;
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
    // One current performance replaces the previous one; observed edges never form a backlog.
    if (!reaction_supported(result, same_chat, same_turn)) {
        result.reaction = CharacterReaction::None;
        result.reaction_remaining_us = 0;
    }
    if (attention_edge) {
        result.reaction = CharacterReaction::Attention;
        result.reaction_remaining_us = kReactionUs;
    } else if (reply_edge) {
        result.reaction = CharacterReaction::ReplyReady;
        result.reaction_remaining_us = kReactionUs;
    } else if (interrupted_edge) {
        result.reaction = CharacterReaction::Interrupted;
        result.reaction_remaining_us = kReactionUs;
    } else {
        result.reaction_remaining_us -= static_cast<uint32_t>(
            std::min<int64_t>(active_delta_us, result.reaction_remaining_us));
        if (result.reaction_remaining_us == 0) {
            result.reaction = CharacterReaction::None;
        }
    }
    const bool work_active = result.working && !compacting && !result.attention;
    result.work_elapsed_us = work_active && same_chat ? static_cast<uint32_t>(
        (state.work_elapsed_us + static_cast<uint64_t>(active_delta_us)) % kWorkCycleUs) : 0;
    result.work = approach(state.work, work_active ? 1.f : 0.f, active_delta_us, .4f);
    if (!same_chat || (state.compacting && !compacting && !refreshed)) {
        result.pose = CharacterCompactionPose::Neutral;
        result.release_remaining_us = 0;
    }
    if (result.attention) {
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

// state: Valid artistic gaze, warmth, observed Codex activity and compaction presentation.
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
    const float work_phase = static_cast<float>(state.work_elapsed_us) / kWorkCycleUs;
    const float work_accent = std::sin(2.f * kPi * work_phase);
    const bool editing = state.work_kind == CharacterWorkKind::Editing;
    const bool running = state.work_kind == CharacterWorkKind::Running;
    offset_channel(face, FaceChannel::BrowHeightLeft, state.work * (.08f + .035f * work_accent));
    offset_channel(face, FaceChannel::BrowHeightRight, state.work * (editing ? .1f : .035f));
    offset_channel(face, FaceChannel::EyeUpperLeft, -.08f * state.work);
    offset_channel(face, FaceChannel::EyeUpperRight, -.08f * state.work);
    // Keep the acquired gaze target; only a small cheek/brow asymmetry accents long work.
    offset_channel(face, FaceChannel::CheekRaiseRight, .05f * state.work * work_accent);
    offset_channel(face, FaceChannel::BrowHeightLeft, .14f * state.compaction);
    offset_channel(face, FaceChannel::EyeCurveLeft, -.1f * state.compaction);
    offset_channel(face, FaceChannel::EyeCurveRight, -.1f * state.compaction);
    face[FaceChannel::MouthWidth] += (.92f - face[FaceChannel::MouthWidth]) * .22f * state.compaction;
    const float release_phase = 1.f - static_cast<float>(state.release_remaining_us) / kReleaseUs;
    const float sine = std::sin(kPi * release_phase);
    const float stretch = state.pose == CharacterCompactionPose::Release ? .18f * sine * sine : 0;
    const float reaction_phase = 1.f - static_cast<float>(state.reaction_remaining_us) / kReactionUs;
    const float reaction_sine = std::sin(kPi * reaction_phase);
    const float reaction_weight = state.reaction == CharacterReaction::None ? 0 : reaction_sine * reaction_sine;
    const float celebration = state.reaction == CharacterReaction::ReplyReady ? .3f * reaction_weight : 0;
    if (state.reaction == CharacterReaction::Attention) {
        // Extra eyelid closure multiplies the existing blink/protective pose; it never forces eyes open.
        float blink = 0;
        for (const float start : {.12f, .36f}) {
            const float phase = (reaction_phase - start) / .12f;
            if (phase >= 0 && phase <= 1) {
                const float value = std::sin(kPi * phase);
                blink = std::max(blink, value * value);
            }
        }
        face[FaceChannel::EyeLeft] *= 1.f - blink;
        face[FaceChannel::EyeRight] *= 1.f - blink;
        offset_channel(face, FaceChannel::BrowHeightLeft, .2f * reaction_weight);
        offset_channel(face, FaceChannel::BrowHeightRight, .2f * reaction_weight);
    } else if (state.reaction == CharacterReaction::Interrupted) {
        offset_channel(face, FaceChannel::BrowHeightLeft, .22f * reaction_weight);
        offset_channel(face, FaceChannel::BrowCurveRight, -.16f * reaction_weight);
    }
    const float smile = .28f * state.warmth + stretch + celebration;
    offset_channel(face, FaceChannel::MouthCornerLeft, smile);
    offset_channel(face, FaceChannel::MouthCornerRight, smile * .85f);
    offset_channel(face, FaceChannel::CheekOpacity, .35f * smile);
    offset_channel(face, FaceChannel::CheekRaiseLeft, .3f * smile);
    offset_channel(face, FaceChannel::CheekRaiseRight, .3f * smile);
    auto &fullness = contour.controls[ContourControl::Fullness];
    fullness += (-.18f - fullness) * .35f * state.compaction;
    fullness += (1.f - fullness) * stretch;
    // A small squash followed by a softer stretch stays in contour space, preserving root motion.
    const float celebration_shape = state.reaction == CharacterReaction::ReplyReady ?
        -.14f * reaction_weight * std::sin(2.f * kPi * reaction_phase) : 0;
    fullness += ((celebration_shape >= 0 ? 1.f : -1.f) - fullness) * std::fabs(celebration_shape);
    auto &amplitude = contour.controls[ContourControl::Amplitude];
    amplitude += (1.f - amplitude) * state.work * (running ? .035f : .015f) * std::fabs(work_accent);
    // Existing breathing, body springs and manga fading retain their own clocks and parameters.
    frame.face = face;
    frame.contour = contour.controls;
    return ESP_OK;
}

// state: Semantic admission rather than the slowly decaying artistic compaction weight.
PersonalityCue character_direction_cue(const CharacterDirectionState &state)
{
    if (!state.enabled || !state.current) {
        return PersonalityCue::None;
    }
    if (state.attention) {
        return PersonalityCue::Curiosity;
    }
    if (state.compacting) {
        return PersonalityCue::Confusion;
    }
    if (state.reaction == CharacterReaction::ReplyReady) {
        return PersonalityCue::Joy;
    }
    return state.reaction == CharacterReaction::Interrupted ? PersonalityCue::Confusion : PersonalityCue::None;
}
// state: Canonical current actor state; stale, disabled and question states cannot override a scene.
// fallback: Ordinary scene/request cue retained when no genuine compaction is active.
PersonalityCue prioritize_compaction_cue(const CharacterDirectionState &state, PersonalityCue fallback)
{
    if (state.enabled && state.current && state.compacting && !state.attention &&
        !personality_cue_is_device(fallback) && fallback != PersonalityCue::Sleep) {
        return character_direction_cue(state);
    }
    return fallback;
}
// state: Current canonical acting; stale, orphaned and completed states cannot originate sound.
// retired_visible: Full-frame visibility with ordinary Home and lifecycle admission.
// retired_cue: Actual retired symbol, preserving device/question/layout priority.
// latch: Bounded chat/turn/sequence identity consumed once, independent of opacity interruptions.
// Returns false for ineligible or already-observed identities; no hardware/audio work occurs here.
bool claim_compaction_sound(const CharacterDirectionState &state, bool retired_visible,
    PersonalityCue retired_cue, CompactionSoundLatch &latch)
{
    if (!retired_visible || !state.enabled || !state.current || !state.compacting || state.attention ||
        retired_cue != PersonalityCue::Confusion || state.compaction_sequence == 0 ||
        state.chat[0] == '\0' || state.turn[0] == '\0') {
        return false;
    }
    if (latch.sequence == state.compaction_sequence && std::strcmp(latch.chat, state.chat) == 0 &&
        std::strcmp(latch.turn, state.turn) == 0) {
        return false;
    }
    std::memcpy(latch.chat, state.chat, sizeof(latch.chat));
    std::memcpy(latch.turn, state.turn, sizeof(latch.turn));
    latch.sequence = state.compaction_sequence;
    return true;
}
} // namespace CODY
