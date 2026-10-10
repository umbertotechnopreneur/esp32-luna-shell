/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 * Original file notice:
 *
 * ESP32 Luna Shell
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 ESP32 Luna Shell. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Author website: https://umbertogiacobbi.biz
 *
 *
 * VBWR E */


#include "LUNASHELL_app.hpp"
#include "LUNASHELL_app_internal.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "LUNASHELL_platform.h"
#if CONFIG_LUNASHELL_BUILD_PROFILE_LEG_EDITION
#include "lvgl.h"
#endif

namespace {

using esp_brookesia::system::core::AppContext;
using LUNASHELL::app_detail::kTag;

constexpr char kGuiRoot[] = "res/root.json";
constexpr char kBackgroundFlow[] = "LUNASHELL_background_flow";
constexpr char kScreenFlow[] = "LUNASHELL_content";
constexpr char kReleaseContentAction[] = "LUNASHELL.content.release";
constexpr char kChromeFlow[] = "LUNASHELL_chrome_flow";

esp_brookesia::system::core::AppGuiResources make_gui_resources()
{
    esp_brookesia::system::core::AppGuiResources resources{};
#if CONFIG_LUNASHELL_BUILD_PROFILE_LEG_EDITION
    // Unscii-16 is a public-domain, fixed-width LVGL built-in font. Register
    // it through Brookesia so the declarative Leg document can request it.
    resources.fonts.push_back({
        .id = "leg_mono",
        .kind = "file",
        .primary_src = {},
        .languages = {"en", "it"},
        .native_fonts = {{
            .native_src = reinterpret_cast<uintptr_t>(&lv_font_unscii_16),
            .native_size = 16,
        }},
    });
#endif
    return resources;
}

}  // namespace

