#pragma once

#include <array>

#include "CODY_scene.hpp"
#include "CODY_widget_state.hpp"

namespace LUNASHELL {
struct DiagnosticsObservation;
struct FirmwareBuildMetadata;
}

namespace CODY {

// About owns these child routes; they are not independently persisted widgets.
enum class InfoPage : uint8_t { System, Memory, Firmware, ProjectLink };

// page: Retained child identity, updated only when the requested neighbor exists.
// step: Direction through System, Memory, Firmware and the terminal project QR.
// Returns false at either boundary so the owner can leave System or keep the QR.
bool step_info_page(InfoPage &page, WidgetStep step);

// Borrowed canonical observations; neither the presenter nor its raster reads services.
struct SystemInfoInput {
    InfoPage page = InfoPage::System;
    const LUNASHELL::DiagnosticsObservation *diagnostics = nullptr;
    esp_err_t diagnostics_error = ESP_ERR_INVALID_STATE;
    const LUNASHELL::FirmwareBuildMetadata *build = nullptr;
    const char *image_version = nullptr;
    uint32_t uptime_seconds = 0;
    uint32_t previous_uptime_seconds = 0;
    bool previous_uptime_valid = false;
};

// Only presentation values and prepared glyphs are cached, never a second sensor source.
struct SystemInfoPresentation {
    enum class ValueState : uint8_t { Current, Unavailable, Stale, Invalid, Error };
    struct Status {
        ValueState state = ValueState::Unavailable;
        esp_err_t error = ESP_OK;
    };
    struct Cpu {
        Status status{};
        uint8_t percent = 0;
    };
    struct Heap {
        Status status{};
        uint32_t total = 0;
        uint32_t free = 0;
        uint32_t largest = 0;
    };
    struct Values {
        InfoPage page = InfoPage::System;
        std::array<Cpu, 2> cpus{};
        std::array<Heap, 2> heaps{};
        const LUNASHELL::FirmwareBuildMetadata *build = nullptr;
        const char *image_version = nullptr;
        uint32_t uptime_seconds = 0;
        uint32_t previous_uptime_seconds = 0;
        bool previous_uptime_valid = false;
    };
    struct Caption {
        char text[32]{};
        CODY2D::TextLine line{};
    };
    struct Bar {
        CODY2D::Rect bounds{};
        uint16_t top = 0;
        uint16_t bottom = 0;
    };

    std::array<Caption, 14> captions{};
    std::array<Bar, 10> bars{};
    Values values{};
    const CODY2D::MonoFont *title_font = nullptr;
    const CODY2D::MonoFont *body_font = nullptr;
    CODY2D::Rect icon_bounds{};
    uint32_t revision = 0;
    uint8_t caption_count = 0;
    uint8_t bar_count = 0;
    bool valid = false;
};
static_assert(sizeof(SystemInfoPresentation) <= 2048,
    "System information retains bounded glyph storage in the display owner");

// input: Model-owned measurements and immutable build identity for the selected child page.
// title_font: Shared native heading and CPU percentage glyphs.
// body_font: Shared native detail glyphs.
// presentation: Persistent owner, changed only after the preceding frame retires.
// content: Receives the central content, without navigation or the allowance perimeter.
// Exception: Preserves invalid page identities and native formatting/raster preparation errors.
esp_err_t prepare_system_info(const SystemInfoInput &input,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    SystemInfoPresentation &presentation, SceneContent &content);

} // namespace CODY
