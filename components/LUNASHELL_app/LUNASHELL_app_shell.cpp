/* Minimal visual app for the display-and-touch board baseline. */

#include "LUNASHELL_app_identity.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "brookesia/system_core.hpp"
#include "esp_log.h"

namespace LUNASHELL {

constexpr char kTag[] = "LUNASHELL_shell";
constexpr std::string_view kTouchAction = "LUNASHELL.shell.touch";

class ShellApp final : public esp_brookesia::system::core::IApp {
public:
    esp_brookesia::system::core::AppManifest get_manifest() const override
    {
        return {
            .id = std::string(kAppManifestId),
            .name = "ESP32 Luna Shell",
            .localized_names = {},
            .version = "0.8.0",
            .kind = esp_brookesia::system::core::AppKind::Native,
            .visible = true,
            .preload_dom = false,
            .icon_id = "",
            .supported_systems = {},
            .icon_path = "",
            .runtime_type = esp_brookesia::runtime::BackendType::Unknown,
            .app_path = "",
            .entry = "",
            .resource_dir = std::string(kAppManifestId),
            .arguments = {},
        };
    }

    esp_brookesia::system::core::AppGuiDescriptor get_gui_descriptor() const override
    {
        return {
            .root_kind = esp_brookesia::system::core::GuiRootKind::File,
            .root = "res/root.json",
            .resources = {},
            .screen_flows = {{
                .screen_flow = "LUNASHELL_shell_flow",
                .layer = esp_brookesia::system::core::GuiAppLayer::AppDefault,
            }},
        };
    }

    std::expected<void, std::string> on_start(
        esp_brookesia::system::core::AppContext &context
    ) override
    {
        touch_count_ = 0;
        auto result = context.gui().subscribe_actions({std::string(kTouchAction)});
        if (!result) {
            ESP_LOGE(kTag, "Touch action subscription failed: %s", result.error().c_str());
            return result;
        }
        ESP_LOGI(kTag, "Minimal display and touch UI ready");
        return {};
    }

    std::expected<void, std::string> on_action(
        esp_brookesia::system::core::AppContext &context,
        std::string_view action
    ) override
    {
        if (action != kTouchAction) {
            ESP_LOGE(kTag, "Unknown shell action: %.*s", static_cast<int>(action.size()), action.data());
            return std::unexpected("Unknown minimal shell action");
        }
        ++touch_count_;
        const std::string status = "TOUCH " + std::to_string(touch_count_);
        auto result = context.gui().set_text(
            "/LUNASHELL_shell/touch_surface/touch_count", status
        );
        if (!result) {
            ESP_LOGE(kTag, "Touch status update failed: %s", result.error().c_str());
            return result;
        }
        ESP_LOGI(kTag, "Touch event %lu", static_cast<unsigned long>(touch_count_));
        return {};
    }

private:
    uint32_t touch_count_ = 0;
};

class ShellAppProvider final : public esp_brookesia::system::core::IAppProvider {
public:
    esp_brookesia::system::core::AppManifest get_manifest() const override
    {
        return ShellApp().get_manifest();
    }

    std::shared_ptr<esp_brookesia::system::core::IApp> create_app() override
    {
        return std::make_shared<ShellApp>();
    }
};

BROOKESIA_SYSTEM_CORE_APP_PROVIDER_REGISTER_WITH_SYMBOL(
    ShellAppProvider,
    "io.esp32.lunashell",
    LUNASHELL_app_provider_symbol
);

}  // namespace LUNASHELL
