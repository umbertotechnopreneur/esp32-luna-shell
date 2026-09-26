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
#include "sdkconfig.h"
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"
#endif

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
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
        standby_ = std::make_shared<StandbySession>();
        auto timer = context.timer().start_periodic(kPowerTimer, 200);
        if (!timer) {
            ESP_LOGE(kTag, "Power synchronization timer failed: %s", timer.error().c_str());
            return std::unexpected(timer.error());
        }
        power_timer_ = *timer;
#endif
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
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
        if (standby_->input_blocked) {
            return {};
        }
#endif
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

#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
    std::expected<void, std::string> on_timer(
        esp_brookesia::system::core::AppContext &context,
        esp_brookesia::system::core::TimerId timer_id,
        std::string_view name
    ) override
    {
        if (timer_id != power_timer_ || name != kPowerTimer) {
            return {};
        }
        const auto power = DeviceSnapshotService::instance().snapshot().power;
        if (power.state == DeviceOperatingState::Active) {
            if (!standby_->input_blocked) {
                return {};
            }
            // Invalidate an asynchronous drain on wake or failed entry rollback.
            ++standby_->generation;
            auto result = context.gui().drain_pending_work();
            if (result) {
                result = context.gui().resume_input();
            }
            if (!result) {
                ESP_LOGE(kTag, "Shell input resume failed: %s", result.error().c_str());
                return result;
            }
            standby_->input_blocked = false;
            standby_->sequence = 0;
            result = start_breathing(context);
            if (!result) {
                ESP_LOGE(kTag, "Idle breathing resume failed: %s", result.error().c_str());
                return result;
            }
            return {};
        }
        if (power.state != DeviceOperatingState::EnteringStandby ||
            (standby_->input_blocked && standby_->sequence == power.transition_sequence)) {
            return {};
        }
        if (auto result = stop_breathing(context); !result) {
            ESP_LOGE(kTag, "Idle breathing quiescence failed: %s", result.error().c_str());
            LUNASHELL_power_manager_note_ui_standby_result(
                power.transition_sequence, ESP_FAIL
            );
            return result;
        }
        standby_->input_blocked = true;
        standby_->sequence = power.transition_sequence;
        const uint32_t generation = ++standby_->generation;
        const uint32_t sequence = power.transition_sequence;
        const std::weak_ptr<StandbySession> session = standby_;
        // quiesce_input drains AppInput, GUIInput and GUI work. Acknowledging
        // earlier would allow the power task to pause LVGL during a touch update.
        auto result = context.gui().quiesce_input(
            [session, generation, sequence](std::expected<void, std::string> drained) {
                const auto state = session.lock();
                if (!state || !state->running || state->generation != generation) {
                    return;
                }
                const auto current = DeviceSnapshotService::instance().snapshot().power;
                if (current.state != DeviceOperatingState::EnteringStandby ||
                    current.transition_sequence != sequence) {
                    return;
                }
                if (!drained) {
                    ESP_LOGE(kTag, "Shell input drain failed: %s", drained.error().c_str());
                }
                LUNASHELL_power_manager_note_ui_standby_result(
                    sequence, drained ? ESP_OK : ESP_FAIL
                );
            }
        );
        if (!result) {
            ESP_LOGE(kTag, "Shell input quiescence failed: %s", result.error().c_str());
            ++standby_->generation;
            LUNASHELL_power_manager_note_ui_standby_result(sequence, ESP_FAIL);
        }
        return result;
    }

#endif

    std::expected<void, std::string> on_stop(
        esp_brookesia::system::core::AppContext &context
    ) override
    {
        std::string first_error;
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
        if (power_timer_ != esp_brookesia::system::core::INVALID_TIMER_ID &&
            !context.timer().stop(power_timer_)) {
            ESP_LOGE(kTag, "Power synchronization timer stop failed");
            if (first_error.empty()) {
                first_error = "Power synchronization timer stop failed";
            }
        }
        power_timer_ = esp_brookesia::system::core::INVALID_TIMER_ID;
        if (standby_) {
            standby_->running = false;
            ++standby_->generation;
        }
#endif
        if (!first_error.empty()) {
            return std::unexpected(first_error);
        }
        return {};
    }

private:
    uint32_t touch_count_ = 0;
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
    struct StandbySession {
        bool running = true;
        bool input_blocked = false;
        uint32_t sequence = 0;
        uint32_t generation = 0;
    };
    static constexpr std::string_view kPowerTimer = "shell.power";
    std::shared_ptr<StandbySession> standby_;
    esp_brookesia::system::core::TimerId power_timer_ =
        esp_brookesia::system::core::INVALID_TIMER_ID;
#endif
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
