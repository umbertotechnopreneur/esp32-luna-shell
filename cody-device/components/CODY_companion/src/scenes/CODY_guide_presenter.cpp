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
    if (page == OptionsPage::GuideThanks) {
        constexpr const char *lines[]{"Thanks for joining us", "From the Cody", "development team"};
        for (size_t i = 0; i < std::size(lines); ++i) {
            const auto error = centered(lines[i], static_cast<uint16_t>(160U + i * 45U), SceneData::body_font,
                i == 0 ? Theme::text_bright : Theme::text_detail);
            if (error != ESP_OK) return error;
        }
        presentation.footer = first_run ? GuideFooter::Continue : GuideFooter::Done;
    } else {
        const uint16_t accent = page == OptionsPage::GuideTouch ? Theme::accent_violet :
            page == OptionsPage::GuideOffline ? Theme::secondary_bright : Theme::accent_cyan;
        presentation.title_color = accent;
        presentation.button_illustration = page == OptionsPage::GuideButtons;
        if (presentation.button_illustration) {
            constexpr const char *lines[]{"BOOT", "Tap: menu", "Hold 2s:",
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
                "Voice input",
#else
                "Settings",
#endif
                "PWR", "Sleep / wake", "Hold 3s: off"};
            for (size_t i = 0; i < std::size(lines); ++i) {
                const auto error = append_line(presentation, lines[i], SceneData::body_font,
                    {93, static_cast<uint16_t>(130U + i * 28U), 137, SceneData::body_font.height},
                    i == 0 || i == 4 ? accent : Theme::text_bright, UI::TextAlignment::Left);
                if (error != ESP_OK) return error;
            }
        } else {
            const auto *commands = page == OptionsPage::GuideTouch ? kTouch : page == OptionsPage::GuideSofia ? kSofia : kOffline;
            for (size_t i = 0; i < std::size(kTouch); ++i) {
                const auto top = static_cast<uint16_t>(130U + i * 68U);
                auto error = centered(commands[i].label, top, SceneData::title_font, accent);
                if (error == ESP_OK) error = centered(commands[i].detail, top + 43U, SceneData::body_font, Theme::text_detail);
                if (error != ESP_OK) return error;
            }
        }
        presentation.footer = first_run ? GuideFooter::Skip : GuideFooter::None;
    }
    if (save_error != ESP_OK) return centered("Save failed. Try again", 340, SceneData::body_font, Theme::text_detail);
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    if (page == OptionsPage::GuideTouch) return centered("Dictation On + Codex link", 340, SceneData::body_font, Theme::text_detail);
#endif
    return ESP_OK;
}

} // namespace CODY