namespace LUNASHELL {

// screen: Named route whose lazily loaded document is requested.
std::optional<App::LazyScreenDescriptor> App::lazy_screen_descriptor(Screen screen)
{
    // Pairing is a small journey inside one document, so its steps share the owner.
    switch (screen) {
    case Screen::PairIntro:
    case Screen::PairBluetooth:
    case Screen::PairApp:
    case Screen::AppStoreQr:
    case Screen::PairQr:
    case Screen::PairResult:
        return LazyScreenDescriptor{
            .document_path = "res/screen_pairing.json",
            .screen_path = "/LUNASHELL_pairing",
        };
    default:
        break;
    }

    struct ScreenDocument {
        Screen route;
        LazyScreenDescriptor descriptor;
    };
    static constexpr std::array screen_documents = {
        ScreenDocument{Screen::Battery, {"res/screen_battery.json", "/LUNASHELL_battery"}},
        ScreenDocument{Screen::Logs, {"res/screen_logs.json", "/LUNASHELL_logs"}},
        ScreenDocument{Screen::Hardware, {"res/screen_hardware.json", "/LUNASHELL_hardware"}},
        ScreenDocument{Screen::HardwareDetail, {"res/screen_hardware_detail.json", "/LUNASHELL_hardware_detail"}},
        ScreenDocument{Screen::Audio, {"res/screen_audio.json", "/LUNASHELL_audio"}},
        ScreenDocument{Screen::AudioFft, {"res/screen_audio_fft.json", "/LUNASHELL_audio_fft"}},
        ScreenDocument{Screen::AudioDetail, {"res/screen_audio_detail.json", "/LUNASHELL_audio_detail"}},
        ScreenDocument{Screen::Imu, {"res/screen_imu.json", "/LUNASHELL_imu"}},
        ScreenDocument{Screen::Leg, {"res/screen_leg.json", "/LUNASHELL_leg"}},
        ScreenDocument{Screen::Gps, {"res/screen_gps.json", "/LUNASHELL_gps"}},
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
        ScreenDocument{Screen::Tof, {"res/screen_tof.json", "/LUNASHELL_tof"}},
        ScreenDocument{Screen::TofSettings, {"res/screen_tof_settings.json", "/LUNASHELL_tof_settings"}},
#endif
        ScreenDocument{Screen::Settings, {"res/screen_settings.json", "/LUNASHELL_settings"}},
        ScreenDocument{Screen::Storage, {"res/screen_storage.json", "/LUNASHELL_storage"}},
        ScreenDocument{Screen::About, {"res/screen_about.json", "/LUNASHELL_about"}},
    };

    const auto document = std::find_if(
        screen_documents.begin(),
        screen_documents.end(),
        [screen](const ScreenDocument &entry) { return entry.route == screen; }
    );
    if (document == screen_documents.end()) {
        return std::nullopt;
    }
    return document->descriptor;
}

// Welcome stays resident; other routes own a lazily loaded document.
bool App::has_active_screen_document() const
{
    return screen_ != Screen::Welcome && active_screen_document_id_.has_value();
}

// left: First named route whose owner is compared.
// right: Second route; sharing a document preserves its active state.
bool App::shares_lazy_document(Screen left, Screen right)
{
    const auto left_descriptor = lazy_screen_descriptor(left);
    const auto right_descriptor = lazy_screen_descriptor(right);
    return left_descriptor.has_value() &&
           right_descriptor.has_value() &&
           left_descriptor->document_path == right_descriptor->document_path &&
           left_descriptor->screen_path == right_descriptor->screen_path;
}

void App::invalidate_screen_document_state(Screen screen)
{
    content_viewport_state_.reset();
    invalidate_audio_screen_cache(screen);
    if (screen == Screen::Hardware) {
        invalidate_hardware_screen_cache();
    }
    if (screen == Screen::Imu) {
        invalidate_imu_screen_cache();
    }
    if (screen == Screen::Leg) {
        invalidate_leg_screen_cache();
    }
    if (screen == Screen::Gps) {
        invalidate_gps_screen_cache();
    }
    if (screen == Screen::AudioDetail) {
        audio_detail_palette_applied_ = false;
    }
    if (screen == Screen::Settings) {
        settings_text_cache_valid_ = false;
        settings_controls_state_.reset();
    }
    if (screen == Screen::Storage) {
        invalidate_storage_screen_cache();
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    if (screen == Screen::TofSettings) {
        tof_settings_controls_state_.reset();
        tof_reference_pending_mm_.reset();
        tof_reference_editing_ = false;
    }
#endif
    if (is_pairing_screen(screen)) {
        pairing_visual_state_.reset();
    }
}

void App::log_screen_document_heap(
    std::string_view operation,
    Screen screen,
    uint32_t loaded_document_count
) const
{
    const auto descriptor = lazy_screen_descriptor(screen);
    const std::string_view document_path = descriptor.has_value()
        ? descriptor->document_path
        : std::string_view(kGuiRoot);
    const uint32_t internal_free = static_cast<uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
    );
    const uint32_t internal_largest = static_cast<uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
    );
    const uint32_t psram_free = static_cast<uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );
    const uint32_t psram_largest = static_cast<uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );
    ESP_LOGI(
        kTag,
        "Lazy GUI %.*s: document(%.*s) loaded_page_documents(%u) "
        "internal_free(%lu) internal_largest(%lu) "
        "psram_free(%lu) psram_largest(%lu)",
        static_cast<int>(operation.size()),
        operation.data(),
        static_cast<int>(document_path.size()),
        document_path.data(),
        static_cast<unsigned>(loaded_document_count),
        static_cast<unsigned long>(internal_free),
        static_cast<unsigned long>(internal_largest),
        static_cast<unsigned long>(psram_free),
        static_cast<unsigned long>(psram_largest)
    );
}

std::expected<void, std::string> App::unload_active_screen_document(
    AppContext &context
)
{
    if (!active_screen_document_id_.has_value()) {
        return {};
    }

    const auto document_id = *active_screen_document_id_;
    ++screen_document_generation_;
    screen_document_connections_.clear();
    if (!context.gui().unload(document_id)) {
        return std::unexpected("Failed to unload active LUNASHELL screen document");
    }
    active_screen_document_id_.reset();
    log_screen_document_heap("unloaded", screen_, 0U);
    return {};
}

