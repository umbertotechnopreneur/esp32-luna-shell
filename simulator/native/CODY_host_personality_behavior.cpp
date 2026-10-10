/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

// Desktop transport for the unchanged firmware mood and caress models.
#include "CODY_happiness.hpp"
#include "CODY_petting.hpp"
#include "CODY_character_direction.hpp"
#include "CODY_personality.hpp"
#include "personality_rules.hpp"
#include "esp_log.h"
#include <algorithm>
#include <new>

#if defined(_WIN32)
#define CODY_BEHAVIOR_EXPORT extern "C" __declspec(dllexport)
#else
#define CODY_BEHAVIOR_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// handle: Borrowed native frame owned by the graphics transport.
// scene: Receives its actual validated RigScene; ownership stays with that frame.
// Exception: Returns the graphics owner's invalid-frame error.
esp_err_t cody_host_scene_model(const void *handle, const CODY::RigScene *&scene);

namespace {
struct HostBehavior {
    CODY::HappinessState happiness{};
    CODY::PettingState petting{};
    CODY::CharacterDirectionState direction{};
    CODY::PersonalityBatteryLatch power{};
};

// PC-only presentation inputs; no synthetic observation enters firmware snapshots.
struct HostCharacterInputs {
    const char *chat;
    uint64_t sequence;
    const char *turn;
    float placement[3];
    float gaze[2];
    bool current;
    bool working;
    bool attention;
    bool compacting;
    bool refreshed;
    bool ready;
    bool interrupted;
    bool enabled;
    bool paused;
    bool rebase;
    bool gaze_visible;
    bool affection_allowed;
    uint8_t work_kind;
};

// operation: Actual rejected host boundary or firmware operation.
// error: Original esp_err_t retained for the Python caller.
esp_err_t behavior_result(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) {
        ESP_LOGE("CodyHostBehavior", "%s failed: %s (0x%x)", operation, esp_err_to_name(error), error);
    }
    return error;
}
}  // namespace

// output: Receives a new independent owner only after authored mood initialization succeeds.
// Exception: Returns invalid arguments, allocation failure or the real configuration error.
CODY_BEHAVIOR_EXPORT int cody_host_behavior_create(void **output)
{
    if (output == nullptr) {
        return behavior_result("create", ESP_ERR_INVALID_ARG);
    }
    *output = nullptr;
    auto *state = new (std::nothrow) HostBehavior;
    if (state == nullptr) {
        return behavior_result("create", ESP_ERR_NO_MEM);
    }
    const esp_err_t error = CODY::initialize_happiness(CODY::PersonalityData::happiness, state->happiness);
    if (error != ESP_OK) {
        delete state;
        return behavior_result("initialize_happiness", error);
    }
    *output = state;
    return ESP_OK;
}

// handle: Independent behavior owner allocated by this DLL, or nullptr.
CODY_BEHAVIOR_EXPORT void cody_host_behavior_destroy(void *handle)
{
    delete static_cast<HostBehavior *>(handle);
}

// handle: Existing isolated character owner; no synthetic observation enters firmware snapshots.
// current: Whether the original synthetic source is still valid and fresh.
// present: Explicitly synthetic battery presence.
// percentage_valid: Whether the synthetic owner actually supplied the charge percentage.
// percentage: Original synthetic percentage, without rounding or clamping.
// charging: Explicitly synthetic charge-state fact, independent from percentage availability.
// cue: Receives the exact shared selector's current symbol on success only.
// low_active: Receives the native owner's current low-battery support for visible-frame invalidation.
// Exception: Returns boundary errors or the original native power-selection error.
CODY_BEHAVIOR_EXPORT int cody_host_power_cue(void *handle, bool current, bool present,
    bool percentage_valid, float percentage, bool charging, uint8_t *cue, bool *low_active)
{
    if (handle == nullptr || cue == nullptr || low_active == nullptr) {
        return behavior_result("power_cue", ESP_ERR_INVALID_ARG);
    }
    CODY::PersonalityCue selected{};
    const CODY::PersonalityPowerObservation observation{current, present, percentage_valid, charging, percentage};
    const esp_err_t error = CODY::select_personality_power_cue(observation,
        static_cast<HostBehavior *>(handle)->power, selected);
    if (error == ESP_OK) {
        *cue = static_cast<uint8_t>(selected);
        *low_active = static_cast<HostBehavior *>(handle)->power.low_battery_active;
    }
    return behavior_result("power_cue", error);
}

