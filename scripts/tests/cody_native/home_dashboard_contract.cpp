#include "CODY_home_dashboard.hpp"
#include "CODY_command_hints.hpp"
#include "CODY_activity_description.hpp"
#include "CODY_model_cost_notice.hpp"
#include "LUNASHELL_audio_sequence.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
// condition: Observable production renderer or canonical projection invariant.
// message: Precise failed behavior, not a reproduction of implementation steps.
// Exception: Throws on failure so an optimized host check cannot silently continue.
void dashboard_require(bool condition, const char *message)
{
    if (!condition) { throw std::runtime_error(message); }
}

// No parameters: real bounded policy must distinguish a model change from repaint or source recovery.
// Exception: Throws when one model selection can notify twice or unavailable metadata creates a cue.
void verify_model_cost_notice()
{
    CODY::ModelCostNoticeState state{};
    CODY::PanelInputs inputs{};
    inputs.model = "expensive-a";
    inputs.model_state = CODY::PanelValueState::Current;
    bool notify = true;
    dashboard_require(CODY::advance_model_cost_notice(inputs, true, state, notify) == ESP_OK && !notify &&
        !state.classified, "unknown catalog price must wait rather than consume or invent a notice");
    inputs.cost_state = CODY::PanelValueState::Current;
    inputs.cost_band = LUNASHELL::CodexCostBand::Expensive;
    dashboard_require(CODY::advance_model_cost_notice(inputs, false, state, notify) == ESP_OK && !notify &&
        state.pending, "expensive model change waits for a completely visible price panel");
    inputs.model_state = CODY::PanelValueState::Stale;
    dashboard_require(CODY::advance_model_cost_notice(inputs, true, state, notify) == ESP_OK && !notify &&
        state.pending, "temporary stale or disconnected source cannot admit the pending sound");
    inputs.model_state = CODY::PanelValueState::Current;
    dashboard_require(CODY::advance_model_cost_notice(inputs, true, state, notify) == ESP_OK && notify &&
        !state.pending, "first current visible price claims exactly one complete five-plin event");
    for (bool visible : {false, true, true, false, true}) {
        dashboard_require(CODY::advance_model_cost_notice(inputs, visible, state, notify) == ESP_OK && !notify,
            "carousel, taps, pause and repeated frames never rearm the same selected model");
    }
    inputs.cost_state = CODY::PanelValueState::Stale;
    dashboard_require(CODY::advance_model_cost_notice(inputs, true, state, notify) == ESP_OK && !notify,
        "cost lease loss does not rearm a consumed model notice");
    inputs.cost_state = CODY::PanelValueState::Current;
    inputs.model_state = CODY::PanelValueState::Invalid;
    dashboard_require(CODY::advance_model_cost_notice(inputs, true, state, notify) == ESP_OK && !notify,
        "canonical owner errors suppress model sound admission");
    inputs.model_state = CODY::PanelValueState::Current;
    dashboard_require(CODY::advance_model_cost_notice(inputs, true, state, notify) == ESP_OK && !notify,
        "reconnect and refreshed same-model metadata do not replay the attempted notice");
    inputs.model = "expensive-b";
    dashboard_require(CODY::advance_model_cost_notice(inputs, false, state, notify) == ESP_OK && !notify &&
        state.pending, "another expensive model creates its own pending notice");
    inputs.model = "cheap-c";
    inputs.cost_state = CODY::PanelValueState::Unavailable;
    dashboard_require(CODY::advance_model_cost_notice(inputs, true, state, notify) == ESP_OK && !notify &&
        !state.pending && !state.classified, "new model identity cancels obsolete pending price before classification");
    inputs.cost_state = CODY::PanelValueState::Current;
    inputs.cost_band = LUNASHELL::CodexCostBand::Economy;
    dashboard_require(CODY::advance_model_cost_notice(inputs, true, state, notify) == ESP_OK && !notify &&
        state.classified, "a cheap model selection has no costly-model notice");
    inputs.cost_band = LUNASHELL::CodexCostBand::Expensive;
    dashboard_require(CODY::advance_model_cost_notice(inputs, true, state, notify) == ESP_OK && !notify,
        "a same-model price refresh is not a model-selection change");
    inputs.model = "expensive-a";
    dashboard_require(CODY::advance_model_cost_notice(inputs, true, state, notify) == ESP_OK && notify,
        "returning A after a genuine A-to-B-to-C change is one new expensive-model selection");
    inputs.model = "bad\nmodel";
    dashboard_require(CODY::advance_model_cost_notice(inputs, true, state, notify) == ESP_ERR_INVALID_ARG && !notify &&
        std::strcmp(state.model, "expensive-a") == 0, "invalid current model is rejected without destroying deduplication");
}

// No parameters: exercise the exact transport timeline used by the existing audio owner.
// Exception: Throws on missing/extra plins, trailing silence, unbounded duration or changed ordinary cues.
void verify_audio_notice_sequence()
{
    for (const uint32_t rate : {8000U, 16000U, 22050U, 48000U}) {
        LUNASHELL::AudioCueSequenceLayout sequence{};
        dashboard_require(LUNASHELL::prepare_audio_cue_sequence(3200, 16000, rate, 5, 100, sequence) &&
            sequence.total_samples * 1000 == rate * 1400,
            "five 200-ms plins and four 100-ms gaps retain the current duplex transport clock");
        unsigned starts = 0;
        unsigned gaps = 0;
        bool was_gap = false;
        for (size_t offset = 0; offset < sequence.total_samples; ++offset) {
            const size_t local = LUNASHELL::audio_cue_sequence_clip_offset(sequence, offset);
            const bool gap = local == SIZE_MAX;
            starts += local == 0;
            gaps += gap && !was_gap;
            dashboard_require(gap || local < sequence.clip_samples,
                "every audible sample belongs to the one immutable chime instead of another request");
            was_gap = gap;
        }
        dashboard_require(starts == 5 && gaps == 4 && !was_gap,
            "one admitted sequence contains exactly five complete plins with no trailing gap");
        dashboard_require(LUNASHELL::prepare_audio_cue_sequence(3200, 16000, rate, 1, 0, sequence) &&
            sequence.total_samples == sequence.clip_samples && sequence.gap_samples == 0,
            "ordinary personality and Codex cues preserve their original single-clip duration");
    }
    LUNASHELL::AudioCueSequenceLayout sequence{};
    dashboard_require(!LUNASHELL::prepare_audio_cue_sequence(5601, 16000, 16000, 5, 100, sequence) &&
        !LUNASHELL::prepare_audio_cue_sequence(3200, 16000, 16000, 0, 100, sequence) &&
        !LUNASHELL::prepare_audio_cue_sequence(3200, 16000, 16000, 6, 100, sequence) &&
        !LUNASHELL::prepare_audio_cue_sequence(3200, 16000, 16000, 5, 201, sequence) &&
        !LUNASHELL::prepare_audio_cue_sequence(3200, 16000, 16000, 1, 100, sequence) &&
        !LUNASHELL::prepare_audio_cue_sequence(5600, 16000, 16000, 5, 200, sequence) &&
        !LUNASHELL::prepare_audio_cue_sequence(3200, 22050, 16000, 5, 100, sequence) &&
        sequence.total_samples == 0,
        "malformed or overlong sequences fail atomically within the fixed audio-owner bounds");
}

