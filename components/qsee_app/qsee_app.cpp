/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * QSee.ai: https://qsee.ai
 * Author website: https://umbertogiacobbi.biz
 * Contact: hello@qsee.ai
 */

#include "qsee_app.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "esp_log.h"
#include "esp_timer.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_logger.h"
#include "qsee_platform.h"
#include "qsee_snapshot_semantics.hpp"
#include "qsee_snapshot_cadence.hpp"

namespace {

using esp_brookesia::system::core::AppContext;
using qsee::snapshot_semantics::component_ready;
using qsee::snapshot_semantics::gps_fix_valid;
using qsee::snapshot_semantics::storage_cleanup_failed;
using qsee::snapshot_semantics::storage_format_required;
using qsee::snapshot_semantics::storage_mounted_ready;

constexpr char kTag[] = "qsee_app";
constexpr char kGuiRoot[] = "res/root.json";
constexpr char kBackgroundFlow[] = "qsee_background_flow";
constexpr char kScreenFlow[] = "qsee_content";
constexpr char kChromeFlow[] = "qsee_chrome_flow";
constexpr char kRefreshTimer[] = "qsee.refresh";
constexpr char kChromePolicyTimer[] = "qsee.chrome.policy";
constexpr char kPairingQrImageId[] = "qsee.pairing.qr";
constexpr char kBackgroundImageId[] = "qsee.background.asset";
constexpr char kBackgroundImagePath[] =
    "/qsee_background_screen/background/asset";
constexpr char kEspressifAppStoreUrl[] =
    "https://play.google.com/store/apps/details?id=com.espressif.provble";
constexpr int64_t kPairingTimeoutUs = 5LL * 60LL * 1000000LL;
constexpr size_t kLogBufferBytes = 4096;
constexpr double kAudioMeterFloorDbfs = -60.0;
constexpr double kPcm16FullScale = 32768.0;
// Fifteen worst-case 256-byte ring lines fit in the persistent 4 KB buffer.
constexpr size_t kLogSnapshotLines = 15;

const char *storage_cleanup_stage_name(
    qsee::StorageCleanupStage stage
)
{
    switch (stage) {
    case qsee::StorageCleanupStage::None:
        return "NONE";
    case qsee::StorageCleanupStage::FatUnmount:
        return "FAT UNMOUNT";
    case qsee::StorageCleanupStage::VfsUnregister:
        return "VFS UNREGISTER";
    case qsee::StorageCleanupStage::HostDeinit:
        return "HOST DEINIT";
    }
    ESP_LOGE(kTag, "Unknown storage cleanup stage %u",
             static_cast<unsigned>(stage));
    std::abort();
}

bool snapshot_available(const qsee::DeviceSnapshot &snapshot)
{
    return snapshot.sequence > 0 && snapshot.last_update_attempt_at_us > 0;
}

void format_full_snapshot_cadence(
    char *text,
    size_t text_size,
    uint32_t period_ms
)
{
    if (period_ms == 1000U) {
        std::snprintf(text, text_size, "1 Hz");
    } else if ((period_ms % 1000U) == 0U) {
        std::snprintf(
            text,
            text_size,
            "%lu s",
            static_cast<unsigned long>(period_ms / 1000U)
        );
    } else {
        std::snprintf(
            text,
            text_size,
            "%lu ms",
            static_cast<unsigned long>(period_ms)
        );
    }
}

// Convert one validated unsigned PCM16 peak to a finite dBFS floor.
double audio_peak_dbfs(uint32_t peak)
{
    if (peak == 0U) {
        return kAudioMeterFloorDbfs;
    }
    const double normalized = std::min(
        1.0,
        static_cast<double>(peak) / kPcm16FullScale
    );
    return 20.0 * std::log10(normalized);
}

// Preserve low-level microphone visibility on the logarithmic meter.
int audio_peak_meter_value(uint32_t peak)
{
    if (peak == 0U) {
        return 0;
    }
    const double dbfs = std::max(
        kAudioMeterFloorDbfs,
        audio_peak_dbfs(peak)
    );
    return static_cast<int>(
        ((dbfs - kAudioMeterFloorDbfs) * 100.0 /
         -kAudioMeterFloorDbfs) +
        0.5
    );
}

// Report a real peak in both full-scale percentage and dBFS.
void format_audio_peak(
    char *text,
    size_t text_size,
    unsigned channel,
    uint32_t peak
)
{
    const double percent_full_scale = std::min(
        100.0,
        static_cast<double>(peak) * 100.0 / kPcm16FullScale
    );
    if (peak == 0U) {
        std::snprintf(
            text,
            text_size,
            "PEAK CH%u  %.2f %%FS | -inf dBFS",
            channel,
            percent_full_scale
        );
        return;
    }
    std::snprintf(
        text,
        text_size,
        "PEAK CH%u  %.2f %%FS | %.1f dBFS",
        channel,
        percent_full_scale,
        audio_peak_dbfs(peak)
    );
}

const char *component_state(const qsee::ComponentSnapshot &component)
{
    if (component_ready(component)) {
        return "READY";
    }
    if (component.health == qsee::DeviceHealth::Missing) {
        return "UNAVAILABLE";
    }
    if (component.last_error != ESP_OK ||
        component.health == qsee::DeviceHealth::Degraded) {
        return "ERROR";
    }
    return component.data_stale ? "STALE" : "UNAVAILABLE";
}

const char *component_reason(const qsee::ComponentSnapshot &component)
{
    if (component.last_error != ESP_OK) {
        return esp_err_to_name(component.last_error);
    }
    if (component.data_stale) {
        return "STALE DATA HIDDEN";
    }
    return component.data_valid
               ? qsee::deviceHealthName(component.health)
               : "NO VALID DATA";
}

bool radio_deferred_by_policy(const qsee::ComponentSnapshot &radio)
{
    return radio.health == qsee::DeviceHealth::Unknown &&
           radio.last_error == ESP_ERR_INVALID_STATE &&
           !radio.data_valid &&
           !radio.data_stale &&
           radio.last_attempt_at_us == 0 &&
           radio.last_success_at_us == 0;
}

bool rtc_reading_valid(const qsee::RtcSnapshot &rtc)
{
    return component_ready(rtc) && rtc.clock_integrity &&
           rtc.year >= 2024 && rtc.year <= 2099 &&
           rtc.month >= 1 && rtc.month <= 12 &&
           rtc.day >= 1 && rtc.day <= 31 &&
           rtc.hour <= 23 && rtc.minute <= 59 && rtc.second <= 59;
}

template <size_t Size, typename... Args>
void append_format(std::array<char, Size> &output, const char *format, Args... args)
{
    const size_t used = std::strlen(output.data());
    if (used < output.size()) {
        std::snprintf(output.data() + used, output.size() - used, format, args...);
    }
}

template <size_t Size>
void format_component_status(
    std::array<char, Size> &output,
    const qsee::ComponentSnapshot &component
)
{
    if (component_ready(component)) {
        std::snprintf(output.data(), output.size(), "READY");
        return;
    }
    std::snprintf(
        output.data(),
        output.size(),
        "%s:%s",
        component_state(component),
        component_reason(component)
    );
}

bool log_line_matches(const char *line, const std::string &query, uint8_t filter_mode)
{
    const char *first = line;
    while (*first == ' ' || *first == '\t') {
        ++first;
    }
    if (filter_mode == 1 && *first != 'W' && *first != 'w') {
        return false;
    }
    if (filter_mode == 2 && *first != 'E' && *first != 'e') {
        return false;
    }
    if (filter_mode == 3 && *first != 'I' && *first != 'i') {
        return false;
    }
    return query.empty() || std::strstr(line, query.c_str()) != nullptr;
}

void filter_logs(
    const char *source,
    const std::string &query,
    uint8_t filter_mode,
    char *output,
    size_t output_size
)
{
    if (output == nullptr || output_size == 0) {
        return;
    }

    output[0] = '\0';
    size_t used = 0;
    const char *cursor = source != nullptr ? source : "";
    while (*cursor != '\0' && used + 1 < output_size) {
        const char *line_end = std::strchr(cursor, '\n');
        const size_t raw_length =
            line_end != nullptr ? static_cast<size_t>(line_end - cursor) : std::strlen(cursor);
        const size_t line_length = std::min(raw_length, size_t{255});
        std::array<char, 256> line{};
        std::memcpy(line.data(), cursor, line_length);
        line[line_length] = '\0';

        if (log_line_matches(line.data(), query, filter_mode)) {
            const size_t copy_length = std::min(line_length, output_size - used - 1);
            std::memcpy(output + used, line.data(), copy_length);
            used += copy_length;
            if (used + 1 < output_size) {
                output[used++] = '\n';
            }
            output[used] = '\0';
        }
        cursor = line_end != nullptr ? line_end + 1 : cursor + raw_length;
    }
}

std::expected<void, std::string> set_text(
    AppContext &context,
    std::string_view path,
    std::string_view text
)
{
    auto result = context.gui().set_text(path, text);
    if (!result) {
        return std::unexpected(
            "Failed to update QSee view '" + std::string(path) + "': " + result.error()
        );
    }
    return {};
}

std::expected<void, std::string> set_value(
    AppContext &context,
    std::string_view path,
    int32_t value
)
{
    if (value < 0 || value > 100) {
        return std::unexpected(
            "Rejected out-of-range QSee value '" + std::string(path) +
            "': " + std::to_string(value)
        );
    }
    auto result = context.gui().set_value(path, value);
    if (!result) {
        return std::unexpected(
            "Failed to update QSee value '" + std::string(path) + "': " + result.error()
        );
    }
    return {};
}

std::expected<void, std::string> set_hidden(
    AppContext &context,
    std::string_view path,
    bool hidden
)
{
    auto result = context.gui().set_binding_value(
        path,
        "hidden",
        hidden ? "true" : "false"
    );
    if (!result) {
        return std::unexpected(
            "Failed to update QSee visibility '" + std::string(path) +
            "': " + result.error()
        );
    }
    return {};
}

std::expected<void, std::string> set_binding_text(
    AppContext &context,
    std::string_view path,
    std::string_view binding,
    std::string_view value
)
{
    auto result = context.gui().set_binding_value(
        path,
        binding,
        std::string(value)
    );
    if (!result) {
        return std::unexpected(
            "Failed to update QSee binding '" + std::string(path) + "/" +
            std::string(binding) + "': " + result.error()
        );
    }
    return {};
}

}  // namespace

