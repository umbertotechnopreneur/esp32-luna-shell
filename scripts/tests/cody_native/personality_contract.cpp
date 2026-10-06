#include "CODY_personality.hpp"
#include "CODY_dirty_plan.hpp"
#include "CODY_rig.hpp"
#include "CODY_petting.hpp"
#include "CODY_happiness.hpp"
#include "CODY_wellbeing.hpp"
#include "personality_rules.hpp"
#include "cloud_rig_data.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using namespace CODY;
int failures = 0;
int assertions = 0;
bool first_sample = true;
constexpr DamageRect exclusions[] = {{10, 203, 70, 263}, {122, 32, 365, 64}};

// condition: Independent observable behavior required from the production source.
// message: Contract description retained in the failed-stage log.
void require(bool condition, const char *message)
{
    ++assertions;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

// scale: Authored nominal scale used by both actual rig evaluation and the controller.
RigScene make_scene(float scale = 145.f)
{
    RigScene scene;
    Transform transform = Data::placement;
    transform.scale = scale;
    require(evaluate_rig(Data::defaults, transform, scene) == ESP_OK, "real vector rig evaluates");
    return scene;
}

struct Scenario {
    const char *name;
    PersonalityLag lag{};
    PersonalityFrame frame{};
    bool was_sleep = false;
    bool release_sleep = false;
};

// scenario: Independent sequence retaining actual controller state across its frames.
// name: Stable fixture step identifier.
// cue: Latest requested identity, with semantic Sleep selection applied by the caller policy.
// delta_us: Active elapsed interval; paused frames explicitly ignore even large wall-clock gaps.
// presentation: Main, semantic Sleep or Disabled scene permission.
// paused: Freeze animation while still accepting the latest desired target.
// dx: Synthetic authored Nose x displacement, never a hardware measurement.
// dy: Synthetic authored Nose y displacement.
// squash: Synthetic relative upper-lobe spread for bounded scale response.
// scale: Nominal authored size shared with the Python fixture.
PersonalityFrame step(Scenario &scenario, const char *name, PersonalityCue cue, int64_t delta_us,
    PersonalityPresentation presentation = PersonalityPresentation::Main, bool paused = false,
    float dx = 0, float dy = 0, float squash = 0, float scale = 145.f)
{
    RigScene scene = make_scene(scale);
    scene.world[to_index(PointId::Nose)].x += dx;
    scene.world[to_index(PointId::Nose)].y += dy;
    const float rest_span = (Data::points[to_index(PointId::LobeUpperRight)].x -
        Data::points[to_index(PointId::LobeUpperLeft)].x) * scene.scale;
    scene.world[to_index(PointId::LobeUpperRight)].x += squash * rest_span;
    const bool sleeping = presentation == PersonalityPresentation::Sleep;
    if (sleeping) {
        scenario.release_sleep = false;
        cue = PersonalityCue::Sleep;
    } else if (scenario.was_sleep) {
        scenario.release_sleep = true;
    }
    scenario.was_sleep = sleeping;
    PersonalityContext context;
    context.presentation = presentation;
    context.nominal_scale = scale;
    context.release_immediately = scenario.release_sleep;
    CodySize size{};
    require(cody_size_for_scale(scale, size) == ESP_OK, "nominal scale has typed identity");
    require(prepare_personality_frame(scene, cue, size, context, delta_us, paused, false,
        exclusions, 2, scenario.lag, scenario.frame) == ESP_OK, "controller frame preparation succeeds");
    if (scenario.lag.cue != PersonalityCue::Sleep || scenario.lag.phase == PersonalityPhase::FadeOut) {
        scenario.release_sleep = false;
    }
    const auto &frame = scenario.frame;
    const auto &lag = scenario.lag;
    std::printf("%s{\"scenario\":\"%s\",\"name\":\"%s\",\"cue\":%u,\"scale\":%.9g,"
        "\"presentation\":%u,\"dt_us\":%lld,\"paused\":%s,\"reset\":false,"
        "\"target_dx\":%.9g,\"target_dy\":%.9g,\"target_squash\":%.9g,\"expected\":{"
        "\"cue\":%u,\"opacity\":%u,\"status\":%u,\"left\":%u,\"top\":%u,"
        "\"right\":%u,\"bottom\":%u,\"phase\":%u,\"desired\":%u,"
        "\"x\":%.9g,\"y\":%.9g,\"width\":%.9g,\"height\":%.9g,"
        "\"active_elapsed_us\":%lld,\"last_admitted_at_us\":[",
        first_sample ? "" : ",", scenario.name, name, static_cast<unsigned>(cue), scale,
        static_cast<unsigned>(presentation), static_cast<long long>(delta_us), paused ? "true" : "false",
        dx, dy, squash, static_cast<unsigned>(frame.cue), frame.opacity, static_cast<unsigned>(frame.status),
        frame.bounds.left, frame.bounds.top, frame.bounds.right, frame.bounds.bottom,
        static_cast<unsigned>(lag.phase), static_cast<unsigned>(lag.desired),
        lag.center_x, lag.center_y, lag.width, lag.height,
        static_cast<long long>(lag.active_elapsed_us));
    for (size_t index = 0; index < lag.last_admitted_at_us.size(); ++index) {
        std::printf("%s%lld", index == 0 ? "" : ",",
                    static_cast<long long>(lag.last_admitted_at_us[index]));
    }
    std::printf("]}}");
    first_sample = false;
    return frame;
}

// No parameters: timings are checked at authored boundaries, not by duplicating transition equations.
void check_channel()
{
    Scenario scenario{"channel"};
    step(scenario, "request", PersonalityCue::Joy, 0);
    require(scenario.lag.phase == PersonalityPhase::Hidden, "new narrative request debounces");
    step(scenario, "stable", PersonalityCue::Joy, 300000);
    require(scenario.lag.phase == PersonalityPhase::FadeIn && scenario.frame.opacity == 0,
        "stable request starts at zero opacity");
    step(scenario, "half-in", PersonalityCue::Joy, 200000);
    require(scenario.frame.opacity == 128, "fade-in midpoint has half opacity");
    step(scenario, "visible", PersonalityCue::Joy, 200000);
    require(scenario.frame.opacity == 255, "fade in completes in400ms");
    step(scenario, "intermediate", PersonalityCue::Curiosity, 100000);
    step(scenario, "replace-pending", PersonalityCue::Confusion, 300000);
    require(scenario.frame.cue == PersonalityCue::Joy, "minimum visible hold preserves current symbol");
    step(scenario, "hold-complete", PersonalityCue::Confusion, 2100000);
    require(scenario.lag.phase == PersonalityPhase::FadeOut, "replacement waits for2500ms full-opacity hold");
    step(scenario, "latest-during-out", PersonalityCue::Hunger, 200000);
    require(scenario.frame.cue == PersonalityCue::Joy && scenario.frame.opacity == 128,
        "replacement during fade does not interrupt current symbol");
    step(scenario, "new-target", PersonalityCue::Hunger, 300000);
    require(scenario.lag.cue == PersonalityCue::Hunger && scenario.frame.opacity == 0,
        "only latest stable target survives fade-out");
    step(scenario, "new-visible", PersonalityCue::Hunger, 400000);
    require(scenario.frame.cue == PersonalityCue::Hunger && scenario.frame.opacity == 255,
        "intermediate Curiosity and Confusion never replay");
}

// No parameters: semantic Sleep bypasses hold and release survives a paused zero-time transition.
void check_sleep_and_pause()
{
    Scenario scenario{"sleep"};
    step(scenario, "request", PersonalityCue::Joy, 0);
    step(scenario, "stable", PersonalityCue::Joy, 300000);
    step(scenario, "visible", PersonalityCue::Joy, 400000);
    step(scenario, "sleep-enter", PersonalityCue::None, 0, PersonalityPresentation::Sleep);
    require(scenario.lag.phase == PersonalityPhase::FadeOut, "Sleep bypasses current hold smoothly");
    step(scenario, "sleep-new", PersonalityCue::None, 400000, PersonalityPresentation::Sleep);
    step(scenario, "sleep-visible", PersonalityCue::None, 400000, PersonalityPresentation::Sleep);
    require(scenario.frame.cue == PersonalityCue::Sleep && scenario.frame.opacity == 255,
        "Sleep becomes visible after previous cue releases");
    const float frozen_x = scenario.lag.center_x;
    step(scenario, "paused-exit", PersonalityCue::None, 30000000, PersonalityPresentation::Main, true, 5);
    require(scenario.frame.opacity == 255 && scenario.lag.center_x == frozen_x,
        "paused redraw freezes hold, opacity and geometry despite wall-clock gap");
    step(scenario, "resume-exit", PersonalityCue::None, 0);
    require(scenario.lag.phase == PersonalityPhase::FadeOut, "paused Sleep exit survives until resume");
    step(scenario, "released", PersonalityCue::None, 400000);
    require(scenario.frame.opacity == 0 && scenario.lag.phase == PersonalityPhase::Hidden,
        "Sleep release completes without minimum hold");
}

// No parameters: a step, irregular cadence and repeated retargeting exercise the actual ring history.
void check_follow_and_layout()
{
    Scenario scenario{"follow"};
    step(scenario, "prime", PersonalityCue::Sleep, 0, PersonalityPresentation::Sleep);
    const float origin_x = scenario.lag.center_x;
    const float origin_y = scenario.lag.center_y;
    step(scenario, "step", PersonalityCue::Sleep, 1000, PersonalityPresentation::Sleep, false, 12, -2, .08f);
    const float lag_x = scenario.lag.center_x - (origin_x + 12);
    const float lag_y = scenario.lag.center_y - (origin_y - 2);
    require(lag_x * lag_x + lag_y * lag_y <= 16.001f, "position lag stays within four logical pixels");
    const int64_t intervals[] = {11000, 27000, 19000, 31000};
    int64_t elapsed = 0;
    for (unsigned index = 0; elapsed < 400000; ++index) {
        const int64_t delta = intervals[index % 4];
        elapsed += delta;
        char name[48];
        std::snprintf(name, sizeof(name), "irregular-settle-%u", index);
        step(scenario, name, PersonalityCue::Sleep, delta,
            PersonalityPresentation::Sleep, false, 12, -2, .08f);
    }
    require(std::abs(scenario.lag.center_x - origin_x - 12) < .001f &&
        std::abs(scenario.lag.center_y - origin_y + 2) < .001f,
        "stationary position settles exactly within bounded active horizon");
    require(std::abs(scenario.lag.width - scenario.lag.previous_target.width) < .001f &&
        std::abs(scenario.lag.height - scenario.lag.previous_target.height) < .001f,
        "stationary dimensions settle within400ms plus frame cadence");
    for (unsigned index = 0; index < 41; ++index) {
        char name[48];
        std::snprintf(name, sizeof(name), "moving-target-%u", index);
        step(scenario, name, PersonalityCue::Sleep, intervals[index % 4],
            PersonalityPresentation::Sleep, false, index % 2 ? 6.25f : -3.25f, -.75f,
            index % 2 ? .035f : -.045f);
    }
    step(scenario, "whole-window-gap", PersonalityCue::Sleep, 500000,
        PersonalityPresentation::Sleep, false, 3.25f, -.75f);
    require(scenario.lag.center_x == scenario.lag.previous_target.center_x,
        "whole-window frame gap leaves no obsolete history");
    step(scenario, "blocked", PersonalityCue::Sleep, 400000,
        PersonalityPresentation::Sleep, false, 0, -60);
    require(scenario.frame.status == PersonalityPresentationStatus::BlockedByLayout && scenario.frame.opacity == 0,
        "status overlap is nonfatal explicit blocked presentation");
    step(scenario, "readmitted", PersonalityCue::Sleep, 400000, PersonalityPresentation::Sleep);
    require(scenario.lag.phase == PersonalityPhase::FadeIn && scenario.frame.opacity == 0,
        "readmission restarts with a gentle zero-opacity entrance");
    step(scenario, "disabled", PersonalityCue::Sleep, 0, PersonalityPresentation::Disabled);
    require(!scenario.lag.primed && scenario.frame.opacity == 0, "non-main scene clears presentation");
    step(scenario, "micro", PersonalityCue::Sleep, 0, PersonalityPresentation::Sleep, false, 0, 0, 0, 22);
    require(!scenario.lag.primed && scenario.frame.opacity == 0, "Micro never displays a symbol");
}

// No parameters: malformed input remains distinct from expected layout rejection.
void check_rejection_and_sizes()
{
    CodySize size = CodySize::Full;
    require(cody_size_for_scale(22, size) == ESP_OK && size == CodySize::Micro, "Micro boundary is inclusive22");
    require(cody_size_for_scale(22.01f, size) == ESP_OK && size == CodySize::Tiny, "larger thanMicro has symbol-capable identity");
    require(cody_size_for_scale(137.5f, size) == ESP_OK && size == CodySize::Greeting,
        "equidistant presentations resolve to stable lower enum identity");
    require(cody_size_for_scale(NAN, size) == ESP_ERR_INVALID_ARG, "nonfinite scale is rejected");
    RigScene scene = make_scene();
    PersonalityLag lag;
    PersonalityFrame frame;
    PersonalityContext context;
    context.presentation = PersonalityPresentation::Main;
    require(prepare_personality_frame(scene, PersonalityCue::Joy, CodySize::Full, context,
        -1, false, false, exclusions, 2, lag, frame) == ESP_ERR_INVALID_ARG && !lag.primed,
        "negative clock is rejected without changing controller");
    require(prepare_personality_frame(scene, PersonalityCue::Charging, CodySize::Full, context,
        0, false, false, exclusions, 2, lag, frame) == ESP_OK, "device target can enter debounce");
    require(prepare_personality_frame(scene, PersonalityCue::Charging, CodySize::Full, context,
        300000, false, false, exclusions, 2, lag, frame) == ESP_OK &&
        lag.cue == PersonalityCue::Charging && frame.opacity == 0, "device target enters zero-alpha fade");
    context.invalidate_current = true;
    require(prepare_personality_frame(scene, PersonalityCue::None, CodySize::Full, context,
        100000, false, false, exclusions, 2, lag, frame) == ESP_OK &&
        lag.cue == PersonalityCue::None && frame.opacity == 0,
        "canonical invalidation removes a zero-alpha device target before it can appear");
    context.invalidate_current = false;
    const PersonalityLag before_invalid = lag;
    scene.valid = false;
    require(prepare_personality_frame(scene, PersonalityCue::Joy, CodySize::Full, context,
        0, false, false, exclusions, 2, lag, frame) == ESP_ERR_INVALID_ARG &&
        lag.center_x == before_invalid.center_x && lag.phase == before_invalid.phase,
        "invalid rig is rejected without a fabricated frame");
}

// No parameters: actual dirty planner must repaint fades and erase a departing sprite.
void check_dirty_and_raster()
{
    RigScene previous = make_scene();
    previous.personality = {PersonalityCue::Joy, {200, 75, 229, 104}, 128,
        PersonalityPresentationStatus::Visible};
    RigScene current = previous;
    current.personality.opacity = 255;
    DirtyPlan plan;
    require(plan_rig_damage(previous, current, false, false, kNoPoint, kNoPoint, plan) == ESP_OK,
        "alpha-only change has a valid dirty plan");
    for (uint16_t y = 75; y < 104; ++y) {
        require(plan.row_pairs[y / 2].left <= 200 && plan.row_pairs[y / 2].right >= 229,
            "every alpha-changing icon row is dirty");
    }
    current.personality = {};
    require(plan_rig_damage(previous, current, false, false, kNoPoint, kNoPoint, plan) == ESP_OK,
        "departing icon has a valid dirty plan");
    require(plan.row_pairs[80 / 2].left <= 200 && plan.row_pairs[80 / 2].right >= 229,
        "departing icon footprint is erased");
    constexpr uint16_t background = 0x3186;
    constexpr uint16_t sentinel = 0xdead;
    std::vector<uint16_t> full(466 * 466, background);
    CODY2D::Surface canvas{full.data(), 466, 466, 466, static_cast<uint32_t>(full.size())};
    require(draw_personality_rows(previous.personality, canvas, 0, 0) == ESP_OK, "full sprite raster succeeds");
    std::vector<uint16_t> opaque(full.size(), background);
    CODY2D::Surface opaque_canvas{opaque.data(), 466, 466, 466, static_cast<uint32_t>(opaque.size())};
    PersonalityFrame opaque_frame = previous.personality;
    opaque_frame.opacity = 255;
    require(draw_personality_rows(opaque_frame, opaque_canvas, 0, 0) == ESP_OK && opaque != full,
        "nonzero frame opacity changes actual RGB565 output");
    std::array<uint16_t, 23 * 20> tile;
    tile.fill(sentinel);
    for (unsigned y = 0; y < 20; ++y) {
        for (unsigned x = 0; x < 20; ++x) {
            tile[y * 23 + x] = background;
        }
    }
    CODY2D::Surface compact{tile.data(), 20, 20, 23, static_cast<uint32_t>(tile.size())};
    require(draw_personality_rows(previous.personality, compact, 80, 205) == ESP_OK,
        "compact sprite raster succeeds");
    bool painted = false;
    for (unsigned y = 0; y < 20; ++y) {
        for (unsigned x = 0; x < 20; ++x) {
            require(tile[y * 23 + x] == full[(80 + y) * 466 + 205 + x], "compact sprite matches full canvas");
            painted = painted || tile[y * 23 + x] != background;
        }
        require(tile[y * 23 + 20] == sentinel && tile[y * 23 + 21] == sentinel &&
            tile[y * 23 + 22] == sentinel, "sprite raster preserves row padding");
    }
    require(painted, "real flash sprite paints nontransparent samples");
    const auto before = tile;
    previous.personality.opacity = 0;
    require(draw_personality_rows(previous.personality, compact, 80, 205) == ESP_OK && tile == before,
        "zero-opacity sprite never alters pixels");
}
// No parameters: explicit synthetic rectangle isolates gesture semantics from artwork authoring.
RigScene make_petting_fixture()
{
    RigScene scene;
    scene.valid = true;
    scene.scale = 145.f;
    scene.world[to_index(PointId::EyeLeft)] = {200, 200};
    scene.world[to_index(PointId::EyeRight)] = {260, 200};
    scene.world[to_index(PointId::Mouth)] = {230, 260};
    scene.vertices[0] = {100, 100};
    scene.vertices[1] = {360, 100};
    scene.vertices[2] = {360, 360};
    scene.vertices[3] = {100, 360};
    scene.paths[to_index(PathId::Body)].count = 4;
    return scene;
}

// scene: Synthetic geometry retained throughout the contact.
// state: Actual production recognizer state.
// x: Acquired logical x.
// y: Acquired logical y.
// at_us: Explicit acquisition time.
// pressed: Acquired press/release state.
// edge: Explicit first acquired report of the contact.
PettingZone petting_sample(const RigScene &scene, PettingState &state, float x, float y,
                          int64_t at_us, bool pressed = true, bool edge = false)
{
    PettingZone zone{};
    require(observe_petting(scene, {x, y}, pressed, edge, at_us, state, zone) == ESP_OK,
            "actual caress recognizer accepts valid acquired fixture report");
    return zone;
}

// scene: Explicit displayed geometry.
// state: Contact state retaining its cooldown.
// at_us: Original press time for this independent stroke.
// y: Selected body-contained upper or lower region.
PettingZone slow_stroke(const RigScene &scene, PettingState &state, int64_t at_us, float y)
{
    petting_sample(scene, state, 220, y, at_us, true, true);
    PettingZone zone{};
    for (int index = 1; index <= 6; ++index) {
        const PettingZone sample = petting_sample(scene, state, 220.f + index * 4.f, y, at_us + index * 100000);
        if (sample != PettingZone::None) {
            zone = sample;
        }
    }
    return zone;
}

// No parameters: the authored automatic Greeting geometry connects a real crown stroke to a visible heart.
// Exception: Host framebuffer allocation may throw std::bad_alloc.
void check_routine_greeting_caress()
{
    // This is the automatic Greeting's authored placement, not the full-size idle preset.
    // Main permission is supplied explicitly: this portable fixture does not execute firmware routing.
    const Transform greeting_transform{233.f, 225.f, 130.f};
    RigScene scene;
    require(evaluate_rig(Data::defaults, greeting_transform, scene) == ESP_OK,
            "automatic Greeting130 evaluates the actual authored cloud geometry");
    const auto crown = scene.world[to_index(PointId::LobeCrown)];
    const auto left_eye = scene.world[to_index(PointId::EyeLeft)];
    const auto right_eye = scene.world[to_index(PointId::EyeRight)];
    const float eye_line = left_eye.y < right_eye.y ? left_eye.y : right_eye.y;
    const float upper_limit = eye_line - scene.scale * PettingConfig::upper_margin_per_scale;
    const float stroke_y = (crown.y + upper_limit) * .5f;
    const float stroke_start_x = crown.x - 16.f;
    require(crown.y < stroke_y && stroke_y < upper_limit,
            "Greeting stroke lies between the real crown and protected eye band");

    PettingState contact;
    HappinessState happiness;
    require(initialize_happiness(PersonalityData::happiness, happiness) == ESP_OK,
            "Greeting interaction initializes the authored session mood");
    unsigned recognized = 0;
    PersonalityCue desired = PersonalityCue::None;
    for (unsigned index = 0; index <= 8; ++index) {
        const CODY2D::Vec2 point{stroke_start_x + index * 4.f, stroke_y};
        bool inside_body = false;
        require(hit_test_body(scene, point, inside_body) == ESP_OK && inside_body,
                "every acquired Greeting crown stroke point lies inside the actual cloud body");
        PettingZone zone{};
        require(observe_petting(scene, point, true, index == 0,
                    1000000 + static_cast<int64_t>(index) * 100000,
                    contact, zone) == ESP_OK,
                "actual Greeting crown stroke accepts bounded100ms acquired reports");
        if (zone != PettingZone::None) {
            ++recognized;
            require(zone == PettingZone::AboveEyes,
                    "actual Greeting crown stroke dispatches the anatomical upper caress");
            require(apply_happiness_event(PersonalityData::happiness,
                        PettingConfig::event, happiness) == ESP_OK,
                    "recognized Greeting caress applies the authored typed mood event");
            bool release_affection = false;
            require(select_personality_target(PersonalityCue::None, false, true,
                        desired, release_affection) == ESP_OK &&
                        desired == PersonalityCue::Affection && !release_affection,
                    "recognized Greeting caress selects a live heart on an empty channel");
        }
    }
    require(recognized == 1 && happiness.happiness == 60 && happiness.decay_remainder_us == 0,
            "one real Greeting stroke increases initial happiness50 to60 exactly once");
    require(contact.rejection == PettingRejection::None && contact.rejected_at_us == 0,
            "recognized real Greeting stroke contains no invented rejection diagnostics");

    CodySize size{};
    require(cody_size_for_scale(greeting_transform.scale, size) == ESP_OK && size == CodySize::Greeting,
            "automatic Greeting130 uses its actual typed character size");
    PersonalityContext context;
    context.presentation = PersonalityPresentation::Main;
    context.nominal_scale = greeting_transform.scale;
    PersonalityLag lag;
    PersonalityFrame frame;
    for (const int64_t elapsed_us : {int64_t{0}, int64_t{300000}, int64_t{400000}}) {
        require(prepare_personality_frame(scene, desired, size, context,
                    elapsed_us, false, false, exclusions, 2, lag, frame) == ESP_OK,
                "actual Greeting130 caress prepares the bounded main-view heart transition");
        require(frame.status != PersonalityPresentationStatus::BlockedByLayout,
                "actual Greeting130 heart clears the cloud, circle and reserved status bounds");
    }
    require(frame.cue == PersonalityCue::Affection && frame.opacity == 255 &&
                frame.status == PersonalityPresentationStatus::Visible,
            "actual Greeting130 heart is fully visible after300ms stability and400ms fade");
    constexpr uint16_t background = 0x3186;
    std::vector<uint16_t> pixels(466 * 466, background);
    const CODY2D::Surface canvas{pixels.data(), 466, 466, 466,
                               static_cast<uint32_t>(pixels.size())};
    require(draw_personality_rows(frame, canvas, 0, 0) == ESP_OK,
            "actual Greeting130 heart paints its flash-backed sprite");
    bool painted = false;
    for (const uint16_t pixel : pixels) {
        painted = painted || pixel != background;
    }
    require(painted, "visible Greeting caress heart changes actual RGB565 pixels");

    PettingState rejected_contact;
    PettingZone rejected_zone{};
    const CODY2D::Vec2 first_point{stroke_start_x, stroke_y};
    const CODY2D::Vec2 rapid_point{stroke_start_x + 8.f, stroke_y};
    require(observe_petting(scene, first_point, true, true, 2000000,
                rejected_contact, rejected_zone) == ESP_OK &&
                observe_petting(scene, rapid_point, true, false, 2001000,
                rejected_contact, rejected_zone) == ESP_OK &&
                rejected_contact.rejection == PettingRejection::TooFast &&
                rejected_contact.rejected_at_us == 2001000 &&
                rejected_contact.rejected_speed_valid && rejected_contact.rejected_body_hit,
            "actual Greeting rapid segment retains its first measured rejection");
    const auto original_rejection = rejected_contact;
    require(observe_petting(scene, {20, 20}, true, false, 2401000,
                rejected_contact, rejected_zone) == ESP_OK &&
                rejected_contact.rejection == original_rejection.rejection &&
                rejected_contact.rejected_at_us == original_rejection.rejected_at_us &&
                rejected_contact.rejected_point.x == rapid_point.x &&
                rejected_contact.rejected_point.y == rapid_point.y &&
                rejected_contact.rejected_sample_gap_us == 1000 &&
                rejected_contact.rejected_speed_pixels_per_second ==
                    original_rejection.rejected_speed_pixels_per_second,
            "later outside-body gap does not overwrite the real Greeting first rejection");
}

// No parameters: actual production gesture logic rejects accidental contacts and duplicate reactions.
void check_petting()
{
    const RigScene scene = make_petting_fixture();
    PettingState state;
    require(slow_stroke(scene, state, 1000000, 150) == PettingZone::AboveEyes,
            "slow upper-body stroke recognizes a caress");
    require(petting_sample(scene, state, 248, 150, 1700000) == PettingZone::None,
            "same contact cannot trigger twice");
    petting_sample(scene, state, 248, 150, 1800000, false);
    require(slow_stroke(scene, state, 2000000, 290) == PettingZone::None,
            "second contact during cooldown is suppressed");
    require(state.rejection == PettingRejection::Cooldown && state.rejected_at_us == 2000000 &&
                state.rejected_zone == PettingZone::BelowMouth && state.rejected_body_hit &&
                !state.rejected_speed_valid,
            "cooldown records its initial anatomical contact without inventing segment speed");
    require(slow_stroke(scene, state, 8000000, 290) == PettingZone::BelowMouth,
            "lower-body stroke works after cooldown");
    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    require(petting_sample(scene, state, 244, 150, 1100000, false) == PettingZone::None,
            "tap or release displacement does not recognize a caress");
    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    petting_sample(scene, state, 270, 150, 1050000);
    require(state.rejected && state.rejection == PettingRejection::TooFast &&
                state.rejected_at_us == 1050000 && state.rejected_sample_gap_us == 50000 &&
                state.rejected_point.x == 270 && state.rejected_point.y == 150 &&
                state.rejected_zone == PettingZone::AboveEyes && state.rejected_body_hit &&
                state.rejected_speed_valid && state.rejected_speed_pixels_per_second == 1000.f,
            "fast swipe retains the real point, interval, anatomical zone and measured1000px/s speed");
    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    petting_sample(scene, state, 221, 150, 1002000);
    petting_sample(scene, state, 222, 150, 1004000);
    require(state.rejected && state.rejection == PettingRejection::TooFast &&
                state.rejected_sample_gap_us == 2000 &&
                state.rejected_speed_pixels_per_second == 500.f,
            "dense reports diagnose speed across the accumulated jitter-filter segment");
    state = {};
    require(slow_stroke(scene, state, 1000000, 230) == PettingZone::None,
            "face middle band excludes petting");
    require(state.rejection == PettingRejection::FaceBand && state.rejected_at_us == 1000000 &&
                state.rejected_body_hit && state.rejected_zone == PettingZone::None &&
                !state.rejected_speed_valid,
            "face-band exclusion remains distinct from an outside-body contact");
    state = {};
    require(slow_stroke(scene, state, 1000000, 80) == PettingZone::None,
            "stroke outside actual body excludes petting");
    require(state.rejection == PettingRejection::OutsideBody && !state.rejected_body_hit &&
                state.rejected_at_us == 1000000 && state.rejected_point.y == 80,
            "initial outside-body rejection preserves its first actual point and timestamp");
    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    petting_sample(scene, state, 224, 150, 1400000);
    require(state.rejected && state.rejection == PettingRejection::SampleGap &&
                state.rejected_sample_gap_us == 400000 && state.rejected_at_us == 1400000 &&
                !state.rejected_speed_valid,
            "missing acquisition interval records400ms gap before any speed evaluation");
    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    petting_sample(scene, state, 224, 150, 1100000);
    cancel_petting(state);
    require(petting_sample(scene, state, 248, 150, 1200000) == PettingZone::None &&
            !state.contact_active && state.rejection == PettingRejection::None,
            "route rotation or park cancellation clears diagnostics and cannot resume an old contact");
    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    for (int index = 1; index <= 8; ++index) {
        require(petting_sample(scene, state, 220.f + index % 2, 150,
                               1000000 + index * 100000) == PettingZone::None,
                 "stationary noisy contact never becomes a caress");
    }

    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    petting_sample(scene, state, 224, 150, 1000000);
    require(state.rejection == PettingRejection::NonMonotonic && state.rejected_sample_gap_us == 0 &&
                state.rejected_at_us == 1000000 && !state.rejected_speed_valid,
            "duplicate acquisition timestamp is explicitly diagnosed before dividing by elapsed time");
    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    petting_sample(scene, state, 224, 150, 999999);
    require(state.rejection == PettingRejection::NonMonotonic && state.rejected_sample_gap_us == -1 &&
                state.rejected_at_us == 999999,
            "backward acquisition timestamp retains the actual negative interval");

    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    for (int index = 1; index <= 31; ++index) {
        petting_sample(scene, state, 220, 150, 1000000 + index * 100000);
    }
    require(state.rejection == PettingRejection::MaxDuration && state.rejected_at_us == 4100000 &&
                state.rejected_sample_gap_us == 100000 && !state.rejected_speed_valid,
            "continuous stationary samples distinguish maximum duration from a missing-report gap");

    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    petting_sample(scene, state, 224, 290, 1100000);
    require(state.rejection == PettingRejection::ZoneChanged &&
                state.zone == PettingZone::AboveEyes && state.rejected_zone == PettingZone::BelowMouth &&
                state.rejected_body_hit && !state.rejected_speed_valid,
            "anatomical zone crossing retains original and observed zones before speed classification");
    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    petting_sample(scene, state, 224, 230, 1100000);
    require(state.rejection == PettingRejection::FaceBand && state.rejected_body_hit,
            "entering the protected face band is distinct from crossing between eligible zones");
    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    petting_sample(scene, state, 224, 80, 1100000);
    require(state.rejection == PettingRejection::OutsideBody && !state.rejected_body_hit,
            "leaving the actual body is distinct from a protected face-band exclusion");
    petting_sample(scene, state, 224, 80, 1200000, false);
    require(!state.contact_active && state.rejection == PettingRejection::None &&
                state.rejected_at_us == 0 && !state.rejected_speed_valid,
            "acquired release clears the finished contact's rejection evidence");

    const PettingRejection reasons[] = {PettingRejection::None, PettingRejection::OutsideBody,
        PettingRejection::FaceBand, PettingRejection::Cooldown, PettingRejection::NonMonotonic,
        PettingRejection::SampleGap, PettingRejection::MaxDuration, PettingRejection::ZoneChanged,
        PettingRejection::TooFast};
    const char *names[] = {"None", "OutsideBody", "FaceBand", "Cooldown", "NonMonotonic",
        "SampleGap", "MaxDuration", "ZoneChanged", "TooFast"};
    for (size_t index = 0; index < std::size(reasons); ++index) {
        require(std::strcmp(petting_rejection_name(reasons[index]), names[index]) == 0,
                "every typed rejection has its own exact bounded ASCII diagnostic name");
    }
    require(std::strcmp(petting_rejection_name(static_cast<PettingRejection>(255)), "Invalid") == 0,
            "unknown rejection identity is never silently presented as no rejection");
}
// No parameters: portable arbitration checks the single canonical-admitted latest target.
void check_symbol_arbitration()
{
    PersonalityCue desired = PersonalityCue::Joy;
    bool release = true;
    require(select_personality_target(PersonalityCue::None, false, true, desired, release) == ESP_OK &&
            desired == PersonalityCue::Affection && !release, "active caress owns an otherwise empty channel");
    require(select_personality_target(PersonalityCue::Charging, false, true, desired, release) == ESP_OK &&
            desired == PersonalityCue::Affection && !release, "charging waits behind a live auto heart");
    require(select_personality_target(PersonalityCue::Charging, false, false, desired, release) == ESP_OK &&
            desired == PersonalityCue::Charging, "charging becomes latest target after the heart lifetime");
    require(select_personality_target(PersonalityCue::LowBattery, false, true, desired, release) == ESP_OK &&
            desired == PersonalityCue::LowBattery && release, "admitted low battery cancels the automatic heart request");
    require(select_personality_target(PersonalityCue::LowBattery, true, true, desired, release) == ESP_OK &&
            desired == PersonalityCue::Sleep && release, "semantic Sleep retains precedence over low battery");
    require(select_personality_target(PersonalityCue::Joy, false, true, desired, release) == ESP_OK &&
            desired == PersonalityCue::Affection && !release, "authored higher-priority heart retains a lower-priority latest baseline");
    require(select_personality_target(PersonalityCue::Joy, false, false, desired, release) == ESP_OK &&
            desired == PersonalityCue::Joy && release, "latest baseline replaces any waiting charge without a queue");
    require(select_personality_target(PersonalityCue::None, false, true, desired, release) == ESP_OK &&
            desired == PersonalityCue::Affection, "canonical rejection represented as None does not block a valid heart");
    desired = PersonalityCue::Joy;
    release = false;
    require(select_personality_target(static_cast<PersonalityCue>(255), false, true,
                                     desired, release) == ESP_ERR_INVALID_ARG &&
            desired == PersonalityCue::Joy && !release, "invalid arbitration identity preserves caller outputs");

    Scenario urgent{"low-battery-affection"};
    step(urgent, "heart-request", PersonalityCue::Affection, 0);
    step(urgent, "heart-stable", PersonalityCue::Affection, 0);
    step(urgent, "heart-half-in", PersonalityCue::Affection, 200000);
    const uint8_t before = urgent.frame.opacity;
    step(urgent, "low-battery-preempts", PersonalityCue::LowBattery, 0);
    require(urgent.lag.phase == PersonalityPhase::FadeOut && urgent.lag.cue == PersonalityCue::Affection &&
            urgent.frame.opacity == before, "urgent battery releases an incomplete heart fade without an opacity jump");
    // The authored fade-out is 400 ms; halfway retains half the departing opacity.
    step(urgent, "heart-half-out", PersonalityCue::LowBattery, 200000);
    require(urgent.frame.cue == PersonalityCue::Affection && urgent.frame.opacity == 64,
            "urgent battery keeps one departing half-opacity heart sprite");
    step(urgent, "battery-entry", PersonalityCue::LowBattery, 200000);
    require(urgent.lag.cue == PersonalityCue::LowBattery && urgent.frame.cue == PersonalityCue::None &&
            urgent.frame.opacity == 0 && urgent.frame.bounds.empty() &&
            urgent.lag.phase == PersonalityPhase::FadeIn, "urgent battery enters only after heart reaches zero");
    step(urgent, "battery-half-in", PersonalityCue::LowBattery, 200000);
    require(urgent.frame.opacity == 128, "urgent battery retains its authored 400 ms fade-in");
    step(urgent, "battery-visible", PersonalityCue::LowBattery, 200000);

    Scenario hidden{"low-battery-empty"};
    step(hidden, "immediate-entry", PersonalityCue::LowBattery, 0);
    require(hidden.lag.phase == PersonalityPhase::FadeIn && hidden.lag.cue == PersonalityCue::LowBattery,
            "admitted low battery bypasses narrative request stability");

    Scenario hold{"low-battery-hold"};
    step(hold, "heart-request", PersonalityCue::Affection, 0);
    step(hold, "heart-stable", PersonalityCue::Affection, 0);
    step(hold, "heart-visible", PersonalityCue::Affection, 400000);
    step(hold, "battery-releases-hold", PersonalityCue::LowBattery, 0);
    require(hold.lag.phase == PersonalityPhase::FadeOut && hold.frame.opacity == 255,
            "admitted low battery releases minimum artistic hold while preserving full current alpha");

    Scenario charging{"charging-after-affection"};
    step(charging, "heart-request", PersonalityCue::Affection, 0);
    step(charging, "heart-stable", PersonalityCue::Affection, 0);
    step(charging, "heart-visible", PersonalityCue::Affection, 400000);
    require(select_personality_target(PersonalityCue::Charging, false, true, desired, release) == ESP_OK,
            "charging baseline selects a portable waiting target");
    step(charging, "charging-waits-auto-heart", desired, 2000000);
    require(charging.lag.phase == PersonalityPhase::Visible && charging.lag.cue == PersonalityCue::Affection,
            "charging baseline cannot release a still-live automatic heart");
    require(select_personality_target(PersonalityCue::Charging, false, false, desired, release) == ESP_OK,
            "expired automatic lifetime exposes latest charging baseline");
    step(charging, "heart-expired", desired, 0);
    require(charging.lag.phase == PersonalityPhase::Visible, "normal charging respects remaining heart dwell");
    step(charging, "heart-finishes-hold", desired, 500000);
    require(charging.lag.phase == PersonalityPhase::FadeOut && charging.frame.opacity == 255,
            "charging transition begins through a smooth existing heart fade");
    step(charging, "heart-half-out", desired, 200000);
    require(charging.frame.cue == PersonalityCue::Affection && charging.frame.opacity == 128,
            "waiting charge does not overlap the departing heart");
    step(charging, "charge-entry", desired, 200000);
    require(charging.lag.cue == PersonalityCue::Charging && charging.frame.cue == PersonalityCue::None &&
            charging.frame.opacity == 0 && charging.frame.bounds.empty(),
            "charging enters at zero only after the heart fade completes");
    step(charging, "charge-visible", desired, 400000);
    require(charging.frame.opacity == 255, "waiting charging retains normal fade-in");
}

// No parameters: actual portable mood arithmetic is checked against elapsed-time and domain outcomes.
void check_happiness()
{
    require(static_cast<unsigned>(PersonalityEvent::Caress) == 1 &&
            static_cast<unsigned>(PettingZone::AboveEyes) == 1 &&
            static_cast<unsigned>(PettingZone::BelowMouth) == 2 &&
            static_cast<unsigned>(PersonalityMetric::Happiness) == 1 &&
            static_cast<unsigned>(PersonalityMetric::Sadness) == 2,
            "generated event, zone and metric identities remain stable independent of ordering");
    HappinessState state;
    require(initialize_happiness(PersonalityData::happiness, state) == ESP_OK &&
            state.happiness == PersonalityData::happiness.initial && state.decay_remainder_us == 0,
            "compiled authored happiness initializes the session without persistence");
    const HappinessConfig config{0, 100, 50, 10, 1, 60000000};
    require(initialize_happiness(config, state) == ESP_OK && state.happiness == 50,
            "explicit mood fixture starts at midpoint");
    require(advance_happiness(config, 59999999, state) == ESP_OK && state.happiness == 50 &&
            state.decay_remainder_us == 59999999, "a partial minute does not decay happiness");
    require(advance_happiness(config, 1, state) == ESP_OK && state.happiness == 49 &&
            state.decay_remainder_us == 0, "exact full minute decays one point");
    require(advance_happiness(config, 30000000, state) == ESP_OK,
            "half minute advances the artistic clock");
    require(apply_happiness_event(config, PersonalityEvent::Caress, state) == ESP_OK && state.happiness == 59 &&
            state.decay_remainder_us == 30000000, "caress gains ten without resetting the minute phase");
    require(advance_happiness(config, 30000000, state) == ESP_OK && state.happiness == 58 &&
            state.decay_remainder_us == 0, "caress does not postpone the already scheduled next decay");
    state = {99, 13000000};
    require(apply_happiness_event(config, PersonalityEvent::Caress, state) == ESP_OK && state.happiness == 100 &&
            state.decay_remainder_us == 13000000, "authored ceiling bounds gain while retaining phase");
    state = {0, 20000000};
    require(advance_happiness(config, 125000000, state) == ESP_OK && state.happiness == 0 &&
            state.decay_remainder_us == 25000000, "floor discards unpaid full decay while retaining bounded phase");
    require(apply_happiness_event(config, PersonalityEvent::Caress, state) == ESP_OK && state.happiness == 10 &&
            state.decay_remainder_us == 25000000, "a caress at the floor creates no replayed decay debt");
    require(advance_happiness(config, 35000000, state) == ESP_OK && state.happiness == 9 &&
            state.decay_remainder_us == 0, "floor recovery pays only the next actual full minute");
    state = {50, 0};
    require(advance_happiness(config, INT64_MAX, state) == ESP_OK && state.happiness == 0 &&
            state.decay_remainder_us == INT64_MAX % config.decay_interval_us,
            "huge elapsed intervals saturate safely without arithmetic overflow");
    HappinessState whole{50, 0};
    HappinessState partitioned{50, 0};
    require(advance_happiness(config, 60000000, whole) == ESP_OK &&
            advance_happiness(config, 27000000, partitioned) == ESP_OK &&
            advance_happiness(config, 8000000, partitioned) == ESP_OK &&
            advance_happiness(config, 25000000, partitioned) == ESP_OK &&
            whole.happiness == partitioned.happiness &&
            whole.decay_remainder_us == partitioned.decay_remainder_us,
            "decay depends on awake elapsed time rather than update frequency");
    const HappinessState unchanged = partitioned;
    require(advance_happiness(config, -1, partitioned) == ESP_ERR_INVALID_ARG &&
            partitioned.happiness == unchanged.happiness &&
            partitioned.decay_remainder_us == unchanged.decay_remainder_us,
            "negative elapsed time fails without changing mood");
    HappinessConfig invalid = config;
    invalid.decay_points = 0;
    require(initialize_happiness(invalid, partitioned) == ESP_ERR_INVALID_ARG &&
            partitioned.happiness == unchanged.happiness &&
            partitioned.decay_remainder_us == unchanged.decay_remainder_us,
            "invalid authored decay is rejected without a default or reset");
    invalid = config;
    invalid.initial = 101;
    require(initialize_happiness(invalid, partitioned) == ESP_ERR_INVALID_ARG,
            "out-of-domain authored initial value is rejected");
    state = {101, 0};
    require(apply_happiness_event(config, PersonalityEvent::Caress, state) == ESP_ERR_INVALID_STATE && state.happiness == 101,
            "invalid existing happiness is rejected rather than clamped as a repair");
    state = {50, 60000000};
    require(advance_happiness(config, 0, state) == ESP_ERR_INVALID_STATE &&
            state.decay_remainder_us == 60000000, "invalid phase is rejected without repair");
    state = {93, 40000000};
    const HappinessState before_invalid_event = state;
    require(apply_happiness_event(config, static_cast<PersonalityEvent>(255), state) == ESP_ERR_INVALID_ARG &&
            state.happiness == before_invalid_event.happiness &&
            state.decay_remainder_us == before_invalid_event.decay_remainder_us,
            "unknown typed event cannot change mood or silently dispatch a caress");
    require(initialize_happiness(config, state) == ESP_OK && state.happiness == 50 &&
            state.decay_remainder_us == 0, "new session deliberately starts from authored initial mood");
}
// No parameters: canonical percentage admission has hysteresis and never fabricates missing values.
void check_battery_hysteresis()
{
    PersonalityBatteryLatch latch;
    bool alert = false;
    const int enter = PersonalityConfig::low_battery_enter_percent;
    const int exit = PersonalityConfig::low_battery_exit_percent;
    require(update_personality_battery_latch(true, enter + 1, latch, alert) == ESP_OK && !alert,
            "initial percentage inside hysteresis band does not invent a prior alert");
    require(update_personality_battery_latch(true, enter, latch, alert) == ESP_OK && alert,
            "fresh percentage enters low-battery alert at the inclusive authored boundary");
    require(update_personality_battery_latch(true, exit - 1, latch, alert) == ESP_OK && alert,
            "fresh percentage inside recovery band retains a confirmed low-battery alert");
    require(update_personality_battery_latch(true, exit, latch, alert) == ESP_OK && !alert,
            "fresh percentage clears at the inclusive authored recovery boundary");
    require(update_personality_battery_latch(true, enter + 1, latch, alert) == ESP_OK && !alert,
            "small discharge after recovery does not chatter into a new alert");
    require(update_personality_battery_latch(true, enter, latch, alert) == ESP_OK && alert,
            "later genuine low percentage can re-enter alert");
    require(update_personality_battery_latch(false, 101, latch, alert) == ESP_OK && !alert &&
            !latch.low_battery_active, "unavailable sample clears alert without interpreting an unmeasured number");
    require(update_personality_battery_latch(true, enter + 1, latch, alert) == ESP_OK && !alert,
            "unknown interval clears hysteresis memory before a mid-band recovery sample");
    require(update_personality_battery_latch(true, enter, latch, alert) == ESP_OK && alert,
            "new current low measurement restores the alert");
    require(update_personality_battery_latch(true, 101, latch, alert) == ESP_ERR_INVALID_RESPONSE &&
            latch.low_battery_active && alert, "invalid actual percentage is rejected without silently repairing the latch");
    require(update_personality_battery_latch(true, -1, latch, alert) == ESP_ERR_INVALID_RESPONSE &&
            latch.low_battery_active && alert, "negative actual percentage is rejected with its real cause");
}

// cue: Stable identity whose authored repeat interval is requested by a fixture.
int64_t repeat_interval(PersonalityCue cue)
{
    for (const auto &rule : PersonalityConfig::cue_rules) {
        if (rule.cue == cue) {
            return rule.repeat_cooldown_us;
        }
    }
    require(false, "fixture identity has an authored repeat rule");
    return 0;
}

// No parameters: exact admission timestamps exercise repeat suppression without retaining old events.
void check_repeat_suppression()
{
    Scenario repeat{"joy-repeat"};
    step(repeat, "first-request", PersonalityCue::Joy, 0);
    step(repeat, "first-admission", PersonalityCue::Joy, 300000);
    const size_t slot = static_cast<size_t>(PersonalityCue::Joy) - 1U;
    const int64_t first_stamp = repeat.lag.last_admitted_at_us[slot];
    require(first_stamp == 300001, "first zero-alpha admission starts the cue repeat interval");
    step(repeat, "first-visible", PersonalityCue::Joy, 400000);
    step(repeat, "release-target", PersonalityCue::None, 0);
    step(repeat, "finish-hold", PersonalityCue::None, 2500000);
    step(repeat, "hidden", PersonalityCue::None, 600000);
    step(repeat, "repeat-request", PersonalityCue::Joy, 0);
    require(repeat.frame.status == PersonalityPresentationStatus::SuppressedByCooldown &&
            repeat.lag.phase == PersonalityPhase::Hidden && repeat.frame.opacity == 0,
            "repeated ordinary cue is suppressed without flashing or replaying a sprite");
    const int64_t clock_before_pause = repeat.lag.active_elapsed_us;
    step(repeat, "paused-cooldown", PersonalityCue::Joy, 20000000,
         PersonalityPresentation::Main, true);
    require(repeat.lag.active_elapsed_us == clock_before_pause &&
            repeat.lag.last_admitted_at_us[slot] == first_stamp,
            "paused animation cannot pay a repeat cooldown using wall-clock time");
    step(repeat, "expire-auto-request", PersonalityCue::None, 0);
    require(repeat.frame.status == PersonalityPresentationStatus::Hidden &&
            repeat.lag.desired == PersonalityCue::None, "expired request leaves no queued symbol");
    step(repeat, "disabled-route", PersonalityCue::None, 1000000, PersonalityPresentation::Disabled);
    require(repeat.lag.last_admitted_at_us[slot] == first_stamp && !repeat.lag.primed,
            "route disable clears presentation but retains per-cue repeat identity");
    step(repeat, "micro-route", PersonalityCue::None, 1000000,
         PersonalityPresentation::Main, false, 0, 0, 0, 22);
    require(repeat.lag.last_admitted_at_us[slot] == first_stamp && !repeat.lag.primed,
            "Micro exclusion cannot reset repeat history");
    step(repeat, "new-live-request", PersonalityCue::Joy, 0);
    const int64_t until_ready = first_stamp - 1 + repeat_interval(PersonalityCue::Joy) -
        repeat.lag.active_elapsed_us;
    require(until_ready > 300000, "fixture retains a real remaining authored symbol cooldown");
    step(repeat, "one-us-before-ready", PersonalityCue::Joy, until_ready - 1);
    require(repeat.frame.status == PersonalityPresentationStatus::SuppressedByCooldown,
            "new live target remains suppressed until the exact repeat boundary");
    step(repeat, "repeat-boundary", PersonalityCue::Joy, 1);
    require(repeat.lag.phase == PersonalityPhase::FadeIn && repeat.frame.opacity == 0 &&
            repeat.lag.last_admitted_at_us[slot] == repeat.lag.active_elapsed_us + 1,
            "eligible latest target re-enters gently and records its new admission");

    const int64_t clock = repeat.lag.active_elapsed_us;
    const auto timestamps = repeat.lag.last_admitted_at_us;
    clear_personality_presentation(repeat.lag);
    require(repeat.lag.phase == PersonalityPhase::Hidden && !repeat.lag.primed &&
            repeat.lag.active_elapsed_us == clock && repeat.lag.last_admitted_at_us == timestamps,
            "explicit route orientation or parked reset preserves all repeat records");
    RigScene scene = make_scene();
    PersonalityContext context;
    context.presentation = PersonalityPresentation::Main;
    PersonalityFrame frame;
    const PersonalityLag unchanged = repeat.lag;
    require(prepare_personality_frame(scene, PersonalityCue::None, CodySize::Full, context,
        INT64_MAX, false, false, exclusions, 2, repeat.lag, frame) == ESP_ERR_INVALID_STATE &&
        repeat.lag.active_elapsed_us == unchanged.active_elapsed_us,
        "active clock overflow is rejected without destroying repeat history");
}
// state: Pure fixture state whose stable identity is requested.
// reminder: Typed reminder, independent of configured storage ordering.
const WellbeingReminderState &wellbeing_item(const WellbeingState &state, PersonalityReminder reminder)
{
    for (const auto &item : state.reminders) {
        if (item.reminder == reminder) {
            return item;
        }
    }
    require(false, "wellbeing fixture contains the requested stable identity");
    return state.reminders[0];
}

// first: One complete state from the pure engine.
// second: Expected unchanged state or an independently partitioned elapsed sequence.
bool same_wellbeing(const WellbeingState &first, const WellbeingState &second)
{
    if (first.session != second.session || first.active_elapsed_us != second.active_elapsed_us ||
        first.fatigue_elapsed_us != second.fatigue_elapsed_us) {
        return false;
    }
    for (const auto &item : first.reminders) {
        const auto &other = wellbeing_item(second, item.reminder);
        if (item.phase != other.phase || item.deadline_us != other.deadline_us) {
            return false;
        }
    }
    return true;
}

// No parameters: dormant authored configuration and pure session/deadline semantics remain separate from UI.
void check_wellbeing()
{
    const auto none = PersonalityReminder::None;
    WellbeingState state;
    require(!PersonalityData::wellbeing.enabled &&
            initialize_wellbeing(PersonalityData::wellbeing, state) == ESP_OK,
            "authored wellbeing engine is intentionally dormant and initializes without a runtime connection");
    const WellbeingState dormant = state;
    require(apply_wellbeing_event(PersonalityData::wellbeing, PersonalityEvent::WorkSessionStart,
                                 none, state) == ESP_ERR_NOT_SUPPORTED && same_wellbeing(state, dormant),
            "disabled authored engine explicitly rejects starting work without mutation");
    require(advance_wellbeing(PersonalityData::wellbeing, 900000000, state) == ESP_OK &&
            same_wellbeing(state, dormant), "disabled dormant state does not accumulate invented work time");
    WellbeingConfig config = PersonalityData::wellbeing;
    config.enabled = true; // Explicit synthetic fixture configuration, never firmware runtime activation.
    require(initialize_wellbeing(config, state) == ESP_OK &&
            apply_wellbeing_event(config, PersonalityEvent::WorkSessionStart, none, state) == ESP_OK,
            "pure fixture starts a new identified work session");
    require(advance_wellbeing(config, 20 * kWellbeingMinuteUs - 1, state) == ESP_OK &&
            wellbeing_item(state, PersonalityReminder::EyeRest).phase == WellbeingReminderPhase::Counting,
            "eye reminder does not become due before its actual active-work boundary");
    WellbeingSnapshot snapshot;
    require(read_wellbeing_snapshot(config, state, snapshot) == ESP_OK && snapshot.fatigue_minutes == 19 &&
            snapshot.fatigue_remainder_us == kWellbeingMinuteUs - 1,
            "fatigue is a derived active-work minute count with its original sub-minute remainder");
    require(advance_wellbeing(config, 1, state) == ESP_OK &&
            wellbeing_item(state, PersonalityReminder::EyeRest).phase == WellbeingReminderPhase::Due &&
            wellbeing_item(state, PersonalityReminder::MovementBreak).phase == WellbeingReminderPhase::Counting,
            "independent eye deadline becomes due at twenty minutes");
    const int64_t eye_deadline = wellbeing_item(state, PersonalityReminder::EyeRest).deadline_us;
    require(advance_wellbeing(config, 10 * kWellbeingMinuteUs, state) == ESP_OK &&
            wellbeing_item(state, PersonalityReminder::MovementBreak).phase == WellbeingReminderPhase::Due &&
            wellbeing_item(state, PersonalityReminder::EyeRest).deadline_us == eye_deadline,
            "overdue eye reminder stays latched while independent movement timer becomes due");
    require(apply_wellbeing_event(config, PersonalityEvent::ReminderAcknowledge,
                                 PersonalityReminder::EyeRest, state) == ESP_OK &&
            state.fatigue_elapsed_us == 30 * kWellbeingMinuteUs,
            "acknowledging eye rest restarts its timer but cannot reset fatigue");
    require(apply_wellbeing_event(config, PersonalityEvent::ReminderSnooze,
                                 PersonalityReminder::MovementBreak, state) == ESP_OK &&
            wellbeing_item(state, PersonalityReminder::MovementBreak).phase == WellbeingReminderPhase::Snoozed &&
            wellbeing_item(state, PersonalityReminder::MovementBreak).deadline_us == 40 * kWellbeingMinuteUs,
            "due movement reminder snoozes on the authored active-work clock");
    require(advance_wellbeing(config, 10 * kWellbeingMinuteUs - 1, state) == ESP_OK &&
            wellbeing_item(state, PersonalityReminder::MovementBreak).phase == WellbeingReminderPhase::Snoozed,
            "snooze cannot expire one microsecond before its deadline");
    require(advance_wellbeing(config, 1, state) == ESP_OK &&
            wellbeing_item(state, PersonalityReminder::MovementBreak).phase == WellbeingReminderPhase::Due,
            "snooze re-enters due at its exact deadline without an event queue");
    require(apply_wellbeing_event(config, PersonalityEvent::WorkSessionPause, none, state) == ESP_OK,
            "active session explicitly pauses");
    const WellbeingState paused = state;
    WellbeingState paused_ack = paused;
    require(apply_wellbeing_event(config, PersonalityEvent::ReminderAcknowledge,
                                 PersonalityReminder::MovementBreak, paused_ack) == ESP_OK &&
            paused_ack.session == WorkSessionPhase::Paused &&
            paused_ack.active_elapsed_us == paused.active_elapsed_us &&
            paused_ack.fatigue_elapsed_us == paused.fatigue_elapsed_us,
            "an explicit due acknowledgment can resolve a paused reminder without resuming work");
    require(advance_wellbeing(config, INT64_MAX, state) == ESP_OK && same_wellbeing(state, paused),
            "paused work freezes fatigue and every reminder despite an arbitrary wall-clock gap");
    require(apply_wellbeing_event(config, PersonalityEvent::WorkSessionResume, none, state) == ESP_OK &&
            advance_wellbeing(config, 20 * kWellbeingMinuteUs, state) == ESP_OK &&
            wellbeing_item(state, PersonalityReminder::ScreenBreak).phase == WellbeingReminderPhase::Due &&
            wellbeing_item(state, PersonalityReminder::DrinkWater).phase == WellbeingReminderPhase::Due,
            "resumed session reaches independent screen and water deadlines on actual work time");
    require(apply_wellbeing_event(config, PersonalityEvent::ReminderAcknowledge,
                                 PersonalityReminder::DrinkWater, state) == ESP_OK &&
            state.fatigue_elapsed_us == 60 * kWellbeingMinuteUs,
            "water acknowledgment cannot reset fatigue");
    require(apply_wellbeing_event(config, PersonalityEvent::ReminderAcknowledge,
                                 PersonalityReminder::MovementBreak, state) == ESP_OK &&
            state.fatigue_elapsed_us == 60 * kWellbeingMinuteUs,
            "movement acknowledgment cannot reset fatigue");
    require(apply_wellbeing_event(config, PersonalityEvent::ReminderAcknowledge,
                                 PersonalityReminder::ScreenBreak, state) == ESP_OK &&
            state.fatigue_elapsed_us == 0 && state.active_elapsed_us == 60 * kWellbeingMinuteUs,
            "only acknowledged screen break resets fatigue while retaining the active-work clock");
    require(advance_wellbeing(config, 90000000, state) == ESP_OK &&
            read_wellbeing_snapshot(config, state, snapshot) == ESP_OK &&
            snapshot.fatigue_minutes == 1 && snapshot.fatigue_remainder_us == 30000000,
            "fatigue resumes with an independent minute phase after acknowledged screen break");
    require(apply_wellbeing_event(config, PersonalityEvent::WorkSessionEnd, none, state) == ESP_OK,
            "explicit end freezes the retained session state");
    const WellbeingState ended = state;
    WellbeingState ended_ack = ended;
    require(apply_wellbeing_event(config, PersonalityEvent::ReminderAcknowledge,
                                 PersonalityReminder::EyeRest, ended_ack) == ESP_OK &&
            ended_ack.session == WorkSessionPhase::Inactive &&
            ended_ack.active_elapsed_us == ended.active_elapsed_us &&
            ended_ack.fatigue_elapsed_us == ended.fatigue_elapsed_us,
            "retained due acknowledgment after End does not restart or add work time");
    require(advance_wellbeing(config, 20 * kWellbeingMinuteUs, state) == ESP_OK && same_wellbeing(state, ended),
            "ended sessions cannot accumulate further work time or reminders");
    require(apply_wellbeing_event(config, PersonalityEvent::WorkSessionPause, none, state) == ESP_ERR_INVALID_STATE &&
            same_wellbeing(state, ended), "inactive session cannot silently accept a pause");
    require(apply_wellbeing_event(config, PersonalityEvent::WorkSessionStart, none, state) == ESP_OK &&
            state.active_elapsed_us == 0 && state.fatigue_elapsed_us == 0,
            "new session deliberately resets elapsed work and all reminder cycles");
    const WellbeingState started = state;
    require(apply_wellbeing_event(config, PersonalityEvent::WorkSessionStart, none, state) == ESP_ERR_INVALID_STATE &&
            same_wellbeing(state, started), "duplicate Start cannot reset an active session");
    require(apply_wellbeing_event(config, PersonalityEvent::ReminderAcknowledge,
                                 PersonalityReminder::ScreenBreak, state) == ESP_ERR_INVALID_STATE &&
            same_wellbeing(state, started), "counting screen break cannot be acknowledged before it is due");
    require(apply_wellbeing_event(config, PersonalityEvent::Caress, none, state) == ESP_ERR_INVALID_ARG &&
            same_wellbeing(state, started), "a caress cannot acknowledge a reminder or affect wellbeing state");
    require(apply_wellbeing_event(config, PersonalityEvent::ReminderSnooze,
                                 static_cast<PersonalityReminder>(255), state) == ESP_ERR_INVALID_ARG &&
            same_wellbeing(state, started), "unknown typed reminder cannot select a raw storage slot");
    require(apply_wellbeing_event(config, PersonalityEvent::WorkSessionPause,
                                 PersonalityReminder::DrinkWater, state) == ESP_ERR_INVALID_ARG &&
            same_wellbeing(state, started), "lifecycle events reject ambiguous reminder payloads");
    require(advance_wellbeing(config, -1, state) == ESP_ERR_INVALID_ARG && same_wellbeing(state, started),
            "negative work interval fails without modifying clocks or deadlines");

    WellbeingState batched = started;
    WellbeingState partitioned = started;
    require(advance_wellbeing(config, 75 * kWellbeingMinuteUs, batched) == ESP_OK &&
            advance_wellbeing(config, 5 * kWellbeingMinuteUs, partitioned) == ESP_OK &&
            advance_wellbeing(config, 19 * kWellbeingMinuteUs, partitioned) == ESP_OK &&
            advance_wellbeing(config, 51 * kWellbeingMinuteUs, partitioned) == ESP_OK &&
            same_wellbeing(batched, partitioned), "deadline and fatigue outcomes are independent of update cadence");
    WellbeingConfig reordered = config;
    const WellbeingRule original_first = reordered.rules[0];
    reordered.rules[0] = reordered.rules[3];
    reordered.rules[3] = original_first;
    require(initialize_wellbeing(reordered, state) == ESP_OK &&
            apply_wellbeing_event(reordered, PersonalityEvent::WorkSessionStart, none, state) == ESP_OK &&
            advance_wellbeing(reordered, 75 * kWellbeingMinuteUs, state) == ESP_OK && same_wellbeing(state, batched),
            "reordering authored rule storage preserves reminder identities and outcomes");
    WellbeingConfig invalid = config;
    invalid.rules[1].reminder = invalid.rules[0].reminder;
    const WellbeingState before_invalid = state;
    require(initialize_wellbeing(invalid, state) == ESP_ERR_INVALID_ARG && same_wellbeing(state, before_invalid),
            "duplicate authored reminder identities fail without replacing session state");
    invalid = config;
    invalid.snooze_us = 90000000;
    require(initialize_wellbeing(invalid, state) == ESP_ERR_INVALID_ARG && same_wellbeing(state, before_invalid),
            "fractional-minute authored snooze cannot bypass the whole-minute configuration domain");
    invalid = config;
    invalid.rules[0].interval_us = 1441 * kWellbeingMinuteUs;
    require(initialize_wellbeing(invalid, state) == ESP_ERR_INVALID_ARG && same_wellbeing(state, before_invalid),
            "authored interval above one day is rejected without a default or repair");
    WellbeingConfig disabled_rule = config;
    for (auto &rule : disabled_rule.rules) {
        if (rule.reminder == PersonalityReminder::EyeRest) {
            rule.enabled = false;
        }
    }
    require(initialize_wellbeing(disabled_rule, state) == ESP_OK &&
            apply_wellbeing_event(disabled_rule, PersonalityEvent::WorkSessionStart, none, state) == ESP_OK &&
            advance_wellbeing(disabled_rule, 60 * kWellbeingMinuteUs, state) == ESP_OK &&
            wellbeing_item(state, PersonalityReminder::EyeRest).phase == WellbeingReminderPhase::Disabled,
            "disabled reminder remains identified and never becomes due when other timers advance");
    const WellbeingState disabled_eye = state;
    require(apply_wellbeing_event(disabled_rule, PersonalityEvent::ReminderAcknowledge,
                                 PersonalityReminder::EyeRest, state) == ESP_ERR_INVALID_STATE &&
            same_wellbeing(state, disabled_eye), "disabled reminder cannot be acknowledged as though measured due");
    require(initialize_wellbeing(config, state) == ESP_OK &&
            apply_wellbeing_event(config, PersonalityEvent::WorkSessionStart, none, state) == ESP_OK &&
            advance_wellbeing(config, INT64_MAX, state) == ESP_OK,
            "representable batched work interval can make all enabled reminders due without multiplication overflow");
    const WellbeingState maximum = state;
    require(advance_wellbeing(config, 1, state) == ESP_ERR_INVALID_STATE && same_wellbeing(state, maximum),
            "elapsed clock overflow is rejected atomically");
    require(apply_wellbeing_event(config, PersonalityEvent::ReminderAcknowledge,
                                 PersonalityReminder::ScreenBreak, state) == ESP_ERR_INVALID_STATE &&
            same_wellbeing(state, maximum), "acknowledgment deadline overflow cannot partially reset fatigue");
}
} // namespace

// No parameters: runs only when explicitly authorized through the public check CLI.
// Exception: Host framebuffer allocations may throw std::bad_alloc.
int main()
{
    std::printf("{\"samples\":[");
    check_channel();
    check_sleep_and_pause();
    check_follow_and_layout();
    check_rejection_and_sizes();
    check_dirty_and_raster();
    check_petting();
    check_routine_greeting_caress();
    check_happiness();
    check_symbol_arbitration();
    check_battery_hysteresis();
    check_repeat_suppression();
    check_wellbeing();
    std::printf("],\"assertions\":%d,\"host_controller_bytes\":%llu,\"repeat_slots\":%llu,\"repeat_bytes\":%llu}\n",
                assertions, static_cast<unsigned long long>(sizeof(PersonalityLag)),
                static_cast<unsigned long long>(PersonalityConfig::cue_rule_count),
                static_cast<unsigned long long>(sizeof(PersonalityLag{}.last_admitted_at_us)));
    return failures == 0 ? 0 : 1;
}
