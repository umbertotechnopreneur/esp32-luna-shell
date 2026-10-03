#pragma once

#include <cstdint>

namespace LUNASHELL::display_timing {

// Panel sleep/wake fades share one default, independently of Cody's cold boot.
inline constexpr uint32_t kTransitionFadeMs = 800;

} // namespace LUNASHELL::display_timing