namespace qsee {

esp_brookesia::system::core::AppManifest App::get_manifest() const
{
    return {
        .id = std::string(kAppManifestId),
        .name = "QSee.ai",
        .localized_names = {
            {"en", "QSee.ai"},
            {"it", "QSee.ai"},
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
        .resources = {},
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

std::expected<void, std::string> App::on_start(AppContext &context)
{
    context_ = &context;
    status_bar_.invalidate();
    screen_ = Screen::Welcome;
    navigation_.reset(Screen::Welcome);
    logs_paused_ = false;
    logs_follow_latest_ = true;
    pairing_qr_registered_ = false;
    background_asset_registered_ = false;
    pairing_result_success_ = false;
    pairing_visual_state_.reset();
    pairing_started_at_us_ = 0;
    pairing_result_.clear();
    log_filter_mode_ = 0;
    diagnostic_activation_sample_at_us_ = 0;
    log_query_.clear();
    release_log_buffers();
    chrome_timing_connections_.clear();
    if (auto result = adaptive_chrome_.begin(); !result) {
        context_ = nullptr;
        return result;
    }

    const std::vector<std::string> actions = {
        "qsee.chrome.activity",
        "qsee.chrome.activity.logs",
        "qsee.chrome.activity.hardware",
        "qsee.chrome.activity.settings",
        "qsee.open.about",
        "qsee.command.0",
        "qsee.command.1",
        "qsee.command.2",
        "qsee.command.3",
        "qsee.command.4",
        "qsee.back",
        "qsee.next",
        "qsee.storage.format",
        "qsee.hardware.refresh",
        "qsee.settings.diagnostic.faster",
        "qsee.settings.diagnostic.slower",
        "qsee.settings.diagnostic.reset",
        "qsee.settings.imu_smoothing.less",
        "qsee.settings.imu_smoothing.more",
        "qsee.settings.storage.mirror.toggle",
        "qsee.settings.radios.start",
        "qsee.settings.display.brightness_down",
        "qsee.settings.display.brightness_up",
        "qsee.settings.display.rotation.toggle",
    };
    auto subscription_result = context.gui().subscribe_actions(actions);
    if (!subscription_result) {
        context_ = nullptr;
        return std::unexpected(
            "Failed to subscribe QSee UI actions: " + subscription_result.error()
        );
    }
    const std::vector<
        esp_brookesia::system::core::GuiActionHandlerSubscription
    > timing_subscriptions = {
        {
            .action = "qsee.settings.command_arc.changed",
            .handler = [this](const esp_brookesia::gui::Event &event) {
                handle_chrome_timing_event(
                    AdaptiveChromeTiming::CommandArc,
                    event
                );
            },
        },
        {
            .action = "qsee.settings.status_arc.changed",
            .handler = [this](const esp_brookesia::gui::Event &event) {
                handle_chrome_timing_event(
                    AdaptiveChromeTiming::StatusArc,
                    event
                );
            },
        },
        {
            .action = "qsee.settings.display_dim.changed",
            .handler = [this](const esp_brookesia::gui::Event &event) {
                handle_chrome_timing_event(
                    AdaptiveChromeTiming::DisplayDim,
                    event
                );
            },
        },
        {
            .action = "qsee.settings.display_off.changed",
            .handler = [this](const esp_brookesia::gui::Event &event) {
                handle_chrome_timing_event(
                    AdaptiveChromeTiming::DisplayOff,
                    event
                );
            },
        },
        {
            .action = "qsee.settings.full_snapshot.changed",
            .handler = [this](const esp_brookesia::gui::Event &event) {
                handle_full_snapshot_period_event(event);
            },
        },
    };
    chrome_timing_connections_ =
        context.gui().subscribe_actions(timing_subscriptions);
    const bool all_timing_connections_ready =
        chrome_timing_connections_.size() == timing_subscriptions.size() &&
        std::all_of(
            chrome_timing_connections_.begin(),
            chrome_timing_connections_.end(),
            [](const esp_brookesia::gui::ScopedConnection &connection) {
                return connection.connected();
            }
        );
    if (!all_timing_connections_ready) {
        chrome_timing_connections_.clear();
        context_ = nullptr;
        return std::unexpected(
            "Failed to subscribe Adaptive Chrome dropdown actions"
        );
    }
    if (auto result = configure_chrome(context); !result) {
        (void)adaptive_chrome_.shutdown(context.gui());
        chrome_timing_connections_.clear();
        context_ = nullptr;
        return result;
    }

    const esp_err_t monitor_result = qsee_hardware_set_diagnostic_monitor(false);
    if (monitor_result != ESP_OK) {
        (void)adaptive_chrome_.shutdown(context.gui());
        chrome_timing_connections_.clear();
        context_ = nullptr;
        return std::unexpected(
            "Failed to disable QSee diagnostic monitor at startup: " +
            std::string(esp_err_to_name(monitor_result))
        );
    }
    const esp_err_t motion_result =
        qsee_hardware_set_ui_motion_monitor(true);
    if (motion_result != ESP_OK) {
        (void)adaptive_chrome_.shutdown(context.gui());
        chrome_timing_connections_.clear();
        context_ = nullptr;
        return std::unexpected(
            "Failed to start Adaptive Chrome motion monitor: " +
            std::string(esp_err_to_name(motion_result))
        );
    }
    const esp_err_t full_snapshot_result =
        qsee_hardware_start_full_snapshot_monitor();
    if (full_snapshot_result != ESP_OK) {
        (void)qsee_hardware_set_ui_motion_monitor(false);
        (void)adaptive_chrome_.shutdown(context.gui());
        chrome_timing_connections_.clear();
        context_ = nullptr;
        return std::unexpected(
            "Failed to start Full Snapshot monitor: " +
            std::string(esp_err_to_name(full_snapshot_result))
        );
    }
    if (auto result = adaptive_chrome_.resume(context.gui()); !result) {
        (void)qsee_hardware_stop_full_snapshot_monitor();
        (void)qsee_hardware_set_ui_motion_monitor(false);
        (void)adaptive_chrome_.shutdown(context.gui());
        chrome_timing_connections_.clear();
        context_ = nullptr;
        return result;
    }

    auto timer_result = context.timer().start_periodic(kRefreshTimer, 1000);
    if (!timer_result) {
        (void)qsee_hardware_stop_full_snapshot_monitor();
        (void)qsee_hardware_set_ui_motion_monitor(false);
        (void)adaptive_chrome_.shutdown(context.gui());
        chrome_timing_connections_.clear();
        context_ = nullptr;
        return std::unexpected("Failed to start QSee refresh timer: " + timer_result.error());
    }
    refresh_timer_ = *timer_result;
    auto policy_timer_result = context.timer().start_periodic(
        kChromePolicyTimer,
        AdaptiveChromeController::kPolicyPeriodMs
    );
    if (!policy_timer_result) {
        if (!context.timer().stop(refresh_timer_)) {
            ESP_LOGE(kTag, "Failed to stop refresh timer after policy timer failure");
        }
        refresh_timer_ = esp_brookesia::system::core::INVALID_TIMER_ID;
        (void)qsee_hardware_stop_full_snapshot_monitor();
        (void)qsee_hardware_set_ui_motion_monitor(false);
        (void)adaptive_chrome_.shutdown(context.gui());
        chrome_timing_connections_.clear();
        context_ = nullptr;
        return std::unexpected(
            "Failed to start Adaptive Chrome policy timer: " +
            policy_timer_result.error()
        );
    }
    chrome_policy_timer_ = *policy_timer_result;

    auto refresh_result = refresh(context);
    if (!refresh_result) {
        if (!context.timer().stop(refresh_timer_)) {
            ESP_LOGE(kTag, "Failed to stop QSee refresh timer after startup failure");
        }
        refresh_timer_ = esp_brookesia::system::core::INVALID_TIMER_ID;
        if (!context.timer().stop(chrome_policy_timer_)) {
            ESP_LOGE(kTag, "Failed to stop policy timer after startup failure");
        }
        chrome_policy_timer_ =
            esp_brookesia::system::core::INVALID_TIMER_ID;
        (void)qsee_hardware_stop_full_snapshot_monitor();
        (void)qsee_hardware_set_ui_motion_monitor(false);
        (void)adaptive_chrome_.shutdown(context.gui());
        chrome_timing_connections_.clear();
        context_ = nullptr;
        return refresh_result;
    }

    ESP_LOGI(kTag, "QSee.ai native app started on Brookesia System 0.8");
    return {};
}

std::expected<void, std::string> App::on_pause(AppContext &context)
{
    std::string pause_error;
    if (auto result = adaptive_chrome_.shutdown(context.gui()); !result) {
        pause_error = result.error();
        ESP_LOGE(kTag, "%s", pause_error.c_str());
    }
    const esp_err_t diagnostic_result =
        qsee_hardware_set_diagnostic_monitor(false);
    if (diagnostic_result != ESP_OK) {
        const std::string error =
            "Failed to pause QSee diagnostic monitor: " +
            std::string(esp_err_to_name(diagnostic_result));
        ESP_LOGE(kTag, "%s", error.c_str());
        if (pause_error.empty()) {
            pause_error = error;
        }
    }
    const esp_err_t motion_result =
        qsee_hardware_set_ui_motion_monitor(false);
    if (motion_result != ESP_OK) {
        const std::string error =
            "Failed to pause Adaptive Chrome motion monitor: " +
            std::string(esp_err_to_name(motion_result));
        ESP_LOGE(kTag, "%s", error.c_str());
        if (pause_error.empty()) {
            pause_error = error;
        }
    }
    const esp_err_t full_snapshot_result =
        qsee_hardware_stop_full_snapshot_monitor();
    if (full_snapshot_result != ESP_OK) {
        const std::string error =
            "Failed to pause Full Snapshot monitor: " +
            std::string(esp_err_to_name(full_snapshot_result));
        ESP_LOGE(kTag, "%s", error.c_str());
        if (pause_error.empty()) {
            pause_error = error;
        }
    }
    if (!pause_error.empty()) {
        return std::unexpected(std::move(pause_error));
    }
    return {};
}

std::expected<void, std::string> App::on_resume(AppContext &context)
{
    const esp_err_t full_snapshot_result =
        qsee_hardware_start_full_snapshot_monitor();
    if (full_snapshot_result != ESP_OK) {
        return std::unexpected(
            "Failed to resume Full Snapshot monitor: " +
            std::string(esp_err_to_name(full_snapshot_result))
        );
    }
    const esp_err_t motion_result =
        qsee_hardware_set_ui_motion_monitor(true);
    if (motion_result != ESP_OK) {
        (void)qsee_hardware_stop_full_snapshot_monitor();
        return std::unexpected(
            "Failed to resume Adaptive Chrome motion monitor: " +
            std::string(esp_err_to_name(motion_result))
        );
    }
    if (auto result = apply_diagnostic_monitor(context, screen_); !result) {
        (void)qsee_hardware_set_diagnostic_monitor(false);
        (void)qsee_hardware_set_ui_motion_monitor(false);
        (void)qsee_hardware_stop_full_snapshot_monitor();
        return result;
    }
    if (auto result = configure_chrome(context); !result) {
        (void)qsee_hardware_set_diagnostic_monitor(false);
        (void)qsee_hardware_set_ui_motion_monitor(false);
        (void)qsee_hardware_stop_full_snapshot_monitor();
        return result;
    }
    if (auto result = adaptive_chrome_.resume(context.gui()); !result) {
        (void)qsee_hardware_set_diagnostic_monitor(false);
        (void)qsee_hardware_set_ui_motion_monitor(false);
        (void)qsee_hardware_stop_full_snapshot_monitor();
        return result;
    }
    if (auto result = refresh(context); !result) {
        (void)adaptive_chrome_.shutdown(context.gui());
        (void)qsee_hardware_set_diagnostic_monitor(false);
        (void)qsee_hardware_set_ui_motion_monitor(false);
        (void)qsee_hardware_stop_full_snapshot_monitor();
        return result;
    }
    return {};
}

std::expected<void, std::string> App::on_stop(AppContext &context)
{
    std::string stop_error;
    if (auto chrome_result = adaptive_chrome_.shutdown(context.gui());
        !chrome_result) {
        stop_error = chrome_result.error();
        ESP_LOGE(kTag, "%s", stop_error.c_str());
    }
    chrome_timing_connections_.clear();
    const esp_err_t monitor_result =
        qsee_hardware_set_diagnostic_monitor(false);
    if (monitor_result != ESP_OK) {
        const std::string error =
            "Failed to stop QSee diagnostic monitor: " +
            std::string(esp_err_to_name(monitor_result));
        ESP_LOGE(kTag, "%s", error.c_str());
        if (stop_error.empty()) {
            stop_error = error;
        }
    }
    const esp_err_t motion_result =
        qsee_hardware_set_ui_motion_monitor(false);
    if (motion_result != ESP_OK) {
        const std::string error =
            "Failed to stop Adaptive Chrome motion monitor: " +
            std::string(esp_err_to_name(motion_result));
        ESP_LOGE(kTag, "%s", error.c_str());
        if (stop_error.empty()) {
            stop_error = error;
        }
    }
    const esp_err_t full_snapshot_result =
        qsee_hardware_stop_full_snapshot_monitor();
    if (full_snapshot_result != ESP_OK) {
        const std::string error =
            "Failed to stop Full Snapshot monitor: " +
            std::string(esp_err_to_name(full_snapshot_result));
        ESP_LOGE(kTag, "%s", error.c_str());
        if (stop_error.empty()) {
            stop_error = error;
        }
    }
    if (refresh_timer_ != esp_brookesia::system::core::INVALID_TIMER_ID) {
        if (!context.timer().stop(refresh_timer_)) {
            ESP_LOGE(kTag, "Failed to stop QSee refresh timer");
            if (stop_error.empty()) {
                stop_error = "Failed to stop QSee refresh timer";
            }
        }
        refresh_timer_ = esp_brookesia::system::core::INVALID_TIMER_ID;
    }
    if (chrome_policy_timer_ !=
        esp_brookesia::system::core::INVALID_TIMER_ID) {
        if (!context.timer().stop(chrome_policy_timer_)) {
            ESP_LOGE(kTag, "Failed to stop Adaptive Chrome policy timer");
            if (stop_error.empty()) {
                stop_error =
                    "Failed to stop Adaptive Chrome policy timer";
            }
        }
        chrome_policy_timer_ =
            esp_brookesia::system::core::INVALID_TIMER_ID;
    }
    if (search_keyboard_ != esp_brookesia::system::core::INVALID_KEYBOARD_REQUEST_ID) {
        auto hide_result = context.system_service().hide_keyboard(search_keyboard_);
        if (!hide_result) {
            ESP_LOGE(kTag, "Failed to hide QSee keyboard: %s", hide_result.error().c_str());
            if (stop_error.empty()) {
                stop_error = "Failed to hide QSee keyboard: " + hide_result.error();
            }
        }
        search_keyboard_ = esp_brookesia::system::core::INVALID_KEYBOARD_REQUEST_ID;
    }
    if (auto hide_result = alert_service_.hide_all(context); !hide_result) {
        ESP_LOGE(kTag, "Failed to hide QSee dialogs: %s", hide_result.error().c_str());
        if (stop_error.empty()) {
            stop_error = "Failed to hide QSee dialogs: " + hide_result.error();
        }
    }
    if (qsee_provisioning_active()) {
        const esp_err_t provisioning_result = qsee_provisioning_stop();
        if (provisioning_result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Failed to stop active provisioning: %s",
                esp_err_to_name(provisioning_result)
            );
            if (stop_error.empty()) {
                stop_error =
                    "Failed to stop active provisioning: " +
                    std::string(esp_err_to_name(provisioning_result));
            }
        }
    }
    if (auto release_result = release_pairing_qr(context); !release_result) {
        ESP_LOGE(kTag, "%s", release_result.error().c_str());
        if (stop_error.empty()) {
            stop_error = std::move(release_result.error());
        }
    }
    if (background_asset_registered_ &&
        !context.gui().unregister_image(kBackgroundImageId)) {
        ESP_LOGE(kTag, "Failed to unregister QSee background image");
        if (stop_error.empty()) {
            stop_error = "Failed to unregister QSee background image";
        }
    }
    background_asset_registered_ = false;
    release_log_buffers();
    context_ = nullptr;
    ESP_LOGI(kTag, "QSee.ai native app stopped");
    if (!stop_error.empty()) {
        return std::unexpected(std::move(stop_error));
    }
    return {};
}

std::expected<void, std::string> App::apply_diagnostic_monitor(
    AppContext &context,
    Screen screen
)
{
    const bool enabled =
        screen == Screen::Audio ||
        screen == Screen::AudioFft ||
        screen == Screen::AudioDetail ||
        screen == Screen::Imu;
    if (enabled) {
        const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
        diagnostic_activation_sample_at_us_ =
            screen == Screen::Audio ||
                screen == Screen::AudioFft ||
                screen == Screen::AudioDetail
                ? snapshot.audio.microphone.last_success_at_us
                : snapshot.imu.last_success_at_us;
    }
    const esp_err_t result = qsee_hardware_set_diagnostic_monitor(enabled);
    if (result == ESP_OK) {
        return {};
    }

    std::array<char, 128> details{};
    std::snprintf(
        details.data(),
        details.size(),
        "The diagnostic sampler could not be %s: %s",
        enabled ? "started" : "stopped",
        esp_err_to_name(result)
    );
    if (auto dialog_result = show_error_dialog(
            context,
            "Diagnostic monitor unavailable",
            details.data()
        ); !dialog_result) {
        ESP_LOGE(kTag, "%s", dialog_result.error().c_str());
        return std::unexpected(
            std::string(details.data()) +
            "; error dialog failed: " + dialog_result.error()
        );
    }
    return std::unexpected(details.data());
}

// Publish only route intent; the persistent component owns layout and colors.
std::expected<void, std::string> App::configure_chrome(
    AppContext &context
)
{
    using palette::ActionRole;
    static constexpr std::array<CommandSpec, 0> kNoCommands{};
    static constexpr std::array<CommandSpec, 5> kWelcomeCommands = {{
        {"\xef\x80\x8b", "qsee.open.logs", ActionRole::Primary, false},
        {"\xef\x80\x81", "qsee.open.audio", ActionRole::Primary, false},
        {"\xef\x80\x9c", "qsee.open.hardware", ActionRole::Success, true},
        {"\xef\x80\xa1", "qsee.open.imu", ActionRole::Success, false},
        {"\xef\x80\x93", "qsee.open.settings", ActionRole::Primary, false},
    }};
    const std::array<CommandSpec, 5> log_commands = {{
        {
            logs_paused_ ? "\xef\x81\x8b" : "\xef\x81\x8c",
            "qsee.logs.pause",
            ActionRole::Success,
            false,
        },
        {"\xef\x8b\xad", "qsee.logs.clear", ActionRole::Danger, false},
        {
            "\xef\x81\xb9",
            "qsee.logs.follow",
            logs_follow_latest_
                ? ActionRole::Success
                : ActionRole::Primary,
            true,
        },
        {
            "\xef\x82\xb0",
            "qsee.logs.filter",
            log_filter_mode_ == 0
                ? ActionRole::Primary
                : ActionRole::Warning,
            false,
        },
        {"\xef\x80\x82", "qsee.logs.search", ActionRole::Primary, false},
    }};
    static constexpr std::array<CommandSpec, 1> kAudioCommands = {{
        {"\xef\x82\x80", "qsee.open.audio_fft", ActionRole::Success, true},
    }};
    static constexpr std::array<CommandSpec, 1> kAudioFftCommands = {{
        {"\xef\x81\xa6", "qsee.open.audio_detail", ActionRole::Success, true},
    }};
    static constexpr std::array<CommandSpec, 1> kSettingsCommands = {{
        {"\xef\x8a\x93", "qsee.open.pairing", ActionRole::Success, true},
    }};

    auto activate_result = [&]() -> std::expected<void, std::string> {
        switch (screen_) {
        case Screen::Welcome:
            return adaptive_chrome_.configure_commands(
                context.gui(),
                kWelcomeCommands
            );
        case Screen::Logs:
            return adaptive_chrome_.configure_commands(
                context.gui(),
                log_commands
            );
        case Screen::Audio:
            return adaptive_chrome_.configure_commands(
                context.gui(),
                kAudioCommands
            );
        case Screen::AudioFft:
            return adaptive_chrome_.configure_commands(
                context.gui(),
                kAudioFftCommands
            );
        case Screen::Settings:
            return adaptive_chrome_.configure_commands(
                context.gui(),
                kSettingsCommands
            );
        case Screen::Hardware:
        case Screen::AudioDetail:
        case Screen::Imu:
        case Screen::About:
        case Screen::PairIntro:
        case Screen::PairBluetooth:
        case Screen::PairApp:
        case Screen::AppStoreQr:
        case Screen::PairQr:
        case Screen::PairResult:
            return adaptive_chrome_.configure_commands(
                context.gui(),
                kNoCommands
            );
        }
        return std::unexpected("Unknown QSee route for adaptive chrome");
    }();
    if (!activate_result) {
        return activate_result;
    }

    return edge_navigation_.configure(
        context.gui(),
        navigation_.can_back(),
        is_pairing_screen(screen_) && screen_ != Screen::PairQr
    );
}

std::expected<void, std::string> App::open_screen(
    AppContext &context,
    Screen screen,
    std::string_view action,
    NavigationMode mode
)
{
    const bool leaving_pairing = is_pairing_screen(screen_) &&
                                 !is_pairing_screen(screen);
    const bool leaving_pairing_qr = screen != Screen::PairQr &&
                                    screen != Screen::AppStoreQr;
    const bool release_live_log_buffers = screen_ == Screen::Logs &&
                                          screen != Screen::Logs &&
                                          !logs_paused_;
    if (leaving_pairing &&
        qsee_provisioning_active()) {
        const esp_err_t stop_result = qsee_provisioning_stop();
        if (stop_result != ESP_OK) {
            return std::unexpected(
                "Failed to leave pairing while provisioning is active: " +
                std::string(esp_err_to_name(stop_result))
            );
        }
        pairing_started_at_us_ = 0;
    }
    auto result = context.gui().trigger_screen_flow(kScreenFlow, action);
    if (!result) {
        return std::unexpected("QSee navigation failed: " + result.error());
    }
    if (release_live_log_buffers) {
        release_log_buffers();
    }
    if (leaving_pairing_qr) {
        if (auto release_result = release_pairing_qr(context); !release_result) {
            return release_result;
        }
    }

    bool history_updated = true;
    switch (mode) {
    case NavigationMode::Push:
        navigation_.navigate(screen);
        break;
    case NavigationMode::Back:
        history_updated = navigation_.commit_back(screen);
        break;
    case NavigationMode::Forward:
        history_updated = navigation_.commit_forward(screen);
        break;
    case NavigationMode::Replace:
        navigation_.replace(screen);
        break;
    case NavigationMode::Clear:
        navigation_.reset(screen);
        break;
    case NavigationMode::ReturnTo:
        history_updated = navigation_.return_to(screen);
        break;
    }
    if (!history_updated) {
        return std::unexpected(
            "QSee route changed but navigation history rejected the transition"
        );
    }
    screen_ = screen;
    pairing_visual_state_.reset();
    if (auto chrome_result = configure_chrome(context); !chrome_result) {
        return chrome_result;
    }
    if (auto monitor_result = apply_diagnostic_monitor(context, screen); !monitor_result) {
        return monitor_result;
    }
    return refresh(context);
}

std::expected<void, std::string> App::on_action(
    AppContext &context,
    std::string_view action
)
{
    if (auto result = adaptive_chrome_.note_activity(context.gui()); !result) {
        return result;
    }
    if (const auto resolved =
            adaptive_chrome_.resolve_action(action);
        resolved.has_value()) {
        action = *resolved;
    }
    if (action == "qsee.chrome.activity" ||
        action == "qsee.chrome.activity.logs" ||
        action == "qsee.chrome.activity.hardware" ||
        action == "qsee.chrome.activity.settings") {
        return {};
    }
    if (action == "qsee.open.welcome") {
        return open_screen(
            context,
            Screen::Welcome,
            action,
            NavigationMode::Clear
        );
    }
    if (action == "qsee.open.logs") {
        return open_screen(context, Screen::Logs, action);
    }
    if (action == "qsee.open.hardware") {
        return open_screen(context, Screen::Hardware, action);
    }
    if (action == "qsee.hardware.refresh") {
        return refresh_hardware(context);
    }
    if (action == "qsee.open.audio") {
        return open_screen(context, Screen::Audio, action);
    }
    if (action == "qsee.open.audio_fft") {
        return open_screen(context, Screen::AudioFft, action);
    }
    if (action == "qsee.open.audio_detail") {
        return open_screen(context, Screen::AudioDetail, action);
    }
    if (action == "qsee.open.imu") {
        return open_screen(context, Screen::Imu, action);
    }
    if (action == "qsee.open.settings") {
        return open_screen(context, Screen::Settings, action);
    }
    if (action == "qsee.open.about") {
        return open_screen(context, Screen::About, action);
    }
    if (action == "qsee.open.pairing") {
        return open_screen(
            context,
            Screen::PairIntro,
            "qsee.open.pair_intro"
        );
    }
    if (action == "qsee.next") {
        return pairing_next(context);
    }
    if (action == "qsee.back") {
        return navigate_back(context);
    }
    if (action == "qsee.logs.pause") {
        logs_paused_ = !logs_paused_;
        if (auto result = configure_chrome(context); !result) {
            return result;
        }
        return refresh_logs(context);
    }
    if (action == "qsee.logs.clear") {
        const esp_err_t result = qsee_logger_clear();
        if (result != ESP_OK) {
            std::array<char, 128> details{};
            std::snprintf(
                details.data(),
                details.size(),
                "Canonical logger clear failed: %s",
                esp_err_to_name(result)
            );
            ESP_LOGE(kTag, "%s", details.data());
            if (auto dialog_result = show_error_dialog(
                    context,
                    "Logs not cleared",
                    details.data()
                ); !dialog_result) {
                return dialog_result;
            }
            return std::unexpected(details.data());
        }
        return refresh_logs(context);
    }
    if (action == "qsee.logs.follow") {
        logs_follow_latest_ = !logs_follow_latest_;
        if (auto result = configure_chrome(context); !result) {
            return result;
        }
        return refresh_logs(context);
    }
    if (action == "qsee.logs.filter") {
        log_filter_mode_ = static_cast<uint8_t>((log_filter_mode_ + 1U) % 4U);
        if (auto result = configure_chrome(context); !result) {
            return result;
        }
        return refresh_logs(context);
    }
    if (action == "qsee.logs.search") {
        return request_log_search(context);
    }
    if (action == "qsee.storage.format") {
        return request_sd_format(context);
    }

    const auto finish_settings_command =
        [this, &context](esp_err_t result, std::string_view operation)
        -> std::expected<void, std::string> {
        if (result != ESP_OK) {
            std::array<char, 160> details{};
            std::snprintf(
                details.data(),
                details.size(),
                "%.*s failed: %s",
                static_cast<int>(operation.size()),
                operation.data(),
                esp_err_to_name(result)
            );
            ESP_LOGE(kTag, "%s", details.data());
            if (auto dialog_result = show_error_dialog(
                    context,
                    "Setting not applied",
                    details.data()
                ); !dialog_result) {
                return std::unexpected(
                    std::string(details.data()) +
                    "; error dialog failed: " + dialog_result.error()
                );
            }
            if (auto refresh_result = refresh_settings(context); !refresh_result) {
                ESP_LOGE(
                    kTag,
                    "Settings view refresh after failure failed: %s",
                    refresh_result.error().c_str()
                );
                return std::unexpected(
                    std::string(details.data()) +
                    "; settings refresh failed: " + refresh_result.error()
                );
            }
            return std::unexpected(details.data());
        }
        return refresh_settings(context);
    };

    if (action == "qsee.settings.diagnostic.faster" ||
        action == "qsee.settings.diagnostic.slower" ||
        action == "qsee.settings.diagnostic.reset") {
        const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
        if (action != "qsee.settings.diagnostic.reset" &&
            !component_ready(snapshot.diagnostics.sample_period)) {
            const esp_err_t error =
                snapshot.diagnostics.sample_period.last_error != ESP_OK
                    ? snapshot.diagnostics.sample_period.last_error
                    : ESP_ERR_INVALID_STATE;
            return finish_settings_command(
                error,
                "Diagnostic sample interval"
            );
        }
        const uint32_t current_period = snapshot.diagnostics.sample_period_ms;
        if (action != "qsee.settings.diagnostic.reset" &&
            (current_period < 100U || current_period > 2000U)) {
            ESP_LOGE(
                kTag,
                "Diagnostic sample interval rejected: current=%lu error=%s",
                static_cast<unsigned long>(current_period),
                esp_err_to_name(ESP_ERR_INVALID_STATE)
            );
            return finish_settings_command(
                ESP_ERR_INVALID_STATE,
                "Diagnostic sample interval"
            );
        }
        const int current = static_cast<int>(current_period);
        const int requested = action == "qsee.settings.diagnostic.reset"
            ? 300
            : current +
                  (action == "qsee.settings.diagnostic.faster" ? -100 : 100);
        if (requested < 100 || requested > 2000) {
            ESP_LOGE(
                kTag,
                "Diagnostic sample interval request rejected: "
                "current=%d requested=%d",
                current,
                requested
            );
            return finish_settings_command(
                ESP_ERR_INVALID_ARG,
                "Diagnostic sample interval"
            );
        }
        return finish_settings_command(
            qsee_hardware_set_diagnostic_period_ms(static_cast<uint32_t>(requested)),
            "Diagnostic sample interval"
        );
    }
    if (action == "qsee.settings.imu_smoothing.less" ||
        action == "qsee.settings.imu_smoothing.more") {
        const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
        if (!component_ready(snapshot.diagnostics.imu_smoothing)) {
            const esp_err_t error =
                snapshot.diagnostics.imu_smoothing.last_error != ESP_OK
                    ? snapshot.diagnostics.imu_smoothing.last_error
                    : ESP_ERR_INVALID_STATE;
            return finish_settings_command(
                error,
                "IMU input smoothing"
            );
        }
        if (snapshot.diagnostics.imu_smoothing_percent > 90U) {
            ESP_LOGE(
                kTag,
                "IMU smoothing adjustment rejected: current=%lu error=%s",
                static_cast<unsigned long>(
                    snapshot.diagnostics.imu_smoothing_percent
                ),
                esp_err_to_name(ESP_ERR_INVALID_STATE)
            );
            return finish_settings_command(
                ESP_ERR_INVALID_STATE,
                "IMU input smoothing"
            );
        }
        const int requested =
            static_cast<int>(snapshot.diagnostics.imu_smoothing_percent) +
            (action == "qsee.settings.imu_smoothing.less" ? -10 : 10);
        if (requested < 0 || requested > 90) {
            ESP_LOGE(
                kTag,
                "IMU smoothing request rejected: current=%lu requested=%d",
                static_cast<unsigned long>(
                    snapshot.diagnostics.imu_smoothing_percent
                ),
                requested
            );
            return finish_settings_command(
                ESP_ERR_INVALID_ARG,
                "IMU input smoothing"
            );
        }
        return finish_settings_command(
            qsee_hardware_set_imu_smoothing_percent(static_cast<uint32_t>(requested)),
            "IMU input smoothing"
        );
    }
    if (action == "qsee.settings.storage.mirror.toggle") {
        const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
        if (!component_ready(snapshot.storage.logger_mirror)) {
            const esp_err_t error =
                snapshot.storage.logger_mirror.last_error != ESP_OK
                    ? snapshot.storage.logger_mirror.last_error
                    : ESP_ERR_INVALID_STATE;
            return finish_settings_command(error, "SD log mirror");
        }
        return finish_settings_command(
            qsee_storage_set_logger_mirror(!snapshot.storage.logger_mirror_requested),
            "SD log mirror"
        );
    }
    if (action == "qsee.settings.radios.start") {
        esp_err_t result = qsee_radios_init();
        if (result == ESP_OK && !qsee_provisioning_active()) {
            result = qsee_wifi_start_scan();
        }
        return finish_settings_command(result, "Radio startup");
    }
    if (action == "qsee.settings.display.rotation.toggle") {
        const DeviceSnapshot snapshot =
            DeviceSnapshotService::instance().snapshot();
        if (!component_ready(snapshot.ui.orientation)) {
            const esp_err_t error =
                snapshot.ui.orientation.last_error != ESP_OK
                    ? snapshot.ui.orientation.last_error
                    : ESP_ERR_INVALID_STATE;
            return finish_settings_command(error, "Automatic rotation");
        }
        return finish_settings_command(
            qsee_display_set_auto_rotation_enabled(
                !snapshot.ui.orientation.auto_rotation_enabled
            ),
            "Automatic rotation"
        );
    }
    if (action == "qsee.settings.display.brightness_down" ||
        action == "qsee.settings.display.brightness_up") {
        const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
        if (!component_ready(snapshot.ui.brightness) ||
            !component_ready(snapshot.ui.display)) {
            const ComponentSnapshot &failed_component =
                !component_ready(snapshot.ui.brightness)
                    ? snapshot.ui.brightness
                    : snapshot.ui.display;
            const esp_err_t error = failed_component.last_error != ESP_OK
                ? failed_component.last_error
                : ESP_ERR_INVALID_STATE;
            return finish_settings_command(error, "AMOLED brightness");
        }
        if (snapshot.ui.brightness_percent < 10U ||
            snapshot.ui.brightness_percent > 100U) {
            ESP_LOGE(
                kTag,
                "AMOLED brightness adjustment rejected: current=%lu error=%s",
                static_cast<unsigned long>(snapshot.ui.brightness_percent),
                esp_err_to_name(ESP_ERR_INVALID_STATE)
            );
            return finish_settings_command(ESP_ERR_INVALID_STATE, "AMOLED brightness");
        }
        const int requested =
            static_cast<int>(snapshot.ui.brightness_percent) +
            (action == "qsee.settings.display.brightness_down" ? -10 : 10);
        if (requested < 10 || requested > 100) {
            ESP_LOGE(
                kTag,
                "AMOLED brightness request rejected: current=%lu requested=%d",
                static_cast<unsigned long>(snapshot.ui.brightness_percent),
                requested
            );
            return finish_settings_command(
                ESP_ERR_INVALID_ARG,
                "AMOLED brightness"
            );
        }
        return finish_settings_command(
            qsee_display_set_brightness_percent(static_cast<uint32_t>(requested)),
            "AMOLED brightness"
        );
    }
    return {};
}

void App::handle_chrome_timing_event(
    AdaptiveChromeTiming timing,
    const esp_brookesia::gui::Event &event
)
{
    if (context_ == nullptr) {
        ESP_LOGE(kTag, "Adaptive Chrome dropdown event arrived without app context");
        return;
    }
    if (auto activity_result =
            adaptive_chrome_.note_activity(context_->gui());
        !activity_result) {
        ESP_LOGE(kTag, "%s", activity_result.error().c_str());
        return;
    }

    const auto selected_index = event.get_int64("selectedIndex");
    if (!selected_index.has_value() || *selected_index < 0) {
        ESP_LOGE(
            kTag,
            "Adaptive Chrome dropdown omitted a valid selectedIndex"
        );
        return;
    }

    std::span<const uint32_t> options;
    switch (timing) {
    case AdaptiveChromeTiming::CommandArc:
    case AdaptiveChromeTiming::StatusArc:
        options = AdaptiveChromeController::kArcTimeouts;
        break;
    case AdaptiveChromeTiming::DisplayDim:
        options = AdaptiveChromeController::kDimTimeouts;
        break;
    case AdaptiveChromeTiming::DisplayOff:
        options = AdaptiveChromeController::kOffDelays;
        break;
    }
    const size_t index = static_cast<size_t>(*selected_index);
    if (index >= options.size()) {
        ESP_LOGE(
            kTag,
            "Adaptive Chrome dropdown index out of range: %lu",
            static_cast<unsigned long>(index)
        );
        return;
    }

    const esp_err_t result =
        adaptive_chrome_.set_timeout_seconds(timing, options[index]);
    if (result != ESP_OK) {
        const std::string details =
            "Adaptive Chrome timing was not saved: " +
            std::string(esp_err_to_name(result));
        ESP_LOGE(kTag, "%s", details.c_str());
        if (auto dialog_result = show_error_dialog(
                *context_,
                "Setting not applied",
                details
            ); !dialog_result) {
            ESP_LOGE(kTag, "%s", dialog_result.error().c_str());
        }
        return;
    }
    if (screen_ == Screen::Settings) {
        if (auto refresh_result = refresh_settings(*context_);
            !refresh_result) {
            ESP_LOGE(kTag, "%s", refresh_result.error().c_str());
        }
    }
}

void App::handle_full_snapshot_period_event(
    const esp_brookesia::gui::Event &event
)
{
    if (context_ == nullptr) {
        ESP_LOGE(
            kTag,
            "Full Snapshot dropdown event arrived without app context"
        );
        return;
    }
    if (auto activity_result =
            adaptive_chrome_.note_activity(context_->gui());
        !activity_result) {
        ESP_LOGE(kTag, "%s", activity_result.error().c_str());
        return;
    }

    const auto selected_index = event.get_int64("selectedIndex");
    if (!selected_index.has_value() || *selected_index < 0) {
        ESP_LOGE(
            kTag,
            "Full Snapshot dropdown omitted a valid selectedIndex"
        );
        return;
    }

    const size_t index = static_cast<size_t>(*selected_index);
    if (index >= kFullSnapshotPeriodsMs.size()) {
        ESP_LOGE(
            kTag,
            "Full Snapshot dropdown index out of range: %lu",
            static_cast<unsigned long>(index)
        );
        return;
    }

    const esp_err_t result = qsee_hardware_set_full_snapshot_period_ms(
        kFullSnapshotPeriodsMs[index]
    );
    if (result != ESP_OK) {
        const std::string details =
            "Full Snapshot cadence was not saved: " +
            std::string(esp_err_to_name(result));
        ESP_LOGE(kTag, "%s", details.c_str());
        if (auto dialog_result = show_error_dialog(
                *context_,
                "Setting not applied",
                details
            ); !dialog_result) {
            ESP_LOGE(kTag, "%s", dialog_result.error().c_str());
        }
        return;
    }

    if (screen_ == Screen::Settings) {
        if (auto refresh_result = refresh_settings(*context_);
            !refresh_result) {
            ESP_LOGE(kTag, "%s", refresh_result.error().c_str());
        }
    }
}

std::expected<void, std::string> App::on_timer(
    AppContext &context,
    esp_brookesia::system::core::TimerId timer_id,
    std::string_view name
)
{
    if (timer_id == chrome_policy_timer_ &&
        name == kChromePolicyTimer) {
        const DisplayIdleStage previous_stage =
            adaptive_chrome_.display_idle_stage();
        const DeviceSnapshot snapshot =
            DeviceSnapshotService::instance().snapshot();
        if (auto result =
                adaptive_chrome_.update(context.gui(), snapshot);
            !result) {
            return result;
        }
        if (previous_stage == DisplayIdleStage::Off &&
            adaptive_chrome_.display_idle_stage() ==
                DisplayIdleStage::Active) {
            return refresh(context);
        }
        return {};
    }
    if (timer_id == refresh_timer_ && name == kRefreshTimer) {
        if (adaptive_chrome_.display_idle_stage() ==
            DisplayIdleStage::Off) {
            return {};
        }
        return refresh(context);
    }
    return {};
}

// Refresh the reusable curved chrome from the same canonical device state.
std::expected<void, std::string> App::refresh_chrome(
    AppContext &context,
    const DeviceSnapshot &snapshot
)
{
    return status_bar_.update(context.gui(), snapshot);
}

std::expected<void, std::string> App::refresh(AppContext &context)
{
    if (auto result = refresh_background(context); !result) {
        return result;
    }
    switch (screen_) {
    case Screen::Welcome:
        return refresh_welcome(context);
    case Screen::Logs:
        return refresh_logs(context);
    case Screen::Hardware:
        return refresh_hardware(context);
    case Screen::Audio:
        return refresh_audio(context);
    case Screen::AudioFft:
        return refresh_audio_fft(context);
    case Screen::AudioDetail:
        return refresh_audio_detail(context);
    case Screen::Imu:
        return refresh_imu(context);
    case Screen::Settings:
        return refresh_settings(context);
    case Screen::PairIntro:
    case Screen::PairBluetooth:
    case Screen::PairApp:
    case Screen::AppStoreQr:
    case Screen::PairQr:
    case Screen::PairResult:
        return refresh_pairing(context);
    case Screen::About: {
        const DeviceSnapshot snapshot =
            DeviceSnapshotService::instance().snapshot();
        return refresh_chrome(context, snapshot);
    }
    }
    return {};
}

std::expected<void, std::string> App::refresh_welcome(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }
    std::array<char, 256> text{};

    const bool heap_valid =
        component_ready(snapshot.memory) && snapshot.memory.heap_free_bytes > 0 &&
        snapshot.memory.heap_minimum_free_bytes > 0 &&
        snapshot.memory.heap_minimum_free_bytes <= snapshot.memory.heap_free_bytes;
    const bool psram_valid =
        component_ready(snapshot.memory) &&
        snapshot.memory.psram_heap_total_bytes > 0 &&
        snapshot.memory.psram_free_bytes <= snapshot.memory.psram_heap_total_bytes;
    int ram_percent = 0;
    bool ram_percent_valid = false;
    if (heap_valid && psram_valid) {
        std::snprintf(
            text.data(),
            text.size(),
            "HEAP %lu KB free | PSRAM %lu / %lu KB free",
            static_cast<unsigned long>(snapshot.memory.heap_free_bytes / 1024U),
            static_cast<unsigned long>(snapshot.memory.psram_free_bytes / 1024U),
            static_cast<unsigned long>(snapshot.memory.psram_heap_total_bytes / 1024U)
        );
        ram_percent = static_cast<int>(
            (static_cast<uint64_t>(snapshot.memory.psram_free_bytes) * 100U) /
            snapshot.memory.psram_heap_total_bytes
        );
        ram_percent_valid = true;
    } else if (heap_valid) {
        std::snprintf(
            text.data(),
            text.size(),
            "HEAP %lu KB free | PSRAM UNAVAILABLE",
            static_cast<unsigned long>(snapshot.memory.heap_free_bytes / 1024U)
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "RAM %s (%s)",
            component_ready(snapshot.memory)
                ? "ERROR"
                : component_state(snapshot.memory),
            component_ready(snapshot.memory)
                ? "INVALID MEMORY SAMPLE"
                : component_reason(snapshot.memory)
        );
    }
    if (auto result = set_text(
            context,
            "/qsee_welcome/surface/ram/ram_value",
            text.data()
        ); !result) {
        return result;
    }
    if (ram_percent_valid) {
        if (auto result = set_value(
                context,
                "/qsee_welcome/surface/ram/ram_bar",
                ram_percent
            ); !result) {
            return result;
        }
    }
    if (auto result = set_hidden(
            context,
            "/qsee_welcome/surface/ram/ram_bar",
            !ram_percent_valid
        ); !result) {
        return result;
    }

    int storage_percent = 0;
    bool storage_percent_valid = false;
    if (storage_cleanup_failed(snapshot.storage)) {
        std::snprintf(
            text.data(),
            text.size(),
            "SD TERMINAL (CLEANUP %s; %s; poisoned=%s)",
            storage_cleanup_stage_name(snapshot.storage.cleanup_stage),
            esp_err_to_name(snapshot.storage.cleanup_error),
            snapshot.storage.poisoned ? "yes" : "no"
        );
    } else if (storage_format_required(snapshot.storage)) {
        std::snprintf(
            text.data(),
            text.size(),
            "SD FORMAT REQUIRED (%s)",
            component_reason(snapshot.storage)
        );
    } else if (storage_mounted_ready(snapshot.storage) &&
        snapshot.storage.filesystem_total_bytes > 0 &&
        snapshot.storage.free_bytes <= snapshot.storage.filesystem_total_bytes) {
        storage_percent = static_cast<int>(
            (snapshot.storage.free_bytes * 100ULL) / snapshot.storage.filesystem_total_bytes
        );
        storage_percent_valid = true;
        std::snprintf(
            text.data(),
            text.size(),
            "SD %llu / %llu MB free%s",
            static_cast<unsigned long long>(snapshot.storage.free_bytes / (1024ULL * 1024ULL)),
            static_cast<unsigned long long>(
                snapshot.storage.filesystem_total_bytes / (1024ULL * 1024ULL)
            ),
            component_ready(snapshot.storage.logger_mirror) &&
                    snapshot.storage.logger_mirror_enabled
                ? " | LOG"
                : ""
        );
    } else if (storage_mounted_ready(snapshot.storage)) {
        std::snprintf(text.data(), text.size(), "SD ERROR (INVALID CAPACITY SAMPLE)");
    } else if (!snapshot.storage.card_response_verified) {
        std::snprintf(
            text.data(),
            text.size(),
            "SD UNAVAILABLE (CARD/TRANSPORT UNCONFIRMED; %s)",
            component_reason(snapshot.storage)
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "SD %s (%s)",
            component_state(snapshot.storage),
            component_reason(snapshot.storage)
        );
    }
    if (auto result = set_text(
            context,
            "/qsee_welcome/surface/sd/sd_value",
            text.data()
        ); !result) {
        return result;
    }
    if (storage_percent_valid) {
        if (auto result = set_value(
                context,
                "/qsee_welcome/surface/sd/sd_bar",
                storage_percent
            ); !result) {
            return result;
        }
    }
    if (auto result = set_hidden(
            context,
            "/qsee_welcome/surface/sd/sd_bar",
            !storage_percent_valid
        ); !result) {
        return result;
    }

    std::array<char, 48> wifi_status{};
    std::array<char, 48> ble_status{};
    std::array<char, 48> gps_status{};
    std::array<char, 48> storage_status{};
    format_component_status(wifi_status, snapshot.radios.wifi);
    format_component_status(ble_status, snapshot.radios.ble);
    if (component_ready(snapshot.gps) && snapshot.gps.has_fix &&
        !gps_fix_valid(snapshot.gps)) {
        std::snprintf(
            gps_status.data(),
            gps_status.size(),
            "ERROR:INVALID FIX"
        );
    } else if (component_ready(snapshot.gps)) {
        std::snprintf(
            gps_status.data(),
            gps_status.size(),
            "%s",
            snapshot.gps.has_fix ? "FIX" : "NO FIX"
        );
    } else {
        format_component_status(gps_status, snapshot.gps);
    }
    if (storage_cleanup_failed(snapshot.storage)) {
        std::snprintf(
            storage_status.data(),
            storage_status.size(),
            "TERMINAL:CLEANUP:%s",
            esp_err_to_name(snapshot.storage.cleanup_error)
        );
    } else if (storage_format_required(snapshot.storage)) {
        std::snprintf(storage_status.data(), storage_status.size(), "FORMAT REQUIRED");
    } else if (!snapshot.storage.card_response_verified &&
               !storage_mounted_ready(snapshot.storage)) {
        std::snprintf(
            storage_status.data(),
            storage_status.size(),
            "UNAVAILABLE:UNCONFIRMED"
        );
    } else {
        format_component_status(storage_status, snapshot.storage);
    }
    std::snprintf(
        text.data(),
        text.size(),
        "WIFI %s  |  BLE %s\nGPS %s  |  SD %s",
        wifi_status.data(),
        ble_status.data(),
        gps_status.data(),
        storage_status.data()
    );
    if (auto result = set_text(
            context,
            "/qsee_welcome/surface/radio_status",
            text.data()
        ); !result) {
        return result;
    }

    std::array<char, 96> snapshot_status{};
    if (!snapshot_available(snapshot)) {
        std::snprintf(
            snapshot_status.data(),
            snapshot_status.size(),
            "SNAPSHOT UNAVAILABLE | %s",
            esp_err_to_name(snapshot.last_update_error)
        );
    } else if (!snapshot.last_update_complete ||
               snapshot.last_update_error != ESP_OK) {
        std::snprintf(
            snapshot_status.data(),
            snapshot_status.size(),
            "SNAPSHOT #%lu | ERROR %s",
            static_cast<unsigned long>(snapshot.sequence),
            esp_err_to_name(snapshot.last_update_error)
        );
    } else {
        std::snprintf(
            snapshot_status.data(),
            snapshot_status.size(),
            "SNAPSHOT #%lu",
            static_cast<unsigned long>(snapshot.sequence)
        );
    }

    if (!component_ready(snapshot.battery)) {
        std::snprintf(
            text.data(),
            text.size(),
            "BATTERY %s | %s\nCPU UNAVAILABLE\n%s",
            component_state(snapshot.battery),
            component_reason(snapshot.battery),
            snapshot_status.data()
        );
    } else if (!snapshot.battery.battery_present) {
        std::snprintf(
            text.data(),
            text.size(),
            "BATTERY ABSENT\nCPU UNAVAILABLE\n%s",
            snapshot_status.data()
        );
    } else if (snapshot.battery.level_percent < 0 ||
               snapshot.battery.level_percent > 100 ||
               snapshot.battery.battery_mv <= 0) {
        std::snprintf(
            text.data(),
            text.size(),
            "BATTERY ERROR | INVALID READING\nCPU UNAVAILABLE\n%s",
            snapshot_status.data()
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "BATTERY %d%% | %dmV\nCPU UNAVAILABLE\n%s",
            snapshot.battery.level_percent,
            snapshot.battery.battery_mv,
            snapshot_status.data()
        );
    }
    return set_text(context, "/qsee_welcome/surface/device_status", text.data());
}

std::expected<void, std::string> App::ensure_log_buffers()
{
    if (log_source_buffer_ != nullptr && log_filtered_buffer_ != nullptr) {
        return {};
    }
    release_log_buffers();

    std::unique_ptr<char[]> source(
        new (std::nothrow) char[kLogBufferBytes]
    );
    std::unique_ptr<char[]> filtered(
        new (std::nothrow) char[kLogBufferBytes]
    );
    if (source == nullptr || filtered == nullptr) {
        return std::unexpected("Failed to allocate QSee log buffers");
    }

    source[0] = '\0';
    filtered[0] = '\0';
    log_source_buffer_ = std::move(source);
    log_filtered_buffer_ = std::move(filtered);
    return {};
}

std::expected<void, std::string> App::refresh_background(AppContext &context)
{
    BackgroundAsset::instance().request();
    if (background_asset_registered_) {
        return {};
    }

    const lv_image_dsc_t *image = BackgroundAsset::instance().image();
    if (image == nullptr) {
        return {};
    }

    auto registration_result = context.gui().register_image(
        esp_brookesia::gui::RuntimeImageResource{
            .id = kBackgroundImageId,
            .primary_src = "",
            .native_src = reinterpret_cast<uintptr_t>(image),
            .width = BackgroundAsset::kWidth,
            .height = BackgroundAsset::kHeight,
            .preload = false,
        }
    );
    if (!registration_result) {
        return std::unexpected(
            "Failed to register QSee background image: " +
            registration_result.error()
        );
    }

    auto source_result = context.gui().set_binding_value(
        kBackgroundImagePath,
        "src",
        kBackgroundImageId
    );
    if (!source_result) {
        if (!context.gui().unregister_image(kBackgroundImageId)) {
            ESP_LOGE(kTag, "Failed to roll back QSee background image registration");
        }
        return std::unexpected(
            "Failed to attach QSee background image: " + source_result.error()
        );
    }

    auto visibility_result = context.gui().set_binding_value(
        kBackgroundImagePath,
        "hidden",
        "false"
    );
    if (!visibility_result) {
        if (!context.gui().unregister_image(kBackgroundImageId)) {
            ESP_LOGE(kTag, "Failed to roll back QSee background image registration");
        }
        return std::unexpected(
            "Failed to show QSee background image: " + visibility_result.error()
        );
    }

    background_asset_registered_ = true;
    ESP_LOGI(kTag, "Attached shared dithered RGB565 background");
    return {};
}

void App::release_log_buffers()
{
    log_source_buffer_.reset();
    log_filtered_buffer_.reset();
}

std::expected<void, std::string> App::refresh_logs(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }
    if (auto result = ensure_log_buffers(); !result) {
        return result;
    }
    if (!logs_paused_) {
        size_t bytes_written = 0;
        esp_err_t snapshot_result = qsee_logger_snapshot(
            log_source_buffer_.get(),
            kLogBufferBytes,
            kLogSnapshotLines,
            &bytes_written
        );
        if (snapshot_result == ESP_OK && bytes_written >= kLogBufferBytes) {
            snapshot_result = ESP_ERR_INVALID_SIZE;
        }
        if (snapshot_result != ESP_OK) {
            log_source_buffer_[0] = '\0';
            std::snprintf(
                log_filtered_buffer_.get(),
                kLogBufferBytes,
                "LOGGER ERROR\n%s",
                esp_err_to_name(snapshot_result)
            );
            ESP_LOGE(
                kTag,
                "Canonical logger snapshot failed: %s",
                esp_err_to_name(snapshot_result)
            );
            if (auto result = set_text(
                    context,
                    "/qsee_logs/surface/log_scroller/log_text",
                    log_filtered_buffer_.get()
                ); !result) {
                return result;
            }
            return std::unexpected(
                "Canonical logger snapshot failed: " +
                std::string(esp_err_to_name(snapshot_result))
            );
        }
        log_source_buffer_[bytes_written] = '\0';
        filter_logs(
            log_source_buffer_.get(),
            log_query_,
            log_filter_mode_,
            log_filtered_buffer_.get(),
            kLogBufferBytes
        );
        if (log_filtered_buffer_[0] == '\0') {
            std::snprintf(
                log_filtered_buffer_.get(),
                kLogBufferBytes,
                "%s",
                bytes_written == 0
                    ? "LOGGER READY | NO LOG ENTRIES"
                    : "LOGGER READY | NO ENTRIES MATCH THE ACTIVE FILTER"
            );
        }
        if (auto result = set_text(
                context,
                "/qsee_logs/surface/log_scroller/log_text",
                log_filtered_buffer_.get()
            ); !result) {
            return result;
        }
    } else {
        std::snprintf(
            log_source_buffer_.get(),
            kLogBufferBytes,
            "PAUSED | PREVIOUS LOGGER SNAPSHOT\n%s",
            log_filtered_buffer_.get()
        );
        if (auto result = set_text(
                context,
                "/qsee_logs/surface/log_scroller/log_text",
                log_source_buffer_.get()
            ); !result) {
            return result;
        }
    }

    if (logs_follow_latest_ && !logs_paused_) {
        auto scroll_result = context.gui().scroll_to(
            "/qsee_logs/surface/log_scroller",
            0,
            32767,
            false
        );
        if (!scroll_result) {
            const std::string details =
                "Failed to follow the canonical logger: " + scroll_result.error();
            ESP_LOGE(kTag, "%s", details.c_str());
            if (auto dialog_result = show_error_dialog(
                    context,
                    "Log view error",
                    details
                ); !dialog_result) {
                return dialog_result;
            }
            return std::unexpected(details);
        }
    }
    return {};
}

std::expected<void, std::string> App::refresh_hardware(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }
    std::array<char, 2048> details{};
    if (snapshot_available(snapshot)) {
        if (snapshot.last_update_complete && snapshot.last_update_error == ESP_OK) {
            append_format(
                details,
                "SNAPSHOT    #%lu READY (%s)\n",
                static_cast<unsigned long>(snapshot.sequence),
                snapshot.reason
            );
        } else {
            append_format(
                details,
                "SNAPSHOT    #%lu ERROR (%s; %s)\n",
                static_cast<unsigned long>(snapshot.sequence),
                snapshot.reason,
                esp_err_to_name(snapshot.last_update_error)
            );
        }
    } else {
        append_format(details, "SNAPSHOT    UNAVAILABLE (NOT REFRESHED)\n");
    }

