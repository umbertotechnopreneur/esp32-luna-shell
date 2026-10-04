#include "CODY_personality.hpp"
#include "CODY_dirty_plan.hpp"
#include "CODY_rig.hpp"
#include "CODY_petting.hpp"
#include "cloud_rig_data.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
using namespace CODY;
int failures = 0;
int assertions = 0;
bool first_sample = true;
constexpr DamageRect exclusions[] = {{10, 203, 70, 263}, {106, 37, 362, 58}};

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
        "\"x\":%.9g,\"y\":%.9g,\"width\":%.9g,\"height\":%.9g}}",
        first_sample ? "" : ",", scenario.name, name, static_cast<unsigned>(cue), scale,
        static_cast<unsigned>(presentation), static_cast<long long>(delta_us), paused ? "true" : "false",
        dx, dy, squash, static_cast<unsigned>(frame.cue), frame.opacity, static_cast<unsigned>(frame.status),
        frame.bounds.left, frame.bounds.top, frame.bounds.right, frame.bounds.bottom,
        static_cast<unsigned>(lag.phase), static_cast<unsigned>(lag.desired),
        lag.center_x, lag.center_y, lag.width, lag.height);
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
    step(scenario, "intermediate", PersonalityCue::Fear, 100000);
    step(scenario, "replace-pending", PersonalityCue::Confusion, 300000);
    require(scenario.frame.cue == PersonalityCue::Joy, "minimum visible hold preserves current symbol");
    step(scenario, "hold-complete", PersonalityCue::Confusion, 2100000);
    require(scenario.lag.phase == PersonalityPhase::FadeOut, "replacement waits for2500ms full-opacity hold");
    step(scenario, "latest-during-out", PersonalityCue::Hunger, 300000);
    require(scenario.frame.cue == PersonalityCue::Joy && scenario.frame.opacity == 128,
        "replacement during fade does not interrupt current symbol");
    step(scenario, "new-target", PersonalityCue::Hunger, 300000);
    require(scenario.lag.cue == PersonalityCue::Hunger && scenario.frame.opacity == 0,
        "only latest stable target survives fade-out");
    step(scenario, "new-visible", PersonalityCue::Hunger, 400000);
    require(scenario.frame.cue == PersonalityCue::Hunger && scenario.frame.opacity == 255,
        "intermediate Fear and Confusion never replay");
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
    step(scenario, "sleep-new", PersonalityCue::None, 600000, PersonalityPresentation::Sleep);
    step(scenario, "sleep-visible", PersonalityCue::None, 400000, PersonalityPresentation::Sleep);
    require(scenario.frame.cue == PersonalityCue::Sleep && scenario.frame.opacity == 255,
        "Sleep becomes visible after previous cue releases");
    const float frozen_x = scenario.lag.center_x;
    step(scenario, "paused-exit", PersonalityCue::None, 30000000, PersonalityPresentation::Main, true, 5);
    require(scenario.frame.opacity == 255 && scenario.lag.center_x == frozen_x,
        "paused redraw freezes hold, opacity and geometry despite wall-clock gap");
    step(scenario, "resume-exit", PersonalityCue::None, 0);
    require(scenario.lag.phase == PersonalityPhase::FadeOut, "paused Sleep exit survives until resume");
    step(scenario, "released", PersonalityCue::None, 600000);
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
        zone = petting_sample(scene, state, 220.f + index * 4.f, y, at_us + index * 100000);
    }
    return zone;
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
    require(slow_stroke(scene, state, 8000000, 290) == PettingZone::BelowMouth,
            "lower-body stroke works after cooldown");
    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    require(petting_sample(scene, state, 244, 150, 1100000, false) == PettingZone::None,
            "tap or release displacement does not recognize a caress");
    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    petting_sample(scene, state, 270, 150, 1050000);
    require(state.rejected, "fast swipe rejects the whole contact");
    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    petting_sample(scene, state, 221, 150, 1002000);
    petting_sample(scene, state, 222, 150, 1004000);
    require(state.rejected, "dense small reports cannot disguise a fast swipe through jitter filtering");
    state = {};
    require(slow_stroke(scene, state, 1000000, 230) == PettingZone::None,
            "face middle band excludes petting");
    state = {};
    require(slow_stroke(scene, state, 1000000, 80) == PettingZone::None,
            "stroke outside actual body excludes petting");
    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    petting_sample(scene, state, 224, 150, 1400000);
    require(state.rejected, "missing acquisition interval rejects contact");
    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    petting_sample(scene, state, 224, 150, 1100000);
    cancel_petting(state);
    require(petting_sample(scene, state, 248, 150, 1200000) == PettingZone::None &&
            !state.contact_active, "route rotation or park cancellation cannot resume an old contact");
    state = {};
    petting_sample(scene, state, 220, 150, 1000000, true, true);
    for (int index = 1; index <= 8; ++index) {
        require(petting_sample(scene, state, 220.f + index % 2, 150,
                               1000000 + index * 100000) == PettingZone::None,
                "stationary noisy contact never becomes a caress");
    }
}
} // namespace

// No parameters: runs only when explicitly authorized through the public check CLI.
int main()
{
    std::printf("{\"samples\":[");
    check_channel();
    check_sleep_and_pause();
    check_follow_and_layout();
    check_rejection_and_sizes();
    check_dirty_and_raster();
    check_petting();
    std::printf("],\"assertions\":%d}\n", assertions);
    return failures == 0 ? 0 : 1;
}