// owner: Actual native presentation, whose borrowed text remains alive.
// text: Exact authored expected caption or numeric value.
bool dashboard_has_caption(const CODY::HomeDashboardPresentation &owner, const char *text)
{
    if (owner.panel == CODY::CompanionPanel::ChatActivity && std::strcmp(owner.chat_title.text, text) == 0) {
        return true;
    }
    for (unsigned row = 0; row < owner.line_count; ++row) {
        if (std::strcmp(owner.captions[row], text) == 0) { return true; }
    }
    for (uint8_t index = 0; index < owner.marquee_count; ++index) {
        if (std::strcmp(owner.detail_marquees[index].text, text) == 0) { return true; }
    }
    return false;
}

// link: Current canonical source fixture, borrowed without changing its source timestamps.
// technical: Current canonical counters, borrowed without substituting motion metadata.
// value_font: Actual generated numeric glyph catalog.
// body_font: Actual body font used by both motion presentations.
// small_font: Actual generated secondary dashboard glyph catalog.
// Exception: Throws when Still is fabricated, stale, or overwritten by an independent HAR failure.
void verify_dashboard_stillness(const LUNASHELL::CodexLinkSnapshot &link,
    const LUNASHELL::CodexTechnicalSnapshot &technical, const CODY2D::MonoFont &value_font,
    const CODY2D::MonoFont &body_font, const CODY2D::MonoFont &small_font)
{
    constexpr int64_t now_us = 3000000;
    static CODY::HomeDashboardPresentation owner{};
    static CODY::PanelPresentation technical_owner{};
    LUNASHELL::ActivitySnapshot motion{};
    CODY::SceneContent content{};
    motion.rejection = LUNASHELL::ActivityRejection::CadenceGap;
    motion.error = ESP_ERR_INVALID_STATE;
    motion.stillness.state = LUNASHELL::ImuStillnessState::Still;
    motion.stillness.data_valid = true;
    motion.stillness.error = ESP_OK;
    motion.stillness.sampled_at_us = now_us;
    motion.stillness.attempted_at_us = now_us;
    motion.stillness.still_since_us = 1500000;
    motion.stillness.sample_count = 16;
    // home_text: Semantic motion caption expected in the general device panel's ID/status row.
    // technical_text: Exact motion caption expected from the canonical technical wrapper.
    // current: Whether a healthy measurement should receive the technical panel's current color.
    // Exception: Throws on real native preparation, caption or health-color disagreement.
    const auto expect = [&](const char *home_text, const char *technical_text, bool current) {
        dashboard_require(CODY::prepare_home_dashboard(CODY::CompanionPanel::SessionLink, link, technical,
            value_font, body_font, now_us, 5000000, owner, content, &motion, &small_font) == ESP_OK,
            "Still fixture must use the actual general-device panel with an original observation clock");
        bool found_home = false;
        for (uint8_t row = 0; row < owner.marquee_count; ++row) {
            found_home |= std::strstr(owner.detail_marquees[row].text, home_text + 4) != nullptr;
        }
        dashboard_require(found_home, "Home must show the admitted measured state or preserve the distinct HAR fallback");
        dashboard_require(CODY::prepare_companion_panel_from_observation(CODY::CompanionPanel::Activity,
            link, technical, body_font, now_us, 5000000, technical_owner, content, "", &motion) == ESP_OK,
            "technical motion must receive the same original clock through its canonical projection");
        bool found_technical = false;
        for (uint8_t row = 0; row < technical_owner.line_count; ++row) {
            if (std::strcmp(technical_owner.captions[row], technical_text) == 0) {
                found_technical = true;
                dashboard_require(technical_owner.lines[row].color == (current ? 0x07FF : 0xFD20),
                    "an independent healthy Still observation must retain current health despite HAR errors");
            }
        }
        dashboard_require(found_technical, "technical text must share Still admission without guessing posture");
    };
    expect("IMU Still", "IMU: Still", true);
    dashboard_require(motion.rejection == LUNASHELL::ActivityRejection::CadenceGap &&
        motion.error == ESP_ERR_INVALID_STATE && !motion.scores_valid,
        "presenting healthy Still must not clear or fabricate the original HAR result");
    motion.rejection = LUNASHELL::ActivityRejection::ModelError;
    motion.error = ESP_FAIL;
    expect("IMU Still", "IMU: Still", true);
    motion.rejection = LUNASHELL::ActivityRejection::CadenceGap;
    motion.stillness.sampled_at_us = now_us - 200000;
    expect("IMU Still", "IMU: Still", true);
    for (const int64_t sampled_at_us : {now_us - 200001, now_us + 1, int64_t{0}}) {
        motion.stillness.sampled_at_us = sampled_at_us;
        expect("IMU CADENCE INVALID", "IMU: CADENCE GAP", false);
    }
    motion.stillness.sampled_at_us = now_us;
    motion.stillness.data_valid = false;
    expect("IMU CADENCE INVALID", "IMU: CADENCE GAP", false);
    motion.stillness.data_valid = true;
    motion.stillness.error = ESP_FAIL;
    expect("IMU CADENCE INVALID", "IMU: CADENCE GAP", false);
    motion.stillness.error = ESP_OK;
    for (const auto state : {LUNASHELL::ImuStillnessState::Unavailable,
        LUNASHELL::ImuStillnessState::Collecting, LUNASHELL::ImuStillnessState::Moving}) {
        motion.stillness.state = state;
        expect("IMU CADENCE INVALID", "IMU: CADENCE GAP", false);
    }
    motion.rejection = LUNASHELL::ActivityRejection::None;
    motion.scores_valid = true;
    motion.error = ESP_OK;
    motion.window_end_us = now_us;
    motion.activity = LUNASHELL::ActivityClass::Walking;
    expect("IMU Walking", "IMU: walking", true);
    motion.stillness.state = LUNASHELL::ImuStillnessState::Still;
    motion.stillness.sampled_at_us = now_us - 200001;
    expect("IMU Walking", "IMU: walking", true);
    motion.activity = LUNASHELL::ActivityClass::Standing;
    expect("IMU Standing", "IMU: standing", true);
    motion.activity = LUNASHELL::ActivityClass::Sitting;
    expect("IMU Sitting", "IMU: sitting", true);
}