    const auto append_component_error =
        [&details](const char *label, const ComponentSnapshot &component) {
            append_format(
                details,
                "%-11s %s (%s)\n",
                label,
                component_state(component),
                component_reason(component)
            );
        };

    if (!component_ready(snapshot.battery)) {
        append_component_error("BATTERY", snapshot.battery);
    } else if (!snapshot.battery.battery_present) {
        append_format(details, "BATTERY     UNAVAILABLE (BATTERY ABSENT; PMU READY)\n");
    } else if (snapshot.battery.level_percent < 0 ||
               snapshot.battery.level_percent > 100 ||
               snapshot.battery.battery_mv <= 0) {
        append_format(details, "BATTERY     ERROR (INVALID READING)\n");
    } else {
        append_format(
            details,
            "BATTERY     READY %d%% %dmV | %s\n",
            snapshot.battery.level_percent,
            snapshot.battery.battery_mv,
            snapshot.battery.power_state
        );
    }

    if (!component_ready(snapshot.rtc)) {
        append_component_error("RTC", snapshot.rtc);
    } else if (!snapshot.rtc.clock_integrity) {
        append_format(details, "RTC         ERROR (CLOCK INTEGRITY LOST)\n");
    } else if (!rtc_reading_valid(snapshot.rtc)) {
        append_format(details, "RTC         ERROR (INVALID CALENDAR READING)\n");
    } else {
        append_format(
            details,
            "RTC         READY %04u-%02u-%02u %02u:%02u:%02u | %s | %s\n",
            static_cast<unsigned>(snapshot.rtc.year),
            static_cast<unsigned>(snapshot.rtc.month),
            static_cast<unsigned>(snapshot.rtc.day),
            static_cast<unsigned>(snapshot.rtc.hour),
            static_cast<unsigned>(snapshot.rtc.minute),
            static_cast<unsigned>(snapshot.rtc.second),
            snapshot.rtc.source,
            snapshot.rtc.system_time_valid
                ? snapshot.rtc.synchronized
                    ? "SYSTEM TIME SYNCED"
                    : "SYSTEM TIME RESTORED"
                : "SYSTEM TIME UNAVAILABLE"
        );
    }

