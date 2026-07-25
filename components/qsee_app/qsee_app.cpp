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
#include <string>
#include <utility>
#include <vector>

#include "esp_log.h"
#include "qsee_background_asset.hpp"
#include "qsee_device_snapshot.hpp"
#include "qsee_logger.h"
#include "qsee_platform.h"

namespace {

using esp_brookesia::system::core::AppContext;
using esp_brookesia::system::core::MessageDialogButton;
using esp_brookesia::system::core::MessageDialogButtonRole;
using esp_brookesia::system::core::MessageDialogIcon;
using esp_brookesia::system::core::MessageDialogOptions;

constexpr char kTag[] = "qsee_app";
constexpr char kGuiRoot[] = "res/root.json";
constexpr char kScreenFlow[] = "qsee_content";
constexpr char kRefreshTimer[] = "qsee.refresh";
constexpr char kBackgroundImageId[] = "qsee.background";
constexpr size_t kLogBufferBytes = 4096;
constexpr char kStatusReady[] = "#20dfc1";
constexpr char kStatusWarning[] = "#f0b44d";
constexpr char kStatusMissing[] = "#eb4d5c";
constexpr char kStatusFault[] = "#ff3859";
constexpr char kStatusMuted[] = "#607889";
constexpr double kAudioMeterFloorDbfs = -60.0;
constexpr double kPcm16FullScale = 32768.0;
constexpr std::array<uint32_t, 7> kChromeTimeoutSeconds = {
    10U, 15U, 30U, 60U, 120U, 300U, 600U,
};
// Fifteen worst-case 256-byte ring lines fit in the persistent 4 KB buffer.
constexpr size_t kLogSnapshotLines = 15;

bool storage_cleanup_failed(const qsee::StorageSnapshot &storage)
{
    return storage.poisoned ||
           !storage.cleanup_complete ||
           storage.cleanup_error != ESP_OK;
}

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

bool storage_format_required(const qsee::StorageSnapshot &storage)
{
    return storage.format_required &&
           storage.mount_outcome == qsee::StorageMountOutcome::FormatRequired &&
           storage.card_response_verified &&
           storage.cleanup_complete &&
           storage.cleanup_error == ESP_OK &&
           !storage.poisoned &&
           !storage.mounted;
}

bool snapshot_available(const qsee::DeviceSnapshot &snapshot)
{
    return snapshot.sequence > 0 && snapshot.last_update_attempt_at_us > 0;
}

bool component_ready(const qsee::ComponentSnapshot &component)
{
    return component.health == qsee::DeviceHealth::Ready &&
           component.last_error == ESP_OK &&
           component.data_valid && !component.data_stale;
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

// Map only canonical component health to the status-bar semantic palette.
const char *component_status_color(const qsee::ComponentSnapshot &component)
{
    if (component.last_error != ESP_OK) {
        return kStatusFault;
    }
    if (component.data_stale) {
        return kStatusWarning;
    }
    switch (component.health) {
    case qsee::DeviceHealth::Ready:
        return component.data_valid ? kStatusReady : kStatusWarning;
    case qsee::DeviceHealth::Degraded:
        return kStatusWarning;
    case qsee::DeviceHealth::Missing:
        return kStatusMissing;
    case qsee::DeviceHealth::Unknown:
        return kStatusMuted;
    }
    std::abort();
}

// Select the battery glyph from a validated percentage without inventing a level.
const char *battery_status_icon(const qsee::BatterySnapshot &battery)
{
    if (!component_ready(battery) || !battery.battery_present ||
        battery.level_percent < 0 || battery.level_percent > 100) {
        return "\xef\x89\x84";
    }
    if (battery.level_percent <= 12) {
        return "\xef\x89\x84";
    }
    if (battery.level_percent <= 35) {
        return "\xef\x89\x83";
    }
    if (battery.level_percent <= 60) {
        return "\xef\x89\x82";
    }
    if (battery.level_percent <= 85) {
        return "\xef\x89\x81";
    }
    return "\xef\x89\x80";
}

bool gps_fix_valid(const qsee::GpsSnapshot &gps)
{
    return gps.has_fix &&
           gps.utc_valid &&
           gps.fix_quality > 0 &&
           gps.satellites > 0 &&
           std::isfinite(gps.latitude_degrees) &&
           gps.latitude_degrees >= -90.0 &&
           gps.latitude_degrees <= 90.0 &&
           std::isfinite(gps.longitude_degrees) &&
           gps.longitude_degrees >= -180.0 &&
           gps.longitude_degrees <= 180.0 &&
           std::isfinite(gps.altitude_m) &&
           std::isfinite(gps.hdop) &&
           gps.hdop >= 0.0F &&
           std::isfinite(gps.speed_mps) &&
           gps.speed_mps >= 0.0F &&
           std::isfinite(gps.course_degrees) &&
           gps.course_degrees >= 0.0F &&
           gps.course_degrees <= 360.0F;
}

bool storage_mounted_ready(const qsee::StorageSnapshot &storage)
{
    return component_ready(storage) &&
           storage.mount_outcome == qsee::StorageMountOutcome::Mounted &&
           storage.mounted &&
           storage.card_response_verified &&
           !storage_cleanup_failed(storage);
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
                .screen_flow = kScreenFlow,
                .layer = esp_brookesia::system::core::GuiAppLayer::AppDefault,
            },
        },
    };
}