std::expected<void, std::string> App::activate_screen_document(
    AppContext &context,
    Screen screen
)
{
    if (screen == Screen::Welcome &&
        !active_screen_document_id_.has_value()) {
        auto result = context.gui().trigger_screen_flow(
            kScreenFlow,
            "LUNASHELL.open.welcome"
        );
        if (!result) {
            return std::unexpected(
                "Failed to mount resident Welcome screen: " + result.error()
            );
        }
        return {};
    }
    if (active_screen_document_id_.has_value() &&
        shares_lazy_document(screen_, screen)) {
        return {};
    }

    auto release_result = context.gui().trigger_screen_flow(
        kScreenFlow,
        kReleaseContentAction
    );
    if (!release_result) {
        return std::unexpected(
            "Failed to release current LUNASHELL content: " + release_result.error()
        );
    }
    log_screen_document_heap(
        "released_to_idle",
        screen_,
        active_screen_document_id_.has_value() ? 1U : 0U
    );

    if (auto result = unload_active_screen_document(context); !result) {
        return result;
    }

    if (screen == Screen::Welcome) {
        auto welcome_result = context.gui().trigger_screen_flow(
            kScreenFlow,
            "LUNASHELL.open.welcome"
        );
        if (!welcome_result) {
            return std::unexpected(
                "Failed to restore resident Welcome screen: " +
                welcome_result.error()
            );
        }
        log_screen_document_heap("mounted", screen, 0U);
        return {};
    }

    const auto descriptor = lazy_screen_descriptor(screen);
    if (!descriptor.has_value()) {
        return std::unexpected("No lazy GUI document is registered for route");
    }

    auto load_result = context.gui().load_file(descriptor->document_path);
    if (!load_result) {
        return std::unexpected(
            "Failed to load LUNASHELL screen document '" +
            std::string(descriptor->document_path) + "': " +
            load_result.error()
        );
    }
    const auto document_id = *load_result;
    log_screen_document_heap("loaded", screen, 1U);

    const uint32_t generation = screen_document_generation_ + 1U;
    const auto retain_document_for_cleanup_retry = [&]() {
        active_screen_document_id_ = document_id;
        screen_document_generation_ = generation;
        log_screen_document_heap("cleanup_deferred", screen, 1U);
    };
    std::vector<esp_brookesia::gui::ScopedConnection> connections;
    auto subscription_result = subscribe_screen_document_actions(
        context,
        document_id,
        screen,
        generation,
        connections
    );
    if (!subscription_result) {
        connections.clear();
        const bool unloaded = context.gui().unload(document_id);
        if (!unloaded) {
            retain_document_for_cleanup_retry();
        }
        return std::unexpected(
            subscription_result.error() +
            (unloaded ? "" : "; document cleanup also failed")
        );
    }

    auto mount_result = context.gui().mount_screen(
        document_id,
        descriptor->screen_path
    );
    if (!mount_result) {
        connections.clear();
        const bool unloaded = context.gui().unload(document_id);
        if (!unloaded) {
            retain_document_for_cleanup_retry();
        }
        return std::unexpected(
            "Failed to mount LUNASHELL screen '" +
            std::string(descriptor->screen_path) + "': " +
            mount_result.error() +
            (unloaded ? "" : "; document cleanup also failed")
        );
    }

    active_screen_document_id_ = document_id;
    screen_document_generation_ = generation;
    screen_document_connections_ = std::move(connections);
    invalidate_screen_document_state(screen);
    log_screen_document_heap("mounted", screen, 1U);
    return {};
}