    if (!component_ready(snapshot.imu)) {
        append_component_error("IMU", snapshot.imu);
    } else {
        append_format(details, "IMU         READY");
        if (component_ready(snapshot.imu.calibration)) {
            append_format(
                details,
                " | calibration %s | samples %u\n",
                snapshot.imu.calibrated ? "READY" : "PENDING",
                static_cast<unsigned>(snapshot.imu.calibration_samples)
            );
        } else {
            append_format(
                details,
                " | calibration %s (%s)\n",
                component_state(snapshot.imu.calibration),
                component_reason(snapshot.imu.calibration)
            );
        }
    }

    if (!component_ready(snapshot.gps)) {
        append_component_error("GPS", snapshot.gps);
    } else if (!snapshot.gps.has_fix) {
        append_format(details, "GPS         RECEIVER READY | NO FIX\n");
    } else if (!gps_fix_valid(snapshot.gps)) {
        append_format(details, "GPS         ERROR (INVALID FIX DATA)\n");
    } else {
        append_format(
            details,
            "GPS         FIX | sats %u | %.6f, %.6f | alt %.1fm\n",
            static_cast<unsigned>(snapshot.gps.satellites),
            snapshot.gps.latitude_degrees,
            snapshot.gps.longitude_degrees,
            snapshot.gps.altitude_m
        );
    }