std::expected<void, std::string> App::on_start(AppContext &context)
{
    context_ = &context;
    screen_ = Screen::Welcome;
    logs_paused_ = false;
    logs_follow_latest_ = true;
    background_registered_ = false;
    background_bound_ = false;
    log_filter_mode_ = 0;
    diagnostic_activation_sample_at_us_ = 0;
    log_query_.clear();
    log_source_buffer_.reset(new (std::nothrow) char[kLogBufferBytes]);
    log_filtered_buffer_.reset(new (std::nothrow) char[kLogBufferBytes]);
    if (log_source_buffer_ == nullptr || log_filtered_buffer_ == nullptr) {
        log_source_buffer_.reset();
        log_filtered_buffer_.reset();
        context_ = nullptr;
        return std::unexpected("Failed to allocate persistent QSee log buffers");
    }
    log_source_buffer_[0] = '\0';
    log_filtered_buffer_[0] = '\0';
    BackgroundAssetCache::instance().request();
    if (auto result = adaptive_command_bar_.begin(); !result) {
        log_source_buffer_.reset();
        log_filtered_buffer_.reset();
        context_ = nullptr;
        return result;
    }

    const std::vector<std::string> actions = {
        "qsee.chrome.activity",
        "qsee.open.welcome",
        "qsee.open.logs",
        "qsee.open.hardware",
        "qsee.open.audio",
        "qsee.open.audio_fft",
        "qsee.open.imu",
        "qsee.open.settings",
        "qsee.open.about",
        "qsee.logs.back",
        "qsee.hardware.back",
        "qsee.audio.back",
        "qsee.audio_fft.back",
        "qsee.imu.back",
        "qsee.settings.back",
        "qsee.about.back",
        "qsee.logs.pause",
        "qsee.logs.clear",
        "qsee.logs.follow",
        "qsee.logs.filter",
        "qsee.logs.search",
        "qsee.storage.format",
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
        "qsee.settings.chrome.timeout.next",
    };
    auto subscription_result = context.gui().subscribe_actions(actions);
    if (!subscription_result) {
        log_source_buffer_.reset();
        log_filtered_buffer_.reset();
        context_ = nullptr;
        return std::unexpected(
            "Failed to subscribe QSee UI actions: " + subscription_result.error()
        );
    }
    if (auto result = configure_adaptive_chrome(context); !result) {
        adaptive_command_bar_.shutdown(context.gui());
        log_source_buffer_.reset();
        log_filtered_buffer_.reset();
        context_ = nullptr;
        return result;
    }

    const esp_err_t monitor_result = qsee_hardware_set_diagnostic_monitor(false);
    if (monitor_result != ESP_OK) {
        adaptive_command_bar_.shutdown(context.gui());
        log_source_buffer_.reset();
        log_filtered_buffer_.reset();
        context_ = nullptr;
        return std::unexpected(
            "Failed to disable QSee diagnostic monitor at startup: " +
            std::string(esp_err_to_name(monitor_result))
        );
    }

    auto timer_result = context.timer().start_periodic(kRefreshTimer, 1000);
    if (!timer_result) {
        adaptive_command_bar_.shutdown(context.gui());
        log_source_buffer_.reset();
        log_filtered_buffer_.reset();
        context_ = nullptr;
        return std::unexpected("Failed to start QSee refresh timer: " + timer_result.error());
    }
    refresh_timer_ = *timer_result;

    auto refresh_result = refresh(context);
    if (!refresh_result) {
        if (!context.timer().stop(refresh_timer_)) {
            ESP_LOGE(kTag, "Failed to stop QSee refresh timer after startup failure");
        }
        refresh_timer_ = esp_brookesia::system::core::INVALID_TIMER_ID;
        adaptive_command_bar_.shutdown(context.gui());
        log_source_buffer_.reset();
        log_filtered_buffer_.reset();
        context_ = nullptr;
        return refresh_result;
    }

    ESP_LOGI(kTag, "QSee.ai native app started on Brookesia System 0.8");
    return {};
}