std::expected<void, std::string> App::restore_welcome_document(
    AppContext &context
)
{
    std::string rollback_error;
    const auto remember_failure = [&](std::string_view operation,
                                      std::string_view error) {
        ESP_LOGE(
            kTag,
            "Welcome rollback %.*s failed: %.*s",
            static_cast<int>(operation.size()),
            operation.data(),
            static_cast<int>(error.size()),
            error.data()
        );
        if (!rollback_error.empty()) {
            rollback_error += "; ";
        }
        rollback_error += std::string(operation) + " failed: " +
                          std::string(error);
    };

    auto welcome_result = context.gui().trigger_screen_flow(
        kScreenFlow,
        "LUNASHELL.open.welcome"
    );
    if (!welcome_result) {
        remember_failure(
            "resident Welcome restore",
            welcome_result.error()
        );
    }
    if (auto unload_result = unload_active_screen_document(context);
        !unload_result) {
        remember_failure("lazy document unload", unload_result.error());
    }
    if (!welcome_result) {
        return std::unexpected(std::move(rollback_error));
    }

    screen_ = Screen::Welcome;
#if CONFIG_LUNASHELL_BUILD_PROFILE_LEG_EDITION && CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
    const esp_err_t leg_policy_result =
        LUNASHELL_power_manager_set_leg_mode(false, leg_wake_enabled_);
    if (leg_policy_result != ESP_OK) {
        remember_failure(
            "Leg power policy rollback",
            esp_err_to_name(leg_policy_result)
        );
    }
#endif
    navigation_.reset(Screen::Welcome);
    pairing_visual_state_.reset();
    content_viewport_state_.reset();
    settings_text_cache_valid_ = false;
    settings_controls_state_.reset();
    invalidate_audio_screen_cache(Screen::Audio);
    invalidate_audio_screen_cache(Screen::AudioFft);
    invalidate_audio_screen_cache(Screen::AudioDetail);
    invalidate_hardware_screen_cache();
    invalidate_imu_screen_cache();
    invalidate_leg_screen_cache();
    invalidate_storage_screen_cache();

    if (auto result = apply_diagnostic_monitor(context, Screen::Welcome);
        !result) {
        remember_failure("diagnostic policy", result.error());
    }
    if (auto result = configure_chrome(context); !result) {
        remember_failure("chrome configuration", result.error());
    }
    if (auto result = refresh(context); !result) {
        remember_failure("Welcome refresh", result.error());
    }
    if (!rollback_error.empty()) {
        return std::unexpected(std::move(rollback_error));
    }
    return {};
}

// context: Active Brookesia application runtime.
// path: Stable absolute view path inside the selected document.
// text: Visible text to publish after selecting the document owner.
std::expected<void, std::string> App::set_text(
    AppContext &context,
    std::string_view path,
    std::string_view text
)
{
    const bool use_screen_document =
        has_active_screen_document();
    auto result = use_screen_document
        ? context.gui().set_text(*active_screen_document_id_, path, text)
        : context.gui().set_text(path, text);
    if (!result) {
        return std::unexpected(
            "Failed to update LUNASHELL view '" + std::string(path) + "': " +
            result.error()
        );
    }
    return {};
}

// context: Active Brookesia application runtime.
// updates: Named binding batch, applied by the correct document owner.
std::expected<void, std::string> App::set_binding_values(
    AppContext &context,
    const std::vector<esp_brookesia::gui::BindingValueUpdate> &updates
)
{
    const bool use_screen_document =
        has_active_screen_document();
    auto result = use_screen_document
        ? context.gui().set_binding_values(
              *active_screen_document_id_,
              updates
          )
        : context.gui().set_binding_values(updates);
    if (!result) {
        return std::unexpected(
            "Failed to update LUNASHELL bindings: " + result.error()
        );
    }
    return {};
}