    if (radio_deferred_by_policy(snapshot.radios.wifi)) {
        append_format(details, "WIFI        OFF BY POLICY | START IN SETTINGS\n");
    } else if (!component_ready(snapshot.radios.wifi)) {
        append_component_error("WIFI", snapshot.radios.wifi);
    } else if (snapshot.radios.wifi.connected) {
        append_format(
            details,
            "WIFI        READY | CONNECTED%s%s\n",
            snapshot.radios.wifi.ssid[0] != '\0' ? " | " : "",
            snapshot.radios.wifi.ssid
        );
    } else {
        append_format(
            details,
            "WIFI        READY | %s\n",
            snapshot.radios.wifi.started ? "STARTED, NOT CONNECTED" : "STOPPED"
        );
    }

    if (radio_deferred_by_policy(snapshot.radios.ble)) {
        append_format(details, "BLE         OFF BY POLICY | START IN SETTINGS\n");
    } else if (!component_ready(snapshot.radios.ble)) {
        append_component_error("BLE", snapshot.radios.ble);
    } else {
        append_format(
            details,
            "BLE         READY | %s\n",
            snapshot.radios.ble.connected
                ? "CONNECTED"
                : snapshot.radios.ble.provisioning
                    ? "PROVISIONING"
                    : snapshot.radios.ble.advertising ? "ADVERTISING" : "IDLE"
        );
    }

    if (!component_ready(snapshot.ui.display)) {
        append_component_error("DISPLAY", snapshot.ui.display);
    } else {
        append_format(details, "DISPLAY     READY\n");
    }
    if (!component_ready(snapshot.ui.touch)) {
        append_component_error("TOUCH", snapshot.ui.touch);
    } else {
        append_format(details, "TOUCH       READY\n");
    }
    if (!component_ready(snapshot.audio.speaker)) {
        append_component_error("SPEAKER", snapshot.audio.speaker);
    } else {
        append_format(details, "SPEAKER     READY\n");
    }
    if (!component_ready(snapshot.audio.microphone)) {
        append_component_error("MICROPHONE", snapshot.audio.microphone);
    } else if (snapshot.audio.channel_count == 0 ||
               snapshot.audio.sample_rate_hz == 0) {
        append_format(details, "MICROPHONE  ERROR (INVALID PCM METADATA)\n");
    } else {
        append_format(
            details,
            "MICROPHONE  READY | PCM %u ch @ %lu Hz\n",
            static_cast<unsigned>(snapshot.audio.channel_count),
            static_cast<unsigned long>(snapshot.audio.sample_rate_hz)
        );
    }

    if (storage_cleanup_failed(snapshot.storage)) {
        append_format(
            details,
            "SD CARD     TERMINAL | cleanup=%s error=%s complete=%s "
            "poisoned=%s\n",
            storage_cleanup_stage_name(snapshot.storage.cleanup_stage),
            esp_err_to_name(snapshot.storage.cleanup_error),
            snapshot.storage.cleanup_complete ? "yes" : "no",
            snapshot.storage.poisoned ? "yes" : "no"
        );
    } else if (storage_format_required(snapshot.storage)) {
        append_format(
            details,
            "SD CARD     FORMAT REQUIRED (%s)\n",
            component_reason(snapshot.storage)
        );
    } else if (storage_mounted_ready(snapshot.storage) &&
               snapshot.storage.filesystem_total_bytes > 0 &&
               snapshot.storage.free_bytes <= snapshot.storage.filesystem_total_bytes) {
        append_format(
            details,
            "SD CARD     READY | %llu / %llu MB free",
            static_cast<unsigned long long>(
                snapshot.storage.free_bytes / (1024ULL * 1024ULL)
            ),
            static_cast<unsigned long long>(
                snapshot.storage.filesystem_total_bytes / (1024ULL * 1024ULL)
            )
        );
        if (component_ready(snapshot.storage.logger_mirror)) {
            append_format(
                details,
                " | mirror %s\n",
                snapshot.storage.logger_mirror_enabled ? "ACTIVE" : "INACTIVE"
            );
        } else {
            if (snapshot.storage.logger_mirror_errno != 0) {
                append_format(
                    details,
                    " | mirror %s (%s; errno=%d; failures=%lu)\n",
                    component_state(snapshot.storage.logger_mirror),
                    component_reason(snapshot.storage.logger_mirror),
                    snapshot.storage.logger_mirror_errno,
                    static_cast<unsigned long>(
                        snapshot.storage.logger_mirror_failure_count
                    )
                );
            } else {
                append_format(
                    details,
                    " | mirror %s (%s)\n",
                    component_state(snapshot.storage.logger_mirror),
                    component_reason(snapshot.storage.logger_mirror)
                );
            }
        }
    } else if (storage_mounted_ready(snapshot.storage)) {
        append_format(details, "SD CARD     ERROR (INVALID CAPACITY SAMPLE)\n");
    } else if (!snapshot.storage.card_response_verified) {
        append_format(
            details,
            "SD CARD     UNAVAILABLE (CARD/TRANSPORT UNCONFIRMED; %s)\n",
            component_reason(snapshot.storage)
        );
    } else {
        append_component_error("SD CARD", snapshot.storage);
    }

    const bool memory_valid =
        component_ready(snapshot.memory) && snapshot.memory.heap_free_bytes > 0 &&
        snapshot.memory.heap_minimum_free_bytes > 0 &&
        snapshot.memory.heap_minimum_free_bytes <= snapshot.memory.heap_free_bytes;
    if (memory_valid) {
        append_format(
            details,
            "MEMORY      HEAP %lu KB free | minimum %lu KB\n",
            static_cast<unsigned long>(snapshot.memory.heap_free_bytes / 1024U),
            static_cast<unsigned long>(snapshot.memory.heap_minimum_free_bytes / 1024U)
        );
        if (snapshot.memory.psram_heap_total_bytes > 0 &&
            snapshot.memory.psram_free_bytes <= snapshot.memory.psram_heap_total_bytes) {
            append_format(
                details,
                "PSRAM       %lu / %lu KB free\n",
                static_cast<unsigned long>(snapshot.memory.psram_free_bytes / 1024U),
                static_cast<unsigned long>(
                    snapshot.memory.psram_heap_total_bytes / 1024U
                )
            );
        } else {
            append_format(details, "PSRAM       UNAVAILABLE (NO VALID SAMPLE)\n");
        }
    } else {
        if (!component_ready(snapshot.memory)) {
            append_component_error("MEMORY", snapshot.memory);
        } else {
            append_format(details, "MEMORY      ERROR (INVALID SAMPLE)\n");
        }
    }
    append_format(details, "CPU UTIL    UNAVAILABLE (NOT MEASURED)");

    if (auto result = set_text(
            context,
            "/qsee_hardware/surface/hardware_scroller/hardware_text",
            details.data()
        ); !result) {
        return result;
    }

    return {};
}

std::expected<void, std::string> App::refresh_audio(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }
    std::array<char, 256> status{};
    std::array<char, 256> values{};
    int level0 = 0;
    int level1 = 0;
    bool level0_valid = false;
    bool level1_valid = false;

    if (!component_ready(snapshot.audio.microphone)) {
        std::snprintf(
            status.data(),
            status.size(),
            "MICROPHONES %s (%s)",
            component_state(snapshot.audio.microphone),
            component_reason(snapshot.audio.microphone)
        );
        std::snprintf(
            values.data(),
            values.size(),
            "AUDIO MEASUREMENTS %s\n%s",
            component_state(snapshot.audio.microphone),
            component_reason(snapshot.audio.microphone)
        );
    } else if (!snapshot_available(snapshot)) {
        std::snprintf(
            status.data(),
            status.size(),
            "MICROPHONES UNAVAILABLE (SNAPSHOT NOT READY)"
        );
        std::snprintf(
            values.data(),
            values.size(),
            "AUDIO MEASUREMENTS UNAVAILABLE\nSNAPSHOT NOT READY"
        );
    } else if (!component_ready(snapshot.diagnostics.monitor)) {
        std::snprintf(
            status.data(),
            status.size(),
            "MICROPHONES %s (MONITOR %s)",
            component_state(snapshot.diagnostics.monitor),
            component_reason(snapshot.diagnostics.monitor)
        );
        std::snprintf(
            values.data(),
            values.size(),
            "AUDIO MEASUREMENTS %s\nMONITOR %s",
            component_state(snapshot.diagnostics.monitor),
            component_reason(snapshot.diagnostics.monitor)
        );
    } else if (!snapshot.diagnostics.monitor_active) {
        std::snprintf(
            status.data(),
            status.size(),
            "MICROPHONES UNAVAILABLE (DIAGNOSTIC MONITOR OFF)"
        );
        std::snprintf(
            values.data(),
            values.size(),
            "AUDIO MEASUREMENTS UNAVAILABLE\nDIAGNOSTIC MONITOR OFF"
        );
    } else if (
        snapshot.audio.microphone.last_success_at_us <=
        diagnostic_activation_sample_at_us_
    ) {
        std::snprintf(
            status.data(),
            status.size(),
            "MICROPHONES STALE (WAITING FOR NEW SAMPLE)"
        );
        std::snprintf(
            values.data(),
            values.size(),
            "AUDIO MEASUREMENTS STALE\nWAITING FOR NEW SAMPLE"
        );
    } else if (snapshot.audio.channel_count == 0 ||
               snapshot.audio.channel_count > 2 ||
               snapshot.audio.sample_rate_hz == 0) {
        std::snprintf(
            status.data(),
            status.size(),
            "MICROPHONES ERROR (INVALID PCM METADATA)"
        );
        std::snprintf(
            values.data(),
            values.size(),
            "AUDIO MEASUREMENTS ERROR\nINVALID PCM METADATA"
        );
    } else if (
        snapshot.audio.microphone_mean_level > 32768U ||
        snapshot.audio.microphone_peak_level > 32768U ||
        snapshot.audio.microphone_mean_level > snapshot.audio.microphone_peak_level ||
        snapshot.audio.channel_mean_level[0] > 32768U ||
        snapshot.audio.channel_peak_level[0] > 32768U ||
        snapshot.audio.channel_mean_level[0] > snapshot.audio.channel_peak_level[0] ||
        (snapshot.audio.channel_count > 1 &&
         (snapshot.audio.channel_mean_level[1] > 32768U ||
          snapshot.audio.channel_peak_level[1] > 32768U ||
          snapshot.audio.channel_mean_level[1] > snapshot.audio.channel_peak_level[1])) ||
        (snapshot.audio.channel_count == 1 &&
         (snapshot.audio.channel_mean_level[1] != 0U ||
          snapshot.audio.channel_peak_level[1] != 0U))
    ) {
        ESP_LOGE(
            kTag,
            "Invalid PCM levels: aggregate=%lu/%lu ch0=%lu/%lu ch1=%lu/%lu channels=%u",
            static_cast<unsigned long>(snapshot.audio.microphone_mean_level),
            static_cast<unsigned long>(snapshot.audio.microphone_peak_level),
            static_cast<unsigned long>(snapshot.audio.channel_mean_level[0]),
            static_cast<unsigned long>(snapshot.audio.channel_peak_level[0]),
            static_cast<unsigned long>(snapshot.audio.channel_mean_level[1]),
            static_cast<unsigned long>(snapshot.audio.channel_peak_level[1]),
            static_cast<unsigned>(snapshot.audio.channel_count)
        );
        std::snprintf(
            status.data(),
            status.size(),
            "MICROPHONES ERROR (INVALID LEVEL SAMPLE)"
        );
        std::snprintf(
            values.data(),
            values.size(),
            "AUDIO MEASUREMENTS ERROR\nINVALID LEVEL SAMPLE"
        );
    } else {
        std::snprintf(
            status.data(),
            status.size(),
            "MICROPHONES READY | PCM %u ch @ %lu Hz | t=%lld us",
            static_cast<unsigned>(snapshot.audio.channel_count),
            static_cast<unsigned long>(snapshot.audio.sample_rate_hz),
            static_cast<long long>(snapshot.audio.microphone.last_success_at_us)
        );
        level0 = audio_peak_meter_value(
            snapshot.audio.channel_peak_level[0]
        );
        level0_valid = true;
        std::array<char, 64> peak0{};
        std::array<char, 64> peak1{};
        format_audio_peak(
            peak0.data(),
            peak0.size(),
            0U,
            snapshot.audio.channel_peak_level[0]
        );
        if (snapshot.audio.channel_count > 1) {
            level1 = audio_peak_meter_value(
                snapshot.audio.channel_peak_level[1]
            );
            level1_valid = true;
            format_audio_peak(
                peak1.data(),
                peak1.size(),
                1U,
                snapshot.audio.channel_peak_level[1]
            );
            std::snprintf(
                values.data(),
                values.size(),
                "FORMAT     SIGNED PCM / 16-bit\n"
                "RATE       %.2f kHz\n"
                "CHANNELS   %u\n"
                "%s\n"
                "%s",
                static_cast<double>(snapshot.audio.sample_rate_hz) / 1000.0,
                static_cast<unsigned>(snapshot.audio.channel_count),
                peak0.data(),
                peak1.data()
            );
        } else {
            std::snprintf(
                values.data(),
                values.size(),
                "FORMAT     SIGNED PCM / 16-bit\n"
                "RATE       %.2f kHz\n"
                "CHANNELS   1\n"
                "%s\n"
                "PEAK CH1   UNAVAILABLE",
                static_cast<double>(snapshot.audio.sample_rate_hz) / 1000.0,
                peak0.data()
            );
        }
    }
    if (auto result = set_text(
            context,
            "/qsee_audio/surface/audio_status",
            status.data()
        ); !result) {
        return result;
    }

    if (level0_valid) {
        if (auto result = set_value(
                context,
                "/qsee_audio/surface/mic0_bar",
                level0
            ); !result) {
            return result;
        }
    }
    if (auto result = set_hidden(
            context,
            "/qsee_audio/surface/mic0_bar",
            !level0_valid
        ); !result) {
        return result;
    }
    if (level1_valid) {
        if (auto result = set_value(
                context,
                "/qsee_audio/surface/mic1_bar",
                level1
            ); !result) {
            return result;
        }
    }
    if (auto result = set_hidden(
            context,
            "/qsee_audio/surface/mic1_bar",
            !level1_valid
        ); !result) {
        return result;
    }

    return set_text(context, "/qsee_audio/surface/audio_values", values.data());
}

bool App::is_pairing_screen(Screen screen)
{
    switch (screen) {
    case Screen::PairIntro:
    case Screen::PairBluetooth:
    case Screen::PairApp:
    case Screen::AppStoreQr:
    case Screen::PairQr:
    case Screen::PairResult:
        return true;
    default:
        return false;
    }
}

std::expected<void, std::string> App::prepare_pairing_qr(
    AppContext &context,
    const char *payload
)
{
    const esp_err_t generation_result = pairing_qr_.generate(payload);
    if (generation_result != ESP_OK) {
        return std::unexpected(
            "Pairing QR generation failed: " +
            std::string(esp_err_to_name(generation_result))
        );
    }
    if (pairing_qr_registered_) {
        return {};
    }

    const lv_image_dsc_t *image = pairing_qr_.image();
    if (image == nullptr) {
        return std::unexpected(
            "Pairing QR generation completed without an image"
        );
    }
    auto registration_result = context.gui().register_image(
        esp_brookesia::gui::RuntimeImageResource{
            .id = kPairingQrImageId,
            .primary_src = "",
            .native_src = reinterpret_cast<uintptr_t>(image),
            .width = PairingQrAsset::kSize,
            .height = PairingQrAsset::kSize,
            .preload = false,
        }
    );
    if (!registration_result) {
        return std::unexpected(
            "Failed to register pairing QR image: " +
            registration_result.error()
        );
    }
    pairing_qr_registered_ = true;
    return {};
}

std::expected<void, std::string> App::release_pairing_qr(AppContext &context)
{
    if (pairing_qr_registered_ &&
        !context.gui().unregister_image(kPairingQrImageId)) {
        return std::unexpected("Failed to unregister QSee pairing QR image");
    }
    pairing_qr_registered_ = false;
    pairing_qr_.reset();
    return {};
}

std::expected<void, std::string> App::open_app_store_qr(
    AppContext &context,
    NavigationMode mode
)
{
    if (auto result = prepare_pairing_qr(
            context,
            kEspressifAppStoreUrl
        ); !result) {
        return show_pairing_result(
            context,
            result.error(),
            false
        );
    }
    return open_screen(
        context,
        Screen::AppStoreQr,
        "qsee.open.app_store_qr",
        mode
    );
}