// state: Counter owned by the host fixture throughout the actual native draw.
// strip: Production compositor tile, unused by this callback invocation probe.
// first_row: Logical tile y origin, unused by this callback invocation probe.
// first_column: Logical tile x origin, unused by this callback invocation probe.
esp_err_t dashboard_count_legacy_draws(const void *state, CODY2D::Surface, uint16_t, uint16_t)
{
    ++*const_cast<unsigned *>(static_cast<const unsigned *>(state));
    return ESP_OK;
}

// body_font: Actual immutable native body glyph catalog for semantic legacy owners.
// small_font: Actual immutable dashboard small font used to exercise title typography changes.
// Exception: Throws when production composition or dirty planning violates retained-owner visibility.
void verify_dashboard_scene_visibility(const CODY2D::MonoFont &body_font, const CODY2D::MonoFont &small_font)
{
    const CODY::SceneDefinition *definition = nullptr;
    CODY::SceneFrame scene{};
    dashboard_require(CODY::find_scene(CODY::SceneId::Working, definition) == ESP_OK &&
        CODY::begin_scene(*definition, scene) == ESP_OK && CODY::advance_scene(scene, 30000000) == ESP_OK,
        "dashboard visibility fixture uses the actual completed native Working scene");
    static CODY::ActivityIdentityState identity{};
    static CODY::ActivityDescriptionState description{};
    static CODY::CommandHintsState hints{};
    bool changed = false;
    dashboard_require(CODY::prepare_activity_identity(identity, body_font, "Cody", "main", 0) == ESP_OK &&
        CODY::prepare_activity_description(description, body_font, "Actual retained purpose", 0) == ESP_OK &&
        CODY::update_command_hints(hints, body_font, true, true, 0, changed) == ESP_OK &&
        CODY::update_command_hints(hints, body_font, true, true, 6000000, changed) == ESP_OK,
        "semantic owners and marquee must be valid real native presentations");
    CODY::capture_scene_activity_identity(scene, &identity, 1);
    scene.activity_description = &description;
    scene.activity_description_revision = 1;
    scene.command_hints = &hints;
    scene.command_hints_revision = hints.revision;
    unsigned callback_count = 0;
    CODY::SceneContent content{};
    content.bounds = {73, 330, 393, 358};
    content.state = &callback_count;
    content.draw_rows = dashboard_count_legacy_draws;
    dashboard_require(CODY::set_scene_content(scene, content) == ESP_OK,
        "legacy content callback is a valid existing scene input");
    const auto legacy = scene;
    scene.home_dashboard = true;
    std::vector<uint16_t> pixels(466U * 466U);
    const CODY2D::Surface surface{pixels.data(), 466, 466, 466, static_cast<uint32_t>(pixels.size())};
    dashboard_require(CODY::draw_scene_rows(scene, surface, 0, 0) == ESP_OK && callback_count == 0 &&
        scene.activity_identity == &identity && scene.activity_description == &description,
        "dashboard hides legacy pixels while retaining semantic owners for audio and priority");
    bool ticker_visible = false;
    for (unsigned y = 408; y < 433; ++y) {
        for (unsigned x = 113; x < 353; ++x) { ticker_visible = ticker_visible || pixels[y * 466 + x] != 0; }
    }
    dashboard_require(ticker_visible, "dashboard keeps the real right-to-left command layer visible");
    for (unsigned y = 300; y < 388; ++y) {
        for (unsigned x = 73; x < 393; ++x) {
            dashboard_require(pixels[y * 466 + x] == 0, "dashboard scene layer does not leak legacy activity rows");
        }
    }
    CODY::DirtyPlan damage{};
    dashboard_require(CODY::plan_scene_damage(legacy, scene, damage) == ESP_OK &&
        damage.row_pairs[150].right > damage.row_pairs[150].left &&
        damage.row_pairs[180].right > damage.row_pairs[180].left,
        "dashboard admission removes the previously displayed identity and purpose regions");
    auto hidden_update = scene;
    ++hidden_update.activity_identity_revision;
    ++hidden_update.activity_description_revision;
    ++hidden_update.content.revision;
    hidden_update.activity_overlay.elapsed_us += 50000;
    damage = {};
    dashboard_require(CODY::plan_scene_damage(scene, hidden_update, damage) == ESP_OK,
        "hidden legacy source updates remain valid semantic changes");
    for (const auto &row : damage.row_pairs) {
        dashboard_require(row.left == row.right, "hidden semantic updates cannot redraw legacy Home rows");
    }
    const auto original_title = hidden_update.title;
    dashboard_require(CODY2D::prepare_text_line(small_font, original_title.text, {73, 190},
        original_title.color, hidden_update.title) == ESP_OK, "dashboard font relocation uses real native text preparation");
    damage = {};
    dashboard_require(CODY::plan_scene_damage(scene, hidden_update, damage) == ESP_OK &&
        damage.row_pairs[95].right > damage.row_pairs[95].left &&
        damage.row_pairs[original_title.bounds.y / 2].right > damage.row_pairs[original_title.bounds.y / 2].left,
        "changing only title font and position invalidates both previous and new extents");
    std::fill(pixels.begin(), pixels.end(), 0);
    dashboard_require(CODY::draw_scene_rows(legacy, surface, 0, 0) == ESP_OK && callback_count == 1,
        "leaving dashboard restores the existing native content callback");
}