// handle: Native mood owner; no Python-owned mood state is accepted.
// values: Two output words containing happiness and its authored complement.
// remainder: Receives the exact partial decay interval from the firmware model.
// Exception: Returns invalid arguments or the original validation error without state mutation.
CODY_BEHAVIOR_EXPORT int cody_host_happiness_read(void *handle, uint16_t *values, int64_t *remainder)
{
    if (handle == nullptr || values == nullptr || remainder == nullptr) {
        return behavior_result("happiness_read", ESP_ERR_INVALID_ARG);
    }
    const auto &state = static_cast<HostBehavior *>(handle)->happiness;
    auto verified = state;
    const esp_err_t error = CODY::advance_happiness(CODY::PersonalityData::happiness, 0, verified);
    if (error != ESP_OK) {
        return behavior_result("happiness_read", error);
    }
    values[0] = state.happiness;
    values[1] = static_cast<uint16_t>(CODY::PersonalityData::happiness.maximum - state.happiness);
    *remainder = state.decay_remainder_us;
    return ESP_OK;
}

// handle: Native mood owner; ordinary Studio does not duplicate its arithmetic.
// elapsed_us: Original nonnegative awake desktop interval, including animation pause.
// Exception: Returns invalid arguments or the original firmware arithmetic error.
CODY_BEHAVIOR_EXPORT int cody_host_happiness_advance(void *handle, int64_t elapsed_us)
{
    if (handle == nullptr) {
        return behavior_result("happiness_advance", ESP_ERR_INVALID_ARG);
    }
    return behavior_result("advance_happiness", CODY::advance_happiness(
        CODY::PersonalityData::happiness, elapsed_us, static_cast<HostBehavior *>(handle)->happiness));
}

// handle: Native artistic mood owner.
// event: Catalog identity validated by the unchanged firmware event handler.
// Exception: Returns invalid arguments or an unbound event without changing mood.
CODY_BEHAVIOR_EXPORT int cody_host_happiness_event(void *handle, uint8_t event)
{
    if (handle == nullptr) {
        return behavior_result("happiness_event", ESP_ERR_INVALID_ARG);
    }
    return behavior_result("apply_happiness_event", CODY::apply_happiness_event(
        CODY::PersonalityData::happiness, static_cast<CODY::PersonalityEvent>(event),
        static_cast<HostBehavior *>(handle)->happiness));
}

// handle: Native gesture owner; cancellation preserves its accepted cooldown.
// Exception: Returns invalid arguments for an absent owner.
CODY_BEHAVIOR_EXPORT int cody_host_petting_cancel(void *handle)
{
    if (handle == nullptr) {
        return behavior_result("petting_cancel", ESP_ERR_INVALID_ARG);
    }
    CODY::cancel_petting(static_cast<HostBehavior *>(handle)->petting);
    return ESP_OK;
}

// handle: Independent native gesture owner.
// frame: Last presented frame whose actual C++ geometry defines caress regions.
// x: Logical desktop pointer x after the shared rotation mapping.
// y: Logical desktop pointer y after the shared rotation mapping.
// pressed: Explicitly synthetic desktop button state.
// pressed_edge: True only for this contact's first acquired desktop sample.
// sampled_us: Original monotonic desktop acquisition time.
// zone: Receives the exact typed recognized zone only on successful observation.
// Exception: Returns original frame, geometry, time or recognizer errors without fallback.
CODY_BEHAVIOR_EXPORT int cody_host_petting_observe(void *handle, const void *frame, float x, float y,
    bool pressed, bool pressed_edge, int64_t sampled_us, uint8_t *zone)
{
    if (handle == nullptr || zone == nullptr) {
        return behavior_result("petting_observe", ESP_ERR_INVALID_ARG);
    }
    const CODY::RigScene *scene = nullptr;
    const esp_err_t frame_error = cody_host_scene_model(frame, scene);
    if (frame_error != ESP_OK) {
        return behavior_result("petting_frame", frame_error);
    }
    CODY::PettingZone triggered = CODY::PettingZone::None;
    const esp_err_t error = CODY::observe_petting(*scene, {x, y}, pressed, pressed_edge, sampled_us,
        static_cast<HostBehavior *>(handle)->petting, triggered);
    if (error == ESP_OK) {
        *zone = static_cast<uint8_t>(triggered);
    }
    return behavior_result("observe_petting", error);
}