std::expected<void, std::string> App::start_pairing_session(
    AppContext &context,
    NavigationMode mode
)
{
    esp_err_t result = qsee_wifi_init();
    if (result == ESP_OK &&
        !qsee_provisioning_initialized()) {
        result = qsee_provisioning_init();
    }
    if (result == ESP_OK) {
        result = qsee_provisioning_start();
    }
    if (result != ESP_OK) {
        return show_pairing_result(
            context,
            "BLE provisioning could not start: " +
                std::string(esp_err_to_name(result)),
            false
        );
    }
    if (auto qr_result = prepare_pairing_qr(
            context,
            qsee_provisioning_qr_payload()
        ); !qr_result) {
        const esp_err_t stop_result = qsee_provisioning_stop();
        if (stop_result != ESP_OK) {
            return std::unexpected(
                qr_result.error() +
                "; provisioning stop also failed: " +
                esp_err_to_name(stop_result)
            );
        }
        return show_pairing_result(
            context,
            qr_result.error(),
            false
        );
    }

    pairing_started_at_us_ = esp_timer_get_time();
    return open_screen(
        context,
        Screen::PairQr,
        "qsee.open.pair_qr",
        mode
    );
}

std::expected<void, std::string> App::show_pairing_result(
    AppContext &context,
    std::string message,
    bool success
)
{
    pairing_result_ = std::move(message);
    pairing_result_success_ = success;
    pairing_started_at_us_ = 0;
    return open_screen(
        context,
        Screen::PairResult,
        "qsee.open.pair_result",
        NavigationMode::Replace
    );
}

std::expected<void, std::string> App::pairing_next(AppContext &context)
{
    const auto mode_for = [this](Screen target) {
        const std::optional<Screen> forward =
            navigation_.forward_target();
        return forward.has_value() && *forward == target
            ? NavigationMode::Forward
            : NavigationMode::Push;
    };

    switch (screen_) {
    case Screen::PairIntro:
        return open_screen(
            context,
            Screen::PairBluetooth,
            "qsee.open.pair_bluetooth",
            mode_for(Screen::PairBluetooth)
        );
    case Screen::PairBluetooth:
        return open_screen(
            context,
            Screen::PairApp,
            "qsee.open.pair_app",
            mode_for(Screen::PairApp)
        );
    case Screen::PairApp:
        return open_app_store_qr(
            context,
            mode_for(Screen::AppStoreQr)
        );
    case Screen::AppStoreQr: {
        const NavigationMode mode = mode_for(Screen::PairQr);
        if (!qsee_provisioning_active()) {
            return start_pairing_session(context, mode);
        }
        if (auto result = prepare_pairing_qr(
                context,
                qsee_provisioning_qr_payload()
            ); !result) {
            return result;
        }
        return open_screen(
            context,
            Screen::PairQr,
            "qsee.open.pair_qr",
            mode
        );
    }
    case Screen::PairResult:
        if (pairing_result_success_) {
            return open_screen(
                context,
                Screen::Settings,
                "qsee.open.settings",
                NavigationMode::ReturnTo
            );
        }
        return start_pairing_session(
            context,
            NavigationMode::Replace
        );
    case Screen::PairQr:
        return std::unexpected(
            "Next action rejected while pairing is in progress"
        );
    default:
        return std::unexpected(
            "Next action rejected outside the pairing wizard"
        );
    }
}

std::string_view App::screen_action(Screen screen)
{
    switch (screen) {
    case Screen::Welcome:
        return "qsee.open.welcome";
    case Screen::Logs:
        return "qsee.open.logs";
    case Screen::Hardware:
        return "qsee.open.hardware";
    case Screen::Audio:
        return "qsee.open.audio";
    case Screen::AudioFft:
        return "qsee.open.audio_fft";
    case Screen::AudioDetail:
        return "qsee.open.audio_detail";
    case Screen::Imu:
        return "qsee.open.imu";
    case Screen::Settings:
        return "qsee.open.settings";
    case Screen::About:
        return "qsee.open.about";
    case Screen::PairIntro:
        return "qsee.open.pair_intro";
    case Screen::PairBluetooth:
        return "qsee.open.pair_bluetooth";
    case Screen::PairApp:
        return "qsee.open.pair_app";
    case Screen::AppStoreQr:
        return "qsee.open.app_store_qr";
    case Screen::PairQr:
        return "qsee.open.pair_qr";
    case Screen::PairResult:
        return "qsee.open.pair_result";
    }
    return "";
}

std::expected<void, std::string> App::navigate_back(AppContext &context)
{
    const std::optional<Screen> target = navigation_.back_target();
    if (!target.has_value()) {
        return std::unexpected(
            "Back action rejected because navigation history is empty"
        );
    }

    if (*target == Screen::AppStoreQr) {
        if (auto result = prepare_pairing_qr(
                context,
                kEspressifAppStoreUrl
            ); !result) {
            return result;
        }
    } else if (*target == Screen::PairQr) {
        if (auto result = prepare_pairing_qr(
                context,
                qsee_provisioning_qr_payload()
            ); !result) {
            return result;
        }
    }

    return open_screen(
        context,
        *target,
        screen_action(*target),
        NavigationMode::Back
    );
}

std::expected<void, std::string> App::refresh_pairing(AppContext &context)
{
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }

    const char *root = "/qsee_pairing/surface";
    const char *title = nullptr;
    const char *step = nullptr;
    const char *message = nullptr;
    const char *detail = "";
    bool show_qr = false;
    std::array<char, 192> dynamic_message{};

    switch (screen_) {
    case Screen::PairIntro:
        title = "PAIR DEVICE";
        step = "1 OF 5";
        message =
            "Connect QSee to Wi-Fi with Bluetooth provisioning.";
        detail =
            "Keep the device near your phone. No PIN or proof-of-possession is required.";
        break;
    case Screen::PairBluetooth:
        title = "BLUETOOTH";
        step = "2 OF 5";
        message =
            "Enable Bluetooth on your phone and allow nearby-device access.";
        detail =
            "The QSee radio starts only when you reach the device QR step.";
        break;
    case Screen::PairApp:
        title = "ESPRESSIF APP";
        step = "3 OF 5";
        message =
            "Install and open ESP BLE Provisioning, the official Espressif app.";
        detail =
            "Next shows the Google Play QR, then the QR for this QSee device.";
        break;
    case Screen::AppStoreQr:
        title = "INSTALL APP";
        step = "4 OF 5";
        message =
            "Scan this code to open ESP BLE Provisioning on Google Play.";
        detail = "Press Next when the app is ready.";
        show_qr = true;
        break;
    case Screen::PairQr: {
        title = "SCAN DEVICE";
        step = "5 OF 5";
        show_qr = true;
        detail = qsee_provisioning_service_name();
        const qsee_provisioning_state_t state = qsee_provisioning_state();
        switch (state) {
        case QSEE_PROVISIONING_STATE_WAITING_FOR_APP:
            message =
                "Bluetooth ready. Scan this QR in the Espressif app.";
            break;
        case QSEE_PROVISIONING_STATE_CREDENTIALS_RECEIVED:
            message =
                "Credentials received. Preparing the Wi-Fi profile...";
            break;
        case QSEE_PROVISIONING_STATE_CONNECTING:
            message =
                "Credentials received. Connecting to Wi-Fi...";
            break;
        case QSEE_PROVISIONING_STATE_SUCCESS: {
            std::array<char, 128> summary{};
            const esp_err_t summary_result =
                qsee_provisioning_connection_summary(
                    summary.data(),
                    summary.size()
                );
            if (summary_result == ESP_OK) {
                return show_pairing_result(
                    context,
                    summary.data(),
                    true
                );
            }
            std::snprintf(
                dynamic_message.data(),
                dynamic_message.size(),
                "Wi-Fi connected. Connection summary unavailable (%s).",
                esp_err_to_name(summary_result)
            );
            return show_pairing_result(
                context,
                dynamic_message.data(),
                true
            );
        }
        case QSEE_PROVISIONING_STATE_FAILED:
            return show_pairing_result(
                context,
                "The network rejected the credentials. Check the password and retry.",
                false
            );
        case QSEE_PROVISIONING_STATE_POISONED:
            return show_pairing_result(
                context,
                "The provisioning manager entered a terminal error state.",
                false
            );
        case QSEE_PROVISIONING_STATE_UNAVAILABLE:
            return show_pairing_result(
                context,
                "Provisioning is disabled in this build profile.",
                false
            );
        default:
            message = "Starting Bluetooth provisioning...";
            break;
        }

        if (pairing_started_at_us_ > 0 &&
            esp_timer_get_time() - pairing_started_at_us_ >=
                kPairingTimeoutUs) {
            const esp_err_t stop_result = qsee_provisioning_stop();
            if (stop_result != ESP_OK) {
                std::snprintf(
                    dynamic_message.data(),
                    dynamic_message.size(),
                    "Pairing timed out and BLE could not stop (%s).",
                    esp_err_to_name(stop_result)
                );
            } else {
                std::snprintf(
                    dynamic_message.data(),
                    dynamic_message.size(),
                    "Pairing timed out after 5 minutes. Retry to start a new session."
                );
            }
            return show_pairing_result(
                context,
                dynamic_message.data(),
                false
            );
        }
        break;
    }
    case Screen::PairResult:
        title = pairing_result_success_
            ? "WI-FI READY"
            : "PAIRING FAILED";
        step = pairing_result_success_ ? "CONNECTED" : "ACTION REQUIRED";
        message = pairing_result_.c_str();
        detail = pairing_result_success_
            ? "Next returns to Settings."
            : "Next starts a new pairing session.";
        break;
    default:
        return std::unexpected(
            "Pairing refresh rejected outside the wizard"
        );
    }

    const PairingVisualState next_state{
        .screen = screen_,
        .title = title,
        .step = step,
        .message = message,
        .detail = detail,
        .show_qr = show_qr,
    };
    if (pairing_visual_state_.has_value() &&
        *pairing_visual_state_ == next_state) {
        return {};
    }

    if (auto result = set_text(
            context,
            std::string(root) + "/title",
            title
        ); !result) {
        return result;
    }
    if (auto result = set_text(
            context,
            std::string(root) + "/step",
            step
        ); !result) {
        return result;
    }
    if (auto result = set_text(
            context,
            std::string(root) + "/message",
            message
        ); !result) {
        return result;
    }
    if (auto result = set_text(
            context,
            std::string(root) + "/detail",
            detail
        ); !result) {
        return result;
    }
    if (auto result = set_hidden(
            context,
            std::string(root) + "/qr",
            !show_qr
        ); !result) {
        return result;
    }
    if (show_qr) {
        if (auto result = set_binding_text(
                context,
                std::string(root) + "/qr",
                "src",
                kPairingQrImageId
            ); !result) {
            return result;
        }
    }
    pairing_visual_state_ = next_state;
    return {};
}

// Render the restored compact DFT bank without bypassing the Brookesia view tree.
std::expected<void, std::string> App::refresh_audio_fft(AppContext &context)
{
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }

    std::array<char, 160> status{};
    bool spectrum_valid = true;
    if (!snapshot_available(snapshot)) {
        std::snprintf(
            status.data(),
            status.size(),
            "SPECTRUM UNAVAILABLE (SNAPSHOT NOT READY)"
        );
        spectrum_valid = false;
    } else if (!component_ready(snapshot.audio.microphone)) {
        std::snprintf(
            status.data(),
            status.size(),
            "SPECTRUM %s (%s)",
            component_state(snapshot.audio.microphone),
            component_reason(snapshot.audio.microphone)
        );
        spectrum_valid = false;
    } else if (!component_ready(snapshot.diagnostics.monitor) ||
               !snapshot.diagnostics.monitor_active) {
        std::snprintf(
            status.data(),
            status.size(),
            "SPECTRUM UNAVAILABLE (DIAGNOSTIC MONITOR OFF)"
        );
        spectrum_valid = false;
    } else if (snapshot.audio.microphone.last_success_at_us <=
               diagnostic_activation_sample_at_us_) {
        std::snprintf(
            status.data(),
            status.size(),
            "SPECTRUM STALE (WAITING FOR NEW SAMPLE)"
        );
        spectrum_valid = false;
    } else if (snapshot.audio.channel_count != 2U ||
               snapshot.audio.sample_rate_hz != 22050U ||
               snapshot.audio.spectrum_sequence == 0U) {
        std::snprintf(
            status.data(),
            status.size(),
            "SPECTRUM ERROR (INVALID PCM OR FFT METADATA)"
        );
        spectrum_valid = false;
    } else {
        for (size_t channel = 0; channel < 2U && spectrum_valid; ++channel) {
            for (size_t band = 0;
                 band < kAudioSpectrumBandCount;
                 ++band) {
                if (snapshot.audio.channel_spectrum[channel][band] > 100U) {
                    spectrum_valid = false;
                    ESP_LOGE(
                        kTag,
                        "Invalid FFT bin: channel=%u band=%u value=%u",
                        static_cast<unsigned>(channel),
                        static_cast<unsigned>(band),
                        static_cast<unsigned>(
                            snapshot.audio.channel_spectrum[channel][band]
                        )
                    );
                    break;
                }
            }
        }
        if (spectrum_valid) {
            std::snprintf(
                status.data(),
                status.size(),
                "SPECTRUM READY | SEQ %lu | 22.05 kHz",
                static_cast<unsigned long>(snapshot.audio.spectrum_sequence)
            );
        } else {
            std::snprintf(
                status.data(),
                status.size(),
                "SPECTRUM ERROR (BIN OUT OF RANGE)"
            );
        }
    }

    if (auto result = set_text(
            context,
            "/qsee_audio_fft/surface/spectrum_status",
            status.data()
        ); !result) {
        return result;
    }
    if (!spectrum_valid) {
        return set_hidden(
            context,
            "/qsee_audio_fft/surface/bands",
            true
        );
    }

    for (size_t band = 0; band < kAudioSpectrumBandCount; ++band) {
        std::array<char, 80> path{};
        std::snprintf(
            path.data(),
            path.size(),
            "/qsee_audio_fft/surface/bands/band%02u/bar",
            static_cast<unsigned>(band)
        );
        const int32_t average =
            (static_cast<int32_t>(snapshot.audio.channel_spectrum[0][band]) +
             static_cast<int32_t>(snapshot.audio.channel_spectrum[1][band]) +
             1) /
            2;
        if (auto result = set_value(context, path.data(), average); !result) {
            return result;
        }
    }
    return set_hidden(context, "/qsee_audio_fft/surface/bands", false);
}

