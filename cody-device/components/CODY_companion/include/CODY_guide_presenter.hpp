#pragma once

#include "CODY_onboarding_catalog.hpp"
#include "CODY2D_text.hpp"

namespace CODY {

enum class GuideFooter : uint8_t { None, Skip, Continue, Done };
struct GuidePresentation {
    CODY2D::TextLine lines[8]{};
    uint8_t line_count = 0;
    uint8_t position = 0;
    uint16_t title_color = 0;
    bool project_qr = false;
    bool button_illustration = false;
    GuideFooter footer = GuideFooter::None;
};

// page: Explicit catalog identity of the guide page.
// first_run: Whether skipping/completing the tour has persistent meaning.
// qr_available: Confirmed availability of the already encoded project QR.
// save_error: Last actual progress-write result, preserved separately from QR availability.
// presentation: Caller-owned display scratch; lines borrow immutable literal text and fonts.
// Exception: Returns invalid catalog IDs or the original measured text-layout error.
esp_err_t prepare_guide_presentation(OptionsPage page, bool first_run,
    bool qr_available, esp_err_t save_error, GuidePresentation &presentation);

} // namespace CODY
