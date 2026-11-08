#include "CODY_home_identity.hpp"
#include "home_dashboard_fonts.hpp"
#include "LUNASHELL_codex_presence.hpp"
#include "esp_log.h"
#include <algorithm>
#include <cstring>

namespace CODY {
namespace {
// operation: Fixed diagnostic operation, containing no chat text or private identifiers.
// error: Original source, metadata or raster failure returned unchanged.
// Exception: Logs failures without repairing source observations.
esp_err_t identity_result(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) { ESP_LOGE("CODY_home", "%s failed (%s)", operation, esp_err_to_name(error)); }
    return error;
}

// state: Borrowed persistent Home identity captured by the scene content.
// strip: Original native tile.
// first_row: Logical y origin.
// first_column: Logical x origin.
// Exception: Returns invalid ownership or the original model-row draw error.
esp_err_t draw_model_content(const void *state, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    if (!state) { return identity_result("model_owner", ESP_ERR_INVALID_ARG); }
    return draw_home_identity_rows(*static_cast<const HomeIdentityPresentation *>(state), strip, first_row, first_column);
}
} // namespace

// scene: Existing title and reveal state, preserved through font/layout preparation.
// link: Canonical synchronized activity/link observation.
// technical: Canonical independent model/source metadata.
// now_us: Source receipt clock, never derived from animation time.
// active_time_us: Pausable model-scroll clock.
// owner: Stable model caption and scrolling state retained through raster retirement.
// model_content: Receives the bounded transparent model callback.
// Exception: Preserves original source and renderer errors; rejects oversized/invalid captions.
esp_err_t prepare_home_identity(SceneFrame &scene, const LUNASHELL::CodexLinkSnapshot &link,
    const LUNASHELL::CodexTechnicalSnapshot &technical, int64_t now_us, int64_t active_time_us,
    HomeIdentityPresentation &owner, SceneContent &model_content)
{
    if (active_time_us < 0 || !scene.title.text.characters || scene.title.text.length == 0) {
        return identity_result("identity_input", ESP_ERR_INVALID_ARG);
    }
    const bool codex = LUNASHELL::codex_presence::ready_current(link, now_us) &&
        LUNASHELL::codex_presence::is_open(link.app_presence, now_us);
    PanelInputs inputs{};
    esp_err_t error = ESP_OK;
    if (codex) {
        error = project_companion_panel_inputs(link, technical, now_us, "", inputs);
        if (error != ESP_OK) { return identity_result("identity_source", error); }
    }
    const auto &heading = HomeFonts::heading_font;
    uint32_t title_width = 0;
    error = CODY2D::measure_text_width(heading, scene.title.text, title_width);
    if (error != ESP_OK) { return identity_result("heading_measure", error); }
    // The complete heading uses its actual circular chord, with no lateral Home navigation.
    if (title_width > 400 || 190U + heading.height > 236U) {
        return identity_result("heading_extent", ESP_ERR_INVALID_SIZE);
    }
    const bool attention = codex && (inputs.phase == LUNASHELL::CodexActivityState::Paused ||
        inputs.phase == LUNASHELL::CodexActivityState::Interrupted);
    const bool compacting = codex && inputs.compaction == LUNASHELL::CodexCompactionState::Active;
    const uint16_t title_color = compacting ? 0xFD20 : attention ? scene.title.color : 0x5FF9;
    error = CODY2D::prepare_text_line(heading, scene.title.text,
        {static_cast<int16_t>((466U - title_width) / 2U), 190}, title_color, scene.title);
    if (error != ESP_OK) { return identity_result("heading_prepare", error); }
    const char *model = "Local companion";
    const char *identity = "";
    if (codex) {
        identity = inputs.chat_id;
        model = inputs.model_state == PanelValueState::Current ? inputs.model :
            inputs.chat_id[0] == '\0' ? "Codex connected" :
            inputs.model_state == PanelValueState::Stale ? "Model stale" :
            inputs.model_state == PanelValueState::Invalid ? "Model invalid" : "Model unavailable";
    }
    const size_t length = std::strlen(model);
    if (length == 0 || length >= sizeof(owner.model) || std::strlen(identity) >= sizeof(owner.chat)) {
        return identity_result("model_extent", ESP_ERR_INVALID_SIZE);
    }
    const auto &font = HomeFonts::small_font;
    for (size_t index = 0; index < length; ++index) {
        if (model[index] < 32 || model[index] > 126) {
            return identity_result("model_ascii", ESP_ERR_INVALID_ARG);
        }
    }
    bool changed = !owner.valid || std::strcmp(owner.model, model) != 0 || std::strcmp(owner.chat, identity) != 0;
    if (changed) {
        std::strcpy(owner.model, model);
        std::strcpy(owner.chat, identity);
        owner.started_us = active_time_us;
    }
    uint32_t width = 0;
    error = CODY2D::measure_text_width(font, {owner.model, static_cast<uint16_t>(length)}, width);
    if (error != ESP_OK) { return identity_result("model_measure", error); }
    if (width > UINT16_MAX) { return identity_result("model_width", ESP_ERR_INVALID_SIZE); }
    uint16_t offset = 0;
    if (width > 320) {
        const uint16_t travel = static_cast<uint16_t>(width - 320U);
        // A measured 25 px/s sweep holds both complete ends for 1.5 seconds.
        const int64_t sweep_us = static_cast<int64_t>(travel) * 40000;
        const int64_t elapsed = std::max<int64_t>(0, active_time_us - owner.started_us);
        const int64_t cycle = elapsed % (sweep_us * 2 + 3000000);
        if (cycle >= 1500000 && cycle < 1500000 + sweep_us) {
            offset = static_cast<uint16_t>((cycle - 1500000) / 40000);
        } else if (cycle >= 1500000 + sweep_us && cycle < 3000000 + sweep_us) {
            offset = travel;
        } else if (cycle >= 3000000 + sweep_us) {
            offset = static_cast<uint16_t>(travel - (cycle - 3000000 - sweep_us) / 40000);
        }
    }
    CODY2D::TextSpan window{};
    int16_t origin_offset = 0;
    error = CODY2D::select_text_window(font, {owner.model, static_cast<uint16_t>(length)},
        offset, 320, window, origin_offset);
    if (error != ESP_OK) { return identity_result("model_window", error); }
    const int16_t left = width <= 320 ? static_cast<int16_t>((466U - width) / 2U) :
        static_cast<int16_t>(73 + origin_offset);
    error = CODY2D::prepare_text_line(font, window, {left, 236}, 0x9CD3, owner.model_line);
    if (error != ESP_OK) { return identity_result("model_prepare", error); }
    changed = changed || owner.offset != offset;
    owner.offset = offset;
    owner.valid = true;
    if (changed) { ++owner.revision; }
    // Hide the legacy layout without erasing semantic state used by audio and character direction.
    scene.home_dashboard = true;
    model_content = {};
    model_content.bounds = kHomeModelBounds;
    model_content.state = &owner;
    model_content.revision = owner.revision;
    model_content.draw_rows = draw_model_content;
    return ESP_OK;
}

// owner: Stable prepared model caption/window, never a mutable source snapshot.
// strip: Original tile before bounded viewport clipping.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Preserves the native glyph/surface failure.
esp_err_t draw_home_identity_rows(const HomeIdentityPresentation &owner, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    if (!owner.valid) { return identity_result("model_not_prepared", ESP_ERR_INVALID_STATE); }
    const uint16_t left = std::max<uint16_t>(first_column, 73);
    const uint16_t right = std::min<uint16_t>(first_column + strip.width, 393);
    const uint16_t top = std::max<uint16_t>(first_row, 236);
    const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, 264);
    if (left >= right || top >= bottom) { return ESP_OK; }
    const uint32_t offset = (top - first_row) * strip.stride + left - first_column;
    strip.pixels += offset;
    strip.pixel_capacity -= offset;
    strip.width = right - left;
    strip.height = bottom - top;
    return identity_result("model_draw", CODY2D::draw_text_rows(owner.model_line,
        owner.model_line.text.length, strip, top, left));
}
} // namespace CODY