std::expected<void, std::string> App::refresh_audio_detail(AppContext &context)
{
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }

    std::array<char, 160> status{};
    bool spectrum_valid = snapshot_available(snapshot) &&
                          component_ready(snapshot.audio.microphone) &&
                          component_ready(snapshot.diagnostics.monitor) &&
                          snapshot.diagnostics.monitor_active &&
                          snapshot.audio.microphone.last_success_at_us >
                              diagnostic_activation_sample_at_us_ &&
                          snapshot.audio.channel_count == 2U &&
                          snapshot.audio.sample_rate_hz == 22050U &&
                          snapshot.audio.spectrum_sequence != 0U;
    if (!snapshot_available(snapshot)) {
        std::snprintf(status.data(), status.size(),
                      "SPECTRUM UNAVAILABLE (SNAPSHOT NOT READY)");
    } else if (!component_ready(snapshot.audio.microphone)) {
        std::snprintf(status.data(), status.size(), "SPECTRUM %s (%s)",
                      component_state(snapshot.audio.microphone),
                      component_reason(snapshot.audio.microphone));
    } else if (!component_ready(snapshot.diagnostics.monitor) ||
               !snapshot.diagnostics.monitor_active) {
        std::snprintf(status.data(), status.size(),
                      "SPECTRUM UNAVAILABLE (DIAGNOSTIC MONITOR OFF)");
    } else if (snapshot.audio.microphone.last_success_at_us <=
               diagnostic_activation_sample_at_us_) {
        std::snprintf(status.data(), status.size(),
                      "SPECTRUM STALE (WAITING FOR NEW SAMPLE)");
    } else if (snapshot.audio.channel_count != 2U ||
               snapshot.audio.sample_rate_hz != 22050U ||
               snapshot.audio.spectrum_sequence == 0U) {
        std::snprintf(status.data(), status.size(),
                      "SPECTRUM ERROR (INVALID PCM OR FFT METADATA)");
    } else {
        for (size_t channel = 0; channel < 2U && spectrum_valid; ++channel) {
            for (size_t band = 0; band < kAudioSpectrumBandCount; ++band) {
                if (snapshot.audio.channel_spectrum[channel][band] > 100U) {
                    spectrum_valid = false;
                    ESP_LOGE(kTag, "Invalid detail FFT bin: channel=%u band=%u value=%u",
                             static_cast<unsigned>(channel),
                             static_cast<unsigned>(band),
                             static_cast<unsigned>(snapshot.audio.channel_spectrum[channel][band]));
                    break;
                }
            }
        }
        std::snprintf(status.data(), status.size(),
                      spectrum_valid
                          ? "SPECTRUM READY | SEQ %lu | 22.05 kHz"
                          : "SPECTRUM ERROR (BIN OUT OF RANGE)",
                      static_cast<unsigned long>(snapshot.audio.spectrum_sequence));
    }

    if (auto result = set_text(context,
                               "/qsee_audio_detail/surface/spectrum_status",
                               status.data());
        !result) {
        return result;
    }
    if (!spectrum_valid) {
        if (auto result = set_hidden(context,
                                     "/qsee_audio_detail/surface/mic0_bands",
                                     true);
            !result) {
            return result;
        }
        return set_hidden(context, "/qsee_audio_detail/surface/mic1_bands", true);
    }

    static constexpr std::array<std::string_view, kAudioSpectrumBandCount> kMic0Colors = {
        "#078dff", "#0b9dff", "#10afff", "#12c5ff", "#16d9ff", "#28e4f2", "#45d9ee", "#5abcfb",
        "#709dff", "#8780ff", "#a466ff", "#bd51ff", "#d342ff", "#e834f8", "#f62de7", "#ff35cf",
    };
    static constexpr std::array<std::string_view, kAudioSpectrumBandCount> kMic0ColorEnds = {
        "#66d6ff", "#69dcff", "#70e4ff", "#75eaff", "#7cefff", "#8af4f0", "#9aeff1", "#a5ddff",
        "#b3c7ff", "#c1b5ff", "#d0a5ff", "#da94ff", "#e887ff", "#f27cf3", "#f97ade", "#ff82d9",
    };
    static constexpr std::array<std::string_view, kAudioSpectrumBandCount> kMic1Colors = {
        "#ff8700", "#ff9500", "#ffa400", "#ffb300", "#ffc000", "#ffcb19", "#ffd43b", "#f4dd4b",
        "#e6e45a", "#d4e660", "#bfe365", "#a8df6b", "#8fdb77", "#72d98b", "#4dd6a3", "#25d3bb",
    };
    static constexpr std::array<std::string_view, kAudioSpectrumBandCount> kMic1ColorEnds = {
        "#ffc24a", "#ffca4f", "#ffd258", "#ffda61", "#ffe26b", "#ffe982", "#ffef9a", "#eaf1a0",
        "#dcf3a2", "#c9f1a4", "#b6efa9", "#a2edb0", "#90ebbc", "#81e9c6", "#72e8d0", "#68e6d8",
    };
    std::vector<esp_brookesia::gui::BindingValueUpdate> palette_updates;
    palette_updates.reserve(kAudioSpectrumBandCount * 4U);
    for (size_t band = 0; band < kAudioSpectrumBandCount; ++band) {
        for (size_t channel = 0; channel < 2U; ++channel) {
            std::array<char, 96> path{};
            const char *row = channel == 0U ? "mic0_bands" : "mic1_bands";
            std::snprintf(path.data(), path.size(),
                          "/qsee_audio_detail/surface/%s/band%02u/bar",
                          row, static_cast<unsigned>(band));
            const auto &colors = channel == 0U ? kMic0Colors : kMic1Colors;
            const auto &color_ends = channel == 0U ? kMic0ColorEnds : kMic1ColorEnds;
            palette_updates.push_back({.absolute_path = path.data(), .key = "color",
                                       .value = std::string(colors[band])});
            palette_updates.push_back({.absolute_path = path.data(), .key = "colorEnd",
                                       .value = std::string(color_ends[band])});
        }
    }
    if (auto result = context.gui().set_binding_values(palette_updates); !result) {
        return std::unexpected("Failed to update QSee detail FFT palette: " + result.error());
    }
    for (size_t band = 0; band < kAudioSpectrumBandCount; ++band) {
        for (size_t channel = 0; channel < 2U; ++channel) {
            std::array<char, 96> path{};
            const char *row = channel == 0U ? "mic0_bands" : "mic1_bands";
            std::snprintf(path.data(), path.size(),
                          "/qsee_audio_detail/surface/%s/band%02u/bar",
                          row, static_cast<unsigned>(band));
            if (auto result = set_value(context, path.data(),
                                        snapshot.audio.channel_spectrum[channel][band]);
                !result) {
                return result;
            }
        }
    }
    if (auto result = set_hidden(context, "/qsee_audio_detail/surface/mic0_bands", false);
        !result) {
        return result;
    }
    return set_hidden(context, "/qsee_audio_detail/surface/mic1_bands", false);
}

// Render only fresh canonical IMU samples into the restored instrument panel.
std::expected<void, std::string> App::refresh_imu(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }

    std::array<char, 160> status{};
    std::array<char, 96> calibration{};
    bool sample_valid = false;
    if (!snapshot_available(snapshot)) {
        std::snprintf(
            status.data(),
            status.size(),
            "QMI8658 UNAVAILABLE (SNAPSHOT NOT READY)"
        );
    } else if (!component_ready(snapshot.imu)) {
        std::snprintf(
            status.data(),
            status.size(),
            "QMI8658 %s (%s)",
            component_state(snapshot.imu),
            component_reason(snapshot.imu)
        );
    } else if (!component_ready(snapshot.diagnostics.monitor)) {
        std::snprintf(
            status.data(),
            status.size(),
            "QMI8658 UNAVAILABLE | MONITOR %s (%s)",
            component_state(snapshot.diagnostics.monitor),
            component_reason(snapshot.diagnostics.monitor)
        );
    } else if (!snapshot.diagnostics.monitor_active) {
        std::snprintf(
            status.data(),
            status.size(),
            "QMI8658 UNAVAILABLE (DIAGNOSTIC MONITOR OFF)"
        );
    } else if (snapshot.imu.last_success_at_us <=
               diagnostic_activation_sample_at_us_) {
        std::snprintf(
            status.data(),
            status.size(),
            "QMI8658 STALE (WAITING FOR NEW SAMPLE)"
        );
    } else if (!std::isfinite(snapshot.imu.acceleration_mps2[0]) ||
               !std::isfinite(snapshot.imu.acceleration_mps2[1]) ||
               !std::isfinite(snapshot.imu.acceleration_mps2[2]) ||
               !std::isfinite(snapshot.imu.angular_velocity_dps[0]) ||
               !std::isfinite(snapshot.imu.angular_velocity_dps[1]) ||
               !std::isfinite(snapshot.imu.angular_velocity_dps[2]) ||
               !std::isfinite(snapshot.imu.temperature_c)) {
        std::snprintf(
            status.data(),
            status.size(),
            "QMI8658 ERROR (NON-FINITE SENSOR SAMPLE)"
        );
    } else {
        std::snprintf(
            status.data(),
            status.size(),
            "QMI8658 READY | t=%lld us",
            static_cast<long long>(snapshot.imu.last_success_at_us)
        );
        sample_valid = true;
    }

    if (!snapshot_available(snapshot)) {
        std::snprintf(
            calibration.data(),
            calibration.size(),
            "CALIB UNAVAILABLE"
        );
    } else if (!component_ready(snapshot.imu.calibration)) {
        std::snprintf(
            calibration.data(),
            calibration.size(),
            "CALIB %s (%s)",
            component_state(snapshot.imu.calibration),
            component_reason(snapshot.imu.calibration)
        );
    } else {
        std::snprintf(
            calibration.data(),
            calibration.size(),
            "CALIB %s | %u",
            snapshot.imu.calibrated ? "READY" : "PENDING",
            static_cast<unsigned>(snapshot.imu.calibration_samples)
        );
    }

    if (auto result = set_text(
            context,
            "/qsee_imu/surface/imu_status",
            status.data()
        ); !result) {
        return result;
    }
    if (auto result = set_text(
            context,
            "/qsee_imu/surface/calibration",
            calibration.data()
        ); !result) {
        return result;
    }

    static constexpr std::array<const char *, 3> kAccelPaths = {
        "/qsee_imu/surface/accel_x",
        "/qsee_imu/surface/accel_y",
        "/qsee_imu/surface/accel_z",
    };
    static constexpr std::array<const char *, 3> kGyroPaths = {
        "/qsee_imu/surface/gyro_x",
        "/qsee_imu/surface/gyro_y",
        "/qsee_imu/surface/gyro_z",
    };
    static constexpr std::array<const char *, 3> kGyroArcPaths = {
        "/qsee_imu/surface/gyro_x_arc",
        "/qsee_imu/surface/gyro_y_arc",
        "/qsee_imu/surface/gyro_z_arc",
    };

    if (!sample_valid) {
        for (size_t axis = 0; axis < kAccelPaths.size(); ++axis) {
            if (auto result = set_text(context, kAccelPaths[axis], "--");
                !result) {
                return result;
            }
            if (auto result = set_text(context, kGyroPaths[axis], "--");
                !result) {
                return result;
            }
            if (auto result = set_hidden(context, kGyroArcPaths[axis], true);
                !result) {
                return result;
            }
        }
        return set_text(context, "/qsee_imu/surface/temperature", "-- C");
    }

    std::array<char, 32> value{};
    for (size_t axis = 0; axis < kAccelPaths.size(); ++axis) {
        std::snprintf(
            value.data(),
            value.size(),
            "%c %+.2f",
            static_cast<int>('X' + axis),
            snapshot.imu.acceleration_mps2[axis]
        );
        if (auto result = set_text(
                context,
                kAccelPaths[axis],
                value.data()
            ); !result) {
            return result;
        }

        std::snprintf(
            value.data(),
            value.size(),
            "%c %+.2f",
            static_cast<int>('X' + axis),
            snapshot.imu.angular_velocity_dps[axis]
        );
        if (auto result = set_text(
                context,
                kGyroPaths[axis],
                value.data()
            ); !result) {
            return result;
        }

        const float normalized_activity = std::clamp(
            std::fabs(snapshot.imu.angular_velocity_dps[axis]) / 128.0F,
            0.0F,
            1.0F
        );
        std::array<char, 12> opacity{};
        std::snprintf(
            opacity.data(),
            opacity.size(),
            "%d",
            77 + static_cast<int>(
                std::lround(normalized_activity * 178.0F)
            )
        );
        if (auto result = set_binding_text(
                context,
                kGyroArcPaths[axis],
                "opacity",
                opacity.data()
            ); !result) {
            return result;
        }
        if (auto result = set_hidden(
                context,
                kGyroArcPaths[axis],
                false
            ); !result) {
            return result;
        }
    }

    std::snprintf(
        value.data(),
        value.size(),
        "%.1f C",
        snapshot.imu.temperature_c
    );
    return set_text(context, "/qsee_imu/surface/temperature", value.data());
}