// No parameters: deterministic presentation clocks verify the shared firmware/Studio rotation policy.
// Exception: Throws when exact dwell boundaries, manual input, visibility or compaction priority regress.
void verify_dashboard_rotation()
{
    CODY::HomeDashboardRotationState rotation{};
    auto panel = CODY::CompanionPanel::Home;
    // now_us: Explicit monotonic presentation time, independent of any source observation timestamp.
    // visible: Whether the lower Home panel can be seen during this policy sample.
    // compacting: Valid current compaction supplied by the existing canonical projection.
    // Exception: Propagates an unexpected production policy rejection as a host contract failure.
    const auto advance = [&](int64_t now_us, bool visible = true, bool compacting = false) {
        dashboard_require(CODY::advance_home_dashboard_rotation(rotation, panel, now_us,
            visible, compacting, panel) == ESP_OK, "shared Home rotation policy rejected a valid active clock");
    };
    advance(0);
    advance(7999999);
    dashboard_require(panel == CODY::CompanionPanel::Home, "first automatic page holds eight complete visible seconds");
    advance(8000000);
    dashboard_require(panel == CODY::CompanionPanel::ModelContext, "eight-second boundary exposes model details without input");
    advance(16000000);
    dashboard_require(panel == CODY::CompanionPanel::SessionLink, "automatic cycle next exposes the real connection panel");
    advance(24000000);
    dashboard_require(panel == CODY::CompanionPanel::ChatActivity, "automatic cycle gives the real chat its own fourth panel");
    advance(32000000);
    dashboard_require(panel == CODY::CompanionPanel::Home, "automatic cycle returns to the token arc");
    advance(40000000);
    dashboard_require(panel == CODY::CompanionPanel::ModelContext, "the next complete four-panel cycle starts with model details");
    CODY::reset_home_dashboard_rotation(rotation);
    advance(40000000);
    advance(47999999);
    dashboard_require(panel == CODY::CompanionPanel::ModelContext, "manual tap or shake grants a fresh full dwell");
    advance(48000000);
    dashboard_require(panel == CODY::CompanionPanel::SessionLink, "automatic rotation resumes after the manual selection dwell");
    advance(50000000);
    advance(60000000, false);
    advance(200000000);
    advance(205999999);
    dashboard_require(panel == CODY::CompanionPanel::SessionLink, "hidden routes preserve the six visible seconds still owed to this page");
    advance(206000000);
    dashboard_require(panel == CODY::CompanionPanel::ChatActivity, "rotation resumes using only the remaining visible dwell");
    panel = CODY::CompanionPanel::SessionLink;
    advance(207000000, true, true);
    dashboard_require(panel == CODY::CompanionPanel::Home, "current compaction immediately brings its orange token arc into view");
    advance(300000000, true, true);
    advance(300000001);
    advance(308000000);
    dashboard_require(panel == CODY::CompanionPanel::Home, "compaction suspends rotation and then grants eight new visible seconds");
    advance(308000001);
    dashboard_require(panel == CODY::CompanionPanel::ModelContext, "rotation resumes after the post-compaction Home dwell");
    const auto previous = rotation;
    dashboard_require(CODY::advance_home_dashboard_rotation(rotation, panel, 308000000, true, false, panel) == ESP_ERR_INVALID_ARG &&
        rotation.sampled_us == previous.sampled_us && rotation.elapsed_us == previous.elapsed_us,
        "a backward clock is rejected without corrupting the retained rotation policy");
    advance(324000001);
    dashboard_require(panel == CODY::CompanionPanel::SessionLink, "a delayed frame advances once instead of skipping an unseen panel");
}

// owner: Actual prepared native presentation.
// pixels: Host-owned output image, never used as a synthetic firmware observation.
// rows: Tile height, including a final partial strip.
// order: Explicit native or swapped RGB565 storage representation.
// Exception: Propagates a failed production draw as a contract failure.
void dashboard_raster(const CODY::HomeDashboardPresentation &owner, std::vector<uint16_t> &pixels,
    uint16_t rows, CODY2D::Rgb565ByteOrder order)
{
    pixels.assign(466U * 466U, 0);
    for (uint16_t first = 0; first < 466; first += rows) {
        const uint16_t height = std::min<uint16_t>(rows, 466 - first);
        CODY2D::Surface surface{pixels.data() + first * 466U, 466, height, 466,
            static_cast<uint32_t>(height) * 466U, order};
        dashboard_require(CODY::draw_home_dashboard_rows(owner, surface, first) == ESP_OK,
            "dashboard actual native strip rendering failed");
    }
}
} // namespace

