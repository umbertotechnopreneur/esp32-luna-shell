#include "CODY_home_dashboard.hpp"
#include "CODY_command_hints.hpp"
#include "CODY_activity_description.hpp"
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

// owner: Actual native presentation, whose borrowed text remains alive.
// text: Exact authored expected caption or numeric value.
bool dashboard_has_caption(const CODY::HomeDashboardPresentation &owner, const char *text)
{
    for (unsigned row = 0; row < owner.line_count; ++row) {
        if (std::strcmp(owner.captions[row], text) == 0) { return true; }
    }
    return false;
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
        CODY::update_command_hints(hints, body_font, true, true, "dashboard-test-chat", 0, changed) == ESP_OK,
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
    CODY::SceneContent content{};
    link.data_valid = link.activity.data_valid = technical.data_valid = true;
    link.last_error = link.activity.last_error = technical.last_error = ESP_OK;
    link.state = LUNASHELL::CodexLinkState::Connected;
    link.health = link.activity.health = technical.health = LUNASHELL::DeviceHealth::Ready;
    link.activity.state = LUNASHELL::CodexActivityState::Working;
    link.activity.revision = technical.revision = 7;
    std::memset(link.activity.chat_id, 'a', 64);
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
            1000000, active_us, owner, content, &motion, &small_font, "192.0.2.17", CODY::PanelValueState::Current) == ESP_OK,
            "real dashboard preparation failed");
    };
    prepare(CODY::CompanionPanel::Home, 0);
    dashboard_require(owner.arc_state == CODY::HomeDashboardArcState::Measured &&
        dashboard_has_caption(owner, "319K") && dashboard_has_caption(owner, "00:12 | IMU Standing") &&
        (dashboard_has_caption(owner, "Last 155K | Window 474K") || dashboard_has_caption(owner, "Last 155K | Win 474K")),
        "default token panel must show genuine free count and canonical standing beside the timer");
    const uint32_t revision = owner.revision;
    prepare(CODY::CompanionPanel::Home, 9000000);
    dashboard_require(owner.revision == revision && technical.received_at_us == 1000000 &&
        dashboard_has_caption(owner, "00:12 | IMU Standing"),
        "independent animation clock must not age a source timer or amplify unchanged presentation revisions");
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
    technical.used = 0;
    prepare(CODY::CompanionPanel::Home, 0);
    dashboard_require(owner.arc_units == 2000, "measured zero consumption leaves the full truthful free arc");
    technical.used = technical.capacity + 1;
    prepare(CODY::CompanionPanel::Home, 0);
    dashboard_require(owner.arc_state == CODY::HomeDashboardArcState::OverLimit &&
        dashboard_has_caption(owner, "Free OVERLIMIT") && owner.arc_path_count == 1,
        "overlimit cannot underflow free space or draw invented progress");
    technical.used = technical.capacity = std::numeric_limits<uint64_t>::max();
    prepare(CODY::CompanionPanel::Home, 0);
    dashboard_require(dashboard_has_caption(owner, "0") && owner.arc_units == 0,
        "full-width canonical values must subtract before compact formatting without overflow");
    technical.used = 0;
    prepare(CODY::CompanionPanel::Home, 0);
    dashboard_require(dashboard_has_caption(owner, "18.446.744.073.709.551K TT") && owner.arc_units == 2000,
        "largest genuine free count retains every compact digit using the bounded stacked native layout");
    technical.context_stale = true;
    prepare(CODY::CompanionPanel::Home, 0);
    dashboard_require(owner.arc_state == CODY::HomeDashboardArcState::Stale &&
        dashboard_has_caption(owner, "Free STALE"), "expired counters never retain a measured arc");
    technical.context_stale = false;
    technical.used = 155000;
    technical.capacity = 474000;
    technical.compaction_valid = technical.compaction_age_valid = true;
    technical.compaction = LUNASHELL::CodexCompactionState::Active;
    technical.compaction_age_at_receive_us = 2000000;
    prepare(CODY::CompanionPanel::Home, 0);
    const uint32_t compact_revision = owner.revision;
    dashboard_require(owner.arc_state == CODY::HomeDashboardArcState::Compacting &&
        owner.arc_path_count == 2,
        "compaction paints a fixed indeterminate accent, never a made-up completion percentage");
    prepare(CODY::CompanionPanel::Home, 100000);
    dashboard_require(owner.revision != compact_revision && owner.animation_step == 1,
        "existing active scene clock advances the real compaction accent");
    technical.last_error = ESP_FAIL;
    prepare(CODY::CompanionPanel::Home, 0);
    dashboard_require(owner.arc_state == CODY::HomeDashboardArcState::Invalid &&
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
    dashboard_require(owner.price_visible && dashboard_has_caption(owner, "gpt-6-astra") &&
        dashboard_has_caption(owner, "Price Expensive") && dashboard_has_caption(owner, "Thinking high") && owner.line_count == 3,
        "model panel contains only model identity, qualitative price capsule and thinking effort");
    prepare(CODY::CompanionPanel::SessionLink, 0);
    dashboard_require(dashboard_has_caption(owner, "PC connected") && dashboard_has_caption(owner, "Chat aaaaaa...aaaaaa") &&
        dashboard_has_caption(owner, "Device IP 192.0.2.17") && !dashboard_has_caption(owner, "Device IP 192.0.2.99"),
        "session panel must show OUR independently sourced IP, never the paired PC address");
    dashboard_require(CODY::prepare_home_dashboard(CODY::CompanionPanel::SessionLink, link, technical, value_font, body_font,
        1000000, 0, owner, content, &motion, &small_font, "255.255.255.255", CODY::PanelValueState::Current) == ESP_OK &&
        dashboard_has_caption(owner, "Device IP") && dashboard_has_caption(owner, "255.255.255.255"),
        "the widest valid IPv4 address retains all four octets within the lower panel");
    CODY::CompanionPanel next = CODY::CompanionPanel::Home;
    for (auto expected : {CODY::CompanionPanel::ModelContext, CODY::CompanionPanel::SessionLink, CODY::CompanionPanel::Home}) {
        dashboard_require(CODY::next_home_dashboard_panel(next, next) == ESP_OK && next == expected,
            "exactly three accessible dashboard identities must cycle back to token Home");
    }
    dashboard_require(CODY::prepare_home_dashboard(CODY::CompanionPanel::Activity, link, technical, value_font, body_font,
        1000000, 0, owner, content, &motion, &small_font) == ESP_ERR_INVALID_ARG,
        "legacy Activity identity must not create a fourth dashboard page");
    verify_dashboard_scene_visibility(body_font, small_font);
    std::fputs("Native Home dashboard: canonical metrics, three panels, chord bounds and full/strip/swapped raster parity passed\n", stderr);
}