std::expected<void, std::string> App::refresh_settings(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }
    std::array<char, 256> text{};
    std::array<char, 16> full_snapshot_cadence{};
    format_full_snapshot_cadence(
        full_snapshot_cadence.data(),
        full_snapshot_cadence.size(),
        snapshot.diagnostics.full_snapshot_period_ms
    );

    if (!component_ready(snapshot.diagnostics.full_snapshot_period)) {
        std::snprintf(
            text.data(),
            text.size(),
            "FULL SNAPSHOT | %s (%s)",
            component_state(snapshot.diagnostics.full_snapshot_period),
            component_reason(snapshot.diagnostics.full_snapshot_period)
        );
    } else if (std::find(
                   kFullSnapshotPeriodsMs.begin(),
                   kFullSnapshotPeriodsMs.end(),
                   snapshot.diagnostics.full_snapshot_period_ms
               ) == kFullSnapshotPeriodsMs.end()) {
        std::snprintf(
            text.data(),
            text.size(),
            "FULL SNAPSHOT | ERROR (INVALID SAVED CADENCE: %lu ms)",
            static_cast<unsigned long>(
                snapshot.diagnostics.full_snapshot_period_ms
            )
        );
    } else if (!component_ready(
                   snapshot.diagnostics.full_snapshot_monitor
               )) {
        std::snprintf(
            text.data(),
            text.size(),
            "FULL SNAPSHOT | %s | monitor %s (%s)",
            full_snapshot_cadence.data(),
            component_state(snapshot.diagnostics.full_snapshot_monitor),
            component_reason(snapshot.diagnostics.full_snapshot_monitor)
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "FULL SNAPSHOT | %s | %s",
            full_snapshot_cadence.data(),
            snapshot.diagnostics.full_snapshot_monitor_active
                ? "ACTIVE"
                : "STANDBY"
        );
    }
    if (auto result = set_text(
            context,
            "/qsee_settings/surface/settings_scroller/"
            "full_snapshot_status",
            text.data()
        ); !result) {
        return result;
    }

    if (!component_ready(snapshot.diagnostics.sample_period)) {
        std::snprintf(
            text.data(),
            text.size(),
            "Diagnostic stream | %s (%s)",
            component_state(snapshot.diagnostics.sample_period),
            component_reason(snapshot.diagnostics.sample_period)
        );
    } else if (snapshot.diagnostics.sample_period_ms < 100U ||
               snapshot.diagnostics.sample_period_ms > 2000U) {
        std::snprintf(
            text.data(),
            text.size(),
            "Diagnostic stream | ERROR (INVALID SAVED INTERVAL: %lu ms)",
            static_cast<unsigned long>(snapshot.diagnostics.sample_period_ms)
        );
    } else if (!component_ready(snapshot.diagnostics.monitor)) {
        std::snprintf(
            text.data(),
            text.size(),
            "Diagnostic stream | %lu ms | monitor %s (%s)",
            static_cast<unsigned long>(snapshot.diagnostics.sample_period_ms),
            component_state(snapshot.diagnostics.monitor),
            component_reason(snapshot.diagnostics.monitor)
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "Diagnostic stream | %lu ms | monitor %s",
            static_cast<unsigned long>(snapshot.diagnostics.sample_period_ms),
            snapshot.diagnostics.monitor_active ? "ON" : "OFF"
        );
    }
    if (auto result = set_text(
            context,
            "/qsee_settings/surface/settings_scroller/period_value",
            text.data()
        ); !result) {
        return result;
    }

    if (!component_ready(snapshot.diagnostics.imu_smoothing)) {
        std::snprintf(
            text.data(),
            text.size(),
            "IMU input smoothing | %s (%s)",
            component_state(snapshot.diagnostics.imu_smoothing),
            component_reason(snapshot.diagnostics.imu_smoothing)
        );
    } else if (snapshot.diagnostics.imu_smoothing_percent > 90U) {
        std::snprintf(
            text.data(),
            text.size(),
            "IMU input smoothing | ERROR (INVALID SAVED VALUE: %lu%%)",
            static_cast<unsigned long>(snapshot.diagnostics.imu_smoothing_percent)
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "IMU input smoothing | %lu%%",
            static_cast<unsigned long>(snapshot.diagnostics.imu_smoothing_percent)
        );
    }
    if (auto result = set_text(
            context,
            "/qsee_settings/surface/settings_scroller/smoothing_value",
            text.data()
        ); !result) {
        return result;
    }

    std::array<char, 112> storage_state{};
    std::array<char, 112> mirror_state{};
    if (storage_cleanup_failed(snapshot.storage)) {
        std::snprintf(
            storage_state.data(),
            storage_state.size(),
            "SD TERMINAL (cleanup=%s error=%s complete=%s poisoned=%s)",
            storage_cleanup_stage_name(snapshot.storage.cleanup_stage),
            esp_err_to_name(snapshot.storage.cleanup_error),
            snapshot.storage.cleanup_complete ? "yes" : "no",
            snapshot.storage.poisoned ? "yes" : "no"
        );
    } else if (storage_format_required(snapshot.storage)) {
        std::snprintf(
            storage_state.data(),
            storage_state.size(),
            "SD FORMAT REQUIRED (%s)",
            component_reason(snapshot.storage)
        );
    } else if (storage_mounted_ready(snapshot.storage)) {
        std::snprintf(
            storage_state.data(),
            storage_state.size(),
            "SD READY"
        );
    } else if (!snapshot.storage.card_response_verified) {
        std::snprintf(
            storage_state.data(),
            storage_state.size(),
            "SD UNAVAILABLE (CARD/TRANSPORT UNCONFIRMED; %s)",
            component_reason(snapshot.storage)
        );
    } else {
        std::snprintf(
            storage_state.data(),
            storage_state.size(),
            "SD %s (%s)",
            component_state(snapshot.storage),
            component_reason(snapshot.storage)
        );
    }
    if (component_ready(snapshot.storage.logger_mirror)) {
        std::snprintf(
            mirror_state.data(),
            mirror_state.size(),
            "mirror requested %s | active %s",
            snapshot.storage.logger_mirror_requested ? "ON" : "OFF",
            snapshot.storage.logger_mirror_enabled ? "ON" : "OFF"
        );
    } else {
        if (snapshot.storage.logger_mirror_errno != 0) {
            std::snprintf(
                mirror_state.data(),
                mirror_state.size(),
                "mirror %s (%s; errno=%d; failures=%lu)",
                component_state(snapshot.storage.logger_mirror),
                component_reason(snapshot.storage.logger_mirror),
                snapshot.storage.logger_mirror_errno,
                static_cast<unsigned long>(
                    snapshot.storage.logger_mirror_failure_count
                )
            );
        } else {
            std::snprintf(
                mirror_state.data(),
                mirror_state.size(),
                "mirror %s (%s)",
                component_state(snapshot.storage.logger_mirror),
                component_reason(snapshot.storage.logger_mirror)
            );
        }
    }
    std::snprintf(
        text.data(),
        text.size(),
        "%s | %s",
        storage_state.data(),
        mirror_state.data()
    );
    if (auto result = set_text(
            context,
            "/qsee_settings/surface/settings_scroller/storage_value",
            text.data()
        ); !result) {
        return result;
    }
    if (auto result = set_text(
            context,
            "/qsee_settings/surface/settings_scroller/mirror/label",
            component_ready(snapshot.storage.logger_mirror)
                ? snapshot.storage.logger_mirror_requested
                    ? "\xef\x81\xae"
                    : "\xef\x81\xb0"
                : "\xef\x81\xb1"
        ); !result) {
        return result;
    }

    std::array<char, 64> wifi_state{};
    std::array<char, 64> ble_state{};
    if (radio_deferred_by_policy(snapshot.radios.wifi)) {
        std::snprintf(wifi_state.data(), wifi_state.size(), "OFF BY POLICY");
    } else if (component_ready(snapshot.radios.wifi)) {
        std::snprintf(
            wifi_state.data(),
            wifi_state.size(),
            "%s",
            snapshot.radios.wifi.connected
                ? "CONNECTED"
                : snapshot.radios.wifi.started ? "STARTED" : "STOPPED"
        );
    } else {
        format_component_status(wifi_state, snapshot.radios.wifi);
    }
    if (radio_deferred_by_policy(snapshot.radios.ble)) {
        std::snprintf(ble_state.data(), ble_state.size(), "OFF BY POLICY");
    } else if (component_ready(snapshot.radios.ble)) {
        std::snprintf(
            ble_state.data(),
            ble_state.size(),
            "%s",
            snapshot.radios.ble.connected
                ? "CONNECTED"
                : snapshot.radios.ble.provisioning
                    ? "PROVISIONING"
                    : snapshot.radios.ble.advertising ? "ADVERTISING" : "IDLE"
        );
    } else {
        format_component_status(ble_state, snapshot.radios.ble);
    }
    std::snprintf(
        text.data(),
        text.size(),
        "RADIOS | Wi-Fi %s | BLE %s",
        wifi_state.data(),
        ble_state.data()
    );
    if (auto result = set_text(
            context,
            "/qsee_settings/surface/settings_scroller/radio_value",
            text.data()
        ); !result) {
        return result;
    }
    if (auto result = set_text(
            context,
            "/qsee_settings/surface/settings_scroller/radio_start/label",
            "\xef\x80\xa1"
        ); !result) {
        return result;
    }

    if (radio_deferred_by_policy(snapshot.radios.wifi)) {
        std::snprintf(
            text.data(),
            text.size(),
            "WI-FI | OFF BY POLICY\nSTART MANUALLY FROM SETTINGS"
        );
    } else if (!component_ready(snapshot.radios.wifi)) {
        std::array<char, 96> wifi_detail{};
        format_component_status(wifi_detail, snapshot.radios.wifi);
        std::snprintf(
            text.data(),
            text.size(),
            "WI-FI | %s",
            wifi_detail.data()
        );
    } else if (snapshot.radios.wifi.connected &&
               snapshot.radios.wifi.ssid[0] == '\0') {
        std::snprintf(
            text.data(),
            text.size(),
            "WI-FI | ERROR (CONNECTED WITHOUT SSID)"
        );
    } else if (snapshot.radios.wifi.ip_acquired &&
               snapshot.radios.wifi.ip_address[0] == '\0') {
        std::snprintf(
            text.data(),
            text.size(),
            "WI-FI | ERROR (IP ACQUIRED WITHOUT ADDRESS)"
        );
    } else if (snapshot.radios.wifi.connected) {
        std::snprintf(
            text.data(),
            text.size(),
            "WI-FI | CONNECTED\nSSID %s | IP %s\nCH %u | RSSI %d dBm",
            snapshot.radios.wifi.ssid,
            snapshot.radios.wifi.ip_acquired
                ? snapshot.radios.wifi.ip_address
                : "PENDING",
            static_cast<unsigned>(snapshot.radios.wifi.channel),
            static_cast<int>(snapshot.radios.wifi.rssi)
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "WI-FI | %s\nCREDENTIALS %s | RECONNECT %s",
            snapshot.radios.wifi.started ? "STARTED" : "STOPPED",
            snapshot.radios.wifi.credentials_configured ? "SET" : "NOT SET",
            snapshot.radios.wifi.reconnect_pending ? "PENDING" : "IDLE"
        );
    }
    if (auto result = set_text(
            context,
            "/qsee_settings/surface/settings_scroller/wifi_detail",
            text.data()
        ); !result) {
        return result;
    }

    if (radio_deferred_by_policy(snapshot.radios.ble)) {
        std::snprintf(
            text.data(),
            text.size(),
            "BLUETOOTH | OFF BY POLICY\nSTART MANUALLY FROM SETTINGS"
        );
    } else if (!component_ready(snapshot.radios.ble)) {
        std::array<char, 96> ble_detail{};
        format_component_status(ble_detail, snapshot.radios.ble);
        std::snprintf(
            text.data(),
            text.size(),
            "BLUETOOTH | %s",
            ble_detail.data()
        );
    } else if (snapshot.radios.ble.device_name[0] == '\0') {
        std::snprintf(
            text.data(),
            text.size(),
            "BLUETOOTH | ERROR (EMPTY DEVICE NAME)"
        );
    } else {
        const char *ble_mode =
            snapshot.radios.ble.connected
                ? "CONNECTED"
                : snapshot.radios.ble.provisioning
                    ? "PROVISIONING"
                    : snapshot.radios.ble.advertising
                        ? "ADVERTISING"
                        : "IDLE";
        std::snprintf(
            text.data(),
            text.size(),
            "BLUETOOTH | %s\nDEVICE %s | LINKS %lu",
            ble_mode,
            snapshot.radios.ble.device_name,
            static_cast<unsigned long>(
                snapshot.radios.ble.connection_count
            )
        );
    }
    if (auto result = set_text(
            context,
            "/qsee_settings/surface/settings_scroller/ble_detail",
            text.data()
        ); !result) {
        return result;
    }

    if (!component_ready(snapshot.gps)) {
        std::array<char, 96> gps_detail{};
        format_component_status(gps_detail, snapshot.gps);
        std::snprintf(
            text.data(),
            text.size(),
            "GPS | %s",
            gps_detail.data()
        );
    } else if (!snapshot.gps.has_fix) {
        std::snprintf(text.data(), text.size(), "GPS | RECEIVER READY | NO FIX");
    } else if (!gps_fix_valid(snapshot.gps)) {
        std::snprintf(
            text.data(),
            text.size(),
            "GPS | NO VALID FIX\nQUALITY %u | SATELLITES %u",
            static_cast<unsigned>(snapshot.gps.fix_quality),
            static_cast<unsigned>(snapshot.gps.satellites)
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "GPS | FIX %u | SAT %u | HDOP %.1f\n%.5f, %.5f",
            static_cast<unsigned>(snapshot.gps.fix_quality),
            static_cast<unsigned>(snapshot.gps.satellites),
            static_cast<double>(snapshot.gps.hdop),
            snapshot.gps.latitude_degrees,
            snapshot.gps.longitude_degrees
        );
    }
    if (auto result = set_text(
            context,
            "/qsee_settings/surface/settings_scroller/gps_detail",
            text.data()
        ); !result) {
        return result;
    }

    if (!component_ready(snapshot.ui.brightness)) {
        std::snprintf(
            text.data(),
            text.size(),
            "AMOLED brightness | %s (%s)",
            component_state(snapshot.ui.brightness),
            component_reason(snapshot.ui.brightness)
        );
    } else if (!component_ready(snapshot.ui.display)) {
        std::snprintf(
            text.data(),
            text.size(),
            "AMOLED BRIGHTNESS | %s (%s)",
            component_state(snapshot.ui.display),
            component_reason(snapshot.ui.display)
        );
    } else if (snapshot.ui.brightness_percent < 10U ||
               snapshot.ui.brightness_percent > 100U) {
        std::snprintf(
            text.data(),
            text.size(),
            "AMOLED brightness | ERROR (INVALID SAVED VALUE: %lu%%)",
            static_cast<unsigned long>(snapshot.ui.brightness_percent)
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "AMOLED brightness | %lu%%",
            static_cast<unsigned long>(snapshot.ui.brightness_percent)
        );
    }
    if (auto result = set_text(
            context,
            "/qsee_settings/surface/settings_scroller/brightness_value",
            text.data()
        ); !result) {
        return result;
    }

    const OrientationSnapshot &orientation = snapshot.ui.orientation;
    if (!component_ready(orientation)) {
        std::snprintf(
            text.data(),
            text.size(),
            "AUTO ROTATION | %s (%s)",
            component_state(orientation),
            component_reason(orientation)
        );
    } else {
        const char *source =
            !orientation.auto_rotation_enabled
                ? "DISABLED"
                : orientation.gravity_valid
                    ? "GRAVITY LOCKED"
                    : component_ready(snapshot.imu)
                        ? "GYRO TRACKING"
                        : "IMU WAITING";
        std::snprintf(
            text.data(),
            text.size(),
            "AUTO ROTATION | %s%s\nPHYSICAL %u° | RENDER %u°",
            source,
            orientation.rotation_pending ? " / PENDING" : "",
            static_cast<unsigned>(orientation.physical_orientation_degrees),
            static_cast<unsigned>(orientation.rendering_rotation_degrees)
        );
    }
    if (auto result = set_text(
            context,
            "/qsee_settings/surface/settings_scroller/rotation_value",
            text.data()
        ); !result) {
        return result;
    }
    if (auto result = set_text(
            context,
            "/qsee_settings/surface/settings_scroller/rotation_toggle/label",
            orientation.auto_rotation_enabled
                ? "\xef\x88\x85"
                : "\xef\x88\x84"
        ); !result) {
        return result;
    }

    std::vector<esp_brookesia::gui::BindingValueUpdate>
        timing_updates = {
            {
                .absolute_path =
                    "/qsee_settings/surface/settings_scroller/"
                    "command_arc_timeout",
                .key = "selectedIndex",
                .value = std::to_string(adaptive_chrome_.selected_index(
                    AdaptiveChromeTiming::CommandArc
                )),
            },
            {
                .absolute_path =
                    "/qsee_settings/surface/settings_scroller/"
                    "status_arc_timeout",
                .key = "selectedIndex",
                .value = std::to_string(adaptive_chrome_.selected_index(
                    AdaptiveChromeTiming::StatusArc
                )),
            },
            {
                .absolute_path =
                    "/qsee_settings/surface/settings_scroller/"
                    "display_dim_timeout",
                .key = "selectedIndex",
                .value = std::to_string(adaptive_chrome_.selected_index(
                    AdaptiveChromeTiming::DisplayDim
                )),
            },
            {
                .absolute_path =
                    "/qsee_settings/surface/settings_scroller/"
                    "display_off_delay",
                .key = "selectedIndex",
                .value = std::to_string(adaptive_chrome_.selected_index(
                    AdaptiveChromeTiming::DisplayOff
                )),
            },
        };
    const auto full_snapshot_period = std::find(
        kFullSnapshotPeriodsMs.begin(),
        kFullSnapshotPeriodsMs.end(),
        snapshot.diagnostics.full_snapshot_period_ms
    );
    if (component_ready(snapshot.diagnostics.full_snapshot_period) &&
        full_snapshot_period != kFullSnapshotPeriodsMs.end()) {
        timing_updates.push_back({
            .absolute_path =
                "/qsee_settings/surface/settings_scroller/"
                "full_snapshot_period",
            .key = "selectedIndex",
            .value = std::to_string(std::distance(
                kFullSnapshotPeriodsMs.begin(),
                full_snapshot_period
            )),
        });
    }
    auto timing_result = context.gui().set_binding_values(timing_updates);
    if (!timing_result) {
        return std::unexpected(
            "Failed to refresh Adaptive Chrome dropdowns: " +
            timing_result.error()
        );
    }
    return {};
}

std::expected<void, std::string> App::show_error_dialog(
    AppContext &context,
    std::string_view title,
    std::string_view details
)
{
    return alert_service_.show_alert(
        context,
        title,
        details,
        esp_brookesia::system::core::MessageDialogIcon::Critical
    );
}

std::expected<void, std::string> App::request_log_search(AppContext &context)
{
    if (search_keyboard_ != esp_brookesia::system::core::INVALID_KEYBOARD_REQUEST_ID) {
        return {};
    }

    esp_brookesia::system::core::KeyboardRequestOptions options = {
        .title = "Search live logs",
        .placeholder = "Empty shows every line",
        .initial_text = log_query_,
        .max_length = 32,
        .password = false,
        .mode = "text",
        .allowed_modes = {"text"},
    };
    auto result = context.system_service().show_keyboard(
        std::move(options),
        [this](const esp_brookesia::system::core::KeyboardResult &keyboard_result) {
            search_keyboard_ =
                esp_brookesia::system::core::INVALID_KEYBOARD_REQUEST_ID;
            if (context_ == nullptr || !keyboard_result.confirmed) {
                return;
            }
            log_query_ = keyboard_result.text;
            if (auto refresh_result = refresh_logs(*context_); !refresh_result) {
                ESP_LOGE(kTag, "Log search refresh failed: %s", refresh_result.error().c_str());
            }
        }
    );
    if (!result) {
        return std::unexpected("Failed to show log search keyboard: " + result.error());
    }
    search_keyboard_ = *result;
    return {};
}

std::expected<void, std::string> App::request_sd_format(AppContext &context)
{
    if (alert_service_.dialog_visible()) {
        return {};
    }

    const StorageSnapshot storage = DeviceSnapshotService::instance().snapshot().storage;
    if (!storage_format_required(storage)) {
        std::array<char, 192> informative_text{};
        if (storage_cleanup_failed(storage)) {
            std::snprintf(
                informative_text.data(),
                informative_text.size(),
                "SD is in a terminal cleanup state: stage=%s, error=%s, "
                "complete=%s, poisoned=%s. Formatting is blocked.",
                storage_cleanup_stage_name(storage.cleanup_stage),
                esp_err_to_name(storage.cleanup_error),
                storage.cleanup_complete ? "yes" : "no",
                storage.poisoned ? "yes" : "no"
            );
        } else if (storage_mounted_ready(storage)) {
            std::snprintf(
                informative_text.data(),
                informative_text.size(),
                "The card is mounted and valid. No format is needed."
            );
        } else if (!storage.card_response_verified) {
            std::snprintf(
                informative_text.data(),
                informative_text.size(),
                "SD UNAVAILABLE: card presence or transport response was not "
                "verified (%s).",
                component_reason(storage)
            );
        } else {
            std::snprintf(
                informative_text.data(),
                informative_text.size(),
                "SD %s: %s. A format-required state was not verified.",
                component_state(storage),
                component_reason(storage)
            );
        }
        auto result = alert_service_.show_notice(
            context,
            "SD format is not available",
            informative_text.data(),
            std::strcmp(component_state(storage), "ERROR") == 0
                ? esp_brookesia::system::core::MessageDialogIcon::Critical
                : esp_brookesia::system::core::MessageDialogIcon::Information
        );
        if (!result) {
            return std::unexpected("Failed to show SD status dialog: " + result.error());
        }
        if (std::strcmp(component_state(storage), "ERROR") == 0) {
            return std::unexpected(
                "SD format status failed: " + std::string(component_reason(storage))
            );
        }
        return {};
    }

    auto result = alert_service_.show_confirmation(
        context,
        "Format SD card?",
        "This erases the card. Formatting starts only after you press FORMAT.",
        "CANCEL",
        "FORMAT",
        esp_brookesia::system::core::MessageDialogButtonRole::Destructive,
        [this]() {
            const esp_err_t format_result = qsee_storage_request_format();
            if (format_result != ESP_OK) {
                ESP_LOGE(
                    kTag,
                    "Explicit SD format request failed: %s",
                    esp_err_to_name(format_result)
                );
                if (context_ != nullptr) {
                    std::array<char, 128> details{};
                    std::snprintf(
                        details.data(),
                        details.size(),
                        "The SD format request was rejected: %s",
                        esp_err_to_name(format_result)
                    );
                    if (auto dialog_result = show_error_dialog(
                            *context_,
                            "SD format not started",
                            details.data()
                        ); !dialog_result) {
                        ESP_LOGE(kTag, "%s", dialog_result.error().c_str());
                    }
                }
            } else {
                ESP_LOGW(kTag, "Explicit user-authorized SD format requested");
            }
            if (context_ != nullptr) {
                if (auto refresh_result = refresh_hardware(*context_); !refresh_result) {
                    ESP_LOGE(
                        kTag,
                        "Hardware view refresh failed: %s",
                        refresh_result.error().c_str()
                    );
                }
            }
        }
    );
    if (!result) {
        return std::unexpected("Failed to show SD format confirmation: " + result.error());
    }
    return {};
}

class AppProvider final : public esp_brookesia::system::core::IAppProvider {
public:
    esp_brookesia::system::core::AppManifest get_manifest() const override
    {
        return App().get_manifest();
    }

    std::shared_ptr<esp_brookesia::system::core::IApp> create_app() override
    {
        return std::make_shared<App>();
    }
};

BROOKESIA_SYSTEM_CORE_APP_PROVIDER_REGISTER_WITH_SYMBOL(
    AppProvider,
    "ai.qsee.device-console",
    qsee_app_provider_symbol
);

}  // namespace qsee
