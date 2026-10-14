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

#pragma once

#include <cstdint>
#include <array>
#include "CODY_damage.hpp"
#include "CODY_personality_config.hpp"
#include "personality_catalog.hpp"

namespace CODY {

struct RigScene;

// Names identify the existing authored presentations, not an array position.
enum class CodySize : uint8_t {
    Micro = 1, Tiny = 2, Preferences = 3, Look = 4, Pairing = 5,
    Small = 6, Invitation = 7, Medium = 8, Greeting = 9, Full = 10,
};

enum class PersonalityPresentation : uint8_t { Disabled = 0, Main = 1, Sleep = 2 };
// Home reserves status chrome above the compact character without changing cue size.
enum class PersonalityLayout : uint8_t { Standard = 0, HomeDashboard = 1 };
enum class PersonalityPresentationStatus : uint8_t {
    Hidden = 0, Visible = 1, BlockedByLayout = 2, SuppressedByCooldown = 3,
};
enum class PersonalityPhase : uint8_t { Hidden = 0, FadeIn = 1, Visible = 2, FadeOut = 3 };

struct PersonalityContext {
    PersonalityPresentation presentation = PersonalityPresentation::Disabled;
    PersonalityLayout layout = PersonalityLayout::Standard;
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
    uint8_t animation_phase = 0;
};
static_assert(sizeof(PersonalityFrame) <= 16, "Retired personality metadata must remain a small projection");

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
    int64_t active_elapsed_us = 0;
    // Storage slots use stable cue IDs; zero means never admitted, otherwise time + one.
    std::array<int64_t, PersonalityConfig::cue_rule_count> last_admitted_at_us{};
    float opacity = 0;
    float fade_out_opacity = 0;
    CodySize size = CodySize::Micro;
    PersonalityLayout layout = PersonalityLayout::Standard;
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

struct PersonalityBatteryLatch {
    bool low_battery_active = false;
};

struct PersonalityPowerObservation {
    bool current = false;
    bool battery_present = false;
    bool percentage_valid = false;
    bool charging = false;
    float level_percent = 0;
};

struct WakeBatteryPrelude {
    bool pending = false;
    bool active = false;
    bool retired = false;
    PersonalityCue cue = PersonalityCue::None;
    int64_t visible_us = 0;
};

// observation: Fresh canonical battery facts, never an inference from USB presence.
// working: The first resumed route is the real Working view.
// delta_us: Active time; only a fully retired prelude contributes visible dwell.
// state: Display-owned single-use wake presentation.
// Exception: Returns invalid timing or measured percentage without changing state.
esp_err_t advance_wake_battery_prelude(const PersonalityPowerObservation &observation,
    bool working, int64_t delta_us, WakeBatteryPrelude &state);

enum class BatteryNotice : uint8_t { None, Low, Critical, Charging, Full };
enum class BatteryWarningBand : uint8_t { None, Low, Critical };

// Display-owner state only: no sensor cache, allocations or independent task.
struct BatteryNoticeState {
    BatteryNotice pending = BatteryNotice::None;
    BatteryNotice active = BatteryNotice::None;
    bool observed = false;
    bool was_charging = false;
    bool was_full = false;
    BatteryWarningBand warned_band = BatteryWarningBand::None;
    int64_t idle_us = 0;
    int64_t visible_us = 0;
    int64_t reminder_us = 0;
};

// observation: Validated canonical battery facts; unavailable values are never interpreted.
// idle: Home is free of conversation, controls, touch and other expressive cues.
// delta_us: Elapsed awake animation time; standby contributes zero.
// state: Display-owned transition and bounded reminder state.
void advance_battery_notice(const PersonalityPowerObservation &observation, bool idle,
                            int64_t delta_us, BatteryNoticeState &state);

// notice: Typed battery event whose short English caption is requested.
const char *battery_notice_text(BatteryNotice notice);

// notice: Typed battery event mapped to an existing manga master.
PersonalityCue battery_notice_cue(BatteryNotice notice);

// lag: Presentation and geometry to clear while preserving repeat admission times and active clock.
void clear_personality_presentation(PersonalityLag &lag);

// current_percentage: Whether the canonical owner supplies a current, present, valid measurement.
// percentage: Original measured percentage; ignored when the measurement is unavailable.
// latch: Caller-owned hysteresis state, cleared on unavailable samples.
// alert: Receives the latched low-battery decision only after validation.
// Exception: Returns invalid thresholds or a real invalid measured percentage without mutation.
esp_err_t update_personality_battery_latch(bool current_percentage, float percentage,
                                         PersonalityBatteryLatch &latch, bool &alert);

// observation: Canonical Cody battery facts, or explicitly synthetic host facts.
// latch: Caller-owned low-battery hysteresis, cleared whenever its source becomes unavailable.
// cue: Receives the currently supported power symbol, never a manufactured charge percentage.
// Exception: Returns invalid measured percentage/thresholds without partially committing state.
esp_err_t select_personality_power_cue(const PersonalityPowerObservation &observation,
    PersonalityBatteryLatch &latch, PersonalityCue &cue);

// cue: Typed identity whose catalog membership is requested.
bool personality_cue_valid(PersonalityCue cue);

// cue: Typed visible identity; only an explicit authored sound opt-out is silent.
// Unknown identities return false so the audio owner still rejects invalid requests.
bool personality_cue_is_silent(PersonalityCue cue);

// cue: Typed identity to classify as a device observation rather than narrative.
bool personality_cue_is_device(PersonalityCue cue);

// admitted_request: Latest baseline after canonical device validity has already been checked.
// semantic_sleep: True only for the authored Sleep presentation, which owns final precedence.
// affection_active: Whether the current auto caress request still has active lifetime.
// desired: Receives the single target; Charging waits behind active auto affection.
// release_affection: Receives whether the admitted target cancels the auto request.
// eye_reaction: None or the current acquired-eye Fear/Frustration request.
// Exception: Returns invalid cue identity without modifying either output.
esp_err_t select_personality_target(PersonalityCue admitted_request, bool semantic_sleep,
                                   bool affection_active, PersonalityCue &desired, bool &release_affection,
                                   PersonalityCue eye_reaction = PersonalityCue::None);

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
// context: Scene permission, authored layout/scale and explicit smooth-release policy.
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
