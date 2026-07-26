#include "qsee_edge_navigation.hpp"

#include <string>
#include <string_view>
#include <vector>

#include "qsee_palette.hpp"

namespace {

constexpr std::string_view kBackPath =
    "/qsee_chrome_screen/chrome/back";
constexpr std::string_view kNextPath =
    "/qsee_chrome_screen/chrome/next";

void add_binding(
    std::vector<esp_brookesia::gui::BindingValueUpdate> &updates,
    std::string_view path,
    std::string_view key,
    std::string_view value
)
{
    updates.push_back({
        .absolute_path = std::string(path),
        .key = std::string(key),
        .value = std::string(value),
    });
}

void add_button(
    std::vector<esp_brookesia::gui::BindingValueUpdate> &updates,
    std::string_view path,
    bool visible
)
{
    const qsee::palette::ActionColors &colors =
        qsee::palette::action_colors(qsee::palette::ActionRole::Success);
    add_binding(updates, path, "hidden", visible ? "false" : "true");
    add_binding(updates, path, "clickable", visible ? "true" : "false");
    add_binding(updates, path, "top", colors.top);
    add_binding(updates, path, "bottom", colors.bottom);
    add_binding(updates, path, "border", colors.border);
    add_binding(updates, path, "shadow", colors.glow);
    add_binding(updates, path, "pressed_top", colors.pressed_top);
    add_binding(updates, path, "pressed_bottom", colors.pressed_bottom);
    add_binding(updates, path, "pressed_border", colors.pressed_border);
}

}  // namespace

namespace qsee {

std::expected<void, std::string> EdgeNavigation::configure(
    const esp_brookesia::system::core::AppGuiRuntime &gui,
    bool back_visible,
    bool next_visible
) const
{
    std::vector<esp_brookesia::gui::BindingValueUpdate> updates;
    updates.reserve(18);
    add_button(updates, kBackPath, back_visible);
    add_button(updates, kNextPath, next_visible);
    auto result = gui.set_binding_values(updates);
    if (!result) {
        return std::unexpected(
            "Failed to configure reusable QSee edge navigation: " +
            result.error()
        );
    }
    return {};
}

}  // namespace qsee
