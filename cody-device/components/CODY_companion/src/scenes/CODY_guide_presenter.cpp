#include "CODY_guide_presenter.hpp"
#include "CODY_ui_components.hpp"
#include "CODY_theme_generated.hpp"
#include "scene_data.hpp"
#include "sdkconfig.h"
#include <iterator>

namespace CODY {
namespace {
struct GuideCommand { const char *label; const char *detail; };
constexpr GuideCommand kTouch[]{{"SWIPE", "Change pages"}, {"SWIPE DOWN", "Home from top edge"}, {"TAP", "Select an action"}};
constexpr GuideCommand kSofia[]{
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    {"SAY SOPHIA", "Can wake from standby"}, {"ON DEVICE", "Detected locally"}, {"SETTINGS", "Change Sofia in Behavior"}
#else
    {"SOFIA", "Wake word is unavailable"}, {"FIRMWARE", "Requires wake-word build"}, {"SETTINGS", "Available when supported"}
#endif
};
constexpr GuideCommand kOffline[]{{"WI-FI", "Connect when ready"}, {"CODEX", "Link your PC separately"}, {"NEXT", "Swipe for project QR"}};
static_assert(std::size(kTouch) == std::size(kSofia) && std::size(kSofia) == std::size(kOffline));

// view: Owner-provided output, with room for one more complete line.
// text: Immutable catalog caption.
// font: Existing immutable title or body font.
// bounds: Admitted caption slot; alignment preserves the circular center or the button column.
// color: Semantic foreground role.
// alignment: Explicit horizontal caption policy.
// Exception: Returns capacity or original text preparation errors before exposing the line.
esp_err_t append_line(GuidePresentation &view, const char *text, const CODY2D::MonoFont &font,
    CODY2D::Rect bounds, uint16_t color, UI::TextAlignment alignment = UI::TextAlignment::Center)
{
    if (view.line_count >= std::size(view.lines)) return ESP_ERR_INVALID_SIZE;
    const auto error = UI::prepare_text(font, text, bounds, color, view.lines[view.line_count], 63, alignment);
    if (error == ESP_OK) ++view.line_count;
    return error;
}
} // namespace

// page: Explicit page selected by the checked catalog.
// first_run: Whether footer actions must persist skip/completion.
// qr_available: Confirmed project QR availability; this presenter never encodes it.
// save_error: Exact prior progress-write failure, displayed without changing page ownership.
// presentation: Caller-owned output; literal text and font lifetimes exceed the frame.
// Exception: Returns invalid page or original caption layout errors.
esp_err_t prepare_guide_presentation(OptionsPage page, bool first_run,
    bool qr_available, esp_err_t save_error, GuidePresentation &presentation)
{
    const auto *definition = options_page_definition(page);
    if (definition == nullptr || definition->tour_position == 0) return ESP_ERR_INVALID_ARG;
    presentation = {};
    presentation.position = definition->tour_position;
    constexpr uint16_t width = 280;
    constexpr uint16_t left = (466U - width) / 2U;
    const auto centered = [&](const char *text, uint16_t top, const CODY2D::MonoFont &font, uint16_t color) {
        return append_line(presentation, text, font, {left, top, width, font.height}, color);
    };
    if (page == OptionsPage::GuideProject) {
        presentation.project_qr = qr_available && save_error == ESP_OK;
        return presentation.project_qr ? ESP_OK : centered(save_error != ESP_OK ? "Save failed. Swipe again" :
            "QR unavailable", 200, SceneData::body_font, Theme::text_detail);
    }
    constexpr const char *buttons[]{"BOOT tap: menu",
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
        "Hold BOOT 2s: voice",
#else
        "Hold BOOT 2s: settings",
#endif
        "PWR: sleep / wake", "Hold PWR 3s: off"};
    constexpr const char *touch[]{"Swipe to change pages", "Swipe down for Home", "Tap to select"};
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    constexpr const char *sofia[]{"Say Sophia to wake", "Detected on device", "Settings: Behavior"};
#else
    constexpr const char *sofia[]{"Wake word unavailable", "Not in this firmware", "Swipe to continue"};
#endif
    constexpr const char *offline[]{"Wi-Fi when ready", "Connect Codex separately", "Swipe for project"};
    constexpr const char *thanks[]{"Thanks for joining us", "Let's get started", "From the Cody team"};
    const auto *lines = page == OptionsPage::GuideButtons ? buttons :
        page == OptionsPage::GuideTouch ? touch : page == OptionsPage::GuideSofia ? sofia :
        page == OptionsPage::GuideThanks ? thanks : offline;
    const size_t line_count = page == OptionsPage::GuideThanks ? 1U : save_error != ESP_OK ? 2U :
        page == OptionsPage::GuideButtons && save_error == ESP_OK ? 4U : 3U;
    for (size_t i = 0; i < line_count; ++i) {
        const auto error = centered(lines[i], static_cast<uint16_t>(277U + i * 25U),
            SceneData::body_font, Theme::text_primary);
        if (error != ESP_OK) return error;
    }
    if (page == OptionsPage::GuideThanks)
        presentation.footer = GuideFooter::Continue;
    else if (first_run)
        presentation.footer = GuideFooter::Skip;
    if (save_error != ESP_OK) return centered("Save failed. Try again",
        page == OptionsPage::GuideThanks ? 303 : 327, SceneData::body_font, Theme::text_primary);
    return ESP_OK;
}

} // namespace CODY