// value_font: Actual generated native numeric glyph assets used by the firmware.
// body_font: Actual native body font; no Python or test-only text renderer.
// small_font: Actual generated native small glyph assets used by the firmware.
// Exception: Throws on failed semantic, source-owner, geometry or raster parity assertions.
void verify_home_dashboard_contract(const CODY2D::MonoFont &value_font,
    const CODY2D::MonoFont &body_font, const CODY2D::MonoFont &small_font)
{
    static LUNASHELL::CodexLinkSnapshot link{};
    static LUNASHELL::CodexTechnicalSnapshot technical{};
    static LUNASHELL::ActivitySnapshot motion{};
    static CODY::HomeDashboardPresentation owner{};
    static LUNASHELL::FirmwareBuildMetadata build{};
    CODY::HomeDeviceDetails device_details{&build};
    CODY::SceneContent content{};
    link.data_valid = link.activity.data_valid = technical.data_valid = true;
    link.last_error = link.activity.last_error = technical.last_error = ESP_OK;
    link.state = LUNASHELL::CodexLinkState::Connected;
    link.health = link.activity.health = technical.health = LUNASHELL::DeviceHealth::Ready;
    link.activity.state = LUNASHELL::CodexActivityState::Working;
    link.activity.revision = technical.revision = 7;
    std::strcpy(link.device_id, "010203040506");
    std::strcpy(build.local_date_time, "14/10/26 12:34");
    std::strcpy(build.time_zone, "SIMULATED/Asia/Long/Build/Time/Zone/Identity");
    std::strcpy(build.computer, "SIMULATED-build-machine-with-a-complete-original-hostname");
    build.clock_state = build.computer_state = LUNASHELL::FirmwareBuildFieldState::Available;
    std::memset(link.activity.chat_id, 'a', 64);
    std::strcpy(link.activity.title, "SIMULATED title");
    link.activity.title_state = LUNASHELL::CodexChatTitleState::Current;
    link.activity.title_error = ESP_OK;
    link.activity.last_attempt_at_us = 1000000;
    std::memcpy(technical.chat_id, link.activity.chat_id, 65);
    std::strcpy(link.peer_ip_address, "192.0.2.99");
    technical.context_valid = technical.used_valid = technical.capacity_valid = technical.cumulative_valid = true;
    technical.used = 155000;
    technical.capacity = 474000;
    technical.cumulative = 600000;
    technical.received_at_us = 1000000;
    technical.turn_elapsed_valid = true;
    technical.turn_elapsed_at_receive_us = 12000000;
    motion.activity = LUNASHELL::ActivityClass::Standing;
    motion.rejection = LUNASHELL::ActivityRejection::None;
    motion.scores_valid = true;
    motion.error = ESP_OK;
    motion.window_end_us = 1000000;
    // panel: Actual lower dashboard identity.
    // active_us: Independent presentation time, not a receipt timestamp.
    // Exception: Fails on the real native preparation error.
    const auto prepare = [&](CODY::CompanionPanel panel, int64_t active_us) {
        dashboard_require(CODY::prepare_home_dashboard(panel, link, technical, value_font, body_font,
            1000000, active_us, owner, content, &motion, &small_font, "192.0.2.17", CODY::PanelValueState::Current,
            -1, &device_details) == ESP_OK,
            "real dashboard preparation failed");
    };
    prepare(CODY::CompanionPanel::Home, 0);
    dashboard_require(owner.arc_state == CODY::HomeDashboardArcState::Measured &&
        dashboard_has_caption(owner, "319K") && owner.line_count == 4 && owner.marquee_count == 0 &&
        (dashboard_has_caption(owner, "Last 155K | Window 474K") || dashboard_has_caption(owner, "Last 155K | Win 474K")),
        "token panel contains only genuine token values and consumed-context progress, with no timer or IMU row");
    const uint32_t revision = owner.revision;
    prepare(CODY::CompanionPanel::Home, 9000000);
    dashboard_require(owner.revision == revision && technical.received_at_us == 1000000 &&
        owner.line_count == 4,
        "independent animation clock must not alter unchanged token-panel revisions");
    verify_dashboard_stillness(link, technical, value_font, body_font, small_font);
    std::vector<uint16_t> reference;
    std::vector<uint16_t> actual;
    dashboard_raster(owner, reference, 466, CODY2D::Rgb565ByteOrder::Native);
    unsigned painted = 0;
    unsigned arc_antialias = 0;
    for (unsigned y = 0; y < 466; ++y) {
        for (unsigned x = 0; x < 466; ++x) {
            const auto pixel = reference[y * 466 + x];
            if (pixel == 0) { continue; }
            ++painted;
            const int dx = static_cast<int>(x) - 233;
            const int dy = static_cast<int>(y) - 233;
            dashboard_require(x >= 40 && x < 426 && y >= 268 && y < 393 && dx * dx + dy * dy <= 233 * 233,
                "dashboard must paint only its declared lower region and actual circular chord");
            if (y >= 376 && pixel != 0x2125 && pixel != 0x5FF9) { ++arc_antialias; }
        }
    }
    dashboard_require(painted > 1000 && arc_antialias > 10,
        "real dashboard must contain visible numeric glyphs and antialiased vector arc edges");
    for (uint16_t rows : {2, 16, 466}) {
        for (auto order : {CODY2D::Rgb565ByteOrder::Native, CODY2D::Rgb565ByteOrder::ByteSwapped}) {
            dashboard_raster(owner, actual, rows, order);
            for (size_t index = 0; index < reference.size(); ++index) {
                dashboard_require(CODY2D::rgb565_stored_to_native(actual[index], order) == reference[index],
                    "dashboard raster must be identical across tile heights and RGB565 byte orders");
            }
        }
    }
    struct OccupancyCase {
        uint64_t used;
        uint64_t capacity;
        uint16_t color;
        uint8_t slot;
        uint16_t units;
    };
    constexpr uint64_t maximum_count = std::numeric_limits<uint64_t>::max();
    constexpr OccupancyCase occupancy_cases[] = {
        {0, 100, 0xFFFF, 3, 0}, {24, 100, 0xFFFF, 3, 480},
        {25, 100, 0x5FF9, 1, 500}, {64, 100, 0x5FF9, 1, 1280},
        {65, 100, 0xFD20, 2, 1300}, {84, 100, 0xFD20, 2, 1680},
        {85, 100, 0xF986, 4, 1700}, {99, 100, 0xF986, 4, 1980},
        {100, 100, 0xF986, 4, 2000},
        {0, 3, 0xFFFF, 3, 0}, {1, 3, 0x5FF9, 1, 666}, {2, 3, 0xFD20, 2, 1333},
        {1, maximum_count, 0xFFFF, 3, 0},
        {4611686018427387903ULL, maximum_count, 0xFFFF, 3, 499},
        {4611686018427387904ULL, maximum_count, 0x5FF9, 1, 500},
        {11990383647911208549ULL, maximum_count, 0x5FF9, 1, 1299},
        {11990383647911208550ULL, maximum_count, 0xFD20, 2, 1300},
        {15679732462653118872ULL, maximum_count, 0xFD20, 2, 1699},
        {15679732462653118873ULL, maximum_count, 0xF986, 4, 1700},
        {maximum_count - 1, maximum_count, 0xF986, 4, 1999},
    };
    for (const auto &sample : occupancy_cases) {
        technical.used = sample.used;
        technical.capacity = sample.capacity;
        prepare(CODY::CompanionPanel::Home, 0);
        dashboard_require(owner.arc_state == CODY::HomeDashboardArcState::Measured && owner.arc_units == sample.units &&
            owner.arc_path_count == (sample.units == 0 ? 1 : 2) && owner.lines[1].color == sample.color &&
            (sample.units == 0 || owner.arc_paths[1].color_slot == sample.slot),
            "arc length follows consumed context and shares exact occupancy colors with Free at full-width count boundaries");
        dashboard_raster(owner, actual, 16, CODY2D::Rgb565ByteOrder::Native);
        unsigned accent_pixels = 0;
        unsigned track_pixels = 0;
        for (unsigned y = 290; y < 331; ++y) {
            for (unsigned x = 40; x < 73; ++x) {
                accent_pixels += actual[y * 466U + x] == sample.color;
                track_pixels += actual[y * 466U + x] == 0x2125;
            }
        }
        dashboard_require(sample.units == 0 ? accent_pixels == 0 && track_pixels > 5 : accent_pixels > 5,
            "zero consumption paints only the track; nonzero occupancy paints its actual opaque palette color");
    }
    unsigned previous_accent_pixels = 0;
    for (const uint64_t percent : {0U, 20U, 40U, 70U, 90U, 100U}) {
        technical.used = percent;
        technical.capacity = 100;
        prepare(CODY::CompanionPanel::Home, 0);
        dashboard_raster(owner, actual, 16, CODY2D::Rgb565ByteOrder::Native);
        const uint16_t color = owner.lines[1].color;
        unsigned accent_pixels = 0;
        for (unsigned y = 290; y < 393; ++y) {
            for (unsigned x = 40; x < 426; ++x) {
                // This mask contains only the arc, outside all actual numeric and timer glyphs.
                if (y >= 376 || x < 73 || x >= 393) {
                    accent_pixels += actual[y * 466U + x] == color;
                }
            }
        }
        dashboard_require(owner.arc_units == percent * 20 &&
            (percent == 0 ? accent_pixels == 0 : accent_pixels > previous_accent_pixels),
            "SIMULATED 0/20/40/70/90/100 percent must produce visibly growing native occupancy fill");
        previous_accent_pixels = accent_pixels;
    }
    technical.capacity = 474000;
    technical.used = 0;
    prepare(CODY::CompanionPanel::Home, 0);
    dashboard_require(owner.arc_units == 0 && owner.arc_path_count == 1,
        "measured zero consumption leaves an empty progress track while Free retains the full capacity");
    technical.used = technical.capacity + 1;
    prepare(CODY::CompanionPanel::Home, 0);
    dashboard_require(owner.arc_state == CODY::HomeDashboardArcState::OverLimit &&
        dashboard_has_caption(owner, "Free OVERLIMIT") && owner.arc_path_count == 1,
        "overlimit cannot underflow free space or draw invented progress");
    technical.used = technical.capacity = std::numeric_limits<uint64_t>::max();
    prepare(CODY::CompanionPanel::Home, 0);
    dashboard_require(dashboard_has_caption(owner, "0") && owner.arc_units == 2000 &&
        owner.arc_path_count == 2 && owner.arc_paths[1].color_slot == 4 && owner.lines[1].color == 0xF986,
        "full-width canonical values must subtract before compact formatting without overflow");
    technical.used = 0;
    prepare(CODY::CompanionPanel::Home, 0);
    dashboard_require(dashboard_has_caption(owner, "18.446.744.073.709.551K TT") && owner.arc_units == 0,
        "largest genuine free count retains every compact digit using the bounded stacked native layout");
    technical.context_stale = true;
    prepare(CODY::CompanionPanel::Home, 0);
    dashboard_require(owner.arc_state == CODY::HomeDashboardArcState::Stale && owner.arc_path_count == 1 && owner.arc_units == 0 &&
        dashboard_has_caption(owner, "Free STALE"), "expired counters never retain a measured arc");
    technical.context_stale = false;
    technical.data_valid = false;
    prepare(CODY::CompanionPanel::Home, 0);
    dashboard_require(owner.arc_state == CODY::HomeDashboardArcState::Unavailable &&
        owner.arc_path_count == 1 && owner.arc_units == 0 && dashboard_has_caption(owner, "Free UNAVAILABLE"),
        "an unavailable owner cannot reuse its historical valid flags as fabricated current progress");
    technical.data_valid = true;
    technical.used = 155000;
    technical.capacity = 474000;
    technical.compaction_valid = technical.compaction_age_valid = true;
    technical.compaction = LUNASHELL::CodexCompactionState::Active;
    technical.compaction_age_at_receive_us = 2000000;
    prepare(CODY::CompanionPanel::Home, 0);
    const uint32_t compact_revision = owner.revision;
    dashboard_require(owner.arc_state == CODY::HomeDashboardArcState::Compacting &&
        owner.arc_path_count == 2 && owner.arc_paths[1].color_slot == 2 && owner.lines[1].color == 0xFD20,
        "compaction paints a fixed indeterminate accent, never a made-up completion percentage");
    for (uint64_t used : {uint64_t{0}, uint64_t{25}, uint64_t{85}, uint64_t{100}}) {
        technical.used = used;
        technical.capacity = 100;
        prepare(CODY::CompanionPanel::Home, 0);
        dashboard_require(owner.arc_state == CODY::HomeDashboardArcState::Compacting &&
            owner.arc_paths[1].color_slot == 2 && owner.lines[1].color == 0xFD20,
            "active compaction keeps orange priority over white, cyan and red occupancy bands");
    }
    technical.used = 155000;
    technical.capacity = 474000;
    prepare(CODY::CompanionPanel::Home, 100000);
    dashboard_require(owner.revision != compact_revision && owner.animation_step == 1,
        "existing active scene clock advances the real compaction accent");
    technical.last_error = ESP_FAIL;
    prepare(CODY::CompanionPanel::Home, 0);
    dashboard_require(owner.arc_state == CODY::HomeDashboardArcState::Invalid && owner.arc_path_count == 1 && owner.arc_units == 0 &&
        dashboard_has_caption(owner, "Free INVALID"), "canonical owner error retires historical numeric validity");
    technical.last_error = ESP_OK;
    technical.compaction = LUNASHELL::CodexCompactionState::None;
    technical.model_valid = technical.effort_valid = true;
    technical.model_source = LUNASHELL::CodexModelSource::HookMetadata;
    std::strcpy(technical.model, "gpt-6-astra");
    std::strcpy(technical.effort, "high");
    technical.cost_band = LUNASHELL::CodexCostBand::Expensive;
    technical.cost_source = LUNASHELL::CodexCostSource::CodexStandardCatalog;
    technical.cost_valid_for_us = 10000000;
    prepare(CODY::CompanionPanel::ModelContext, 0);
    dashboard_require(owner.price_visible && !dashboard_has_caption(owner, "gpt-6-astra") &&
        dashboard_has_caption(owner, "Price Expensive") && dashboard_has_caption(owner, "Thinking high") && owner.line_count == 2,
        "lower model panel complements the shared identity header with only price and thinking effort");
    prepare(CODY::CompanionPanel::SessionLink, 0);
    dashboard_require(dashboard_has_caption(owner, "PC connected") && !dashboard_has_caption(owner, "SIMULATED title") &&
        dashboard_has_caption(owner, "IP 192.0.2.17") && !dashboard_has_caption(owner, "IP 192.0.2.99") &&
        dashboard_has_caption(owner, "ID 010203040506 | Standing") && dashboard_has_caption(owner, "Built 14/10/26 12:34") &&
        dashboard_has_caption(owner, "Zone SIMULATED/Asia/Long/Build/Time/Zone/Identity") &&
        dashboard_has_caption(owner, "PC SIMULATED-build-machine-with-a-complete-original-hostname"),
        "general panel must retain canonical device IP/ID and motion with explicit immutable build-machine metadata");
    const int64_t metadata_dwell = owner.minimum_dwell_us;
    dashboard_require(metadata_dwell > CODY::kHomeDashboardRotationUs &&
        !CODY::home_dashboard_marquee_moving(owner, 1199999),
        "long build metadata must reserve complete reading time including its stationary start hold");
    prepare(CODY::CompanionPanel::SessionLink, 1250000);
    dashboard_require(owner.detail_marquees[1].offset == 2 && owner.detail_marquees[2].offset == 2 &&
        CODY::home_dashboard_marquee_moving(owner, 1250000),
        "timezone and computer name use the shared 50-ms native movement gate");
    prepare(CODY::CompanionPanel::SessionLink, metadata_dwell - 1800000);
    dashboard_require(owner.minimum_dwell_us == metadata_dwell &&
        owner.detail_marquees[2].offset == owner.detail_marquees[2].width - owner.detail_marquees[2].bounds.width &&
        !CODY::home_dashboard_marquee_moving(owner, metadata_dwell - 1800000),
        "unchanged metadata refreshes preserve their reading deadline and stop animation after complete suffixes");
    dashboard_raster(owner, reference, 466, CODY2D::Rgb565ByteOrder::Native);
    dashboard_raster(owner, actual, 16, CODY2D::Rgb565ByteOrder::ByteSwapped);
    for (unsigned y = 0; y < 466; ++y) {
        for (unsigned x = 0; x < 466; ++x) {
            const auto pixel = reference[y * 466U + x];
            dashboard_require(CODY2D::rgb565_stored_to_native(actual[y * 466U + x], CODY2D::Rgb565ByteOrder::ByteSwapped) == pixel,
                "all six general rows retain full-frame/strip and byte-order native parity");
            if (pixel != 0) {
                dashboard_require(x >= 73 && x < 393 && y >= 268 && y < 393 &&
                    (int(x) - 233) * (int(x) - 233) + (int(y) - 233) * (int(y) - 233) <= 233 * 233,
                    "general text and both metadata marquees stay inside their lower circular viewport");
            }
        }
    }
    build.clock_state = LUNASHELL::FirmwareBuildFieldState::Error;
    build.computer_state = LUNASHELL::FirmwareBuildFieldState::Unsupported;
    prepare(CODY::CompanionPanel::SessionLink, 0);
    dashboard_require(dashboard_has_caption(owner, "Built ERROR") && dashboard_has_caption(owner, "Zone ERROR") &&
        dashboard_has_caption(owner, "PC UNSUPPORTED") && !dashboard_has_caption(owner, "Built 14/10/26 12:34"),
        "failed or unsupported immutable metadata never exposes its historical strings as current");
    build.clock_state = build.computer_state = LUNASHELL::FirmwareBuildFieldState::Available;
    dashboard_require(CODY::prepare_home_dashboard(CODY::CompanionPanel::SessionLink, link, technical, value_font, body_font,
        1000000, 0, owner, content, &motion, &small_font, "255.255.255.255", CODY::PanelValueState::Current) == ESP_OK &&
        dashboard_has_caption(owner, "IP 255.255.255.255") && dashboard_has_caption(owner, "Built UNAVAILABLE"),
        "the widest valid IPv4 address retains every octet and absent build metadata stays explicit");
    prepare(CODY::CompanionPanel::ChatActivity, 0);
    dashboard_require(dashboard_has_caption(owner, "SIMULATED title") && dashboard_has_caption(owner, "Turn 00:12") &&
        dashboard_has_caption(owner, "Working") && !dashboard_has_caption(owner, "PC connected") && owner.arc_path_count == 0,
        "chat owns actual title, reported current-turn timer and phase fallback without device or token fields");
    std::memset(link.activity.description, 'A', 96);
    link.activity.description[96] = '\0';
    prepare(CODY::CompanionPanel::ChatActivity, 0);
    const int64_t activity_dwell = owner.minimum_dwell_us;
    dashboard_require(activity_dwell > CODY::kHomeDashboardRotationUs &&
        dashboard_has_caption(owner, link.activity.description), "genuine long activity retains its full original text");
    for (unsigned update = 1; update <= 5; ++update) {
        link.activity.description[0] = static_cast<char>('A' + update);
        prepare(CODY::CompanionPanel::ChatActivity, update * 3000000);
        dashboard_require(owner.minimum_dwell_us == activity_dwell && owner.detail_marquees[0].entered_elapsed_us == 0 &&
            owner.detail_marquees[0].started_elapsed_us == update * 3000000,
            "tool updates restart their visible text but cannot perpetually extend the chat panel's entry-based deadline");
    }
    CODY::HomeDashboardRotationState activity_rotation{};
    auto activity_panel = CODY::CompanionPanel::ChatActivity;
    dashboard_require(CODY::advance_home_dashboard_rotation(activity_rotation, activity_panel, 0, true, false,
        activity_panel, activity_dwell) == ESP_OK &&
        CODY::advance_home_dashboard_rotation(activity_rotation, activity_panel, activity_dwell, true, false,
            activity_panel, activity_dwell) == ESP_OK && activity_panel == CODY::CompanionPanel::Home,
        "ongoing genuine tool activity still releases the four-panel carousel at its bounded deadline");
    std::strcpy(link.activity.description, "Working A");
    for (unsigned update = 0; update < 8; ++update) {
        link.activity.description[8] = static_cast<char>('A' + update);
        prepare(CODY::CompanionPanel::ChatActivity, update * 1000000);
        dashboard_require(owner.minimum_dwell_us == CODY::kHomeDashboardRotationUs,
            "short genuine activity changes never reset or extend the default eight-second dwell");
    }
    link.activity.last_error = ESP_FAIL;
    prepare(CODY::CompanionPanel::ChatActivity, 7000000);
    dashboard_require(dashboard_has_caption(owner, "Activity INVALID") &&
        !dashboard_has_caption(owner, link.activity.description), "a failed activity owner never presents its old command as current");
    link.activity.last_error = ESP_OK;
    link.activity.description[0] = '\0';
    std::memset(link.activity.title, 'W', 254);
    link.activity.title[254] = 'Z';
    link.activity.title[255] = '\0';
    prepare(CODY::CompanionPanel::ChatActivity, 0);
    const int64_t reading_dwell = owner.minimum_dwell_us;
    dashboard_require(reading_dwell > CODY::kHomeDashboardRotationUs && owner.chat_title.offset == 0 &&
        std::strcmp(owner.chat_title.text, link.activity.title) == 0,
        "a long canonical title retains all 255 characters and extends its panel for a complete reading pass");
    prepare(CODY::CompanionPanel::ChatActivity, 2000000);
    const auto title_offset = owner.chat_title.offset;
    dashboard_require(title_offset > 0, "the existing active clock scrolls the native title after its start hold");
    ++link.activity.revision;
    prepare(CODY::CompanionPanel::ChatActivity, 2000000);
    dashboard_require(owner.chat_title.offset == title_offset && owner.minimum_dwell_us == reading_dwell,
        "same-title source refreshes do not restart scrolling or continually postpone the carousel");
    prepare(CODY::CompanionPanel::ChatActivity, reading_dwell - 1800000);
    dashboard_require(owner.chat_title.offset == owner.chat_title.width - CODY::kHomeChatTitleBounds.width &&
        owner.chat_title.window.text.characters[owner.chat_title.window.text.length - 1U] == 'Z',
        "the final title suffix becomes visible before the retained end hold");
    dashboard_raster(owner, reference, 466, CODY2D::Rgb565ByteOrder::Native);
    dashboard_raster(owner, actual, 16, CODY2D::Rgb565ByteOrder::ByteSwapped);
    for (size_t index = 0; index < reference.size(); ++index) {
        dashboard_require(CODY2D::rgb565_stored_to_native(actual[index], CODY2D::Rgb565ByteOrder::ByteSwapped) == reference[index],
            "the clipped title retains full-frame/tiled and byte-order raster parity");
    }
    for (unsigned y = CODY::kHomeChatTitleBounds.y; y < CODY::kHomeChatTitleBounds.y + CODY::kHomeChatTitleBounds.height; ++y) {
        for (unsigned x = 0; x < 466; ++x) {
            if (x < 73 || x >= 393) {
                dashboard_require(reference[y * 466 + x] == 0, "marquee pixels never escape the measured title viewport");
            }
        }
    }
    CODY::HomeDashboardRotationState reading_rotation{};
    auto reading_panel = CODY::CompanionPanel::ChatActivity;
    dashboard_require(CODY::advance_home_dashboard_rotation(reading_rotation, reading_panel, 0, true, false,
        reading_panel, reading_dwell) == ESP_OK && CODY::advance_home_dashboard_rotation(reading_rotation, reading_panel,
        reading_dwell - 1, true, false, reading_panel, reading_dwell) == ESP_OK &&
        reading_panel == CODY::CompanionPanel::ChatActivity,
        "the carousel cannot hide a long title before its entire measured reading duration");
    dashboard_require(CODY::advance_home_dashboard_rotation(reading_rotation, reading_panel, reading_dwell, true, false,
        reading_panel, reading_dwell) == ESP_OK && reading_panel == CODY::CompanionPanel::Home,
        "the complete title and end hold release the carousel without catch-up transitions");
    link.activity.title_age_at_receive_us = 45000000;
    prepare(CODY::CompanionPanel::ChatActivity, reading_dwell);
    dashboard_require(dashboard_has_caption(owner, "Title stale"), "a current marker cannot extend an expired original title lease");
    link.activity.title_age_at_receive_us = 0;
    link.activity.title_state = LUNASHELL::CodexChatTitleState::Unsupported;
    prepare(CODY::CompanionPanel::ChatActivity, reading_dwell);
    dashboard_require(dashboard_has_caption(owner, "Title unsupported"), "unsupported metadata never falls back to a GUID or prompt");
    CODY::CompanionPanel next = CODY::CompanionPanel::Home;
    for (auto expected : {CODY::CompanionPanel::ModelContext, CODY::CompanionPanel::SessionLink,
        CODY::CompanionPanel::ChatActivity, CODY::CompanionPanel::Home}) {
        dashboard_require(CODY::next_home_dashboard_panel(next, next) == ESP_OK && next == expected,
            "exactly four accessible dashboard identities must cycle back to token Home");
    }
    dashboard_require(CODY::prepare_home_dashboard(CODY::CompanionPanel::Activity, link, technical, value_font, body_font,
        1000000, 0, owner, content, &motion, &small_font) == ESP_ERR_INVALID_ARG,
        "legacy Activity identity must not create a fifth dashboard page");
    verify_dashboard_scene_visibility(body_font, small_font);
    verify_dashboard_rotation();
    verify_model_cost_notice();
    verify_audio_notice_sequence();
    std::fputs("Native Home dashboard: canonical metrics, four panels, chord bounds and full/strip/swapped raster parity passed\n", stderr);
}
