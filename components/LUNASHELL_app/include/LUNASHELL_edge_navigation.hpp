#pragma once

#include <expected>
#include <string>

#include "brookesia/system_core.hpp"

namespace LUNASHELL {

// Controls the single declarative Back/Next pair owned by the app chrome.
class EdgeNavigation final {
public:
    std::expected<void, std::string> configure(
        const esp_brookesia::system::core::AppGuiRuntime &gui,
        bool back_visible,
        bool next_visible
    ) const;
};

}  // namespace LUNASHELL