// No parameters: returns the compiled shared rule, without replicating a desktop default.
CODY_BEHAVIOR_EXPORT int64_t cody_host_petting_affection_us()
{
    return CODY::PettingConfig::affection_us;
}

// handle: Existing native mood/caress owner; the director retains no second mood value.
// wire: Borrowed visibly SIMULATED presentation facts, validated by the shared director.
// elapsed_us: Active animation time; pause supplies zero and consumes source edges.
// face: Complete native response channels, or nullptr while a non-character route is visible.
// count: Exact channel count, or zero when face and contour are both absent.
// contour: Four native response controls, committed with face only after success.
// cue: Receives the native optional manga request after a successful update.
// Exception: Returns original boundary, mood or direction errors without a partial commit.
CODY_BEHAVIOR_EXPORT int cody_host_character_compose(void *handle, const HostCharacterInputs *wire,
    int64_t elapsed_us, float *face, uint32_t count, float *contour, uint8_t *cue)
{
    if (handle == nullptr || wire == nullptr || cue == nullptr) {
        return behavior_result("character_inputs", ESP_ERR_INVALID_ARG);
    }
    const bool no_frame = face == nullptr && contour == nullptr && count == 0;
    const bool complete_frame = face != nullptr && contour != nullptr && count == CODY::kFaceChannelCount;
    if ((!no_frame && !complete_frame) || (no_frame && wire->enabled)) {
        return behavior_result("character_frame_shape", ESP_ERR_INVALID_ARG);
    }
    auto &owner = *static_cast<HostBehavior *>(handle);
    CODY::CharacterDirectionInputs inputs{};
    inputs.activity = {wire->chat, wire->sequence, wire->current, wire->working,
        wire->attention, wire->compacting, wire->refreshed};
    inputs.activity.turn = wire->turn;
    inputs.activity.work_kind = static_cast<CODY::CharacterWorkKind>(wire->work_kind);
    inputs.activity.ready = wire->ready;
    inputs.activity.interrupted = wire->interrupted;
    inputs.placement = {wire->placement[0], wire->placement[1], wire->placement[2]};
    inputs.gaze_point = {wire->gaze[0], wire->gaze[1]};
    inputs.caress_at_us = owner.petting.last_triggered_at_us;
    inputs.enabled = wire->enabled;
    inputs.paused = wire->paused;
    inputs.rebase = wire->rebase;
    inputs.gaze_visible = wire->gaze_visible;
    inputs.affection_allowed = wire->affection_allowed;
    esp_err_t error = CODY::positive_character_affect(CODY::PersonalityData::happiness,
        owner.happiness, inputs.positive_affect);
    CODY::CharacterDirectionState next{};
    if (error == ESP_OK) {
        error = CODY::advance_character_direction(inputs, elapsed_us, owner.direction, next);
    }
    // These bounded host-only locals never consume an ESP32 display-task stack.
    CODY::ResponseFrame frame{};
    if (error == ESP_OK && face != nullptr) {
        std::copy(face, face + count, frame.face.values);
        std::copy(contour, contour + CODY::kContourControlCount, frame.contour.values);
        error = CODY::compose_character_direction(next, frame);
    }
    if (error != ESP_OK) {
        return behavior_result("character_compose", error);
    }
    if (face != nullptr) {
        std::copy(frame.face.values, frame.face.values + count, face);
        std::copy(frame.contour.values, frame.contour.values + CODY::kContourControlCount, contour);
    }
    owner.direction = next;
    *cue = static_cast<uint8_t>(CODY::character_direction_cue(next));
    return ESP_OK;
}