// context: Active Brookesia application runtime.
// path: Stable view path receiving the progress value.
// value: Explicit percentage in the accepted zero-to-one-hundred range.
std::expected<void, std::string> App::set_value(
    AppContext &context,
    std::string_view path,
    int32_t value
)
{
    if (value < 0 || value > 100) {
        return std::unexpected(
            "Rejected out-of-range LUNASHELL value '" + std::string(path) +
            "': " + std::to_string(value)
        );
    }
    if (!has_active_screen_document()) {
        auto result = context.gui().set_value(path, value);
        if (!result) {
            return std::unexpected(
                "Failed to update LUNASHELL value '" + std::string(path) +
                "': " + result.error()
            );
        }
        return {};
    }
    return set_binding_values(
        context,
        {
            {
                .absolute_path = std::string(path),
                .key = "value",
                .value = std::to_string(value),
            },
        }
    );
}

// context: Active Brookesia application runtime.
// path: Stable view path whose visibility is changed.
// hidden: Requested visibility state, never a speculative cached value.
std::expected<void, std::string> App::set_hidden(
    AppContext &context,
    std::string_view path,
    bool hidden
)
{
    if (has_active_screen_document()) {
        return set_binding_values(
            context,
            {
                {
                    .absolute_path = std::string(path),
                    .key = "hidden",
                    .value = hidden ? "true" : "false",
                },
            }
        );
    }
    auto result = context.gui().set_binding_value(
        path,
        "hidden",
        hidden ? "true" : "false"
    );
    if (!result) {
        return std::unexpected(
            "Failed to update LUNASHELL visibility '" + std::string(path) +
            "': " + result.error()
        );
    }
    return {};
}

// context: Active Brookesia application runtime.
// path: Stable view path owning the binding.
// binding: Unique binding name in that view.
// value: Text to publish through the selected document owner.
std::expected<void, std::string> App::set_binding_text(
    AppContext &context,
    std::string_view path,
    std::string_view binding,
    std::string_view value
)
{
    if (has_active_screen_document()) {
        return set_binding_values(
            context,
            {
                {
                    .absolute_path = std::string(path),
                    .key = std::string(binding),
                    .value = std::string(value),
                },
            }
        );
    }
    auto result = context.gui().set_binding_value(
        path,
        binding,
        std::string(value)
    );
    if (!result) {
        return std::unexpected(
            "Failed to update LUNASHELL binding '" + std::string(path) + "/" +
            std::string(binding) + "': " + result.error()
        );
    }
    return {};
}

// context: Active Brookesia application runtime.
// path: Stable scroll-container path.
// x: Requested horizontal scroll offset.
// y: Requested vertical scroll offset.
// animated: Whether Brookesia should animate this explicit scroll request.
std::expected<void, std::string> App::scroll_to(
    AppContext &context,
    std::string_view path,
    int32_t x,
    int32_t y,
    bool animated
)
{
    return has_active_screen_document()
        ? context.gui().scroll_to(
              *active_screen_document_id_,
              path,
              x,
              y,
              animated
          )
        : context.gui().scroll_to(path, x, y, animated);
}

esp_brookesia::system::core::AppManifest App::get_manifest() const
{
    return {
        .id = std::string(kAppManifestId),
        .name = "ESP32 Luna Shell",
        .localized_names = {
            {"en", "ESP32 Luna Shell"},
            {"it", "ESP32 Luna Shell"},
        },
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

esp_brookesia::system::core::AppGuiDescriptor App::get_gui_descriptor() const
{
    return {
        .root_kind = esp_brookesia::system::core::GuiRootKind::File,
        .root = kGuiRoot,
        .resources = make_gui_resources(),
        .screen_flows = {
            {
                .screen_flow = kBackgroundFlow,
                .layer = esp_brookesia::system::core::GuiAppLayer::SystemBottom,
            },
            {
                .screen_flow = kScreenFlow,
                .layer = esp_brookesia::system::core::GuiAppLayer::AppDefault,
            },
            {
                .screen_flow = kChromeFlow,
                .layer = esp_brookesia::system::core::GuiAppLayer::AppTop,
            },
        },
    };
}

}  // namespace LUNASHELL