std::expected<void, std::string> App::on_pause(AppContext &context)
{
    adaptive_command_bar_.shutdown(context.gui());
    const esp_err_t result = qsee_hardware_set_diagnostic_monitor(false);
    if (result != ESP_OK) {
        return std::unexpected(
            "Failed to pause QSee diagnostic monitor: " +
            std::string(esp_err_to_name(result))
        );
    }
    return {};
}

std::expected<void, std::string> App::on_resume(AppContext &context)
{
    if (auto result = apply_diagnostic_monitor(context, screen_); !result) {
        return result;
    }
    if (auto result = configure_adaptive_chrome(context); !result) {
        return result;
    }
    return refresh(context);
}

std::expected<void, std::string> App::on_stop(AppContext &context)
{
    adaptive_command_bar_.shutdown(context.gui());
    const esp_err_t monitor_result = qsee_hardware_set_diagnostic_monitor(false);
    std::string stop_error;
    if (monitor_result != ESP_OK) {
        stop_error =
            "Failed to stop QSee diagnostic monitor: " +
            std::string(esp_err_to_name(monitor_result));
        ESP_LOGE(kTag, "%s", stop_error.c_str());
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
    if (format_dialog_ != esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID) {
        auto hide_result = context.system_service().hide_message_dialog(format_dialog_);
        if (!hide_result) {
            ESP_LOGE(
                kTag,
                "Failed to hide QSee format dialog: %s",
                hide_result.error().c_str()
            );
            if (stop_error.empty()) {
                stop_error = "Failed to hide QSee format dialog: " + hide_result.error();
            }
        }
        format_dialog_ = esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
    }
    if (error_dialog_ != esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID) {
        auto hide_result = context.system_service().hide_message_dialog(error_dialog_);
        if (!hide_result) {
            ESP_LOGE(
                kTag,
                "Failed to hide QSee error dialog: %s",
                hide_result.error().c_str()
            );
            if (stop_error.empty()) {
                stop_error = "Failed to hide QSee error dialog: " + hide_result.error();
            }
        }
        error_dialog_ = esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
    }
    if (background_registered_ &&
        !context.gui().unregister_image(kBackgroundImageId)) {
        ESP_LOGE(kTag, "Failed to unregister QSee background image");
        if (stop_error.empty()) {
            stop_error = "Failed to unregister QSee background image";
        }
    }
    background_registered_ = false;
    background_bound_ = false;

    log_source_buffer_.reset();
    log_filtered_buffer_.reset();
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
        screen == Screen::Imu;
    if (enabled) {
        const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
        diagnostic_activation_sample_at_us_ =
            screen == Screen::Audio || screen == Screen::AudioFft
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

// Select only the lower commands and passive status chrome for the active route.
std::expected<void, std::string> App::configure_adaptive_chrome(
    AppContext &context
)
{
    static constexpr std::array<std::string_view, 0> kNoControls{};
    static constexpr std::array<std::string_view, 6> kWelcomeControls = {
        "/qsee_welcome/surface/menu/logs",
        "/qsee_welcome/surface/menu/audio",
        "/qsee_welcome/surface/menu/imu",
        "/qsee_welcome/surface/menu/hardware",
        "/qsee_welcome/surface/menu/about",
        "/qsee_welcome/surface/menu/settings",
    };
    static constexpr std::array<std::string_view, 5> kLogControls = {
        "/qsee_logs/surface/controls/pause",
        "/qsee_logs/surface/controls/clear",
        "/qsee_logs/surface/controls/follow",
        "/qsee_logs/surface/controls/filter",
        "/qsee_logs/surface/controls/search",
    };
    static constexpr std::array<std::string_view, 1> kAudioControls = {
        "/qsee_audio/fft",
    };
    static constexpr std::array<std::string_view, 1> kWelcomeVisuals = {
        "/qsee_welcome/chrome/status",
    };
    static constexpr std::array<std::string_view, 1> kLogVisuals = {
        "/qsee_logs/chrome/status",
    };
    static constexpr std::array<std::string_view, 1> kHardwareVisuals = {
        "/qsee_hardware/chrome/status",
    };
    static constexpr std::array<std::string_view, 1> kAudioVisuals = {
        "/qsee_audio/chrome/status",
    };
    static constexpr std::array<std::string_view, 1> kAudioFftVisuals = {
        "/qsee_audio_fft/chrome/status",
    };
    static constexpr std::array<std::string_view, 1> kImuVisuals = {
        "/qsee_imu/chrome/status",
    };
    static constexpr std::array<std::string_view, 1> kSettingsVisuals = {
        "/qsee_settings/chrome/status",
    };
    static constexpr std::array<std::string_view, 1> kAboutVisuals = {
        "/qsee_about/chrome/status",
    };

    switch (screen_) {
    case Screen::Welcome:
        return adaptive_command_bar_.activate(
            context.gui(),
            kWelcomeControls,
            kWelcomeVisuals,
            "/qsee_welcome/chrome/wake"
        );
    case Screen::Logs:
        return adaptive_command_bar_.activate(
            context.gui(),
            kLogControls,
            kLogVisuals,
            "/qsee_logs/chrome/wake"
        );
    case Screen::Hardware:
        return adaptive_command_bar_.activate(
            context.gui(),
            kNoControls,
            kHardwareVisuals,
            "/qsee_hardware/chrome/wake"
        );
    case Screen::Audio:
        return adaptive_command_bar_.activate(
            context.gui(),
            kAudioControls,
            kAudioVisuals,
            "/qsee_audio/chrome/wake"
        );
    case Screen::AudioFft:
        return adaptive_command_bar_.activate(
            context.gui(),
            kNoControls,
            kAudioFftVisuals,
            "/qsee_audio_fft/chrome/wake"
        );
    case Screen::Imu:
        return adaptive_command_bar_.activate(
            context.gui(),
            kNoControls,
            kImuVisuals,
            "/qsee_imu/chrome/wake"
        );
    case Screen::Settings:
        return adaptive_command_bar_.activate(
            context.gui(),
            kNoControls,
            kSettingsVisuals,
            "/qsee_settings/chrome/wake"
        );
    case Screen::About:
        return adaptive_command_bar_.activate(
            context.gui(),
            kNoControls,
            kAboutVisuals,
            "/qsee_about/chrome/wake"
        );
    }
    return std::unexpected("Unknown QSee route for adaptive chrome");
}

std::expected<void, std::string> App::open_screen(
    AppContext &context,
    Screen screen,
    std::string_view action
)
{
    auto result = context.gui().trigger_screen_flow(kScreenFlow, action);
    if (!result) {
        return std::unexpected("QSee navigation failed: " + result.error());
    }
    screen_ = screen;
    if (auto chrome_result = configure_adaptive_chrome(context); !chrome_result) {
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
    if (auto result = adaptive_command_bar_.note_activity(context.gui()); !result) {
        return result;
    }
    if (action == "qsee.chrome.activity") {
        return {};
    }
    if (action == "qsee.open.welcome") {
        return open_screen(context, Screen::Welcome, action);
    }
    if (action == "qsee.open.logs") {
        return open_screen(context, Screen::Logs, action);
    }
    if (action == "qsee.open.hardware") {
        return open_screen(context, Screen::Hardware, action);
    }
    if (action == "qsee.open.audio") {
        return open_screen(context, Screen::Audio, action);
    }
    if (action == "qsee.open.audio_fft") {
        return open_screen(context, Screen::AudioFft, action);
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
    if (action == "qsee.audio_fft.back") {
        return open_screen(context, Screen::Audio, "qsee.open.audio");
    }
    if (action == "qsee.logs.back" || action == "qsee.hardware.back" ||
        action == "qsee.audio.back" || action == "qsee.imu.back" ||
        action == "qsee.settings.back" || action == "qsee.about.back") {
        return open_screen(context, Screen::Welcome, "qsee.open.welcome");
    }
    if (action == "qsee.logs.pause") {
        logs_paused_ = !logs_paused_;
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
        return refresh_logs(context);
    }
    if (action == "qsee.logs.filter") {
        log_filter_mode_ = static_cast<uint8_t>((log_filter_mode_ + 1U) % 4U);
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
    if (action == "qsee.settings.chrome.timeout.next") {
        const uint32_t current = adaptive_command_bar_.timeout_seconds();
        const auto current_it = std::find(
            kChromeTimeoutSeconds.begin(),
            kChromeTimeoutSeconds.end(),
            current
        );
        if (current_it == kChromeTimeoutSeconds.end()) {
            ESP_LOGE(
                kTag,
                "Adaptive chrome timeout is outside the declared presets: %lu s",
                static_cast<unsigned long>(current)
            );
            return finish_settings_command(
                ESP_ERR_INVALID_STATE,
                "Adaptive interface timeout"
            );
        }
        const auto next_it = std::next(current_it) ==
                                     kChromeTimeoutSeconds.end()
            ? kChromeTimeoutSeconds.begin()
            : std::next(current_it);
        return finish_settings_command(
            adaptive_command_bar_.set_timeout_seconds(*next_it),
            "Adaptive interface timeout"
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

std::expected<void, std::string> App::on_timer(
    AppContext &context,
    esp_brookesia::system::core::TimerId timer_id,
    std::string_view name
)
{
    if (timer_id != refresh_timer_ || name != kRefreshTimer) {
        return {};
    }
    return refresh(context);
}

// Refresh the reusable curved chrome from the same canonical device state.
std::expected<void, std::string> App::refresh_chrome(
    AppContext &context,
    const DeviceSnapshot &snapshot
)
{
    const char *screen_id = nullptr;
    switch (screen_) {
    case Screen::Welcome:
        screen_id = "qsee_welcome";
        break;
    case Screen::Logs:
        screen_id = "qsee_logs";
        break;
    case Screen::Hardware:
        screen_id = "qsee_hardware";
        break;
    case Screen::Audio:
        screen_id = "qsee_audio";
        break;
    case Screen::AudioFft:
        screen_id = "qsee_audio_fft";
        break;
    case Screen::Imu:
        screen_id = "qsee_imu";
        break;
    case Screen::Settings:
        screen_id = "qsee_settings";
        break;
    case Screen::About:
        screen_id = "qsee_about";
        break;
    }
    if (auto result =
            adaptive_command_bar_.update(context.gui(), snapshot);
        !result) {
        return result;
    }

    const auto update_status_item =
        [&](const char *item, const char *color)
        -> std::expected<void, std::string> {
        std::array<char, 96> path{};
        std::snprintf(
            path.data(),
            path.size(),
            "/%s/chrome/status/%s/icon",
            screen_id,
            item
        );
        if (auto result =
                set_binding_text(context, path.data(), "color", color);
            !result) {
            return result;
        }
        std::snprintf(
            path.data(),
            path.size(),
            "/%s/chrome/status/%s/dot",
            screen_id,
            item
        );
        return set_binding_text(context, path.data(), "color", color);
    };

    const char *battery_color = component_status_color(snapshot.battery);
    const char *usb_color = battery_color;
    if (component_ready(snapshot.battery)) {
        usb_color = snapshot.battery.usb_present ? kStatusReady : kStatusMuted;
        if (!snapshot.battery.battery_present) {
            battery_color = kStatusMuted;
        } else if (snapshot.battery.level_percent < 0 ||
                   snapshot.battery.level_percent > 100) {
            battery_color = kStatusFault;
        } else if (snapshot.battery.level_percent <= 12) {
            battery_color = kStatusFault;
        } else if (snapshot.battery.level_percent <= 30) {
            battery_color = kStatusWarning;
        }
    }

    const char *ble_color = component_status_color(snapshot.radios.ble);
    if (component_ready(snapshot.radios.ble)) {
        ble_color = snapshot.radios.ble.connected ||
                    snapshot.radios.ble.provisioning ||
                    snapshot.radios.ble.advertising
                        ? kStatusReady
                        : kStatusWarning;
    }

    const char *wifi_color = component_status_color(snapshot.radios.wifi);
    if (component_ready(snapshot.radios.wifi)) {
        wifi_color = snapshot.radios.wifi.connected
            ? kStatusReady
            : snapshot.radios.wifi.started ? kStatusWarning : kStatusMuted;
    }

    const char *gps_color = component_status_color(snapshot.gps);
    if (component_ready(snapshot.gps)) {
        gps_color = gps_fix_valid(snapshot.gps)
            ? kStatusReady
            : snapshot.gps.has_fix ? kStatusFault : kStatusWarning;
    }

    const char *sd_color = component_status_color(snapshot.storage);
    if (storage_cleanup_failed(snapshot.storage)) {
        sd_color = kStatusFault;
    } else if (storage_format_required(snapshot.storage)) {
        sd_color = kStatusWarning;
    } else if (storage_mounted_ready(snapshot.storage)) {
        sd_color = kStatusReady;
    }

    if (auto result = update_status_item("usb", usb_color); !result) {
        return result;
    }
    if (auto result = update_status_item("battery", battery_color); !result) {
        return result;
    }
    if (auto result = update_status_item("ble", ble_color); !result) {
        return result;
    }
    if (auto result = update_status_item("wifi", wifi_color); !result) {
        return result;
    }
    if (auto result = update_status_item("gps", gps_color); !result) {
        return result;
    }
    if (auto result = update_status_item("sd", sd_color); !result) {
        return result;
    }

    std::array<char, 96> battery_path{};
    std::snprintf(
        battery_path.data(),
        battery_path.size(),
        "/%s/chrome/status/battery/icon",
        screen_id
    );
    return set_binding_text(
        context,
        battery_path.data(),
        "icon",
        battery_status_icon(snapshot.battery)
    );
}

// Publish the original radial atmosphere through Brookesia's native image registry.
std::expected<void, std::string> App::refresh_background(AppContext &context)
{
    auto &asset = BackgroundAssetCache::instance();
    if (asset.failed()) {
        return std::unexpected(
            "QSee background generation failed; inspect qsee_background logs"
        );
    }

    const lv_image_dsc_t *image = asset.image();
    if (image == nullptr) {
        return {};
    }

    if (!background_registered_) {
        auto result = context.gui().register_image(
            esp_brookesia::gui::RuntimeImageResource{
                .id = kBackgroundImageId,
                .primary_src = "",
                .native_src = reinterpret_cast<uintptr_t>(image),
                .width = 466,
                .height = 466,
                .preload = false,
            }
        );
        if (!result) {
            return std::unexpected(
                "Failed to register QSee background image: " + result.error()
            );
        }
        background_registered_ = true;
    }

    if (background_bound_) {
        return {};
    }

    constexpr std::array<std::string_view, 8> kScreenIds = {
        "qsee_welcome",
        "qsee_logs",
        "qsee_hardware",
        "qsee_audio",
        "qsee_audio_fft",
        "qsee_imu",
        "qsee_settings",
        "qsee_about",
    };
    for (std::string_view screen_id : kScreenIds) {
        std::array<char, 64> path{};
        std::snprintf(
            path.data(),
            path.size(),
            "/%.*s/background",
            static_cast<int>(screen_id.size()),
            screen_id.data()
        );
        if (auto result =
                set_binding_text(context, path.data(), "src", kBackgroundImageId);
            !result) {
            return result;
        }
    }
    background_bound_ = true;
    ESP_LOGI(kTag, "QSee radial background bound through Brookesia");
    return {};
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
    case Screen::Imu:
        return refresh_imu(context);
    case Screen::Settings:
        return refresh_settings(context);
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
        "WiFi %s  BLE %s  GPS %s  SD %s",
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
            "SNAPSHOT UNAVAILABLE:%s",
            esp_err_to_name(snapshot.last_update_error)
        );
    } else if (!snapshot.last_update_complete ||
               snapshot.last_update_error != ESP_OK) {
        std::snprintf(
            snapshot_status.data(),
            snapshot_status.size(),
            "SNAPSHOT #%lu ERROR:%s",
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
            "BATTERY %s (%s) | CPU UTILIZATION UNAVAILABLE\n%s",
            component_state(snapshot.battery),
            component_reason(snapshot.battery),
            snapshot_status.data()
        );
    } else if (!snapshot.battery.battery_present) {
        std::snprintf(
            text.data(),
            text.size(),
            "BATTERY UNAVAILABLE (ABSENT) | CPU UTILIZATION UNAVAILABLE\n%s",
            snapshot_status.data()
        );
    } else if (snapshot.battery.level_percent < 0 ||
               snapshot.battery.level_percent > 100 ||
               snapshot.battery.battery_mv <= 0) {
        std::snprintf(
            text.data(),
            text.size(),
            "BATTERY ERROR (INVALID READING) | CPU UTILIZATION UNAVAILABLE\n%s",
            snapshot_status.data()
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "BATTERY %d%% %dmV | CPU UTILIZATION UNAVAILABLE\n%s",
            snapshot.battery.level_percent,
            snapshot.battery.battery_mv,
            snapshot_status.data()
        );
    }
    return set_text(context, "/qsee_welcome/surface/device_status", text.data());
}

std::expected<void, std::string> App::refresh_logs(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }
    if (log_source_buffer_ == nullptr || log_filtered_buffer_ == nullptr) {
        return std::unexpected("Persistent QSee log buffers are unavailable");
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

    if (auto result = set_text(
            context,
            "/qsee_logs/surface/controls/pause/icon",
            logs_paused_ ? "\xef\x81\x8b" : "\xef\x81\x8c"
        );
        !result) {
        return result;
    }
    if (auto result = set_text(
            context,
            "/qsee_logs/surface/controls/follow/icon",
            logs_follow_latest_ ? "\xef\x81\xae" : "\xef\x81\xb0"
        );
        !result) {
        return result;
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
            "RTC         READY %04u-%02u-%02u %02u:%02u:%02u | %s\n",
            static_cast<unsigned>(snapshot.rtc.year),
            static_cast<unsigned>(snapshot.rtc.month),
            static_cast<unsigned>(snapshot.rtc.day),
            static_cast<unsigned>(snapshot.rtc.hour),
            static_cast<unsigned>(snapshot.rtc.minute),
            static_cast<unsigned>(snapshot.rtc.second),
            snapshot.rtc.source
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
        append_format(details, "GPS         READY | NO FIX\n");
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

    if (!component_ready(snapshot.radios.wifi)) {
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

    if (!component_ready(snapshot.radios.ble)) {
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
        for (size_t channel = 0; channel < 2U; ++channel) {
            std::snprintf(
                path.data(),
                path.size(),
                "/qsee_audio_fft/surface/bands/band%02u/mic%u",
                static_cast<unsigned>(band),
                static_cast<unsigned>(channel)
            );
            if (auto result = set_value(
                    context,
                    path.data(),
                    snapshot.audio.channel_spectrum[channel][band]
                ); !result) {
                return result;
            }
        }
    }
    return set_hidden(context, "/qsee_audio_fft/surface/bands", false);
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

    if (!component_ready(snapshot.diagnostics.sample_period)) {
        std::snprintf(
            text.data(),
            text.size(),
            "DIAGNOSTIC INTERVAL | %s (%s)",
            component_state(snapshot.diagnostics.sample_period),
            component_reason(snapshot.diagnostics.sample_period)
        );
    } else if (snapshot.diagnostics.sample_period_ms < 100U ||
               snapshot.diagnostics.sample_period_ms > 2000U) {
        std::snprintf(
            text.data(),
            text.size(),
            "DIAGNOSTICS | ERROR (INVALID SAVED INTERVAL: %lu ms)",
            static_cast<unsigned long>(snapshot.diagnostics.sample_period_ms)
        );
    } else if (!component_ready(snapshot.diagnostics.monitor)) {
        std::snprintf(
            text.data(),
            text.size(),
            "DIAGNOSTICS | sample %lu ms | monitor %s (%s)",
            static_cast<unsigned long>(snapshot.diagnostics.sample_period_ms),
            component_state(snapshot.diagnostics.monitor),
            component_reason(snapshot.diagnostics.monitor)
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "DIAGNOSTICS | sample %lu ms | monitor %s",
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
            "IMU INPUT SMOOTHING | %s (%s)",
            component_state(snapshot.diagnostics.imu_smoothing),
            component_reason(snapshot.diagnostics.imu_smoothing)
        );
    } else if (snapshot.diagnostics.imu_smoothing_percent > 90U) {
        std::snprintf(
            text.data(),
            text.size(),
            "IMU INPUT SMOOTHING | ERROR (INVALID SAVED VALUE: %lu%%)",
            static_cast<unsigned long>(snapshot.diagnostics.imu_smoothing_percent)
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "IMU INPUT SMOOTHING | %lu%%",
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
    if (component_ready(snapshot.radios.wifi)) {
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
    if (component_ready(snapshot.radios.ble)) {
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

    if (!component_ready(snapshot.radios.wifi)) {
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

    if (!component_ready(snapshot.radios.ble)) {
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
            "AMOLED BRIGHTNESS | %s (%s)",
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
            "AMOLED BRIGHTNESS | ERROR (INVALID SAVED VALUE: %lu%%)",
            static_cast<unsigned long>(snapshot.ui.brightness_percent)
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "AMOLED BRIGHTNESS | %lu%%",
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

    std::snprintf(
        text.data(),
        text.size(),
        "ADAPTIVE INTERFACE | %lu s\nTOUCH OR MOVEMENT REVEALS CHROME",
        static_cast<unsigned long>(adaptive_command_bar_.timeout_seconds())
    );
    return set_text(
        context,
        "/qsee_settings/surface/settings_scroller/chrome_timeout_value",
        text.data()
    );
}

std::expected<void, std::string> App::show_error_dialog(
    AppContext &context,
    std::string_view title,
    std::string_view details
)
{
    if (error_dialog_ != esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID) {
        auto hide_result = context.system_service().hide_message_dialog(error_dialog_);
        if (!hide_result) {
            return std::unexpected(
                "Failed to replace the visible QSee error dialog: " +
                hide_result.error()
            );
        }
        error_dialog_ = esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
    }

    MessageDialogOptions options = {
        .text = std::string(title),
        .informative_text = std::string(details),
        .icon = MessageDialogIcon::Critical,
        .buttons = {
            MessageDialogButton{
                .text = "OK",
                .role = MessageDialogButtonRole::Accept,
            },
        },
        .auto_close_ms = 0,
    };
    auto result = context.system_service().show_message_dialog(
        std::move(options),
        [this](const auto &) {
            error_dialog_ =
                esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
        }
    );
    if (!result) {
        return std::unexpected("Failed to show QSee error dialog: " + result.error());
    }
    error_dialog_ = *result;
    return {};
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
    if (format_dialog_ !=
        esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID) {
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
        MessageDialogOptions info = {
            .text = "SD format is not available",
            .informative_text = informative_text.data(),
            .icon = std::strcmp(component_state(storage), "ERROR") == 0
                ? MessageDialogIcon::Critical
                : MessageDialogIcon::Information,
            .buttons = {
                MessageDialogButton{
                    .text = "OK",
                    .role = MessageDialogButtonRole::Accept,
                },
            },
            .auto_close_ms = 0,
        };
        auto result = context.system_service().show_message_dialog(
            std::move(info),
            [this](const auto &) {
                format_dialog_ =
                    esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
            }
        );
        if (!result) {
            return std::unexpected("Failed to show SD status dialog: " + result.error());
        }
        format_dialog_ = *result;
        if (std::strcmp(component_state(storage), "ERROR") == 0) {
            return std::unexpected(
                "SD format status failed: " + std::string(component_reason(storage))
            );
        }
        return {};
    }

    MessageDialogOptions options = {
        .text = "Format SD card?",
        .informative_text =
            "This erases the card. Formatting starts only after you press FORMAT.",
        .icon = MessageDialogIcon::Warning,
        .buttons = {
            MessageDialogButton{
                .text = "CANCEL",
                .role = MessageDialogButtonRole::No,
            },
            MessageDialogButton{
                .text = "FORMAT",
                .role = MessageDialogButtonRole::Destructive,
            },
        },
        .auto_close_ms = 0,
    };
    auto result = context.system_service().show_message_dialog(
        std::move(options),
        [this](const esp_brookesia::system::core::MessageDialogResult &dialog_result) {
            format_dialog_ =
                esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
            if (dialog_result.button_role != MessageDialogButtonRole::Destructive) {
                return;
            }

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
    format_dialog_ = *result;
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
