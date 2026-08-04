/*
 * LUNASHELL.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 LUNASHELL.ai. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * LUNASHELL.ai: https://LUNASHELL.ai
 * Author website: https://umbertogiacobbi.biz
 * Contact: hello@LUNASHELL.ai
 */

#include "LUNASHELL_app.hpp"

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

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_logger.h"
#include "LUNASHELL_platform.h"
#include "LUNASHELL_snapshot_semantics.hpp"
#include "LUNASHELL_snapshot_cadence.hpp"

namespace {

using esp_brookesia::system::core::AppContext;
using LUNASHELL::snapshot_semantics::component_ready;
using LUNASHELL::snapshot_semantics::gps_fix_valid;
using LUNASHELL::snapshot_semantics::storage_cleanup_failed;
using LUNASHELL::snapshot_semantics::storage_format_required;
using LUNASHELL::snapshot_semantics::storage_mounted_ready;

constexpr char kTag[] = "LUNASHELL_app";
constexpr char kGuiRoot[] = "res/root.json";
constexpr char kBackgroundFlow[] = "LUNASHELL_background_flow";
constexpr char kScreenFlow[] = "LUNASHELL_content";
constexpr char kReleaseContentAction[] = "LUNASHELL.content.release";
constexpr char kChromeFlow[] = "LUNASHELL_chrome_flow";
constexpr char kRefreshTimer[] = "LUNASHELL.refresh";
constexpr char kPowerStateSyncTimer[] = "LUNASHELL.power.state_sync";
constexpr uint32_t kActiveRefreshPeriodMs = 1000;
constexpr uint32_t kLiveDiagnosticRefreshPeriodMs = 300;
constexpr uint32_t kDimmedRefreshPeriodMs = 5000;
constexpr uint32_t kPerformanceRefreshPeriodMs = 5000;
constexpr int64_t kTouchRefreshQuietPeriodUs = 750000;
constexpr int32_t kDisplaySizeDp = 466;
constexpr int32_t kChromeInsetDp = 73;
constexpr int32_t kContentSafeSizeDp = 320;
constexpr int32_t kCircularWideContentInsetDp = 55;
constexpr std::array<uint32_t, 6> kDiagnosticPeriodsMs = {
    100, 200, 300, 500, 1000, 2000,
};
constexpr std::array<uint32_t, 10> kImuSmoothingPercents = {
    0, 10, 20, 30, 40, 50, 60, 70, 80, 90,
};
constexpr std::array<uint32_t, 10> kBrightnessPercents = {
    10, 20, 30, 40, 50, 60, 70, 80, 90, 100,
};
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
constexpr std::array<uint16_t, 6> kTofSampleCounts = {
    20, 50, 100, 200, 300, 500,
};
constexpr std::array<uint32_t, 3> kTofSampleIntervalsMs = {
    250, 500, 1000,
};
constexpr int32_t kTofReferenceMinimumMm = 500;
constexpr int32_t kTofReferenceMaximumMm = 1500;
constexpr int32_t kTofReferenceStepMm = 10;
#endif
constexpr char kPairingQrImageId[] = "LUNASHELL.pairing.qr";
constexpr char kBackgroundImageId[] = "LUNASHELL.background.asset";
constexpr char kBackgroundImagePath[] =
    "/LUNASHELL_background_screen/background/asset";
constexpr char kEspressifAppStoreUrl[] =
    "https://play.google.com/store/apps/details?id=com.espressif.provble";
constexpr int64_t kPairingTimeoutUs = 5LL * 60LL * 1000000LL;
constexpr size_t kLogBufferBytes = 4096;
constexpr double kAudioMeterFloorDbfs = -60.0;
constexpr double kPcm16FullScale = 32768.0;
// The logger contract keeps the bounded snapshot inside the persistent 4 KB buffer.
constexpr size_t kLogSnapshotLines = LUNASHELL_LOGGER_SNAPSHOT_MAX_LINES;

template <size_t CommandCount>
constexpr bool valid_command_main_layout(
    const std::array<LUNASHELL::CommandSpec, CommandCount> &commands
)
{
    if constexpr (CommandCount == 0U) {
        return true;
    }
    const size_t right_center = CommandCount / 2U;
    const size_t left_center =
        (CommandCount % 2U) == 0U ? right_center - 1U : right_center;
    size_t main_count = 0;
    for (size_t index = 0; index < CommandCount; ++index) {
        if (!commands[index].is_main) {
            continue;
        }
        if (index != left_center && index != right_center) {
            return false;
        }
        ++main_count;
    }
    return main_count == ((CommandCount % 2U) == 0U ? 2U : 1U);
}

const char *storage_cleanup_stage_name(
    LUNASHELL::StorageCleanupStage stage
)
{
    switch (stage) {
    case LUNASHELL::StorageCleanupStage::None:
        return "NONE";
    case LUNASHELL::StorageCleanupStage::FatUnmount:
        return "FAT UNMOUNT";
    case LUNASHELL::StorageCleanupStage::VfsUnregister:
        return "VFS UNREGISTER";
    case LUNASHELL::StorageCleanupStage::HostDeinit:
        return "HOST DEINIT";
    }
    ESP_LOGE(kTag, "Unknown storage cleanup stage %u",
             static_cast<unsigned>(stage));
    std::abort();
}

const char *storage_mount_stage_name(LUNASHELL::StorageMountStage stage)
{
    switch (stage) {
    case LUNASHELL::StorageMountStage::None:
        return "NONE";
    case LUNASHELL::StorageMountStage::Request:
        return "REQUEST";
    case LUNASHELL::StorageMountStage::HostInit:
        return "HOST INIT";
    case LUNASHELL::StorageMountStage::SlotInit:
        return "SLOT INIT";
    case LUNASHELL::StorageMountStage::CardInit:
        return "CARD INIT";
    case LUNASHELL::StorageMountStage::CardIdentity:
        return "CARD IDENTITY";
    case LUNASHELL::StorageMountStage::DriveAlloc:
        return "DRIVE ALLOC";
    case LUNASHELL::StorageMountStage::DiskIoRegister:
        return "DISKIO REGISTER";
    case LUNASHELL::StorageMountStage::VfsRegister:
        return "VFS REGISTER";
    case LUNASHELL::StorageMountStage::FatMount:
        return "FAT MOUNT";
    case LUNASHELL::StorageMountStage::PreFormatUnmount:
        return "PRE-FORMAT UNMOUNT";
    case LUNASHELL::StorageMountStage::Partition:
        return "PARTITION";
    case LUNASHELL::StorageMountStage::Format:
        return "FORMAT";
    case LUNASHELL::StorageMountStage::PostFormatMount:
        return "POST-FORMAT MOUNT";
    case LUNASHELL::StorageMountStage::RuntimeStatus:
        return "RUNTIME STATUS";
    case LUNASHELL::StorageMountStage::RuntimeFilesystem:
        return "RUNTIME FILESYSTEM";
    case LUNASHELL::StorageMountStage::RuntimeUnmount:
        return "RUNTIME UNMOUNT";
    }
    ESP_LOGE(
        kTag,
        "Unknown storage mount stage %u",
        static_cast<unsigned>(stage)
    );
    std::abort();
}

bool snapshot_available(const LUNASHELL::DeviceSnapshot &snapshot)
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
            "CH%u  %.2f %%FS | -inf dBFS",
            channel,
            percent_full_scale
        );
        return;
    }
    std::snprintf(
        text,
        text_size,
        "CH%u  %.2f %%FS | %.1f dBFS",
        channel,
        percent_full_scale,
        audio_peak_dbfs(peak)
    );
}

const char *component_state(const LUNASHELL::ComponentSnapshot &component)
{
    if (component_ready(component)) {
        return "READY";
    }
    if (component.health == LUNASHELL::DeviceHealth::Missing) {
        return "UNAVAILABLE";
    }
    if (component.last_error != ESP_OK ||
        component.health == LUNASHELL::DeviceHealth::Degraded) {
        return "ERROR";
    }
    return component.data_stale ? "STALE" : "UNAVAILABLE";
}

const char *component_reason(const LUNASHELL::ComponentSnapshot &component)
{
    if (component.last_error != ESP_OK) {
        return esp_err_to_name(component.last_error);
    }
    if (component.data_stale) {
        return "STALE DATA HIDDEN";
    }
    return component.data_valid
               ? LUNASHELL::deviceHealthName(component.health)
               : "NO VALID DATA";
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
const char *distance_calibration_state_name(
    LUNASHELL::DistanceCalibrationState state
)
{
    switch (state) {
    case LUNASHELL::DistanceCalibrationState::Required:
        return "REQUIRED";
    case LUNASHELL::DistanceCalibrationState::Ready:
        return "READY";
    case LUNASHELL::DistanceCalibrationState::Running:
        return "RUNNING";
    case LUNASHELL::DistanceCalibrationState::Succeeded:
        return "SUCCEEDED";
    case LUNASHELL::DistanceCalibrationState::Failed:
        return "FAILED";
    }
    ESP_LOGE(
        kTag,
        "Unknown distance calibration state %u",
        static_cast<unsigned>(state)
    );
    return "UNKNOWN";
}
#endif

bool radio_deferred_by_policy(const LUNASHELL::ComponentSnapshot &radio)
{
    return radio.health == LUNASHELL::DeviceHealth::Unknown &&
           radio.last_error == ESP_ERR_INVALID_STATE &&
           !radio.data_valid &&
           !radio.data_stale &&
           radio.last_attempt_at_us == 0 &&
           radio.last_success_at_us == 0;
}

bool audio_spectrum_available(const LUNASHELL::DeviceSnapshot &snapshot)
{
    return snapshot_available(snapshot) &&
           component_ready(snapshot.audio.microphone) &&
           component_ready(snapshot.diagnostics.monitor) &&
           snapshot.diagnostics.monitor_active &&
           snapshot.audio.channel_count >= 1U &&
           snapshot.audio.channel_count <= 2U &&
           snapshot.audio.sample_rate_hz == 22050U;
}

bool dual_audio_spectrum_available(const LUNASHELL::DeviceSnapshot &snapshot)
{
    return audio_spectrum_available(snapshot) &&
           snapshot.audio.channel_count == 2U;
}

bool wifi_stopped_by_user(const LUNASHELL::WifiSnapshot &wifi)
{
    return !wifi.started &&
           wifi.last_error == ESP_ERR_INVALID_STATE &&
           !wifi.data_valid &&
           !wifi.data_stale &&
           wifi.last_attempt_at_us > 0;
}

bool ble_stopped_by_user(const LUNASHELL::BleSnapshot &ble)
{
    return !ble.provisioning &&
           !ble.advertising &&
           !ble.connected &&
           ble.last_error == ESP_ERR_INVALID_STATE &&
           !ble.data_valid &&
           !ble.data_stale &&
           ble.last_attempt_at_us > 0;
}

bool rtc_reading_valid(const LUNASHELL::RtcSnapshot &rtc)
{
    return component_ready(rtc) && rtc.clock_integrity &&
           rtc.year >= 2024 && rtc.year <= 2099 &&
           rtc.month >= 1 && rtc.month <= 12 &&
           rtc.day >= 1 && rtc.day <= 31 &&
           rtc.hour <= 23 && rtc.minute <= 59 && rtc.second <= 59;
}

bool rtc_calendar_structurally_valid(const LUNASHELL::RtcSnapshot &rtc)
{
    return rtc.year >= 2000 && rtc.year <= 2099 &&
           rtc.month >= 1 && rtc.month <= 12 &&
           rtc.day >= 1 && rtc.day <= 31 &&
           rtc.hour <= 23 && rtc.minute <= 59 && rtc.second <= 59;
}

bool rtc_waiting_for_time_source(const LUNASHELL::RtcSnapshot &rtc)
{
    return rtc.last_error == ESP_ERR_INVALID_STATE &&
           rtc.last_attempt_at_us > 0 &&
           rtc_calendar_structurally_valid(rtc) &&
           !rtc.data_valid;
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
    const LUNASHELL::ComponentSnapshot &component
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

}  // namespace

namespace LUNASHELL {

std::optional<App::LazyScreenDescriptor> App::lazy_screen_descriptor(
    Screen screen
)
{
    switch (screen) {
    case Screen::Welcome:
        return std::nullopt;
    case Screen::Battery:
        return LazyScreenDescriptor{
            .document_path = "res/screen_battery.json",
            .screen_path = "/LUNASHELL_battery",
        };
    case Screen::Logs:
        return LazyScreenDescriptor{
            .document_path = "res/screen_logs.json",
            .screen_path = "/LUNASHELL_logs",
        };
    case Screen::Hardware:
        return LazyScreenDescriptor{
            .document_path = "res/screen_hardware.json",
            .screen_path = "/LUNASHELL_hardware",
        };
    case Screen::HardwareDetail:
        return LazyScreenDescriptor{
            .document_path = "res/screen_hardware_detail.json",
            .screen_path = "/LUNASHELL_hardware_detail",
        };
    case Screen::Audio:
        return LazyScreenDescriptor{
            .document_path = "res/screen_audio.json",
            .screen_path = "/LUNASHELL_audio",
        };
    case Screen::AudioFft:
        return LazyScreenDescriptor{
            .document_path = "res/screen_audio_fft.json",
            .screen_path = "/LUNASHELL_audio_fft",
        };
    case Screen::AudioDetail:
        return LazyScreenDescriptor{
            .document_path = "res/screen_audio_detail.json",
            .screen_path = "/LUNASHELL_audio_detail",
        };
    case Screen::Imu:
        return LazyScreenDescriptor{
            .document_path = "res/screen_imu.json",
            .screen_path = "/LUNASHELL_imu",
        };
    case Screen::Gps:
        return LazyScreenDescriptor{
            .document_path = "res/screen_gps.json",
            .screen_path = "/LUNASHELL_gps",
        };
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    case Screen::Tof:
        return LazyScreenDescriptor{
            .document_path = "res/screen_tof.json",
            .screen_path = "/LUNASHELL_tof",
        };
    case Screen::TofSettings:
        return LazyScreenDescriptor{
            .document_path = "res/screen_tof_settings.json",
            .screen_path = "/LUNASHELL_tof_settings",
        };
#endif
    case Screen::Settings:
        return LazyScreenDescriptor{
            .document_path = "res/screen_settings.json",
            .screen_path = "/LUNASHELL_settings",
        };
    case Screen::Storage:
        return LazyScreenDescriptor{
            .document_path = "res/screen_storage.json",
            .screen_path = "/LUNASHELL_storage",
        };
    case Screen::About:
        return LazyScreenDescriptor{
            .document_path = "res/screen_about.json",
            .screen_path = "/LUNASHELL_about",
        };
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
    }
    return std::nullopt;
}

bool App::shares_lazy_document(Screen left, Screen right)
{
    const auto left_descriptor = lazy_screen_descriptor(left);
    const auto right_descriptor = lazy_screen_descriptor(right);
    return left_descriptor.has_value() &&
           right_descriptor.has_value() &&
           left_descriptor->document_path == right_descriptor->document_path &&
           left_descriptor->screen_path == right_descriptor->screen_path;
}

bool App::is_current_screen_document(
    esp_brookesia::gui::DocumentId document_id,
    uint32_t generation
) const
{
    return active_screen_document_id_.has_value() &&
           *active_screen_document_id_ == document_id &&
           screen_document_generation_ == generation;
}

void App::dispatch_screen_document_action(std::string action)
{
    if (context_ == nullptr) {
        ESP_LOGE(
            kTag,
            "Rejected lazy GUI action '%s': app context is unavailable",
            action.c_str()
        );
        return;
    }
    auto result = on_action(*context_, action);
    if (!result) {
        ESP_LOGE(
            kTag,
            "Lazy GUI action '%s' failed: %s",
            action.c_str(),
            result.error().c_str()
        );
    }
}

// Route every lazy document callback through the standby producer gate.
std::expected<void, std::string> App::subscribe_screen_document_actions(
    AppContext &context,
    esp_brookesia::gui::DocumentId document_id,
    Screen screen,
    uint32_t generation,
    std::vector<esp_brookesia::gui::ScopedConnection> &connections
)
{
    const auto add_connection = [&](
        std::string_view action,
        esp_brookesia::gui::Runtime::ActionHandler handler
    ) -> std::expected<void, std::string> {
        auto guarded_handler =
            [this, handler = std::move(handler)](
                const esp_brookesia::gui::Event &event
            ) mutable {
                if (standby_ui_state_ != StandbyUiState::Active) {
                    return;
                }
                handler(event);
            };
        auto connection = context.gui().subscribe_action(
            document_id,
            action,
            std::move(guarded_handler)
        );
        if (!connection.connected()) {
            return std::unexpected(
                "Failed to subscribe lazy GUI action '" + std::string(action) + "'"
            );
        }
        connections.push_back(std::move(connection));
        return {};
    };

    static constexpr std::array<std::string_view, 1> kBatteryActions = {
        "LUNASHELL.chrome.activity.battery",
    };
    static constexpr std::array<std::string_view, 1> kGpsActions = {
        "LUNASHELL.chrome.activity.gps",
    };
    static constexpr std::array<std::string_view, 1> kLogActions = {
        "LUNASHELL.chrome.activity.logs",
    };
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    static constexpr std::size_t kHardwareActionCount = 14;
#else
    static constexpr std::size_t kHardwareActionCount = 13;
#endif
    static constexpr std::array<
        std::string_view,
        kHardwareActionCount
    > kHardwareActions = {
        "LUNASHELL.chrome.activity.hardware",
        "LUNASHELL.hardware.detail.0",
        "LUNASHELL.hardware.detail.1",
        "LUNASHELL.hardware.detail.2",
        "LUNASHELL.hardware.detail.3",
        "LUNASHELL.hardware.detail.4",
        "LUNASHELL.hardware.detail.5",
        "LUNASHELL.hardware.detail.6",
        "LUNASHELL.hardware.detail.7",
        "LUNASHELL.hardware.detail.8",
        "LUNASHELL.hardware.detail.9",
        "LUNASHELL.hardware.detail.10",
        "LUNASHELL.hardware.detail.11",
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
        "LUNASHELL.hardware.detail.12",
#endif
    };
    static constexpr std::array<std::string_view, 1> kHardwareDetailActions = {
        "LUNASHELL.chrome.activity.hardware_detail",
    };
    static constexpr std::array<std::string_view, 2> kSettingsActions = {
        "LUNASHELL.chrome.activity.settings",
        "LUNASHELL.open.about",
    };
    static constexpr std::array<std::string_view, 2> kStorageActions = {
        "LUNASHELL.chrome.activity.storage",
        "LUNASHELL.storage.format",
    };
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    static constexpr std::array<std::string_view, 1> kTofSettingsActions = {
        "LUNASHELL.chrome.activity.tof_settings",
    };
#endif

    std::span<const std::string_view> forwarded_actions;
    switch (screen) {
    case Screen::Battery:
        forwarded_actions = kBatteryActions;
        break;
    case Screen::Gps:
        forwarded_actions = kGpsActions;
        break;
    case Screen::Logs:
        forwarded_actions = kLogActions;
        break;
    case Screen::Hardware:
        forwarded_actions = kHardwareActions;
        break;
    case Screen::HardwareDetail:
        forwarded_actions = kHardwareDetailActions;
        break;
    case Screen::Settings:
        forwarded_actions = kSettingsActions;
        break;
    case Screen::Storage:
        forwarded_actions = kStorageActions;
        break;
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    case Screen::TofSettings:
        forwarded_actions = kTofSettingsActions;
        break;
#endif
    default:
        break;
    }

    for (const std::string_view action : forwarded_actions) {
        auto result = add_connection(
            action,
            [this,
             document_id,
             generation,
             action = std::string(action)](const esp_brookesia::gui::Event &) {
                if (!is_current_screen_document(document_id, generation)) {
                    return;
                }
                dispatch_screen_document_action(action);
            }
        );
        if (!result) {
            connections.clear();
            return result;
        }
    }

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    if (screen == Screen::TofSettings) {
        const auto add_tof_settings_connection = [&](
            std::string_view action,
            auto handler
        ) -> std::expected<void, std::string> {
            return add_connection(
                action,
                [this,
                 document_id,
                 generation,
                 handler = std::move(handler)](
                    const esp_brookesia::gui::Event &event
                ) mutable {
                    if (!is_current_screen_document(document_id, generation)) {
                        return;
                    }
                    handler(event);
                }
            );
        };

        if (auto result = add_tof_settings_connection(
                "LUNASHELL.tof.settings.samples.changed",
                [this](const esp_brookesia::gui::Event &event) {
                    handle_tof_sample_count_event(event);
                }
            ); !result) {
            connections.clear();
            return result;
        }
        if (auto result = add_tof_settings_connection(
                "LUNASHELL.tof.settings.interval.changed",
                [this](const esp_brookesia::gui::Event &event) {
                    handle_tof_sample_interval_event(event);
                }
            ); !result) {
            connections.clear();
            return result;
        }
        if (auto result = add_tof_settings_connection(
                "LUNASHELL.tof.settings.reference.preview",
                [this](const esp_brookesia::gui::Event &event) {
                    handle_tof_reference_preview_event(event);
                }
            ); !result) {
            connections.clear();
            return result;
        }
        if (auto result = add_tof_settings_connection(
                "LUNASHELL.tof.settings.reference.commit",
                [this](const esp_brookesia::gui::Event &event) {
                    handle_tof_reference_commit_event(event);
                }
            ); !result) {
            connections.clear();
            return result;
        }
        return {};
    }
#endif

    if (screen != Screen::Settings) {
        return {};
    }

    const auto add_settings_connection = [&](
        std::string_view action,
        auto handler
    ) -> std::expected<void, std::string> {
        return add_connection(
            action,
            [this,
             document_id,
             generation,
             handler = std::move(handler)](
                const esp_brookesia::gui::Event &event
            ) mutable {
                if (!is_current_screen_document(document_id, generation)) {
                    return;
                }
                handler(event);
            }
        );
    };

    if (auto result = add_settings_connection(
            "LUNASHELL.settings.command_arc.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_chrome_timing_event(
                    AdaptiveChromeTiming::CommandArc,
                    event
                );
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.status_arc.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_chrome_timing_event(
                    AdaptiveChromeTiming::StatusArc,
                    event
                );
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.display_dim.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_chrome_timing_event(
                    AdaptiveChromeTiming::DisplayDim,
                    event
                );
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.screen_sleep.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_chrome_timing_event(
                    AdaptiveChromeTiming::ScreenSleep,
                    event
                );
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.full_snapshot.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_full_snapshot_period_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.diagnostic.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_diagnostic_period_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.imu_smoothing.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_imu_smoothing_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.display.brightness.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_brightness_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.performance.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_performance_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.storage.mirror.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_storage_mirror_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "LUNASHELL.settings.display.rotation.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_auto_rotation_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    return {};
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

std::expected<void, std::string> App::set_text(
    AppContext &context,
    std::string_view path,
    std::string_view text
)
{
    const bool use_screen_document =
        screen_ != Screen::Welcome &&
        active_screen_document_id_.has_value();
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

std::expected<void, std::string> App::set_binding_values(
    AppContext &context,
    const std::vector<esp_brookesia::gui::BindingValueUpdate> &updates
)
{
    const bool use_screen_document =
        screen_ != Screen::Welcome &&
        active_screen_document_id_.has_value();
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
    if (screen_ == Screen::Welcome ||
        !active_screen_document_id_.has_value()) {
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

std::expected<void, std::string> App::set_hidden(
    AppContext &context,
    std::string_view path,
    bool hidden
)
{
    if (screen_ != Screen::Welcome &&
        active_screen_document_id_.has_value()) {
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

std::expected<void, std::string> App::set_binding_text(
    AppContext &context,
    std::string_view path,
    std::string_view binding,
    std::string_view value
)
{
    if (screen_ != Screen::Welcome &&
        active_screen_document_id_.has_value()) {
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

std::expected<void, std::string> App::scroll_to(
    AppContext &context,
    std::string_view path,
    int32_t x,
    int32_t y,
    bool animated
)
{
    return screen_ != Screen::Welcome &&
           active_screen_document_id_.has_value()
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
        .name = "LUNASHELL.ai",
        .localized_names = {
            {"en", "LUNASHELL.ai"},
            {"it", "LUNASHELL.ai"},
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

// Initialize a fresh active input generation for the mounted LUNASHELL app.
std::expected<void, std::string> App::on_start(AppContext &context)
{
    context_ = &context;
    ++standby_ui_generation_;
    standby_ui_state_ = StandbyUiState::Active;
    standby_ui_transition_sequence_ = 0;
    status_bar_.invalidate();
    screen_ = Screen::Welcome;
    navigation_.reset(Screen::Welcome);
    logs_paused_ = false;
    logs_follow_latest_ = true;
    performance_mode_enabled_ = LUNASHELL_performance_mode_get_enabled();
    pairing_qr_registered_ = false;
    background_asset_registered_ = false;
    pairing_result_success_ = false;
    pairing_visual_state_.reset();
    settings_text_cache_valid_ = false;
    settings_controls_state_.reset();
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    tof_settings_controls_state_.reset();
    tof_reference_pending_mm_.reset();
    tof_reference_editing_ = false;
#endif
    content_viewport_state_.reset();
    invalidate_audio_screen_cache(Screen::Audio);
    invalidate_audio_screen_cache(Screen::AudioFft);
    invalidate_audio_screen_cache(Screen::AudioDetail);
    invalidate_hardware_screen_cache();
    invalidate_imu_screen_cache();
    invalidate_storage_screen_cache();
    storage_format_request_pending_ = false;
    storage_format_request_attempt_ = 0;
    pairing_started_at_us_ = 0;
    pairing_result_.clear();
    log_filter_mode_ = 0;
    diagnostic_activation_sample_at_us_ = 0;
    log_query_.clear();
    release_log_buffers();
    if (active_screen_document_id_.has_value()) {
        if (auto result = unload_active_screen_document(context); !result) {
            context_ = nullptr;
            return std::unexpected(
                "Failed to clean up the previous lazy GUI document: " +
                result.error()
            );
        }
    }
    ++screen_document_generation_;
    screen_document_connections_.clear();
    refresh_timer_ =
        esp_brookesia::system::core::INVALID_TIMER_ID;
    refresh_period_ms_ = 0;
    power_state_sync_timer_ =
        esp_brookesia::system::core::INVALID_TIMER_ID;
    log_screen_document_heap("bootstrap_ready", Screen::Welcome, 0U);
    if (auto result = adaptive_chrome_.begin(); !result) {
        context_ = nullptr;
        return result;
    }

    const std::vector<std::string> actions = {
        "LUNASHELL.chrome.activity",
        "LUNASHELL.chrome.activity.wake",
        "LUNASHELL.command.0",
        "LUNASHELL.command.1",
        "LUNASHELL.command.2",
        "LUNASHELL.command.3",
        "LUNASHELL.command.4",
        "LUNASHELL.back",
        "LUNASHELL.next",
    };
    auto subscription_result = context.gui().subscribe_actions(actions);
    if (!subscription_result) {
        context_ = nullptr;
        return std::unexpected(
            "Failed to subscribe LUNASHELL UI actions: " + subscription_result.error()
        );
    }
    if (auto result = configure_chrome(context); !result) {
        (void)adaptive_chrome_.shutdown(context.gui());
        screen_document_connections_.clear();
        context_ = nullptr;
        return result;
    }

    const esp_err_t monitor_result =
        LUNASHELL_hardware_set_diagnostic_streams(false, false);
    if (monitor_result != ESP_OK) {
        (void)adaptive_chrome_.shutdown(context.gui());
        screen_document_connections_.clear();
        context_ = nullptr;
        return std::unexpected(
            "Failed to disable LUNASHELL diagnostic monitor at startup: " +
            std::string(esp_err_to_name(monitor_result))
        );
    }
    const esp_err_t full_snapshot_result =
        LUNASHELL_hardware_start_full_snapshot_monitor();
    if (full_snapshot_result != ESP_OK) {
        (void)adaptive_chrome_.shutdown(context.gui());
        screen_document_connections_.clear();
        context_ = nullptr;
        return std::unexpected(
            "Failed to start Full Snapshot monitor: " +
            std::string(esp_err_to_name(full_snapshot_result))
        );
    }
    if (auto result = adaptive_chrome_.resume(context.gui()); !result) {
        (void)LUNASHELL_hardware_stop_full_snapshot_monitor();
        (void)adaptive_chrome_.shutdown(context.gui());
        screen_document_connections_.clear();
        context_ = nullptr;
        return result;
    }

    if (auto timer_result =
            sync_refresh_timer(context, DisplayIdleStage::Active);
        !timer_result) {
        (void)LUNASHELL_hardware_stop_full_snapshot_monitor();
        (void)adaptive_chrome_.shutdown(context.gui());
        screen_document_connections_.clear();
        context_ = nullptr;
        return timer_result;
    }
    auto state_sync_timer_result = context.timer().start_periodic(
        kPowerStateSyncTimer,
        AdaptiveChromeController::kPolicyPeriodMs
    );
    if (!state_sync_timer_result) {
        if (!context.timer().stop(refresh_timer_)) {
            ESP_LOGE(
                kTag,
                "Failed to stop refresh timer after power-state sync failure"
            );
        }
        refresh_timer_ = esp_brookesia::system::core::INVALID_TIMER_ID;
        refresh_period_ms_ = 0;
        (void)LUNASHELL_hardware_stop_full_snapshot_monitor();
        (void)adaptive_chrome_.shutdown(context.gui());
        screen_document_connections_.clear();
        context_ = nullptr;
        return std::unexpected(
            "Failed to start power-state UI synchronization timer: " +
            state_sync_timer_result.error()
        );
    }
    power_state_sync_timer_ = *state_sync_timer_result;

    auto refresh_result = refresh(context);
    if (!refresh_result) {
        if (!context.timer().stop(refresh_timer_)) {
            ESP_LOGE(kTag, "Failed to stop LUNASHELL refresh timer after startup failure");
        }
        refresh_timer_ = esp_brookesia::system::core::INVALID_TIMER_ID;
        refresh_period_ms_ = 0;
        if (!context.timer().stop(power_state_sync_timer_)) {
            ESP_LOGE(kTag, "Failed to stop power-state sync timer after startup failure");
        }
        power_state_sync_timer_ =
            esp_brookesia::system::core::INVALID_TIMER_ID;
        (void)LUNASHELL_hardware_stop_full_snapshot_monitor();
        (void)adaptive_chrome_.shutdown(context.gui());
        screen_document_connections_.clear();
        context_ = nullptr;
        return refresh_result;
    }

    ESP_LOGI(kTag, "LUNASHELL.ai native app started on Brookesia System 0.8");
    return {};
}

// Invalidate pending standby callbacks before pausing app-owned producers.
std::expected<void, std::string> App::on_pause(AppContext &context)
{
    ++standby_ui_generation_;
    standby_ui_state_ = StandbyUiState::Quiescing;
    standby_ui_transition_sequence_ = 0;
    audio_detail_palette_applied_ = false;
    std::string pause_error;
    if (auto result = adaptive_chrome_.shutdown(context.gui()); !result) {
        pause_error = result.error();
        ESP_LOGE(kTag, "%s", pause_error.c_str());
    }
    const esp_err_t diagnostic_result =
        LUNASHELL_hardware_set_diagnostic_streams(false, false);
    if (diagnostic_result != ESP_OK) {
        const std::string error =
            "Failed to pause LUNASHELL diagnostic monitor: " +
            std::string(esp_err_to_name(diagnostic_result));
        ESP_LOGE(kTag, "%s", error.c_str());
        if (pause_error.empty()) {
            pause_error = error;
        }
    }
    if (audio_diagnostic_capture_active_) {
        const esp_err_t audio_result =
            LUNASHELL_voice_afe_set_diagnostic_suspended(false);
        if (audio_result == ESP_OK) {
            audio_diagnostic_capture_active_ = false;
        } else {
            const std::string error =
                "Failed to restore voice audio after pausing diagnostics: " +
                std::string(esp_err_to_name(audio_result));
            ESP_LOGE(kTag, "%s", error.c_str());
            if (pause_error.empty()) {
                pause_error = error;
            }
        }
    }
    const esp_err_t full_snapshot_result =
        LUNASHELL_hardware_stop_full_snapshot_monitor();
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
        // Core keeps a failed pause in Running state, so restore every producer
        // before returning the original lifecycle failure.
        auto rollback_result = on_resume(context);
        if (!rollback_result) {
            ESP_LOGE(
                kTag,
                "LUNASHELL active rollback after pause failure failed: %s",
                rollback_result.error().c_str()
            );
            pause_error += "; active rollback failed: " +
                rollback_result.error();
        } else {
            ESP_LOGW(kTag, "LUNASHELL active state restored after pause failure");
        }
        return std::unexpected(std::move(pause_error));
    }
    return {};
}

// Reopen Brookesia input only after every resumed producer and GUI update agrees.
std::expected<void, std::string> App::on_resume(AppContext &context)
{
    ++standby_ui_generation_;
    standby_ui_transition_sequence_ = 0;
    standby_ui_state_ = StandbyUiState::Quiescing;
    settings_text_cache_valid_ = false;
    settings_controls_state_.reset();
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    tof_settings_controls_state_.reset();
    tof_reference_pending_mm_.reset();
    tof_reference_editing_ = false;
#endif
    invalidate_audio_screen_cache(Screen::Audio);
    invalidate_audio_screen_cache(Screen::AudioFft);
    invalidate_audio_screen_cache(Screen::AudioDetail);
    invalidate_hardware_screen_cache();
    invalidate_imu_screen_cache();
    invalidate_storage_screen_cache();
    const auto rollback_diagnostics = [this]() {
        const esp_err_t monitor_result =
            LUNASHELL_hardware_set_diagnostic_streams(false, false);
        if (monitor_result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Diagnostic monitor resume rollback failed: %s",
                esp_err_to_name(monitor_result)
            );
        }
        if (audio_diagnostic_capture_active_) {
            const esp_err_t audio_result =
                LUNASHELL_voice_afe_set_diagnostic_suspended(false);
            if (audio_result == ESP_OK) {
                audio_diagnostic_capture_active_ = false;
            } else {
                ESP_LOGE(
                    kTag,
                    "Audio diagnostic resume rollback failed: %s",
                    esp_err_to_name(audio_result)
                );
            }
        }
    };
    const auto stop_full_snapshot_after_failure = []() {
        const esp_err_t result = LUNASHELL_hardware_stop_full_snapshot_monitor();
        if (result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Full Snapshot resume rollback failed: %s",
                esp_err_to_name(result)
            );
        }
    };
    const auto shutdown_chrome_after_failure = [this, &context]() {
        if (auto result = adaptive_chrome_.shutdown(context.gui()); !result) {
            ESP_LOGE(
                kTag,
                "Adaptive Chrome resume rollback failed: %s",
                result.error().c_str()
            );
        }
    };
    const esp_err_t full_snapshot_result =
        LUNASHELL_hardware_start_full_snapshot_monitor();
    if (full_snapshot_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Failed to resume Full Snapshot monitor: %s",
            esp_err_to_name(full_snapshot_result)
        );
        return std::unexpected(
            "Failed to resume Full Snapshot monitor: " +
            std::string(esp_err_to_name(full_snapshot_result))
        );
    }
    if (auto result = apply_diagnostic_monitor(context, screen_); !result) {
        rollback_diagnostics();
        stop_full_snapshot_after_failure();
        return result;
    }
    if (auto result = configure_chrome(context); !result) {
        rollback_diagnostics();
        stop_full_snapshot_after_failure();
        return result;
    }
    if (auto result = adaptive_chrome_.resume(context.gui()); !result) {
        rollback_diagnostics();
        stop_full_snapshot_after_failure();
        return result;
    }
    if (auto result =
            sync_refresh_timer(context, DisplayIdleStage::Active);
        !result) {
        shutdown_chrome_after_failure();
        rollback_diagnostics();
        stop_full_snapshot_after_failure();
        return result;
    }
    if (auto result = refresh(context); !result) {
        shutdown_chrome_after_failure();
        rollback_diagnostics();
        stop_full_snapshot_after_failure();
        return result;
    }
    if (auto result = context.gui().drain_pending_work(); !result) {
        shutdown_chrome_after_failure();
        rollback_diagnostics();
        stop_full_snapshot_after_failure();
        return std::unexpected(
            "Failed to drain LUNASHELL GUI while resuming: " + result.error()
        );
    }
    if (auto result = context.gui().resume_input(); !result) {
        shutdown_chrome_after_failure();
        rollback_diagnostics();
        stop_full_snapshot_after_failure();
        return std::unexpected(
            "Failed to reopen LUNASHELL input while resuming: " + result.error()
        );
    }
    standby_ui_state_ = StandbyUiState::Active;
    return {};
}

// Block late input callbacks while the LUNASHELL app tears down its GUI ownership.
std::expected<void, std::string> App::on_stop(AppContext &context)
{
    ++standby_ui_generation_;
    standby_ui_state_ = StandbyUiState::Quiescing;
    standby_ui_transition_sequence_ = 0;
    std::string stop_error;
    if (auto chrome_result = adaptive_chrome_.shutdown(context.gui());
        !chrome_result) {
        stop_error = chrome_result.error();
        ESP_LOGE(kTag, "%s", stop_error.c_str());
    }
    ++screen_document_generation_;
    screen_document_connections_.clear();
    const esp_err_t monitor_result =
        LUNASHELL_hardware_set_diagnostic_streams(false, false);
    if (monitor_result != ESP_OK) {
        const std::string error =
            "Failed to stop LUNASHELL diagnostic monitor: " +
            std::string(esp_err_to_name(monitor_result));
        ESP_LOGE(kTag, "%s", error.c_str());
        if (stop_error.empty()) {
            stop_error = error;
        }
    }
    if (audio_diagnostic_capture_active_) {
        const esp_err_t audio_result =
            LUNASHELL_voice_afe_set_diagnostic_suspended(false);
        if (audio_result == ESP_OK) {
            audio_diagnostic_capture_active_ = false;
        } else {
            const std::string error =
                "Failed to restore voice audio while stopping LUNASHELL: " +
                std::string(esp_err_to_name(audio_result));
            ESP_LOGE(kTag, "%s", error.c_str());
            if (stop_error.empty()) {
                stop_error = error;
            }
        }
    }
    const esp_err_t full_snapshot_result =
        LUNASHELL_hardware_stop_full_snapshot_monitor();
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
            ESP_LOGE(kTag, "Failed to stop LUNASHELL refresh timer");
            if (stop_error.empty()) {
                stop_error = "Failed to stop LUNASHELL refresh timer";
            }
        }
        refresh_timer_ = esp_brookesia::system::core::INVALID_TIMER_ID;
        refresh_period_ms_ = 0;
    }
    if (power_state_sync_timer_ !=
        esp_brookesia::system::core::INVALID_TIMER_ID) {
        if (!context.timer().stop(power_state_sync_timer_)) {
            ESP_LOGE(kTag, "Failed to stop power-state UI synchronization timer");
            if (stop_error.empty()) {
                stop_error =
                    "Failed to stop power-state UI synchronization timer";
            }
        }
        power_state_sync_timer_ =
            esp_brookesia::system::core::INVALID_TIMER_ID;
    }
    if (search_keyboard_ != esp_brookesia::system::core::INVALID_KEYBOARD_REQUEST_ID) {
        auto hide_result = context.system_service().hide_keyboard(search_keyboard_);
        if (!hide_result) {
            ESP_LOGE(kTag, "Failed to hide LUNASHELL keyboard: %s", hide_result.error().c_str());
            if (stop_error.empty()) {
                stop_error = "Failed to hide LUNASHELL keyboard: " + hide_result.error();
            }
        }
        search_keyboard_ = esp_brookesia::system::core::INVALID_KEYBOARD_REQUEST_ID;
    }
    if (auto hide_result = alert_service_.hide_all(context); !hide_result) {
        ESP_LOGE(kTag, "Failed to hide LUNASHELL dialogs: %s", hide_result.error().c_str());
        if (stop_error.empty()) {
            stop_error = "Failed to hide LUNASHELL dialogs: " + hide_result.error();
        }
    }
    if (LUNASHELL_provisioning_active()) {
        const esp_err_t provisioning_result = LUNASHELL_provisioning_stop();
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
    if (auto unload_result = unload_active_screen_document(context);
        !unload_result) {
        ESP_LOGE(kTag, "%s", unload_result.error().c_str());
        if (stop_error.empty()) {
            stop_error = std::move(unload_result.error());
        }
    }
    if (background_asset_registered_ &&
        !context.gui().unregister_image(kBackgroundImageId)) {
        ESP_LOGE(kTag, "Failed to unregister LUNASHELL background image");
        if (stop_error.empty()) {
            stop_error = "Failed to unregister LUNASHELL background image";
        }
    }
    background_asset_registered_ = false;
    release_log_buffers();
    context_ = nullptr;
    ESP_LOGI(kTag, "LUNASHELL.ai native app stopped");
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
    const bool audio_screen =
        screen == Screen::Audio ||
        screen == Screen::AudioFft ||
        screen == Screen::AudioDetail;
    const bool diagnostics_allowed = !performance_mode_enabled_;
    const bool audio_enabled = diagnostics_allowed && audio_screen;
    const bool imu_enabled =
        diagnostics_allowed && screen == Screen::Imu;
    const bool enabled = audio_enabled || imu_enabled;
    const bool entering_audio =
        audio_enabled && !audio_diagnostic_capture_active_;
    const bool leaving_audio =
        !audio_enabled && audio_diagnostic_capture_active_;
    if (entering_audio) {
        const esp_err_t audio_result =
            LUNASHELL_voice_afe_set_diagnostic_suspended(true);
        if (audio_result != ESP_OK) {
            std::array<char, 128> details{};
            std::snprintf(
                details.data(),
                details.size(),
                "Microphone ownership could not move to audio diagnostics: %s",
                esp_err_to_name(audio_result)
            );
            if (auto dialog_result = show_error_dialog(
                    context,
                    "Audio capture unavailable",
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
    }

    // Stop diagnostic capture before the AFE can reclaim the microphones.
    const esp_err_t result = LUNASHELL_hardware_set_diagnostic_streams(
        imu_enabled,
        audio_enabled
    );
    if (result != ESP_OK && entering_audio) {
        const esp_err_t rollback_result =
            LUNASHELL_voice_afe_set_diagnostic_suspended(false);
        if (rollback_result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Audio diagnostic entry rollback failed: %s",
                esp_err_to_name(rollback_result)
            );
        }
    }
    if (result != ESP_OK) {
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

    if (leaving_audio) {
        const esp_err_t audio_result =
            LUNASHELL_voice_afe_set_diagnostic_suspended(false);
        if (audio_result != ESP_OK) {
            const esp_err_t rollback_result =
                LUNASHELL_hardware_set_diagnostic_streams(false, true);
            if (rollback_result != ESP_OK) {
                ESP_LOGE(
                    kTag,
                    "Audio diagnostic exit rollback failed: %s",
                    esp_err_to_name(rollback_result)
                );
            }
            std::array<char, 160> details{};
            std::snprintf(
                details.data(),
                details.size(),
                "Microphone ownership could not return to the voice AFE: %s",
                esp_err_to_name(audio_result)
            );
            if (auto dialog_result = show_error_dialog(
                    context,
                    "Audio capture unavailable",
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
    }

    if (enabled) {
        const DeviceSnapshot snapshot =
            DeviceSnapshotService::instance().snapshot();
        diagnostic_activation_sample_at_us_ =
            audio_enabled
                ? snapshot.audio.microphone.last_success_at_us
                : snapshot.imu.last_success_at_us;
    }
    audio_diagnostic_capture_active_ = audio_enabled;
    return {};
}

// Publish only route intent; the persistent component owns layout and colors.
std::expected<void, std::string> App::configure_chrome(
    AppContext &context
)
{
    using palette::ActionRole;
    static constexpr std::array<CommandSpec, 0> kNoCommands{};
    static constexpr std::array<CommandSpec, 5> kWelcomeCommands = {{
        {"\xef\x89\x82", "LUNASHELL.open.battery", ActionRole::Success, false},
        {"\xef\x80\x81", "LUNASHELL.open.audio", ActionRole::Primary, false},
        {"\xef\x80\x9c", "LUNASHELL.open.hardware", ActionRole::Success, true},
        {"\xef\x80\xa1", "LUNASHELL.open.imu", ActionRole::Success, false},
        {"\xef\x80\x93", "LUNASHELL.open.settings", ActionRole::Primary, false},
    }};
    const std::array<CommandSpec, 5> log_commands = {{
        {
            logs_paused_ ? "\xef\x81\x8b" : "\xef\x81\x8c",
            "LUNASHELL.logs.pause",
            ActionRole::Success,
            false,
        },
        {"\xef\x8b\xad", "LUNASHELL.logs.clear", ActionRole::Danger, false},
        {
            "\xef\x81\xb9",
            "LUNASHELL.logs.follow",
            logs_follow_latest_
                ? ActionRole::Success
                : ActionRole::Primary,
            true,
        },
        {
            "\xef\x81\xae",
            "LUNASHELL.logs.filter",
            log_filter_mode_ == 0
                ? ActionRole::Primary
                : ActionRole::Warning,
            false,
        },
        {"\xef\x8c\x84", "LUNASHELL.logs.search", ActionRole::Primary, false},
    }};
    static constexpr std::array<CommandSpec, 1> kAudioSingleCommand = {{
        {"\xef\x83\x89", "LUNASHELL.open.audio_fft", ActionRole::Success, true},
    }};
    static constexpr std::array<CommandSpec, 2> kAudioDualCommands = {{
        {"\xef\x83\x89", "LUNASHELL.open.audio_fft", ActionRole::Success, true},
        {"\xef\x83\x9b", "LUNASHELL.open.audio_detail", ActionRole::Success, true},
    }};
    static constexpr std::array<CommandSpec, 1> kHardwareCommands = {{
        {"\xef\x80\xa1", "LUNASHELL.hardware.refresh", ActionRole::Success, true},
    }};
    static constexpr std::array<CommandSpec, 1> kAudioFftCommands = {{
        {"\xef\x81\xae", "LUNASHELL.open.audio_detail", ActionRole::Success, true},
    }};
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    static constexpr std::array<CommandSpec, 1> kTofCommands = {{
        {"\xef\x80\x93", "LUNASHELL.open.tof_settings", ActionRole::Primary, true},
    }};
    static constexpr std::array<CommandSpec, 1> kTofSettingsCommands = {{
        {"\xef\x81\x8b", "LUNASHELL.tof.calibration.start", ActionRole::Success, true},
    }};
#endif
    static constexpr std::array<CommandSpec, 5> kSettingsCommandLayout = {{
        {"\xef\x80\x8b", "LUNASHELL.open.logs", ActionRole::Primary, false},
        {"\xef\x8a\x93", "LUNASHELL.open.pairing", ActionRole::Success, false},
        {
            "\xef\x87\xab",
            "LUNASHELL.settings.radios.start",
            ActionRole::Warning,
            true,
        },
        {"\xef\x8f\x85", "LUNASHELL.open.gps", ActionRole::Neutral, false},
        {"\xef\x83\x87", "LUNASHELL.open.storage", ActionRole::Neutral, false},
    }};
    const DeviceSnapshot chrome_snapshot =
        DeviceSnapshotService::instance().snapshot();
    const bool average_spectrum_available =
        !performance_mode_enabled_ &&
        audio_spectrum_available(chrome_snapshot);
    const bool dual_spectrum_available =
        !performance_mode_enabled_ &&
        dual_audio_spectrum_available(chrome_snapshot);
    const bool audio_spectrum_fault =
        snapshot_available(chrome_snapshot) &&
        !component_ready(chrome_snapshot.audio.microphone) &&
        chrome_snapshot.audio.microphone.last_error != ESP_ERR_INVALID_STATE;
    std::array<CommandSpec, 1> audio_single_command = kAudioSingleCommand;
    std::array<CommandSpec, 2> audio_dual_commands = kAudioDualCommands;
    audio_single_command[0].role =
        average_spectrum_available
            ? ActionRole::Success
            : audio_spectrum_fault ? ActionRole::Danger : ActionRole::Neutral;
    audio_dual_commands[0].role = audio_single_command[0].role;
    const bool wireless_active =
        (component_ready(chrome_snapshot.radios.wifi) &&
         chrome_snapshot.radios.wifi.started) ||
        (component_ready(chrome_snapshot.radios.ble) &&
         (chrome_snapshot.radios.ble.provisioning ||
          chrome_snapshot.radios.ble.advertising ||
          chrome_snapshot.radios.ble.connected));
    const bool wireless_fault =
        (!radio_deferred_by_policy(chrome_snapshot.radios.wifi) &&
         !wifi_stopped_by_user(chrome_snapshot.radios.wifi) &&
         !component_ready(chrome_snapshot.radios.wifi)) ||
        (!radio_deferred_by_policy(chrome_snapshot.radios.ble) &&
         !ble_stopped_by_user(chrome_snapshot.radios.ble) &&
         !component_ready(chrome_snapshot.radios.ble));
    std::array<CommandSpec, 5> settings_commands = kSettingsCommandLayout;
    settings_commands[2] = {
        wireless_active ? "\xef\x80\x91" : "\xef\x87\xab",
        wireless_active
            ? "LUNASHELL.settings.radios.stop"
            : "LUNASHELL.settings.radios.start",
        wireless_fault
            ? ActionRole::Danger
            : wireless_active ? ActionRole::Success : ActionRole::Warning,
        true,
    };
    settings_commands[3].role =
        !snapshot_available(chrome_snapshot)
            ? ActionRole::Neutral
            : component_ready(chrome_snapshot.gps)
                ? gps_fix_valid(chrome_snapshot.gps)
                    ? ActionRole::Success
                    : ActionRole::Warning
                : chrome_snapshot.gps.last_error == ESP_ERR_INVALID_STATE
                    ? ActionRole::Neutral
                    : ActionRole::Danger;
    settings_commands[4].role =
        storage_cleanup_failed(chrome_snapshot.storage)
            ? ActionRole::Danger
            : storage_format_required(chrome_snapshot.storage)
                ? ActionRole::Warning
                : storage_mounted_ready(chrome_snapshot.storage)
                    ? ActionRole::Success
                    : ActionRole::Neutral;
    static_assert(valid_command_main_layout(kNoCommands));
    static_assert(valid_command_main_layout(kWelcomeCommands));
    static_assert(valid_command_main_layout(kAudioSingleCommand));
    static_assert(valid_command_main_layout(kAudioDualCommands));
    static_assert(valid_command_main_layout(kHardwareCommands));
    static_assert(valid_command_main_layout(kAudioFftCommands));
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    static_assert(valid_command_main_layout(kTofCommands));
    static_assert(valid_command_main_layout(kTofSettingsCommands));
#endif
    static_assert(valid_command_main_layout(kSettingsCommandLayout));

    auto activate_result = [&]() -> std::expected<void, std::string> {
        switch (screen_) {
        case Screen::Welcome:
            return adaptive_chrome_.configure_commands(
                context.gui(),
                kWelcomeCommands
            );
        case Screen::Battery:
            return adaptive_chrome_.configure_commands(
                context.gui(),
                kNoCommands
            );
        case Screen::Logs:
            return adaptive_chrome_.configure_commands(
                context.gui(),
                log_commands
            );
        case Screen::Audio:
            return adaptive_chrome_.configure_commands(
                context.gui(),
                dual_spectrum_available
                    ? std::span<const CommandSpec>(audio_dual_commands)
                    : std::span<const CommandSpec>(audio_single_command)
            );
        case Screen::AudioFft:
            return adaptive_chrome_.configure_commands(
                context.gui(),
                kAudioFftCommands
            );
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
        case Screen::Tof:
            return adaptive_chrome_.configure_commands(
                context.gui(),
                kTofCommands
            );
        case Screen::TofSettings:
            return adaptive_chrome_.configure_commands(
                context.gui(),
                kTofSettingsCommands
            );
#endif
        case Screen::Settings:
            return adaptive_chrome_.configure_commands(
                context.gui(),
                settings_commands
            );
        case Screen::Hardware:
            return adaptive_chrome_.configure_commands(
                context.gui(),
                kHardwareCommands
            );
        case Screen::HardwareDetail:
        case Screen::AudioDetail:
        case Screen::Imu:
        case Screen::Gps:
        case Screen::Storage:
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
        return std::unexpected("Unknown LUNASHELL route for adaptive chrome");
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

struct HardwareDescriptor {
    std::string_view id;
    std::string_view name;
    std::string_view part;
    std::string_view description;
};

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
constexpr std::size_t kHardwareComponentCount = 13;
#else
constexpr std::size_t kHardwareComponentCount = 12;
#endif
constexpr std::array<
    HardwareDescriptor,
    kHardwareComponentCount
> kHardwareComponents = {{
    {"battery", "BATTERY", "AXP2101", "POWER AND CHARGE MONITOR"},
    {"rtc", "RTC", "PCF85063", "REAL-TIME CLOCK"},
    {"imu", "IMU", "QMI8658", "MOTION AND TEMPERATURE SENSOR"},
    {"gps", "GPS", "LC76G", "GNSS POSITION RECEIVER"},
    {"wifi", "WIFI", "ESP32-S3", "WIRELESS NETWORK RADIO"},
    {"ble", "BLE", "ESP32-S3", "BLUETOOTH PROVISIONING RADIO"},
    {"display", "DISPLAY", "CO5300", "ROUND AMOLED PANEL"},
    {"touch", "TOUCH", "CST9217", "CAPACITIVE TOUCH CONTROLLER"},
    {"speaker", "SPEAKER", "ES8311", "AUDIO OUTPUT CODEC"},
    {"microphone", "MICROPHONE", "ES7210", "DUAL MICROPHONE CODEC"},
    {"sd", "SD CARD", "SDMMC", "REMOVABLE STORAGE"},
    {"processor", "PROCESSOR", "ESP32-S3", "DUAL-CORE SYSTEM CONTROLLER"},
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    {"tof", "DISTANCE", "2 x TOF400F", "DUAL CALIBRATED TIME-OF-FLIGHT SENSOR"},
#endif
}};

const LUNASHELL::ComponentSnapshot &hardware_component(
    const LUNASHELL::DeviceSnapshot &snapshot,
    size_t index
)
{
    switch (index) {
    case 0:
        return snapshot.battery;
    case 1:
        return snapshot.rtc;
    case 2:
        return snapshot.imu;
    case 3:
        return snapshot.gps;
    case 4:
        return snapshot.radios.wifi;
    case 5:
        return snapshot.radios.ble;
    case 6:
        return snapshot.ui.display;
    case 7:
        return snapshot.ui.touch;
    case 8:
        return snapshot.audio.speaker;
    case 9:
        return snapshot.audio.microphone;
    case 10:
        return snapshot.storage;
    case 11:
        return snapshot.processor;
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    case 12:
        return snapshot.distance;
#endif
    default:
        ESP_LOGE(kTag, "Rejected hardware component index %u",
                 static_cast<unsigned>(index));
        std::abort();
    }
}

void format_hardware_status(
    std::array<char, 64> &output,
    const LUNASHELL::DeviceSnapshot &snapshot,
    size_t index
)
{
    const LUNASHELL::ComponentSnapshot &component =
        hardware_component(snapshot, index);
    if ((index == 4 || index == 5) &&
        radio_deferred_by_policy(component)) {
        std::snprintf(output.data(), output.size(), "OFF BY POLICY");
        return;
    }
    if (index == 1 && rtc_waiting_for_time_source(snapshot.rtc)) {
        std::snprintf(output.data(), output.size(), "WAITING FOR SNTP/GPS");
        return;
    }
    if (index == 3 && component_ready(snapshot.gps)) {
        std::snprintf(
            output.data(),
            output.size(),
            "%s",
            snapshot.gps.has_fix && gps_fix_valid(snapshot.gps)
                ? "FIX"
                : "NO FIX"
        );
        return;
    }
    if (index == 10) {
        if (storage_cleanup_failed(snapshot.storage)) {
            std::snprintf(output.data(), output.size(), "CLEANUP ERROR");
        } else if (storage_format_required(snapshot.storage)) {
            std::snprintf(output.data(), output.size(), "FORMAT REQUIRED");
        } else if (storage_mounted_ready(snapshot.storage)) {
            std::snprintf(output.data(), output.size(), "READY");
        } else if (!snapshot.storage.card_response_verified) {
            std::snprintf(output.data(), output.size(), "UNCONFIRMED");
        } else {
            std::snprintf(
                output.data(),
                output.size(),
                "%s",
                component_state(snapshot.storage)
            );
        }
        return;
    }
    if (index == 11 && component_ready(snapshot.processor) &&
        snapshot.processor.core_count > 0 &&
        snapshot.processor.core_count <= 2) {
        std::snprintf(
            output.data(),
            output.size(),
            "%u CORES PRESENT",
            static_cast<unsigned>(snapshot.processor.core_count)
        );
        return;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    if (index == 12) {
        if (snapshot.distance.calibration.state ==
            LUNASHELL::DistanceCalibrationState::Running) {
            if (component_ready(snapshot.distance.calibration) &&
                snapshot.distance.calibration.progress_percent <= 100U) {
                std::snprintf(output.data(), output.size(), "CALIBRATING");
            } else {
                std::snprintf(
                    output.data(),
                    output.size(),
                    "CALIBRATION %s",
                    component_state(snapshot.distance.calibration)
                );
            }
            return;
        }
        if (component_ready(snapshot.distance)) {
            std::snprintf(output.data(), output.size(), "READY");
            return;
        }
        if (snapshot.distance.calibration.state ==
                LUNASHELL::DistanceCalibrationState::Required ||
            snapshot.distance.calibration.state ==
                LUNASHELL::DistanceCalibrationState::Failed) {
            std::snprintf(
                output.data(),
                output.size(),
                "CALIBRATION %s",
                distance_calibration_state_name(
                    snapshot.distance.calibration.state
                )
            );
            return;
        }
    }
#endif
    std::snprintf(
        output.data(),
        output.size(),
        "%s",
        component_state(component)
    );
}

const char *hardware_status_color(
    const LUNASHELL::DeviceSnapshot &snapshot,
    size_t index
)
{
    const LUNASHELL::ComponentSnapshot &component =
        hardware_component(snapshot, index);
    if ((index == 4 || index == 5) &&
        radio_deferred_by_policy(component)) {
        return "#9fb1c1";
    }
    if ((index == 3 && component_ready(snapshot.gps) &&
         !snapshot.gps.has_fix) ||
        (index == 1 && rtc_waiting_for_time_source(snapshot.rtc)) ||
        (index == 10 &&
         (storage_format_required(snapshot.storage) ||
          !snapshot.storage.card_response_verified))
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
        ||
        (index == 12 &&
         (snapshot.distance.calibration.state ==
              LUNASHELL::DistanceCalibrationState::Required ||
          (snapshot.distance.calibration.state ==
               LUNASHELL::DistanceCalibrationState::Running &&
           component_ready(snapshot.distance.calibration) &&
           snapshot.distance.calibration.progress_percent <= 100U)))
#endif
    ) {
        return "#f0b44d";
    }
    if (component_ready(component)) {
        return "#63f4e0";
    }
    if (component.last_error != ESP_OK ||
        component.health == LUNASHELL::DeviceHealth::Degraded) {
        return "#ff8a66";
    }
    return "#9fb1c1";
}

std::expected<void, std::string> App::open_screen(
    AppContext &context,
    Screen screen,
    std::string_view action,
    NavigationMode mode
)
{
    const auto fail_navigation = [&](std::string details)
        -> std::expected<void, std::string> {
        const auto append_failure = [&](std::string_view operation,
                                        std::string_view error) {
            details += "; " + std::string(operation) + " failed: " +
                       std::string(error);
        };
        if (is_pairing_screen(screen_) || is_pairing_screen(screen)) {
            if (LUNASHELL_provisioning_active()) {
                const esp_err_t stop_result = LUNASHELL_provisioning_stop();
                if (stop_result != ESP_OK) {
                    append_failure(
                        "provisioning rollback",
                        esp_err_to_name(stop_result)
                    );
                } else {
                    pairing_started_at_us_ = 0;
                }
            }
            if (auto release_result = release_pairing_qr(context);
                !release_result) {
                append_failure("pairing QR rollback", release_result.error());
            }
        }
        auto rollback_result = restore_welcome_document(context);
        if (!rollback_result) {
            append_failure("Welcome rollback", rollback_result.error());
        }
        return std::unexpected(std::move(details));
    };

    const bool leaving_pairing = is_pairing_screen(screen_) &&
                                 !is_pairing_screen(screen);
    const bool leaving_pairing_qr = screen != Screen::PairQr &&
                                    screen != Screen::AppStoreQr;
    if (screen == Screen::Settings && screen_ != Screen::Settings) {
        settings_text_cache_valid_ = false;
        settings_controls_state_.reset();
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    if (screen == Screen::TofSettings &&
        screen_ != Screen::TofSettings) {
        tof_settings_controls_state_.reset();
        tof_reference_pending_mm_.reset();
        tof_reference_editing_ = false;
    }
#endif
    if (leaving_pairing &&
        LUNASHELL_provisioning_active()) {
        const esp_err_t stop_result = LUNASHELL_provisioning_stop();
        if (stop_result != ESP_OK) {
            return std::unexpected(
                "Failed to leave pairing while provisioning is active: " +
                std::string(esp_err_to_name(stop_result))
            );
        }
        pairing_started_at_us_ = 0;
    }
    if (leaving_pairing_qr) {
        if (auto release_result = release_pairing_qr(context); !release_result) {
            return release_result;
        }
    }
    if (auto result = activate_screen_document(context, screen); !result) {
        return fail_navigation(
            "LUNASHELL navigation '" + std::string(action) + "' failed: " +
            result.error()
        );
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
        return fail_navigation(
            "LUNASHELL route changed but navigation history rejected the transition"
        );
    }
    screen_ = screen;
    pairing_visual_state_.reset();
    if (auto chrome_result = configure_chrome(context); !chrome_result) {
        return fail_navigation(
            "LUNASHELL chrome configuration failed after navigation: " +
            chrome_result.error()
        );
    }
    if (auto monitor_result = apply_diagnostic_monitor(context, screen); !monitor_result) {
        return fail_navigation(
            "LUNASHELL diagnostic policy failed after navigation: " +
            monitor_result.error()
        );
    }
    if (auto refresh_result = refresh(context); !refresh_result) {
        return fail_navigation(
            "LUNASHELL screen refresh failed after navigation: " +
            refresh_result.error()
        );
    }
    return {};
}

// Reject touch actions while the power owner has quiesced every GUI producer.
std::expected<void, std::string> App::on_action(
    AppContext &context,
    std::string_view action
)
{
    if (standby_ui_state_ != StandbyUiState::Active) {
        return {};
    }
    if (auto result = note_activity(context); !result) {
        return result;
    }
    if (const auto resolved =
            adaptive_chrome_.resolve_action(action);
        resolved.has_value()) {
        action = *resolved;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    if (action == "LUNASHELL.chrome.activity.tof_settings") {
        return {};
    }
#endif
    if (action == "LUNASHELL.chrome.activity" ||
        action == "LUNASHELL.chrome.activity.wake" ||
        action == "LUNASHELL.chrome.activity.battery" ||
        action == "LUNASHELL.chrome.activity.gps" ||
        action == "LUNASHELL.chrome.activity.logs" ||
        action == "LUNASHELL.chrome.activity.hardware" ||
        action == "LUNASHELL.chrome.activity.hardware_detail" ||
        action == "LUNASHELL.chrome.activity.settings" ||
        action == "LUNASHELL.chrome.activity.storage") {
        return {};
    }
    if (action == "LUNASHELL.open.welcome") {
        return open_screen(
            context,
            Screen::Welcome,
            action,
            NavigationMode::Clear
        );
    }
    if (action == "LUNASHELL.open.battery") {
        return open_screen(context, Screen::Battery, action);
    }
    if (action == "LUNASHELL.open.logs") {
        return open_screen(context, Screen::Logs, action);
    }
    if (action == "LUNASHELL.open.hardware") {
        return open_screen(context, Screen::Hardware, action);
    }
    if (action == "LUNASHELL.hardware.refresh") {
        return refresh_hardware(context);
    }
    constexpr std::string_view kHardwareDetailPrefix =
        "LUNASHELL.hardware.detail.";
    if (action.starts_with(kHardwareDetailPrefix)) {
        const std::string_view suffix =
            action.substr(kHardwareDetailPrefix.size());
        size_t component_index = 0;
        if (suffix.empty()) {
            return std::unexpected(
                "Rejected hardware detail action without component index"
            );
        }
        for (const char digit : suffix) {
            if (digit < '0' || digit > '9') {
                return std::unexpected(
                    "Rejected hardware detail action with invalid index"
                );
            }
            component_index =
                component_index * 10U + static_cast<size_t>(digit - '0');
        }
        if (component_index >= kHardwareComponents.size()) {
            return std::unexpected(
                "Rejected hardware detail action with out-of-range index"
            );
        }
        return show_hardware_details(context, component_index);
    }
    if (action == "LUNASHELL.open.audio") {
        return open_screen(context, Screen::Audio, action);
    }
    if (action == "LUNASHELL.open.audio_fft") {
        return open_screen(context, Screen::AudioFft, action);
    }
    if (action == "LUNASHELL.open.audio_detail") {
        return open_screen(context, Screen::AudioDetail, action);
    }
    if (action == "LUNASHELL.open.imu") {
        return open_screen(context, Screen::Imu, action);
    }
    if (action == "LUNASHELL.open.gps") {
        return open_screen(context, Screen::Gps, action);
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    if (action == "LUNASHELL.open.tof") {
        return open_screen(context, Screen::Tof, action);
    }
    if (action == "LUNASHELL.open.tof_settings") {
        return open_screen(context, Screen::TofSettings, action);
    }
#endif
    if (action == "LUNASHELL.open.settings") {
        return open_screen(context, Screen::Settings, action);
    }
    if (action == "LUNASHELL.open.storage") {
        return open_screen(context, Screen::Storage, action);
    }
    if (action == "LUNASHELL.open.about") {
        return open_screen(context, Screen::About, action);
    }
    if (action == "LUNASHELL.open.pairing") {
        return open_screen(
            context,
            Screen::PairIntro,
            "LUNASHELL.open.pair_intro"
        );
    }
    if (action == "LUNASHELL.next") {
        return pairing_next(context);
    }
    if (action == "LUNASHELL.back") {
        return navigate_back(context);
    }
    if (action == "LUNASHELL.logs.pause") {
        logs_paused_ = !logs_paused_;
        if (auto result = configure_chrome(context); !result) {
            return result;
        }
        return refresh_logs(context);
    }
    if (action == "LUNASHELL.logs.clear") {
        const esp_err_t result = LUNASHELL_logger_clear();
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
    if (action == "LUNASHELL.logs.follow") {
        logs_follow_latest_ = !logs_follow_latest_;
        if (auto result = configure_chrome(context); !result) {
            return result;
        }
        return refresh_logs(context);
    }
    if (action == "LUNASHELL.logs.filter") {
        log_filter_mode_ = static_cast<uint8_t>((log_filter_mode_ + 1U) % 4U);
        if (auto result = configure_chrome(context); !result) {
            return result;
        }
        return refresh_logs(context);
    }
    if (action == "LUNASHELL.logs.search") {
        return request_log_search(context);
    }
    if (action == "LUNASHELL.storage.format") {
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

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    if (action == "LUNASHELL.tof.calibration.start") {
        const esp_err_t result = LUNASHELL_distance_start_calibration();
        if (result != ESP_OK) {
            std::array<char, 160> details{};
            std::snprintf(
                details.data(),
                details.size(),
                "Distance calibration could not start: %s",
                esp_err_to_name(result)
            );
            ESP_LOGE(kTag, "%s", details.data());
            if (auto dialog_result = show_error_dialog(
                    context,
                    "Calibration not started",
                    details.data()
                ); !dialog_result) {
                return std::unexpected(
                    std::string(details.data()) +
                    "; error dialog failed: " + dialog_result.error()
                );
            }
            if (auto refresh_result = refresh_tof_settings(context);
                !refresh_result) {
                return std::unexpected(
                    std::string(details.data()) +
                    "; TOF settings refresh failed: " +
                    refresh_result.error()
                );
            }
            return std::unexpected(details.data());
        }
        return refresh_tof_settings(context);
    }
#endif
    if (action == "LUNASHELL.settings.radios.start") {
        esp_err_t result = LUNASHELL_radios_init();
        if (result == ESP_OK && !LUNASHELL_provisioning_active()) {
            result = LUNASHELL_wifi_start_scan();
        }
        return finish_settings_command(result, "Radio startup");
    }
    if (action == "LUNASHELL.settings.radios.stop") {
        return finish_settings_command(
            LUNASHELL_radios_stop(),
            "Radio shutdown"
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
    if (auto activity_result = note_activity(*context_);
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

    const std::span<const uint32_t> options =
        AdaptiveChromeController::timeout_options(timing);
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
    if (auto activity_result = note_activity(*context_);
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

    const esp_err_t result = LUNASHELL_hardware_set_full_snapshot_period_ms(
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

void App::finish_settings_event(
    esp_err_t result,
    std::string_view operation
)
{
    if (context_ == nullptr) {
        ESP_LOGE(
            kTag,
            "%.*s event completed without app context",
            static_cast<int>(operation.size()),
            operation.data()
        );
        return;
    }
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
                *context_,
                "Setting not applied",
                details.data()
            ); !dialog_result) {
            ESP_LOGE(kTag, "%s", dialog_result.error().c_str());
        }
    }
    if (screen_ == Screen::Settings) {
        if (auto refresh_result = refresh_settings(*context_);
            !refresh_result) {
            ESP_LOGE(kTag, "%s", refresh_result.error().c_str());
        }
    }
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
void App::finish_tof_settings_event(
    esp_err_t result,
    std::string_view operation
)
{
    if (context_ == nullptr) {
        ESP_LOGE(
            kTag,
            "%.*s event completed without app context",
            static_cast<int>(operation.size()),
            operation.data()
        );
        return;
    }
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
                *context_,
                "Distance setting not applied",
                details.data()
            ); !dialog_result) {
            ESP_LOGE(kTag, "%s", dialog_result.error().c_str());
        }
    }
    if (screen_ == Screen::TofSettings) {
        if (auto refresh_result = refresh_tof_settings(*context_);
            !refresh_result) {
            ESP_LOGE(kTag, "%s", refresh_result.error().c_str());
        }
    }
}
#endif

void App::handle_diagnostic_period_event(
    const esp_brookesia::gui::Event &event
)
{
    if (context_ == nullptr) {
        ESP_LOGE(kTag, "Diagnostic dropdown event arrived without app context");
        return;
    }
    if (auto activity_result = note_activity(*context_);
        !activity_result) {
        ESP_LOGE(kTag, "%s", activity_result.error().c_str());
        return;
    }
    const auto selected_index = event.get_int64("selectedIndex");
    if (!selected_index.has_value() || *selected_index < 0 ||
        static_cast<size_t>(*selected_index) >=
            kDiagnosticPeriodsMs.size()) {
        ESP_LOGE(kTag, "Diagnostic dropdown supplied an invalid selectedIndex");
        return;
    }
    const size_t index = static_cast<size_t>(*selected_index);
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    if (component_ready(snapshot.diagnostics.sample_period) &&
        snapshot.diagnostics.sample_period_ms ==
            kDiagnosticPeriodsMs[index]) {
        return;
    }
    finish_settings_event(
        LUNASHELL_hardware_set_diagnostic_period_ms(
            kDiagnosticPeriodsMs[index]
        ),
        "Diagnostic refresh"
    );
}

void App::handle_imu_smoothing_event(
    const esp_brookesia::gui::Event &event
)
{
    if (context_ == nullptr) {
        ESP_LOGE(kTag, "IMU smoothing event arrived without app context");
        return;
    }
    if (auto activity_result = note_activity(*context_);
        !activity_result) {
        ESP_LOGE(kTag, "%s", activity_result.error().c_str());
        return;
    }
    const auto selected_index = event.get_int64("selectedIndex");
    if (!selected_index.has_value() || *selected_index < 0 ||
        static_cast<size_t>(*selected_index) >=
            kImuSmoothingPercents.size()) {
        ESP_LOGE(kTag, "IMU smoothing dropdown supplied an invalid selectedIndex");
        return;
    }
    const size_t index = static_cast<size_t>(*selected_index);
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    if (component_ready(snapshot.diagnostics.imu_smoothing) &&
        snapshot.diagnostics.imu_smoothing_percent ==
            kImuSmoothingPercents[index]) {
        return;
    }
    finish_settings_event(
        LUNASHELL_hardware_set_imu_smoothing_percent(
            kImuSmoothingPercents[index]
        ),
        "Motion smoothing"
    );
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
void App::handle_tof_sample_count_event(
    const esp_brookesia::gui::Event &event
)
{
    if (context_ == nullptr) {
        ESP_LOGE(kTag, "TOF sample-count event arrived without app context");
        return;
    }
    if (auto activity_result = note_activity(*context_);
        !activity_result) {
        ESP_LOGE(kTag, "%s", activity_result.error().c_str());
        return;
    }
    const auto selected_index = event.get_int64("selectedIndex");
    if (!selected_index.has_value() || *selected_index < 0 ||
        static_cast<size_t>(*selected_index) >=
            kTofSampleCounts.size()) {
        ESP_LOGE(
            kTag,
            "TOF sample-count dropdown supplied an invalid selectedIndex"
        );
        return;
    }
    const size_t index = static_cast<size_t>(*selected_index);
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    if (component_ready(snapshot.distance.settings) &&
        snapshot.distance.settings.sample_count ==
            kTofSampleCounts[index]) {
        return;
    }
    tof_settings_controls_state_.reset();
    finish_tof_settings_event(
        LUNASHELL_distance_set_sample_count(kTofSampleCounts[index]),
        "TOF sample count"
    );
}

void App::handle_tof_sample_interval_event(
    const esp_brookesia::gui::Event &event
)
{
    if (context_ == nullptr) {
        ESP_LOGE(kTag, "TOF interval event arrived without app context");
        return;
    }
    if (auto activity_result = note_activity(*context_);
        !activity_result) {
        ESP_LOGE(kTag, "%s", activity_result.error().c_str());
        return;
    }
    const auto selected_index = event.get_int64("selectedIndex");
    if (!selected_index.has_value() || *selected_index < 0 ||
        static_cast<size_t>(*selected_index) >=
            kTofSampleIntervalsMs.size()) {
        ESP_LOGE(
            kTag,
            "TOF interval dropdown supplied an invalid selectedIndex"
        );
        return;
    }
    const size_t index = static_cast<size_t>(*selected_index);
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    if (component_ready(snapshot.distance.settings) &&
        snapshot.distance.settings.sample_interval_ms ==
            kTofSampleIntervalsMs[index]) {
        return;
    }
    tof_settings_controls_state_.reset();
    finish_tof_settings_event(
        LUNASHELL_distance_set_sample_interval_ms(
            kTofSampleIntervalsMs[index]
        ),
        "TOF sample interval"
    );
}

void App::handle_tof_reference_preview_event(
    const esp_brookesia::gui::Event &event
)
{
    if (context_ == nullptr) {
        ESP_LOGE(kTag, "TOF reference preview arrived without app context");
        return;
    }
    if (auto activity_result = note_activity(*context_);
        !activity_result) {
        ESP_LOGE(kTag, "%s", activity_result.error().c_str());
        return;
    }
    const auto value = event.get_int64("value");
    if (!value.has_value() ||
        *value < kTofReferenceMinimumMm ||
        *value > kTofReferenceMaximumMm) {
        ESP_LOGE(kTag, "TOF reference slider supplied an invalid value");
        return;
    }
    const int32_t quantized =
        kTofReferenceMinimumMm +
        static_cast<int32_t>(
            ((*value - kTofReferenceMinimumMm) +
             (kTofReferenceStepMm / 2)) /
            kTofReferenceStepMm
        ) *
        kTofReferenceStepMm;
    if (quantized < kTofReferenceMinimumMm ||
        quantized > kTofReferenceMaximumMm) {
        ESP_LOGE(
            kTag,
            "TOF reference slider quantized outside the supported range"
        );
        return;
    }

    tof_reference_pending_mm_ = quantized;
    tof_reference_editing_ = true;
    std::array<char, 32> text{};
    std::snprintf(
        text.data(),
        text.size(),
        "%ld mm",
        static_cast<long>(quantized)
    );
    if (auto result = set_text(
            *context_,
            "/LUNASHELL_tof_settings/surface/tof_settings_scroller/"
            "reference_value",
            text.data()
        ); !result) {
        ESP_LOGE(kTag, "%s", result.error().c_str());
    }
}

void App::handle_tof_reference_commit_event(
    const esp_brookesia::gui::Event &event
)
{
    (void)event;
    if (context_ == nullptr) {
        ESP_LOGE(kTag, "TOF reference commit arrived without app context");
        return;
    }
    if (auto activity_result = note_activity(*context_);
        !activity_result) {
        ESP_LOGE(kTag, "%s", activity_result.error().c_str());
        return;
    }
    if (!tof_reference_pending_mm_.has_value() ||
        *tof_reference_pending_mm_ < kTofReferenceMinimumMm ||
        *tof_reference_pending_mm_ > kTofReferenceMaximumMm ||
        ((*tof_reference_pending_mm_ - kTofReferenceMinimumMm) %
         kTofReferenceStepMm) != 0) {
        ESP_LOGE(
            kTag,
            "TOF reference commit rejected without a valid pending value"
        );
        tof_reference_editing_ = false;
        tof_reference_pending_mm_.reset();
        tof_settings_controls_state_.reset();
        finish_tof_settings_event(
            ESP_ERR_INVALID_ARG,
            "TOF physical reference"
        );
        return;
    }

    const uint16_t reference_mm =
        static_cast<uint16_t>(*tof_reference_pending_mm_);
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    tof_reference_editing_ = false;
    tof_settings_controls_state_.reset();
    if (component_ready(snapshot.distance.settings) &&
        snapshot.distance.settings.reference_distance_mm ==
            reference_mm) {
        if (auto refresh_result = refresh_tof_settings(*context_);
            !refresh_result) {
            ESP_LOGE(kTag, "%s", refresh_result.error().c_str());
        }
        return;
    }
    finish_tof_settings_event(
        LUNASHELL_distance_set_reference_mm(reference_mm),
        "TOF physical reference"
    );
}
#endif

void App::handle_brightness_event(
    const esp_brookesia::gui::Event &event
)
{
    if (context_ == nullptr) {
        ESP_LOGE(kTag, "Brightness event arrived without app context");
        return;
    }
    if (auto activity_result = note_activity(*context_);
        !activity_result) {
        ESP_LOGE(kTag, "%s", activity_result.error().c_str());
        return;
    }
    const auto selected_index = event.get_int64("selectedIndex");
    if (!selected_index.has_value() || *selected_index < 0 ||
        static_cast<size_t>(*selected_index) >=
            kBrightnessPercents.size()) {
        ESP_LOGE(kTag, "Brightness dropdown supplied an invalid selectedIndex");
        return;
    }
    const size_t index = static_cast<size_t>(*selected_index);
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    if (component_ready(snapshot.ui.brightness) &&
        snapshot.ui.brightness_percent == kBrightnessPercents[index]) {
        return;
    }
    finish_settings_event(
        LUNASHELL_display_set_brightness_percent(kBrightnessPercents[index]),
        "AMOLED brightness"
    );
}

void App::handle_performance_event(
    const esp_brookesia::gui::Event &event
)
{
    if (context_ == nullptr) {
        ESP_LOGE(kTag, "Performance switch event arrived without app context");
        return;
    }
    if (auto activity_result = note_activity(*context_);
        !activity_result) {
        ESP_LOGE(kTag, "%s", activity_result.error().c_str());
        return;
    }
    const auto checked = event.get_bool("checked");
    if (!checked.has_value()) {
        ESP_LOGE(kTag, "Performance switch event omitted checked state");
        return;
    }
    if (*checked == performance_mode_enabled_) {
        return;
    }
    const esp_err_t result =
        LUNASHELL_performance_mode_set_enabled(*checked);
    if (result != ESP_OK) {
        finish_settings_event(result, "Performance mode");
        return;
    }
    performance_mode_enabled_ = LUNASHELL_performance_mode_get_enabled();
    if (performance_mode_enabled_ != *checked) {
        finish_settings_event(
            ESP_ERR_INVALID_RESPONSE,
            "Performance mode"
        );
        return;
    }
    if (auto timer_result = sync_refresh_timer(
            *context_,
            adaptive_chrome_.display_idle_stage()
        ); !timer_result) {
        ESP_LOGE(kTag, "%s", timer_result.error().c_str());
        finish_settings_event(ESP_FAIL, "Performance refresh timer");
        return;
    }
    finish_settings_event(ESP_OK, "Performance mode");
}

void App::handle_storage_mirror_event(
    const esp_brookesia::gui::Event &event
)
{
    if (context_ == nullptr) {
        ESP_LOGE(kTag, "SD mirror switch event arrived without app context");
        return;
    }
    if (auto activity_result = note_activity(*context_);
        !activity_result) {
        ESP_LOGE(kTag, "%s", activity_result.error().c_str());
        return;
    }
    const auto checked = event.get_bool("checked");
    if (!checked.has_value()) {
        ESP_LOGE(kTag, "SD mirror switch event omitted checked state");
        return;
    }
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    if (!component_ready(snapshot.storage.logger_mirror)) {
        finish_settings_event(
            snapshot.storage.logger_mirror.last_error != ESP_OK
                ? snapshot.storage.logger_mirror.last_error
                : ESP_ERR_INVALID_STATE,
            "SD log copy"
        );
        return;
    }
    if (*checked == snapshot.storage.logger_mirror_requested) {
        return;
    }
    finish_settings_event(
        LUNASHELL_storage_set_logger_mirror(*checked),
        "SD log copy"
    );
}

void App::handle_auto_rotation_event(
    const esp_brookesia::gui::Event &event
)
{
    if (context_ == nullptr) {
        ESP_LOGE(kTag, "Rotation switch event arrived without app context");
        return;
    }
    if (auto activity_result = note_activity(*context_);
        !activity_result) {
        ESP_LOGE(kTag, "%s", activity_result.error().c_str());
        return;
    }
    const auto checked = event.get_bool("checked");
    if (!checked.has_value()) {
        ESP_LOGE(kTag, "Rotation switch event omitted checked state");
        return;
    }
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    if (!component_ready(snapshot.ui.orientation)) {
        finish_settings_event(
            snapshot.ui.orientation.last_error != ESP_OK
                ? snapshot.ui.orientation.last_error
                : ESP_ERR_INVALID_STATE,
            "Automatic rotation"
        );
        return;
    }
    if (*checked == snapshot.ui.orientation.auto_rotation_enabled) {
        return;
    }
    finish_settings_event(
        LUNASHELL_display_set_auto_rotation_enabled(*checked),
        "Automatic rotation"
    );
}

// Close every UI producer before acknowledging a renderer pause request.
std::expected<void, std::string> App::on_timer(
    AppContext &context,
    esp_brookesia::system::core::TimerId timer_id,
    std::string_view name
)
{
    if (timer_id == power_state_sync_timer_ &&
        name == kPowerStateSyncTimer) {
        const DeviceSnapshot snapshot =
            DeviceSnapshotService::instance().snapshot();
        if (snapshot.power.state != DeviceOperatingState::Active) {
            if (standby_ui_state_ == StandbyUiState::Active) {
                standby_ui_state_ = StandbyUiState::Quiescing;
                const uint32_t generation = ++standby_ui_generation_;
                const uint32_t transition_sequence =
                    snapshot.power.transition_sequence;
                standby_ui_transition_sequence_ = transition_sequence;
                if (auto result =
                        sync_refresh_timer(context, DisplayIdleStage::Off);
                    !result) {
                    ESP_LOGE(
                        kTag,
                        "UI refresh timer quiescence failed for transition %lu: %s",
                        static_cast<unsigned long>(transition_sequence),
                        result.error().c_str()
                    );
                    LUNASHELL_power_manager_note_ui_standby_result(
                        transition_sequence,
                        ESP_FAIL
                    );
                    return result;
                }
                if (auto result = adaptive_chrome_.quiesce(context.gui());
                    !result) {
                    ESP_LOGE(
                        kTag,
                        "Adaptive Chrome quiescence failed for transition %lu: %s",
                        static_cast<unsigned long>(transition_sequence),
                        result.error().c_str()
                    );
                    LUNASHELL_power_manager_note_ui_standby_result(
                        transition_sequence,
                        ESP_FAIL
                    );
                    return result;
                }
                auto quiesce_result = context.gui().quiesce_input(
                    [this, generation, transition_sequence](
                        std::expected<void, std::string> drain_result
                    ) {
                        if (generation != standby_ui_generation_ ||
                            standby_ui_state_ !=
                                StandbyUiState::Quiescing ||
                            standby_ui_transition_sequence_ !=
                                transition_sequence) {
                            return;
                        }
                        if (!drain_result) {
                            ESP_LOGE(
                                kTag,
                                "UI standby drain failed for transition %lu: %s",
                                static_cast<unsigned long>(transition_sequence),
                                drain_result.error().c_str()
                            );
                            LUNASHELL_power_manager_note_ui_standby_result(
                                transition_sequence,
                                ESP_FAIL
                            );
                            return;
                        }
                        const DeviceSnapshot latest =
                            DeviceSnapshotService::instance().snapshot();
                        if (latest.power.state !=
                                DeviceOperatingState::EnteringStandby ||
                            latest.power.transition_sequence !=
                                transition_sequence) {
                            ESP_LOGW(
                                kTag,
                                "Ignored stale UI standby drain: requested=%lu current=%lu state=%u",
                                static_cast<unsigned long>(transition_sequence),
                                static_cast<unsigned long>(
                                    latest.power.transition_sequence
                                ),
                                static_cast<unsigned>(latest.power.state)
                            );
                            return;
                        }
                        standby_ui_state_ = StandbyUiState::Quiesced;
                        ESP_LOGI(
                            kTag,
                            "UI input and GUI work quiesced for transition %lu",
                            static_cast<unsigned long>(transition_sequence)
                        );
                        LUNASHELL_power_manager_note_ui_standby_result(
                            transition_sequence,
                            ESP_OK
                        );
                    }
                );
                if (!quiesce_result) {
                    ESP_LOGE(
                        kTag,
                        "Failed to schedule LUNASHELL input quiescence for transition %lu: %s",
                        static_cast<unsigned long>(transition_sequence),
                        quiesce_result.error().c_str()
                    );
                    LUNASHELL_power_manager_note_ui_standby_result(
                        transition_sequence,
                        ESP_FAIL
                    );
                    return std::unexpected(
                        "Failed to schedule LUNASHELL input quiescence: " +
                        quiesce_result.error()
                    );
                }
            }
            return {};
        }

        const bool resume_from_standby =
            standby_ui_state_ != StandbyUiState::Active;
        if (resume_from_standby) {
            ++standby_ui_generation_;
        }
        const DisplayIdleStage previous_stage =
            adaptive_chrome_.display_idle_stage();
        if (auto result =
                adaptive_chrome_.update(context.gui(), snapshot);
            !result) {
            return result;
        }
        if (auto result = sync_content_viewport(context); !result) {
            return result;
        }
        const DisplayIdleStage current_stage =
            adaptive_chrome_.display_idle_stage();
        if (auto result =
                sync_refresh_timer(context, current_stage);
            !result) {
            return result;
        }
        if (resume_from_standby) {
            if (auto result = refresh(context); !result) {
                return result;
            }
            if (auto result = context.gui().drain_pending_work(); !result) {
                return std::unexpected(
                    "Failed to drain restored LUNASHELL GUI: " + result.error()
                );
            }
            if (auto result = context.gui().resume_input(); !result) {
                return std::unexpected(
                    "Failed to reopen LUNASHELL input after standby: " +
                    result.error()
                );
            }
            standby_ui_state_ = StandbyUiState::Active;
            standby_ui_transition_sequence_ = 0;
            ESP_LOGI(kTag, "UI input reopened after renderer wake");
            return {};
        }
        if (previous_stage == DisplayIdleStage::Off &&
            current_stage == DisplayIdleStage::Active) {
            return refresh(context);
        }
        return {};
    }
    if (timer_id == refresh_timer_ && name == kRefreshTimer) {
        if (standby_ui_state_ != StandbyUiState::Active ||
            adaptive_chrome_.display_idle_stage() ==
            DisplayIdleStage::Off) {
            return {};
        }
        const DeviceSnapshot snapshot =
            DeviceSnapshotService::instance().snapshot();
        const int64_t now_us = esp_timer_get_time();
        if (snapshot.ui.touch_last_activity_at_us > 0 &&
            now_us >= snapshot.ui.touch_last_activity_at_us &&
            now_us - snapshot.ui.touch_last_activity_at_us <
                kTouchRefreshQuietPeriodUs) {
            return {};
        }
        return refresh(context);
    }
    return {};
}

std::expected<void, std::string> App::sync_refresh_timer(
    AppContext &context,
    DisplayIdleStage stage
)
{
    const bool live_diagnostic_screen =
        screen_ == Screen::Audio ||
        screen_ == Screen::AudioFft ||
        screen_ == Screen::AudioDetail ||
        screen_ == Screen::Imu
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
        ||
        screen_ == Screen::Tof ||
        screen_ == Screen::TofSettings
#endif
        ;
    const uint32_t desired_period_ms =
        stage == DisplayIdleStage::Active
            ? performance_mode_enabled_
                ? kPerformanceRefreshPeriodMs
                : live_diagnostic_screen
                    ? kLiveDiagnosticRefreshPeriodMs
                    : kActiveRefreshPeriodMs
            : stage == DisplayIdleStage::Dimmed
                ? kDimmedRefreshPeriodMs
                : 0U;
    const bool timer_running =
        refresh_timer_ !=
        esp_brookesia::system::core::INVALID_TIMER_ID;
    if (desired_period_ms == refresh_period_ms_ &&
        timer_running == (desired_period_ms > 0U)) {
        return {};
    }

    if (timer_running) {
        if (!context.timer().stop(refresh_timer_)) {
            return std::unexpected(
                "Failed to stop LUNASHELL refresh timer while changing "
                "display idle stage"
            );
        }
        refresh_timer_ =
            esp_brookesia::system::core::INVALID_TIMER_ID;
        refresh_period_ms_ = 0;
    }

    if (desired_period_ms == 0U) {
        ESP_LOGI(kTag, "UI Refresh stopped while display is off");
        return {};
    }

    auto timer_result = context.timer().start_periodic(
        kRefreshTimer,
        desired_period_ms
    );
    if (!timer_result) {
        return std::unexpected(
            "Failed to start LUNASHELL refresh timer at " +
            std::to_string(desired_period_ms) + " ms: " +
            timer_result.error()
        );
    }
    refresh_timer_ = *timer_result;
    refresh_period_ms_ = desired_period_ms;
    ESP_LOGI(
        kTag,
        "UI Refresh period: %lu ms",
        static_cast<unsigned long>(desired_period_ms)
    );
    return {};
}

std::expected<void, std::string> App::note_activity(AppContext &context)
{
    return adaptive_chrome_.note_activity(context.gui());
}

// Refresh the reusable curved chrome from the same canonical device state.
std::expected<void, std::string> App::refresh_chrome(
    AppContext &context,
    const DeviceSnapshot &snapshot
)
{
    if (auto result = adaptive_chrome_.update(context.gui(), snapshot); !result) {
        return result;
    }
    if (auto result = sync_content_viewport(context); !result) {
        return result;
    }
    if (auto result = sync_refresh_timer(
            context,
            adaptive_chrome_.display_idle_stage()
        ); !result) {
        return result;
    }
    return status_bar_.update(context.gui(), snapshot);
}

std::expected<void, std::string> App::sync_content_viewport(
    AppContext &context
)
{
    enum class ViewportLayout : uint8_t {
        Flexible,
        CircularScroll,
        FixedCanvas,
        Pairing,
    };

    struct ViewportSpec {
        std::string_view surface_path;
        ViewportLayout layout;
        int32_t top_padding;
        int32_t bottom_padding;
    };

    const ViewportSpec spec = [this]() -> ViewportSpec {
        switch (screen_) {
        case Screen::Welcome:
            return {
                "/LUNASHELL_welcome/surface",
                ViewportLayout::CircularScroll,
                48,
                70,
            };
        case Screen::Battery:
            return {
                "/LUNASHELL_battery/surface",
                ViewportLayout::CircularScroll,
                48,
                8,
            };
        case Screen::Logs:
            return {
                "/LUNASHELL_logs/surface",
                ViewportLayout::CircularScroll,
                48,
                8,
            };
        case Screen::Hardware:
            return {
                "/LUNASHELL_hardware/surface",
                ViewportLayout::CircularScroll,
                48,
                8,
            };
        case Screen::HardwareDetail:
            return {
                "/LUNASHELL_hardware_detail/surface",
                ViewportLayout::CircularScroll,
                48,
                12,
            };
        case Screen::Audio:
            return {
                "/LUNASHELL_audio/surface",
                ViewportLayout::FixedCanvas,
                0,
                0,
            };
        case Screen::AudioFft:
            return {
                "/LUNASHELL_audio_fft/surface",
                ViewportLayout::FixedCanvas,
                0,
                0,
            };
        case Screen::AudioDetail:
            return {
                "/LUNASHELL_audio_detail/surface",
                ViewportLayout::FixedCanvas,
                0,
                0,
            };
        case Screen::Imu:
            return {
                "/LUNASHELL_imu/surface",
                ViewportLayout::FixedCanvas,
                0,
                0,
            };
        case Screen::Gps:
            return {
                "/LUNASHELL_gps/surface",
                ViewportLayout::FixedCanvas,
                0,
                0,
            };
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
        case Screen::Tof:
            return {
                "/LUNASHELL_tof/surface",
                ViewportLayout::FixedCanvas,
                0,
                0,
            };
        case Screen::TofSettings:
            return {
                "/LUNASHELL_tof_settings/surface",
                ViewportLayout::CircularScroll,
                48,
                8,
            };
#endif
        case Screen::Settings:
            return {
                "/LUNASHELL_settings/surface",
                ViewportLayout::CircularScroll,
                48,
                8,
            };
        case Screen::Storage:
            return {
                "/LUNASHELL_storage/surface",
                ViewportLayout::CircularScroll,
                48,
                8,
            };
        case Screen::About:
            return {
                "/LUNASHELL_about/surface",
                ViewportLayout::Flexible,
                0,
                0,
            };
        case Screen::PairIntro:
        case Screen::PairBluetooth:
        case Screen::PairApp:
        case Screen::AppStoreQr:
        case Screen::PairQr:
        case Screen::PairResult:
            return {
                "/LUNASHELL_pairing/surface",
                ViewportLayout::Pairing,
                50,
                6,
            };
        }
        return {};
    }();
    if (spec.surface_path.empty()) {
        return std::unexpected(
            "Unknown LUNASHELL route for adaptive content viewport"
        );
    }

    const ContentViewportState desired{
        .screen = screen_,
        .command_arc_visible =
            adaptive_chrome_.command_arc_visible(),
        .status_arc_visible =
            adaptive_chrome_.status_arc_visible(),
    };
    if (content_viewport_state_ == desired) {
        return {};
    }

    const int32_t surface_y =
        desired.status_arc_visible ? kChromeInsetDp : 0;
    const int32_t surface_height =
        kDisplaySizeDp -
        surface_y -
        (desired.command_arc_visible ? kChromeInsetDp : 0);
    const int32_t released_height =
        surface_height - kContentSafeSizeDp;

    std::vector<esp_brookesia::gui::BindingValueUpdate> updates = {
        {
            .absolute_path = std::string(spec.surface_path),
            .key = "viewport_y",
            .value = std::to_string(surface_y),
        },
        {
            .absolute_path = std::string(spec.surface_path),
            .key = "viewport_height",
            .value = std::to_string(surface_height),
        },
    };
    if (spec.layout != ViewportLayout::Flexible) {
        int32_t top_padding = spec.top_padding;
        int32_t bottom_padding = spec.bottom_padding;
        if (spec.layout == ViewportLayout::CircularScroll) {
            if (!desired.status_arc_visible) {
                top_padding = kCircularWideContentInsetDp;
            }
            if (!desired.command_arc_visible) {
                bottom_padding = kCircularWideContentInsetDp;
            }
        } else {
            const int32_t released_above = released_height / 2;
            top_padding += released_above;
            bottom_padding += released_height - released_above;
        }
        updates.push_back({
            .absolute_path = std::string(spec.surface_path),
            .key = "viewport_padding_top",
            .value = std::to_string(top_padding),
        });
        updates.push_back({
            .absolute_path = std::string(spec.surface_path),
            .key = "viewport_padding_bottom",
            .value = std::to_string(bottom_padding),
        });
    }

    auto result = set_binding_values(context, updates);
    if (!result) {
        return std::unexpected(
            "Failed to resize LUNASHELL content viewport: " +
            result.error()
        );
    }
    content_viewport_state_ = desired;
    ESP_LOGI(
        kTag,
        "Content viewport: y=%ld height=%ld command=%s status=%s",
        static_cast<long>(surface_y),
        static_cast<long>(surface_height),
        desired.command_arc_visible ? "visible" : "hidden",
        desired.status_arc_visible ? "visible" : "hidden"
    );
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
    case Screen::Battery:
        return refresh_battery(context);
    case Screen::Logs:
        return refresh_logs(context);
    case Screen::Hardware:
        return refresh_hardware(context);
    case Screen::HardwareDetail:
        return refresh_hardware_detail(context);
    case Screen::Audio:
        return refresh_audio(context);
    case Screen::AudioFft:
        return refresh_audio_fft(context);
    case Screen::AudioDetail:
        return refresh_audio_detail(context);
    case Screen::Imu:
        return refresh_imu(context);
    case Screen::Gps:
        return refresh_gps(context);
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    case Screen::Tof:
        return refresh_tof(context);
    case Screen::TofSettings:
        return refresh_tof_settings(context);
#endif
    case Screen::Settings:
        return refresh_settings(context);
    case Screen::Storage:
        return refresh_storage(context);
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
    std::array<char, 1024> text{};

    const bool heap_valid =
        component_ready(snapshot.memory) &&
        snapshot.memory.internal_heap_total_bytes > 0 &&
        snapshot.memory.internal_free_bytes <=
            snapshot.memory.internal_heap_total_bytes &&
        snapshot.memory.internal_largest_block_bytes <=
            snapshot.memory.internal_free_bytes &&
        snapshot.memory.internal_minimum_free_bytes <=
            snapshot.memory.internal_free_bytes &&
        snapshot.memory.internal_allocated_blocks <=
            snapshot.memory.internal_total_blocks &&
        snapshot.memory.internal_free_blocks <=
            snapshot.memory.internal_total_blocks;
    const bool psram_valid =
        component_ready(snapshot.memory) &&
        snapshot.memory.psram_heap_total_bytes > 0 &&
        snapshot.memory.psram_free_bytes <=
            snapshot.memory.psram_heap_total_bytes &&
        snapshot.memory.psram_largest_block_bytes <=
            snapshot.memory.psram_free_bytes &&
        snapshot.memory.psram_minimum_free_bytes <=
            snapshot.memory.psram_free_bytes &&
        snapshot.memory.psram_allocated_blocks <=
            snapshot.memory.psram_total_blocks &&
        snapshot.memory.psram_free_blocks <=
            snapshot.memory.psram_total_blocks;
    int heap_percent = 0;
    if (heap_valid) {
        const uint32_t heap_used_bytes =
            snapshot.memory.internal_heap_total_bytes -
            snapshot.memory.internal_free_bytes;
        heap_percent = static_cast<int>(
            (static_cast<float>(heap_used_bytes) * 100.0F) /
            static_cast<float>(snapshot.memory.internal_heap_total_bytes)
        );
        if (snapshot.memory.allocation_failure_count > 0U) {
            std::snprintf(
                text.data(),
                text.size(),
                "ALLOC FAIL #%lu | MAX %lu B",
                static_cast<unsigned long>(
                    snapshot.memory.allocation_failure_count
                ),
                static_cast<unsigned long>(
                    snapshot.memory.largest_failed_allocation_bytes
                )
            );
        } else {
            std::snprintf(
                text.data(),
                text.size(),
                "%lu/%luK L%luK F%lu",
                static_cast<unsigned long>(heap_used_bytes / 1024U),
                static_cast<unsigned long>(
                    snapshot.memory.internal_heap_total_bytes / 1024U
                ),
                static_cast<unsigned long>(
                    snapshot.memory.internal_largest_block_bytes / 1024U
                ),
                static_cast<unsigned long>(
                    snapshot.memory.internal_free_blocks
                )
            );
        }
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "%s | %s",
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
            "/LUNASHELL_welcome/surface/ram/value",
            text.data()
        ); !result) {
        return result;
    }
    if (heap_valid) {
        if (auto result = set_value(
                context,
                "/LUNASHELL_welcome/surface/ram/bar",
                heap_percent
            ); !result) {
            return result;
        }
    }
    if (auto result = set_hidden(
            context,
            "/LUNASHELL_welcome/surface/ram/bar",
            !heap_valid
        ); !result) {
        return result;
    }

    int psram_percent = 0;
    if (psram_valid) {
        psram_percent = static_cast<int>(
            (static_cast<float>(snapshot.memory.psram_used_bytes) * 100.0F) /
            static_cast<float>(snapshot.memory.psram_heap_total_bytes)
        );
        std::snprintf(
            text.data(),
            text.size(),
            "%lu/%luK L%luK F%lu",
            static_cast<unsigned long>(snapshot.memory.psram_used_bytes / 1024U),
            static_cast<unsigned long>(
                snapshot.memory.psram_heap_total_bytes / 1024U
            ),
            static_cast<unsigned long>(
                snapshot.memory.psram_largest_block_bytes / 1024U
            ),
            static_cast<unsigned long>(snapshot.memory.psram_free_blocks)
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "%s | %s",
            component_state(snapshot.memory),
            component_reason(snapshot.memory)
        );
    }
    if (auto result = set_text(
            context,
            "/LUNASHELL_welcome/surface/psram/value",
            text.data()
        ); !result) {
        return result;
    }
    if (psram_valid) {
        if (auto result = set_value(
                context,
                "/LUNASHELL_welcome/surface/psram/bar",
                psram_percent
            ); !result) {
            return result;
        }
    }
    if (auto result = set_hidden(
            context,
            "/LUNASHELL_welcome/surface/psram/bar",
            !psram_valid
        ); !result) {
        return result;
    }

    const bool flash_valid =
        component_ready(snapshot.memory) &&
        snapshot.memory.app_partition_bytes > 0 &&
        snapshot.memory.firmware_image_bytes <=
            snapshot.memory.app_partition_bytes;
    int flash_percent = 0;
    if (flash_valid) {
        flash_percent = static_cast<int>(
            (static_cast<uint64_t>(snapshot.memory.firmware_image_bytes) *
             100U) /
            snapshot.memory.app_partition_bytes
        );
        std::snprintf(
            text.data(),
            text.size(),
            "%lu/%lu KB | %d%%",
            static_cast<unsigned long>(
                snapshot.memory.firmware_image_bytes / 1024U
            ),
            static_cast<unsigned long>(
                snapshot.memory.app_partition_bytes / 1024U
            ),
            flash_percent
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "%s | %s",
            component_state(snapshot.memory),
            component_reason(snapshot.memory)
        );
    }
    if (auto result = set_text(
            context,
            "/LUNASHELL_welcome/surface/flash/value",
            text.data()
        ); !result) {
        return result;
    }
    if (flash_valid) {
        if (auto result = set_value(
                context,
                "/LUNASHELL_welcome/surface/flash/bar",
                flash_percent
            ); !result) {
            return result;
        }
    }
    if (auto result = set_hidden(
            context,
            "/LUNASHELL_welcome/surface/flash/bar",
            !flash_valid
        ); !result) {
        return result;
    }

    int storage_percent = 0;
    bool storage_percent_valid = false;
    if (storage_cleanup_failed(snapshot.storage)) {
        std::snprintf(
            text.data(),
            text.size(),
            "CLEANUP %s | %s",
            storage_cleanup_stage_name(snapshot.storage.cleanup_stage),
            esp_err_to_name(snapshot.storage.cleanup_error)
        );
    } else if (storage_format_required(snapshot.storage)) {
        std::snprintf(
            text.data(),
            text.size(),
            "FORMAT REQUIRED | %s",
            component_reason(snapshot.storage)
        );
    } else if (storage_mounted_ready(snapshot.storage) &&
        snapshot.storage.filesystem_total_bytes > 0 &&
        snapshot.storage.free_bytes <= snapshot.storage.filesystem_total_bytes) {
        const uint64_t used_bytes =
            snapshot.storage.filesystem_total_bytes -
            snapshot.storage.free_bytes;
        storage_percent = static_cast<int>(
            (used_bytes * 100ULL) /
            snapshot.storage.filesystem_total_bytes
        );
        storage_percent_valid = true;
        std::snprintf(
            text.data(),
            text.size(),
            "%llu/%llu MB | %d%%%s",
            static_cast<unsigned long long>(
                used_bytes / (1024ULL * 1024ULL)
            ),
            static_cast<unsigned long long>(
                snapshot.storage.filesystem_total_bytes / (1024ULL * 1024ULL)
            ),
            storage_percent,
            component_ready(snapshot.storage.logger_mirror) &&
                    snapshot.storage.logger_mirror_enabled
                ? " | LOG"
                : ""
        );
    } else if (storage_mounted_ready(snapshot.storage)) {
        std::snprintf(text.data(), text.size(), "ERROR | INVALID CAPACITY");
    } else if (!snapshot.storage.card_response_verified) {
        std::snprintf(
            text.data(),
            text.size(),
            "UNCONFIRMED | %s",
            component_reason(snapshot.storage)
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "%s | %s",
            component_state(snapshot.storage),
            component_reason(snapshot.storage)
        );
    }
    if (auto result = set_text(
            context,
            "/LUNASHELL_welcome/surface/sd/value",
            text.data()
        ); !result) {
        return result;
    }
    if (storage_percent_valid) {
        if (auto result = set_value(
                context,
                "/LUNASHELL_welcome/surface/sd/bar",
                storage_percent
            ); !result) {
            return result;
        }
    }
    if (auto result = set_hidden(
            context,
            "/LUNASHELL_welcome/surface/sd/bar",
            !storage_percent_valid
        ); !result) {
        return result;
    }

    for (size_t core = 0; core < 2; ++core) {
        const std::string path = core == 0
            ? "/LUNASHELL_welcome/surface/core0"
            : "/LUNASHELL_welcome/surface/core1";
        bool utilization_available = false;
        uint8_t utilization_percent = 0;
        if (component_ready(snapshot.processor) &&
            core < snapshot.processor.core_count) {
            const CoreSnapshot &core_snapshot =
                snapshot.processor.cores[core];
            utilization_available =
                core_snapshot.available &&
                core_snapshot.utilization_available;
            utilization_percent = core_snapshot.utilization_percent;
            if (utilization_available) {
                std::snprintf(
                    text.data(),
                    text.size(),
                    "%u%%",
                    static_cast<unsigned>(utilization_percent)
                );
            } else {
                std::snprintf(text.data(), text.size(), "MEASURING...");
            }
        } else {
            std::snprintf(
                text.data(),
                text.size(),
                "%s (%s)",
                component_state(snapshot.processor),
                component_reason(snapshot.processor)
            );
        }
        if (auto result = set_text(context, path + "/value", text.data()); !result) {
            return result;
        }
        if (utilization_available) {
            if (auto result = set_binding_text(
                    context,
                    path + "/bar",
                    "value",
                    std::to_string(utilization_percent)
                ); !result) {
                return result;
            }
        }
        if (auto result = set_binding_text(
                context,
                path + "/bar",
                "hidden",
                utilization_available ? "false" : "true"
            ); !result) {
            return result;
        }
    }

    const LUNASHELL::WifiSnapshot &wifi = snapshot.radios.wifi;
    const char *ip_state =
        wifi.ip_acquired && wifi.ip_address[0] != '\0'
            ? wifi.ip_address
            : wifi.started
                ? "CONNECTING"
                : "OFFLINE";
    std::snprintf(
        text.data(),
        text.size(),
        "LUNASHELL device console\nIP ADDRESS | %s",
        ip_state
    );
    return set_text(
        context,
        "/LUNASHELL_welcome/surface/device_status",
        text.data()
    );
}

std::expected<void, std::string> App::refresh_battery(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }

    const BatterySnapshot &battery = snapshot.battery;
    const bool pmic_ready = component_ready(battery);
    const bool level_valid =
        pmic_ready &&
        battery.battery_present &&
        battery.level_percent >= 0 &&
        battery.level_percent <= 100 &&
        battery.battery_mv > 0;
    const bool charging =
        pmic_ready &&
        battery.battery_present &&
        battery.usb_present &&
        std::string_view(battery.power_state) == "charging";

    std::string_view level_color = palette::kStatusMuted;
    if (level_valid) {
        if (battery.level_percent <= 12) {
            level_color = palette::kStatusFault;
        } else if (battery.level_percent <= 30) {
            level_color = palette::kStatusWarning;
        } else {
            level_color = palette::kStatusReady;
        }
    } else if (battery.last_error != ESP_OK ||
               battery.health == DeviceHealth::Degraded ||
               battery.health == DeviceHealth::Missing) {
        level_color = palette::kStatusFault;
    } else if (battery.data_stale) {
        level_color = palette::kStatusWarning;
    }

    std::array<char, 32> percent_text{};
    std::array<char, 96> state_text{};
    std::array<char, 96> health_text{};
    if (level_valid) {
        std::snprintf(
            percent_text.data(),
            percent_text.size(),
            "%d%%",
            battery.level_percent
        );
    } else {
        std::snprintf(percent_text.data(), percent_text.size(), "--");
    }

    if (!pmic_ready) {
        std::snprintf(
            state_text.data(),
            state_text.size(),
            "%s | %s",
            component_state(battery),
            component_reason(battery)
        );
        std::snprintf(
            health_text.data(),
            health_text.size(),
            "AXP2101 %s",
            component_state(battery)
        );
    } else if (!battery.battery_present) {
        std::snprintf(
            state_text.data(),
            state_text.size(),
            "NO BATTERY DETECTED"
        );
        std::snprintf(
            health_text.data(),
            health_text.size(),
            "PMIC READY | CELL ABSENT"
        );
    } else if (charging) {
        std::snprintf(
            state_text.data(),
            state_text.size(),
            "CHARGING | USB CONNECTED"
        );
        std::snprintf(
            health_text.data(),
            health_text.size(),
            "PMIC READY | CELL PRESENT"
        );
    } else if (std::string_view(battery.power_state) == "discharging") {
        std::snprintf(
            state_text.data(),
            state_text.size(),
            "DISCHARGING | ON BATTERY"
        );
        std::snprintf(
            health_text.data(),
            health_text.size(),
            "PMIC READY | CELL PRESENT"
        );
    } else {
        std::snprintf(
            state_text.data(),
            state_text.size(),
            "STANDBY | %s",
            battery.usb_present ? "USB CONNECTED" : "POWER IDLE"
        );
        std::snprintf(
            health_text.data(),
            health_text.size(),
            "PMIC READY | CELL PRESENT"
        );
    }

    std::array<char, 48> cell_value{};
    std::array<char, 112> cell_detail{};
    if (level_valid) {
        std::snprintf(
            cell_value.data(),
            cell_value.size(),
            "%d mV",
            battery.battery_mv
        );
        std::snprintf(
            cell_detail.data(),
            cell_detail.size(),
            "PMIC LEVEL %d%% | CHARGE TARGET 4200 mV",
            battery.level_percent
        );
    } else if (pmic_ready && !battery.battery_present) {
        std::snprintf(cell_value.data(), cell_value.size(), "NOT PRESENT");
        std::snprintf(
            cell_detail.data(),
            cell_detail.size(),
            "CELL TELEMETRY UNAVAILABLE"
        );
    } else {
        std::snprintf(cell_value.data(), cell_value.size(), "UNAVAILABLE");
        std::snprintf(
            cell_detail.data(),
            cell_detail.size(),
            "%s",
            component_reason(battery)
        );
    }

    std::array<char, 48> usb_value{};
    std::array<char, 112> usb_detail{};
    std::string_view usb_color = palette::kStatusMuted;
    if (!pmic_ready) {
        std::snprintf(usb_value.data(), usb_value.size(), "UNAVAILABLE");
        std::snprintf(
            usb_detail.data(),
            usb_detail.size(),
            "%s",
            component_reason(battery)
        );
        usb_color = level_color;
    } else if (!battery.usb_present) {
        std::snprintf(usb_value.data(), usb_value.size(), "NOT ACTIVE");
        std::snprintf(
            usb_detail.data(),
            usb_detail.size(),
            "VBUS NOT GOOD OR NOT PRESENT"
        );
    } else {
        std::snprintf(usb_value.data(), usb_value.size(), "CONNECTED");
        std::snprintf(
            usb_detail.data(),
            usb_detail.size(),
            "VBUS %d mV | INPUT PRESENT",
            battery.usb_mv
        );
        usb_color = palette::kStatusReady;
    }

    std::array<char, 48> system_value{};
    std::array<char, 112> system_detail{};
    const bool system_valid = pmic_ready && battery.system_mv > 0;
    if (system_valid) {
        std::snprintf(
            system_value.data(),
            system_value.size(),
            "%d mV",
            battery.system_mv
        );
        std::snprintf(
            system_detail.data(),
            system_detail.size(),
            "AXP2101 SYSTEM OUTPUT"
        );
    } else {
        std::snprintf(system_value.data(), system_value.size(), "UNAVAILABLE");
        std::snprintf(
            system_detail.data(),
            system_detail.size(),
            "%s",
            component_reason(battery)
        );
    }

    std::array<char, 48> temperature_value{};
    std::array<char, 112> temperature_detail{};
    const bool temperature_valid =
        pmic_ready && std::isfinite(battery.die_temperature_c);
    if (temperature_valid) {
        std::snprintf(
            temperature_value.data(),
            temperature_value.size(),
            "%.1f C",
            static_cast<double>(battery.die_temperature_c)
        );
        std::snprintf(
            temperature_detail.data(),
            temperature_detail.size(),
            "AXP2101 INTERNAL SENSOR"
        );
    } else {
        std::snprintf(
            temperature_value.data(),
            temperature_value.size(),
            "UNAVAILABLE"
        );
        std::snprintf(
            temperature_detail.data(),
            temperature_detail.size(),
            "%s",
            component_reason(battery)
        );
    }

    std::array<char, 48> policy_value{};
    std::array<char, 112> policy_detail{};
    if (pmic_ready) {
        std::snprintf(
            policy_value.data(),
            policy_value.size(),
            "4.20 V / 400 mA"
        );
        std::snprintf(
            policy_detail.data(),
            policy_detail.size(),
            "PRECHARGE 50 mA | TERMINATION 25 mA\n"
            "VERIFIED AT PMIC INITIALIZATION"
        );
    } else {
        std::snprintf(policy_value.data(), policy_value.size(), "UNCONFIRMED");
        std::snprintf(
            policy_detail.data(),
            policy_detail.size(),
            "PMIC INITIALIZATION DID NOT CONFIRM THE PROFILE"
        );
    }

    std::array<char, 48> phase_value{};
    std::array<char, 112> phase_detail{};
    std::string_view phase_color = palette::kStatusMuted;
    if (pmic_ready) {
        const std::string_view phase = battery.charger_phase;
        const char *display_phase = "UNKNOWN";
        if (phase == "trickle") {
            display_phase = "TRICKLE";
        } else if (phase == "precharge") {
            display_phase = "PRECHARGE";
        } else if (phase == "constant-current") {
            display_phase = "CC CHARGE";
        } else if (phase == "constant-voltage") {
            display_phase = "CV CHARGE";
        } else if (phase == "done") {
            display_phase = "DONE";
        } else if (phase == "stopped") {
            display_phase = "STOPPED";
        }
        std::snprintf(
            phase_value.data(),
            phase_value.size(),
            "%s",
            display_phase
        );
        std::snprintf(
            phase_detail.data(),
            phase_detail.size(),
            "LIVE AXP2101 STATUS2 CHARGER PHASE"
        );
        phase_color =
            phase == "stopped"
                ? palette::kStatusMuted
                : palette::kStatusReady;
    } else {
        std::snprintf(
            phase_value.data(),
            phase_value.size(),
            "UNAVAILABLE"
        );
        std::snprintf(
            phase_detail.data(),
            phase_detail.size(),
            "%s",
            component_reason(battery)
        );
        phase_color = level_color;
    }

    std::array<char, 48> flags_value{};
    std::array<char, 128> flags_detail{};
    std::string_view flags_color = palette::kStatusMuted;
    if (pmic_ready) {
        std::snprintf(
            flags_value.data(),
            flags_value.size(),
            "BATFET %s",
            battery.batfet_enabled ? "ON" : "OFF"
        );
        std::snprintf(
            flags_detail.data(),
            flags_detail.size(),
            "VBUS %s | BAT ACTIVE %s\nTHERMAL %s | INPUT LIMIT %s",
            battery.vbus_good ? "GOOD" : "NOT GOOD",
            battery.battery_active_mode ? "YES" : "NO",
            battery.thermal_regulation_active ? "ACTIVE" : "NO",
            battery.input_current_limit_active ? "ACTIVE" : "NO"
        );
        flags_color =
            battery.thermal_regulation_active ||
                    battery.input_current_limit_active ||
                    battery.battery_active_mode
                ? palette::kStatusWarning
                : (battery.batfet_enabled
                       ? palette::kStatusReady
                       : palette::kStatusMuted);
    } else {
        std::snprintf(
            flags_value.data(),
            flags_value.size(),
            "UNAVAILABLE"
        );
        std::snprintf(
            flags_detail.data(),
            flags_detail.size(),
            "%s",
            component_reason(battery)
        );
        flags_color = level_color;
    }

    std::array<char, 48> sample_value{};
    std::array<char, 128> sample_detail{};
    std::string_view sample_color = level_color;
    const int64_t now_us = esp_timer_get_time();
    const bool has_success = battery.last_success_at_us > 0;
    const double age_seconds =
        has_success
            ? static_cast<double>(
                  std::max<int64_t>(0, now_us - battery.last_success_at_us)
              ) /
                  1000000.0
            : 0.0;
    if (pmic_ready) {
        std::snprintf(sample_value.data(), sample_value.size(), "FRESH");
        std::snprintf(
            sample_detail.data(),
            sample_detail.size(),
            "LAST VALID %.1f s AGO | ESP_OK",
            age_seconds
        );
        sample_color = palette::kStatusReady;
    } else if (has_success) {
        std::snprintf(
            sample_value.data(),
            sample_value.size(),
            "%s",
            battery.data_stale ? "STALE" : "ERROR"
        );
        std::snprintf(
            sample_detail.data(),
            sample_detail.size(),
            "LAST VALID %.1f s AGO | %s",
            age_seconds,
            esp_err_to_name(battery.last_error)
        );
        sample_color = battery.data_stale
            ? palette::kStatusWarning
            : palette::kStatusFault;
    } else {
        std::snprintf(sample_value.data(), sample_value.size(), "UNAVAILABLE");
        std::snprintf(
            sample_detail.data(),
            sample_detail.size(),
            "NO VALID SAMPLE | %s",
            component_reason(battery)
        );
    }

    std::array<char, 48> device_power_value{};
    std::array<char, 112> device_power_detail{};
    std::string_view device_power_color = palette::kStatusMuted;
    if (component_ready(snapshot.power)) {
        const char *state = "UNKNOWN";
        switch (snapshot.power.state) {
        case DeviceOperatingState::Active:
            state = "ACTIVE";
            break;
        case DeviceOperatingState::EnteringStandby:
            state = "ENTERING";
            break;
        case DeviceOperatingState::Standby:
            state = "STANDBY";
            break;
        case DeviceOperatingState::Waking:
            state = "WAKING";
            break;
        }
        const char *reason = "UNKNOWN";
        switch (snapshot.power.reason) {
        case DeviceStateReason::Startup:
            reason = "STARTUP";
            break;
        case DeviceStateReason::BootButton:
            reason = "BOOT BUTTON";
            break;
        case DeviceStateReason::PowerButton:
            reason = "PWR KEY";
            break;
        case DeviceStateReason::Touch:
            reason = "TOUCH";
            break;
        case DeviceStateReason::ImuMotion:
            reason = "IMU MOTION";
            break;
        }
        std::snprintf(
            device_power_value.data(),
            device_power_value.size(),
            "%s",
            state
        );
        std::snprintf(
            device_power_detail.data(),
            device_power_detail.size(),
            "%sREASON %s | AUDIO %s\nTRANSITION %lu",
            snapshot.power.state == DeviceOperatingState::EnteringStandby
                ? "STANDBY | "
                : "",
            reason,
            snapshot.power.audio_suspended ? "SUSPENDED" : "ACTIVE",
            static_cast<unsigned long>(snapshot.power.transition_sequence)
        );
        device_power_color =
            snapshot.power.state == DeviceOperatingState::Active
                ? palette::kStatusReady
                : palette::kStatusWarning;
    } else {
        std::snprintf(
            device_power_value.data(),
            device_power_value.size(),
            "UNAVAILABLE"
        );
        std::snprintf(
            device_power_detail.data(),
            device_power_detail.size(),
            "%s",
            component_reason(snapshot.power)
        );
        device_power_color = palette::kStatusFault;
    }

    std::array<char, 48> pwr_key_value{};
    std::array<char, 112> pwr_key_detail{};
    std::string_view pwr_key_color = palette::kStatusMuted;
    if (component_ready(snapshot.input.power_button)) {
        std::snprintf(
            pwr_key_value.data(),
            pwr_key_value.size(),
            "SHORT %lu",
            static_cast<unsigned long>(
                snapshot.input.power_short_press_count
            )
        );
        std::snprintf(
            pwr_key_detail.data(),
            pwr_key_detail.size(),
            "LONG %lu\nAXP2101 PKEY IRQ EVENT COUNTERS",
            static_cast<unsigned long>(
                snapshot.input.power_long_press_count
            )
        );
        pwr_key_color = palette::kStatusReady;
    } else {
        std::snprintf(
            pwr_key_value.data(),
            pwr_key_value.size(),
            "UNAVAILABLE"
        );
        std::snprintf(
            pwr_key_detail.data(),
            pwr_key_detail.size(),
            "%s",
            component_reason(snapshot.input.power_button)
        );
        pwr_key_color = palette::kStatusFault;
    }

    constexpr std::string_view kRoot =
        "/LUNASHELL_battery/surface/battery_scroller";
    std::vector<esp_brookesia::gui::BindingValueUpdate> updates;
    updates.reserve(54);
    const auto add_binding = [&updates](
        std::string path,
        std::string key,
        std::string_view value
    ) {
        updates.push_back({
            .absolute_path = std::move(path),
            .key = std::move(key),
            .value = std::string(value),
        });
    };
    const auto path = [&](std::string_view suffix) {
        return std::string(kRoot) + std::string(suffix);
    };
    const auto add_metric = [&](
        std::string_view id,
        std::string_view value,
        std::string_view detail,
        std::string_view color
    ) {
        const std::string base = path("/" + std::string(id));
        add_binding(base + "/accent", "color", color);
        add_binding(base + "/value", "text", value);
        add_binding(base + "/value", "color", color);
        add_binding(base + "/detail", "text", detail);
    };

    add_binding(path("/hero/terminal"), "health_color", level_color);
    add_binding(path("/hero/body"), "health_color", level_color);
    add_binding(path("/hero/body/state"), "text", state_text.data());
    add_binding(path("/hero/body/state"), "color", level_color);
    add_binding(path("/hero/body/percent"), "text", percent_text.data());
    add_binding(path("/hero/body/percent"), "color", level_color);
    add_binding(
        path("/hero/body/charge"),
        "hidden",
        charging ? "false" : "true"
    );
    add_binding(
        path("/hero/body/charge"),
        "color",
        palette::kStatusCharge
    );
    add_binding(
        path("/hero/body/level"),
        "hidden",
        level_valid ? "false" : "true"
    );
    add_binding(
        path("/hero/body/level"),
        "value",
        level_valid ? std::to_string(battery.level_percent) : "0"
    );
    add_binding(path("/hero/body/level"), "color", level_color);
    add_binding(path("/hero/health"), "text", health_text.data());
    add_binding(path("/hero/health"), "color", level_color);

    add_metric(
        "cell_voltage",
        cell_value.data(),
        cell_detail.data(),
        level_valid ? level_color : palette::kStatusMuted
    );
    add_metric(
        "usb_input",
        usb_value.data(),
        usb_detail.data(),
        usb_color
    );
    add_metric(
        "system_rail",
        system_value.data(),
        system_detail.data(),
        system_valid ? palette::kStatusReady : level_color
    );
    add_metric(
        "pmic_die",
        temperature_value.data(),
        temperature_detail.data(),
        temperature_valid ? palette::kStatusReady : level_color
    );
    add_metric(
        "charge_policy",
        policy_value.data(),
        policy_detail.data(),
        pmic_ready ? palette::kStatusReady : level_color
    );
    add_metric(
        "charger_phase",
        phase_value.data(),
        phase_detail.data(),
        phase_color
    );
    add_metric(
        "pmic_flags",
        flags_value.data(),
        flags_detail.data(),
        flags_color
    );
    add_metric(
        "device_power",
        device_power_value.data(),
        device_power_detail.data(),
        device_power_color
    );
    add_metric(
        "pwr_key",
        pwr_key_value.data(),
        pwr_key_detail.data(),
        pwr_key_color
    );
    add_metric(
        "sample",
        sample_value.data(),
        sample_detail.data(),
        sample_color
    );

    auto result = set_binding_values(context, updates);
    if (!result) {
        return std::unexpected(
            "Failed to refresh Battery telemetry: " + result.error()
        );
    }
    return {};
}

std::expected<void, std::string> App::ensure_log_buffers()
{
    if (log_source_buffer_ != nullptr && log_filtered_buffer_ != nullptr) {
        return {};
    }
    // Retain these bounded buffers until app stop so route changes do not
    // repeatedly split and rejoin the heap.
    release_log_buffers();

    std::unique_ptr<char[]> source(
        new (std::nothrow) char[kLogBufferBytes]
    );
    std::unique_ptr<char[]> filtered(
        new (std::nothrow) char[kLogBufferBytes]
    );
    if (source == nullptr || filtered == nullptr) {
        return std::unexpected("Failed to allocate LUNASHELL log buffers");
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
            "Failed to register LUNASHELL background image: " +
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
            ESP_LOGE(kTag, "Failed to roll back LUNASHELL background image registration");
        }
        return std::unexpected(
            "Failed to attach LUNASHELL background image: " + source_result.error()
        );
    }

    auto visibility_result = context.gui().set_binding_value(
        kBackgroundImagePath,
        "hidden",
        "false"
    );
    if (!visibility_result) {
        if (!context.gui().unregister_image(kBackgroundImageId)) {
            ESP_LOGE(kTag, "Failed to roll back LUNASHELL background image registration");
        }
        return std::unexpected(
            "Failed to show LUNASHELL background image: " + visibility_result.error()
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
        esp_err_t snapshot_result = LUNASHELL_logger_snapshot(
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
                    "/LUNASHELL_logs/surface/log_scroller/log_text",
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
                "/LUNASHELL_logs/surface/log_scroller/log_text",
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
                "/LUNASHELL_logs/surface/log_scroller/log_text",
                log_source_buffer_.get()
            ); !result) {
            return result;
        }
    }

    if (logs_follow_latest_ && !logs_paused_) {
        auto scroll_result = scroll_to(
            context,
            "/LUNASHELL_logs/surface/log_scroller",
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
    static_assert(
        kHardwareComponents.size() == kHardwareUiComponentCount,
        "Hardware UI cache must match the descriptor table"
    );
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }

    for (size_t index = 0; index < kHardwareComponents.size(); ++index) {
        std::array<char, 64> status{};
        format_hardware_status(status, snapshot, index);
        std::array<char, 128> status_path{};
        std::snprintf(
            status_path.data(),
            status_path.size(),
            "/LUNASHELL_hardware/surface/hardware_scroller/%.*s/status",
            static_cast<int>(kHardwareComponents[index].id.size()),
            kHardwareComponents[index].id.data()
        );
        if (auto result = set_cached_text(
                context,
                hardware_ui_cache_.statuses[index],
                status_path.data(),
                status.data()
            ); !result) {
            return result;
        }
        const char *color = hardware_status_color(snapshot, index);
        if (auto result = set_cached_binding_text(
                context,
                hardware_ui_cache_.colors[index],
                status_path.data(),
                "statusColor",
                color
            ); !result) {
            return result;
        }
    }

    std::array<char, 128> summary{};
    if (!snapshot_available(snapshot)) {
        std::snprintf(
            summary.data(),
            summary.size(),
            "SNAPSHOT UNAVAILABLE | TAP A ROW"
        );
    } else {
        std::snprintf(
            summary.data(),
            summary.size(),
            "SNAPSHOT #%lu %s | TAP A ROW",
            static_cast<unsigned long>(snapshot.sequence),
            snapshot.last_update_complete &&
                    snapshot.last_update_error == ESP_OK
                ? "READY"
                : "PARTIAL"
        );
    }
    return set_cached_text(
        context,
        hardware_ui_cache_.summary,
        "/LUNASHELL_hardware/surface/hardware_scroller/summary",
        summary.data()
    );
}

std::expected<void, std::string> App::show_hardware_details(
    AppContext &context,
    size_t component_index
)
{
    if (component_index >= kHardwareComponents.size()) {
        ESP_LOGE(
            kTag,
            "Hardware detail rejected: index=%u component-count=%u",
            static_cast<unsigned>(component_index),
            static_cast<unsigned>(kHardwareComponents.size())
        );
        return std::unexpected("Hardware detail index is out of range");
    }
    if (component_index == 0U) {
        return open_screen(
            context,
            Screen::Battery,
            "LUNASHELL.open.battery"
        );
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    if (component_index == 12U) {
        return open_screen(
            context,
            Screen::Tof,
            "LUNASHELL.open.tof"
        );
    }
#endif

    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    const HardwareDescriptor &descriptor =
        kHardwareComponents[component_index];
    const ComponentSnapshot &component =
        hardware_component(snapshot, component_index);
    std::array<char, 64> status{};
    format_hardware_status(status, snapshot, component_index);
    std::array<char, 768> details{};
    append_format(
        details,
        "CODE: %.*s\nNAME: %.*s\nABOUT: %.*s\n"
        "STATE: %s\nERROR: %s\n"
        "LAST ATTEMPT: %lld us\nLAST SUCCESS: %lld us\n",
        static_cast<int>(descriptor.part.size()),
        descriptor.part.data(),
        static_cast<int>(descriptor.name.size()),
        descriptor.name.data(),
        static_cast<int>(descriptor.description.size()),
        descriptor.description.data(),
        status.data(),
        esp_err_to_name(component.last_error),
        static_cast<long long>(component.last_attempt_at_us),
        static_cast<long long>(component.last_success_at_us)
    );

    switch (component_index) {
    case 0:
        if (component_ready(snapshot.battery)) {
            append_format(
                details,
                "BATTERY: %s\nUSB: %s",
                snapshot.battery.battery_present ? "PRESENT" : "ABSENT",
                snapshot.battery.usb_present ? "PRESENT" : "ABSENT"
            );
            if (snapshot.battery.battery_present &&
                snapshot.battery.level_percent >= 0 &&
                snapshot.battery.level_percent <= 100 &&
                snapshot.battery.battery_mv > 0) {
                append_format(
                    details,
                    "\nLEVEL: %d%%\nVOLTAGE: %d mV",
                    snapshot.battery.level_percent,
                    snapshot.battery.battery_mv
                );
            } else {
                append_format(details, "\nLEVEL: UNAVAILABLE");
            }
        }
        break;
    case 1:
        if (rtc_reading_valid(snapshot.rtc)) {
            append_format(
                details,
                "TIME: %04u-%02u-%02u %02u:%02u:%02u\nSOURCE: %s",
                static_cast<unsigned>(snapshot.rtc.year),
                static_cast<unsigned>(snapshot.rtc.month),
                static_cast<unsigned>(snapshot.rtc.day),
                static_cast<unsigned>(snapshot.rtc.hour),
                static_cast<unsigned>(snapshot.rtc.minute),
                static_cast<unsigned>(snapshot.rtc.second),
                snapshot.rtc.source
            );
        } else if (rtc_waiting_for_time_source(snapshot.rtc)) {
            append_format(
                details,
                "CLOCK: NOT INITIALIZED\n"
                "RECOVERY: WAITING FOR A VALID SNTP OR GPS UTC FIX"
            );
        }
        break;
    case 2:
        if (component_ready(snapshot.imu)) {
            append_format(
                details,
                "CALIBRATION: %s\nSAMPLES: %u\nTEMPERATURE: %.1f C",
                component_ready(snapshot.imu.calibration)
                    ? snapshot.imu.calibrated ? "READY" : "PENDING"
                    : component_state(snapshot.imu.calibration),
                static_cast<unsigned>(snapshot.imu.calibration_samples),
                static_cast<double>(snapshot.imu.temperature_c)
            );
        }
        break;
    case 3:
        if (component_ready(snapshot.gps)) {
            append_format(
                details,
                "FIX: %s\nSATELLITES: %u",
                snapshot.gps.has_fix ? "YES" : "NO",
                static_cast<unsigned>(snapshot.gps.satellites)
            );
            if (snapshot.gps.has_fix && gps_fix_valid(snapshot.gps)) {
                append_format(
                    details,
                    "\nHDOP: %.1f\nPOSITION: %.6f, %.6f\nALTITUDE: %.1f m",
                    static_cast<double>(snapshot.gps.hdop),
                    snapshot.gps.latitude_degrees,
                    snapshot.gps.longitude_degrees,
                    static_cast<double>(snapshot.gps.altitude_m)
                );
            } else {
                append_format(details, "\nHDOP: UNAVAILABLE");
            }
        }
        break;
    case 4:
        append_format(
            details,
            "STARTED: %s\nCONNECTED: %s\nSSID: %s\nIP: %s",
            snapshot.radios.wifi.started ? "YES" : "NO",
            snapshot.radios.wifi.connected ? "YES" : "NO",
            snapshot.radios.wifi.connected &&
                    snapshot.radios.wifi.ssid[0] != '\0'
                ? snapshot.radios.wifi.ssid
                : "UNAVAILABLE",
            snapshot.radios.wifi.ip_acquired &&
                    snapshot.radios.wifi.ip_address[0] != '\0'
                ? snapshot.radios.wifi.ip_address
                : "UNAVAILABLE"
        );
        break;
    case 5:
        append_format(
            details,
            "PROVISIONING: %s\nADVERTISING: %s\nCONNECTED: %s",
            snapshot.radios.ble.provisioning ? "YES" : "NO",
            snapshot.radios.ble.advertising ? "YES" : "NO",
            snapshot.radios.ble.connected ? "YES" : "NO"
        );
        break;
    case 6:
        if (component_ready(snapshot.ui.display)) {
            append_format(
                details,
                "BRIGHTNESS: %lu%%\nCONFIGURED: %lu%%",
                static_cast<unsigned long>(snapshot.ui.brightness_percent),
                static_cast<unsigned long>(
                    snapshot.ui.configured_brightness_percent
                )
            );
        }
        break;
    case 7:
        if (component_ready(snapshot.ui.touch)) {
            append_format(
                details,
                "ACTIVITY SEQUENCE: %lu\nLAST ACTIVITY: %lld us",
                static_cast<unsigned long>(
                    snapshot.ui.touch_activity_sequence
                ),
                static_cast<long long>(
                    snapshot.ui.touch_last_activity_at_us
                )
            );
        }
        break;
    case 8:
        append_format(details, "OUTPUT CODEC READY: %s",
                      component_ready(snapshot.audio.speaker) ? "YES" : "NO");
        break;
    case 9:
        if (component_ready(snapshot.audio.microphone)) {
            append_format(
                details,
                "PCM CHANNELS: %u\nSAMPLE RATE: %lu Hz",
                static_cast<unsigned>(snapshot.audio.channel_count),
                static_cast<unsigned long>(snapshot.audio.sample_rate_hz)
            );
        }
        break;
    case 10:
        append_format(
            details,
            "MOUNTED: %s\nCARD RESPONSE: %s\nFORMAT REQUIRED: %s\n"
            "FREE: %llu / %llu MB",
            snapshot.storage.mounted ? "YES" : "NO",
            snapshot.storage.card_response_verified
                ? "VERIFIED"
                : "UNCONFIRMED",
            snapshot.storage.format_required ? "YES" : "NO",
            static_cast<unsigned long long>(
                snapshot.storage.free_bytes / (1024ULL * 1024ULL)
            ),
            static_cast<unsigned long long>(
                snapshot.storage.filesystem_total_bytes /
                (1024ULL * 1024ULL)
            )
        );
        break;
    case 11:
        append_format(
            details,
            "CORES PRESENT: %u",
            static_cast<unsigned>(snapshot.processor.core_count)
        );
        for (size_t core = 0;
             core < std::min<size_t>(snapshot.processor.core_count, 2);
             ++core) {
            if (snapshot.processor.cores[core].utilization_available) {
                append_format(
                    details,
                    "\nCORE %u: %u%% BUSY",
                    static_cast<unsigned>(core),
                    static_cast<unsigned>(
                        snapshot.processor.cores[core]
                            .utilization_percent
                    )
                );
            } else {
                append_format(
                    details,
                    "\nCORE %u: MEASURING",
                    static_cast<unsigned>(core)
                );
            }
        }
        break;
    default:
        break;
    }

    hardware_detail_title_ =
        std::string(descriptor.name) + " | " + std::string(descriptor.part);
    hardware_detail_body_ = details.data();
    return open_screen(
        context,
        Screen::HardwareDetail,
        "LUNASHELL.open.hardware_detail"
    );
}

std::expected<void, std::string> App::refresh_hardware_detail(
    AppContext &context
)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }
    if (hardware_detail_title_.empty() || hardware_detail_body_.empty()) {
        return std::unexpected("Hardware detail view opened without detail data");
    }
    if (auto result = set_text(
            context,
            "/LUNASHELL_hardware_detail/surface/title",
            hardware_detail_title_
        ); !result) {
        return result;
    }
    return set_text(
        context,
        "/LUNASHELL_hardware_detail/surface/detail_scroller/body",
        hardware_detail_body_
    );
}

template <std::size_t Capacity>
std::expected<void, std::string> App::set_cached_text(
    AppContext &context,
    FixedTextCache<Capacity> &cache,
    std::string_view path,
    std::string_view text
)
{
    if (text.size() >= cache.value.size()) {
        return std::unexpected(
            "Rejected oversized cached LUNASHELL text for '" +
            std::string(path) + "'"
        );
    }
    if (cache.valid &&
        cache.length == text.size() &&
        std::memcmp(cache.value.data(), text.data(), text.size()) == 0) {
        return {};
    }
    if (auto result = set_text(context, path, text); !result) {
        return result;
    }
    std::memcpy(cache.value.data(), text.data(), text.size());
    cache.value[text.size()] = '\0';
    cache.length = text.size();
    cache.valid = true;
    return {};
}

template <std::size_t Capacity>
std::expected<void, std::string> App::set_cached_binding_text(
    AppContext &context,
    FixedTextCache<Capacity> &cache,
    std::string_view path,
    std::string_view binding,
    std::string_view value
)
{
    if (value.size() >= cache.value.size()) {
        return std::unexpected(
            "Rejected oversized cached LUNASHELL binding for '" +
            std::string(path) + "'"
        );
    }
    if (cache.valid &&
        cache.length == value.size() &&
        std::memcmp(cache.value.data(), value.data(), value.size()) == 0) {
        return {};
    }
    if (auto result = set_binding_text(context, path, binding, value);
        !result) {
        return result;
    }
    std::memcpy(cache.value.data(), value.data(), value.size());
    cache.value[value.size()] = '\0';
    cache.length = value.size();
    cache.valid = true;
    return {};
}

void App::invalidate_audio_screen_cache(Screen screen)
{
    switch (screen) {
    case Screen::Audio:
        audio_status_cache_.valid = false;
        audio_values_cache_.valid = false;
        audio_meter_cache_.level_known.fill(false);
        audio_meter_cache_.hidden_known.fill(false);
        audio_dual_command_available_.reset();
        break;
    case Screen::AudioFft:
        audio_fft_status_cache_.valid = false;
        audio_fft_heights_valid_ = false;
        audio_fft_hidden_known_ = false;
        break;
    case Screen::AudioDetail:
        audio_detail_status_cache_.valid = false;
        audio_detail_heights_valid_ = false;
        audio_detail_hidden_known_ = false;
        break;
    default:
        break;
    }
}

void App::invalidate_hardware_screen_cache()
{
    for (auto &cache : hardware_ui_cache_.statuses) {
        cache.valid = false;
    }
    for (auto &cache : hardware_ui_cache_.colors) {
        cache.valid = false;
    }
    hardware_ui_cache_.summary.valid = false;
}

void App::invalidate_imu_screen_cache()
{
    imu_ui_cache_.status.valid = false;
    imu_ui_cache_.calibration.valid = false;
    for (auto &cache : imu_ui_cache_.acceleration) {
        cache.valid = false;
    }
    for (auto &cache : imu_ui_cache_.angular_velocity) {
        cache.valid = false;
    }
    for (auto &cache : imu_ui_cache_.arc_opacity) {
        cache.valid = false;
    }
    imu_ui_cache_.arc_hidden_known.fill(false);
    imu_ui_cache_.temperature.valid = false;
}

void App::invalidate_gps_screen_cache()
{
    gps_ui_cache_.device_state.valid = false;
    gps_ui_cache_.fix_state.valid = false;
    gps_ui_cache_.latitude.valid = false;
    gps_ui_cache_.longitude.valid = false;
    gps_ui_cache_.satellites.valid = false;
    gps_ui_cache_.hdop.valid = false;
    gps_ui_cache_.altitude.valid = false;
    gps_ui_cache_.speed.valid = false;
    gps_ui_cache_.course.valid = false;
    gps_ui_cache_.utc.valid = false;
    gps_ui_cache_.quality.valid = false;
    for (auto &cache : gps_ui_cache_.metric_values) {
        cache.valid = false;
    }
    for (auto &cache : gps_ui_cache_.accent) {
        cache.valid = false;
    }
    gps_ui_cache_.satellite_signal_known = false;
    gps_ui_cache_.satellite_signal_hidden_known = false;
}

void App::invalidate_storage_screen_cache()
{
    storage_ui_cache_.status.valid = false;
    storage_ui_cache_.status_color.valid = false;
    storage_ui_cache_.detail.valid = false;
    storage_ui_cache_.mount.valid = false;
    storage_ui_cache_.total.valid = false;
    storage_ui_cache_.free.valid = false;
    storage_ui_cache_.usage.valid = false;
    storage_ui_cache_.format_label.valid = false;
    storage_ui_cache_.usage_percent_known = false;
    storage_ui_cache_.usage_hidden_known = false;
    storage_ui_cache_.format_disabled_known = false;
}

std::expected<void, std::string> App::refresh_audio(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    const bool dual_command_available =
        !performance_mode_enabled_ &&
        dual_audio_spectrum_available(snapshot);
    if (!audio_dual_command_available_.has_value() ||
        *audio_dual_command_available_ != dual_command_available) {
        if (auto result = configure_chrome(context); !result) {
            return result;
        }
        audio_dual_command_available_ = dual_command_available;
    }
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }
    std::array<char, 256> status{};
    std::array<char, 256> values{};
    int level0 = 0;
    int level1 = 0;
    bool level0_valid = false;
    bool level1_valid = false;

    if (performance_mode_enabled_) {
        std::snprintf(
            status.data(),
            status.size(),
            "MIC DISABLED | PERFORMANCE MODE"
        );
        std::snprintf(
            values.data(),
            values.size(),
            "AUDIO MEASUREMENTS DISABLED\nPERFORMANCE MODE"
        );
    } else if (!component_ready(snapshot.audio.microphone)) {
        std::snprintf(
            status.data(),
            status.size(),
            "MIC %s | %s",
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
            "MIC UNAVAILABLE | SNAPSHOT NOT READY"
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
            "MIC %s | MONITOR: %s",
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
            "MIC UNAVAILABLE | MONITOR OFF"
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
            "MIC STALE | WAITING FOR NEW SAMPLE"
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
            "MIC ERROR | INVALID PCM METADATA"
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
            "MIC ERROR | INVALID LEVEL SAMPLE"
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
            "MIC LIVE | PCM %u ch @ %.2f kHz",
            static_cast<unsigned>(snapshot.audio.channel_count),
            static_cast<double>(snapshot.audio.sample_rate_hz) / 1000.0
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
                "PCM16 | %.2f kHz | %u CHANNELS\n"
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
                "PCM16 | %.2f kHz | 1 CHANNEL\n"
                "%s\n"
                "CH1 UNAVAILABLE",
                static_cast<double>(snapshot.audio.sample_rate_hz) / 1000.0,
                peak0.data()
            );
        }
    }
    if (auto result = set_cached_text(
            context,
            audio_status_cache_,
            "/LUNASHELL_audio/surface/audio_status",
            status.data()
        ); !result) {
        return result;
    }

    static constexpr std::array<std::string_view, 2> kMeterPaths = {
        "/LUNASHELL_audio/surface/mic0_bar",
        "/LUNASHELL_audio/surface/mic1_bar",
    };
    const std::array<int32_t, 2> levels = {level0, level1};
    const std::array<bool, 2> level_valid = {
        level0_valid,
        level1_valid,
    };
    for (size_t channel = 0; channel < kMeterPaths.size(); ++channel) {
        if (!level_valid[channel] ||
            (audio_meter_cache_.level_known[channel] &&
             audio_meter_cache_.levels[channel] == levels[channel])) {
            continue;
        }
        if (auto result = set_value(
                context,
                kMeterPaths[channel],
                levels[channel]
            ); !result) {
            return result;
        }
        audio_meter_cache_.levels[channel] = levels[channel];
        audio_meter_cache_.level_known[channel] = true;
    }

    std::array<bool, 2> hidden_changed{};
    size_t hidden_change_count = 0;
    for (size_t channel = 0; channel < kMeterPaths.size(); ++channel) {
        const bool hidden = !level_valid[channel];
        hidden_changed[channel] =
            !audio_meter_cache_.hidden_known[channel] ||
            audio_meter_cache_.hidden[channel] != hidden;
        if (hidden_changed[channel]) {
            ++hidden_change_count;
        }
    }
    if (hidden_change_count > 0U) {
        std::vector<esp_brookesia::gui::BindingValueUpdate>
            visibility_updates;
        visibility_updates.reserve(hidden_change_count);
        for (size_t channel = 0; channel < kMeterPaths.size(); ++channel) {
            if (!hidden_changed[channel]) {
                continue;
            }
            visibility_updates.push_back({
                .absolute_path = std::string(kMeterPaths[channel]),
                .key = "hidden",
                .value = level_valid[channel] ? "false" : "true",
            });
        }
        if (auto result =
                set_binding_values(context, visibility_updates);
            !result) {
            return std::unexpected(
                "Failed to update LUNASHELL audio meter visibility: " +
                result.error()
            );
        }
        for (size_t channel = 0; channel < kMeterPaths.size(); ++channel) {
            if (!hidden_changed[channel]) {
                continue;
            }
            audio_meter_cache_.hidden[channel] = !level_valid[channel];
            audio_meter_cache_.hidden_known[channel] = true;
        }
    }

    return set_cached_text(
        context,
        audio_values_cache_,
        "/LUNASHELL_audio/surface/audio_values",
        values.data()
    );
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
        return std::unexpected("Failed to unregister LUNASHELL pairing QR image");
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
        "LUNASHELL.open.app_store_qr",
        mode
    );
}

std::expected<void, std::string> App::start_pairing_session(
    AppContext &context,
    NavigationMode mode
)
{
    esp_err_t result = LUNASHELL_wifi_init();
    if (result == ESP_OK &&
        !LUNASHELL_provisioning_initialized()) {
        result = LUNASHELL_provisioning_init();
    }
    if (result == ESP_OK) {
        result = LUNASHELL_provisioning_start();
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
            LUNASHELL_provisioning_qr_payload()
        ); !qr_result) {
        const esp_err_t stop_result = LUNASHELL_provisioning_stop();
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
        "LUNASHELL.open.pair_qr",
        mode
    );
}

std::expected<void, std::string> App::show_pairing_result(
    AppContext &context,
    std::string message,
    bool success
)
{
    // The result page no longer owns an active BLE provisioning session.
    if (LUNASHELL_provisioning_active()) {
        const esp_err_t stop_result = LUNASHELL_provisioning_stop();
        if (stop_result != ESP_OK) {
            return std::unexpected(
                "Failed to stop BLE provisioning before showing its result: " +
                std::string(esp_err_to_name(stop_result))
            );
        }
    }
    pairing_result_ = std::move(message);
    pairing_result_success_ = success;
    pairing_started_at_us_ = 0;
    return open_screen(
        context,
        Screen::PairResult,
        "LUNASHELL.open.pair_result",
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
            "LUNASHELL.open.pair_bluetooth",
            mode_for(Screen::PairBluetooth)
        );
    case Screen::PairBluetooth:
        return open_screen(
            context,
            Screen::PairApp,
            "LUNASHELL.open.pair_app",
            mode_for(Screen::PairApp)
        );
    case Screen::PairApp:
        return open_app_store_qr(
            context,
            mode_for(Screen::AppStoreQr)
        );
    case Screen::AppStoreQr: {
        const NavigationMode mode = mode_for(Screen::PairQr);
        if (!LUNASHELL_provisioning_active()) {
            return start_pairing_session(context, mode);
        }
        if (auto result = prepare_pairing_qr(
                context,
                LUNASHELL_provisioning_qr_payload()
            ); !result) {
            return result;
        }
        return open_screen(
            context,
            Screen::PairQr,
            "LUNASHELL.open.pair_qr",
            mode
        );
    }
    case Screen::PairResult:
        if (pairing_result_success_) {
            return open_screen(
                context,
                Screen::Settings,
                "LUNASHELL.open.settings",
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
        return "LUNASHELL.open.welcome";
    case Screen::Battery:
        return "LUNASHELL.open.battery";
    case Screen::Logs:
        return "LUNASHELL.open.logs";
    case Screen::Hardware:
        return "LUNASHELL.open.hardware";
    case Screen::HardwareDetail:
        return "LUNASHELL.open.hardware_detail";
    case Screen::Audio:
        return "LUNASHELL.open.audio";
    case Screen::AudioFft:
        return "LUNASHELL.open.audio_fft";
    case Screen::AudioDetail:
        return "LUNASHELL.open.audio_detail";
    case Screen::Imu:
        return "LUNASHELL.open.imu";
    case Screen::Gps:
        return "LUNASHELL.open.gps";
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    case Screen::Tof:
        return "LUNASHELL.open.tof";
    case Screen::TofSettings:
        return "LUNASHELL.open.tof_settings";
#endif
    case Screen::Settings:
        return "LUNASHELL.open.settings";
    case Screen::Storage:
        return "LUNASHELL.open.storage";
    case Screen::About:
        return "LUNASHELL.open.about";
    case Screen::PairIntro:
        return "LUNASHELL.open.pair_intro";
    case Screen::PairBluetooth:
        return "LUNASHELL.open.pair_bluetooth";
    case Screen::PairApp:
        return "LUNASHELL.open.pair_app";
    case Screen::AppStoreQr:
        return "LUNASHELL.open.app_store_qr";
    case Screen::PairQr:
        return "LUNASHELL.open.pair_qr";
    case Screen::PairResult:
        return "LUNASHELL.open.pair_result";
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
                LUNASHELL_provisioning_qr_payload()
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

    const char *root = "/LUNASHELL_pairing/surface";
    const char *title = nullptr;
    const char *step = nullptr;
    const char *message = nullptr;
    const char *detail = "";
    const char *accent_color = "#20dfc1";
    bool show_qr = false;
    int32_t progress_value = 0;
    bool progress_hidden = false;
    std::array<char, 192> dynamic_message{};

    switch (screen_) {
    case Screen::PairIntro:
        title = "PAIR DEVICE";
        step = "STEP 1 OF 5";
        message = "Connect LUNASHELL to your Wi-Fi network.";
        detail = "Bluetooth carries the Wi-Fi setup details.";
        progress_value = 1;
        break;
    case Screen::PairBluetooth:
        title = "BLUETOOTH";
        step = "STEP 2 OF 5";
        message = "Turn on Bluetooth on your phone.";
        detail = "Allow Nearby devices when prompted.";
        progress_value = 2;
        break;
    case Screen::PairApp:
        title = "GET THE APP";
        step = "STEP 3 OF 5";
        message = "Install ESP BLE Provisioning on your phone.";
        detail = "Next shows the official Google Play download.";
        progress_value = 3;
        break;
    case Screen::AppStoreQr:
        title = "INSTALL APP";
        step = "STEP 4 OF 5";
        message = "Scan to install ESP BLE Provisioning.";
        detail = "Then return here and press Next.";
        show_qr = true;
        progress_value = 4;
        break;
    case Screen::PairQr: {
        title = "SCAN DEVICE";
        step = "STEP 5 OF 5";
        show_qr = true;
        detail = LUNASHELL_provisioning_service_name();
        progress_value = 5;
        const LUNASHELL_provisioning_state_t state = LUNASHELL_provisioning_state();
        switch (state) {
        case LUNASHELL_PROVISIONING_STATE_IDLE:
        case LUNASHELL_PROVISIONING_STATE_STARTING:
            message = "Starting Bluetooth setup...";
            break;
        case LUNASHELL_PROVISIONING_STATE_WAITING_FOR_APP:
            message = "In the app, scan this device QR.";
            break;
        case LUNASHELL_PROVISIONING_STATE_CREDENTIALS_RECEIVED:
            message = "Wi-Fi details received. Preparing the connection...";
            break;
        case LUNASHELL_PROVISIONING_STATE_CONNECTING:
            message = "Wi-Fi details received. Connecting...";
            break;
        case LUNASHELL_PROVISIONING_STATE_SUCCESS: {
            std::array<char, 128> summary{};
            const esp_err_t summary_result =
                LUNASHELL_provisioning_connection_summary(
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
        case LUNASHELL_PROVISIONING_STATE_FAILED:
            switch (LUNASHELL_provisioning_failure_reason()) {
            case LUNASHELL_PROVISIONING_FAILURE_AUTHENTICATION:
                return show_pairing_result(
                    context,
                    "Wi-Fi password was rejected. Check it and try again.",
                    false
                );
            case LUNASHELL_PROVISIONING_FAILURE_ACCESS_POINT_NOT_FOUND:
                return show_pairing_result(
                    context,
                    "Wi-Fi network not found. Check its name or move closer.",
                    false
                );
            case LUNASHELL_PROVISIONING_FAILURE_NONE:
            case LUNASHELL_PROVISIONING_FAILURE_UNKNOWN:
                return show_pairing_result(
                    context,
                    "Wi-Fi connection failed without a reported reason.",
                    false
                );
            }
            return show_pairing_result(
                context,
                "Wi-Fi connection failed with an invalid reason.",
                false
            );
        case LUNASHELL_PROVISIONING_STATE_POISONED:
            return show_pairing_result(
                context,
                "Bluetooth setup stopped after an unrecoverable error.",
                false
            );
        case LUNASHELL_PROVISIONING_STATE_UNAVAILABLE:
            return show_pairing_result(
                context,
                "Wi-Fi setup is unavailable in this firmware build.",
                false
            );
        case LUNASHELL_PROVISIONING_STATE_STOPPING:
        case LUNASHELL_PROVISIONING_STATE_DEINITIALIZING:
            message = "Closing Bluetooth setup...";
            break;
        case LUNASHELL_PROVISIONING_STATE_STOPPED:
            return show_pairing_result(
                context,
                "Bluetooth setup ended before Wi-Fi connected.",
                false
            );
        }

        if (pairing_started_at_us_ > 0 &&
            esp_timer_get_time() - pairing_started_at_us_ >=
                kPairingTimeoutUs) {
            const esp_err_t stop_result = LUNASHELL_provisioning_stop();
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
                    "Setup timed out after 5 minutes. Try again."
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
            : "SETUP FAILED";
        step = pairing_result_success_ ? "CONNECTED" : "ACTION REQUIRED";
        message = pairing_result_.c_str();
        detail = pairing_result_success_
            ? "Setup complete. Next returns to Settings."
            : "Press Next to try again.";
        accent_color = pairing_result_success_ ? "#20dfc1" : "#ff3859";
        progress_hidden = true;
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
        .accent_color = accent_color,
        .show_qr = show_qr,
        .progress_value = progress_value,
        .progress_hidden = progress_hidden,
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
    if (auto result = set_binding_values(
            context,
            {
                {
                    .absolute_path = std::string(root) + "/step",
                    .key = "color",
                    .value = accent_color,
                },
                {
                    .absolute_path = std::string(root) + "/progress",
                    .key = "value",
                    .value = std::to_string(progress_value),
                },
                {
                    .absolute_path = std::string(root) + "/progress",
                    .key = "hidden",
                    .value = progress_hidden ? "true" : "false",
                },
            }
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
    static_assert(
        kAudioUiSpectrumBandCount == kAudioSpectrumBandCount,
        "LUNASHELL FFT cache must match the canonical snapshot band count"
    );
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }

    std::array<char, 160> status{};
    bool spectrum_valid = true;
    if (performance_mode_enabled_) {
        std::snprintf(
            status.data(),
            status.size(),
            "SPECTRUM DISABLED (PERFORMANCE MODE)"
        );
        spectrum_valid = false;
    } else if (!snapshot_available(snapshot)) {
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
    } else if (snapshot.audio.channel_count == 0U ||
               snapshot.audio.channel_count > 2U ||
               snapshot.audio.sample_rate_hz != 22050U ||
               snapshot.audio.spectrum_sequence == 0U) {
        std::snprintf(
            status.data(),
            status.size(),
            "SPECTRUM ERROR (INVALID PCM OR FFT METADATA)"
        );
        spectrum_valid = false;
    } else {
        for (size_t channel = 0;
             channel < snapshot.audio.channel_count && spectrum_valid;
             ++channel) {
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
                snapshot.audio.channel_count == 1U
                    ? "SPECTRUM READY | CH1 | SEQ %lu | 22.05 kHz"
                    : "SPECTRUM READY | MEAN 2CH | SEQ %lu | 22.05 kHz",
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

    if (auto result = set_cached_text(
            context,
            audio_fft_status_cache_,
            "/LUNASHELL_audio_fft/surface/spectrum_status",
            status.data()
        ); !result) {
        return result;
    }
    if (!spectrum_valid) {
        if (!audio_fft_hidden_known_ || !audio_fft_hidden_) {
            if (auto result = set_hidden(
                    context,
                    "/LUNASHELL_audio_fft/surface/bands",
                    true
                ); !result) {
                return result;
            }
            audio_fft_hidden_ = true;
            audio_fft_hidden_known_ = true;
        }
        return {};
    }

    std::array<int16_t, kAudioUiSpectrumBandCount> next_heights{};
    size_t height_change_count = 0;
    for (size_t band = 0; band < kAudioSpectrumBandCount; ++band) {
        const int32_t average =
            snapshot.audio.channel_count == 1U
                ? static_cast<int32_t>(
                      snapshot.audio.channel_spectrum[0][band]
                  )
                : (static_cast<int32_t>(
                       snapshot.audio.channel_spectrum[0][band]
                   ) +
                   static_cast<int32_t>(
                       snapshot.audio.channel_spectrum[1][band]
                   ) +
                   1) /
                      2;
        const int32_t height =
            std::max<int32_t>(1, (average * 94 + 99) / 100);
        next_heights[band] = static_cast<int16_t>(height);
        if (!audio_fft_heights_valid_ ||
            audio_fft_heights_[band] != next_heights[band]) {
            ++height_change_count;
        }
    }

    const bool visibility_changed =
        !audio_fft_hidden_known_ || audio_fft_hidden_;
    if (height_change_count == 0U && !visibility_changed) {
        return {};
    }

    std::vector<esp_brookesia::gui::BindingValueUpdate> spectrum_updates;
    spectrum_updates.reserve(
        height_change_count + (visibility_changed ? 1U : 0U)
    );
    for (size_t band = 0; band < kAudioSpectrumBandCount; ++band) {
        if (audio_fft_heights_valid_ &&
            audio_fft_heights_[band] == next_heights[band]) {
            continue;
        }
        std::array<char, 80> path{};
        std::snprintf(
            path.data(),
            path.size(),
            "/LUNASHELL_audio_fft/surface/bands/band%02u",
            static_cast<unsigned>(band)
        );
        spectrum_updates.push_back({
            .absolute_path = path.data(),
            .key = "height",
            .value = std::to_string(next_heights[band]) + "dp",
        });
    }
    if (visibility_changed) {
        spectrum_updates.push_back({
            .absolute_path = "/LUNASHELL_audio_fft/surface/bands",
            .key = "hidden",
            .value = "false",
        });
    }
    if (auto result = set_binding_values(context, spectrum_updates);
        !result) {
        return std::unexpected(
            "Failed to update LUNASHELL FFT band bindings: " + result.error()
        );
    }
    audio_fft_heights_ = next_heights;
    audio_fft_heights_valid_ = true;
    if (visibility_changed) {
        audio_fft_hidden_ = false;
        audio_fft_hidden_known_ = true;
    }
    return {};
}

std::expected<void, std::string> App::refresh_audio_detail(AppContext &context)
{
    static_assert(
        kAudioUiSpectrumBandCount == kAudioSpectrumBandCount,
        "LUNASHELL detail FFT cache must match the canonical snapshot band count"
    );
    const DeviceSnapshot snapshot =
        DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }

    std::array<char, 160> status{};
    bool spectrum_valid = !performance_mode_enabled_ &&
                          snapshot_available(snapshot) &&
                          component_ready(snapshot.audio.microphone) &&
                          component_ready(snapshot.diagnostics.monitor) &&
                          snapshot.diagnostics.monitor_active &&
                          snapshot.audio.microphone.last_success_at_us >
                              diagnostic_activation_sample_at_us_ &&
                          snapshot.audio.channel_count == 2U &&
                          snapshot.audio.sample_rate_hz == 22050U &&
                          snapshot.audio.spectrum_sequence != 0U;
    if (performance_mode_enabled_) {
        std::snprintf(status.data(), status.size(),
                      "SPECTRUM DISABLED (PERFORMANCE MODE)");
    } else if (!snapshot_available(snapshot)) {
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

    if (auto result = set_cached_text(
            context,
            audio_detail_status_cache_,
            "/LUNASHELL_audio_detail/surface/spectrum_status",
            status.data()
        );
        !result) {
        return result;
    }
    if (!spectrum_valid) {
        if (!audio_detail_hidden_known_ || !audio_detail_hidden_) {
            const std::vector<esp_brookesia::gui::BindingValueUpdate>
                visibility_updates = {
                    {
                        .absolute_path =
                            "/LUNASHELL_audio_detail/surface/mic0_bands",
                        .key = "hidden",
                        .value = "true",
                    },
                    {
                        .absolute_path =
                            "/LUNASHELL_audio_detail/surface/mic1_bands",
                        .key = "hidden",
                        .value = "true",
                    },
                };
            if (auto result =
                    set_binding_values(context, visibility_updates);
                !result) {
                return std::unexpected(
                    "Failed to hide LUNASHELL detail FFT bands: " +
                    result.error()
                );
            }
            audio_detail_hidden_ = true;
            audio_detail_hidden_known_ = true;
        }
        return {};
    }

    static constexpr std::array<std::string_view, kAudioSpectrumBandCount> kMic0Colors = {
        "#078dff", "#0b9dff", "#10afff", "#12c5ff", "#16d9ff", "#28e4f2", "#45d9ee", "#5abcfb",
        "#709dff", "#8780ff", "#a466ff", "#bd51ff", "#d342ff", "#e834f8", "#f62de7", "#ff35cf",
    };
    static constexpr std::array<std::string_view, kAudioSpectrumBandCount> kMic1Colors = {
        "#ff8700", "#ff9500", "#ffa400", "#ffb300", "#ffc000", "#ffcb19", "#ffd43b", "#f4dd4b",
        "#e6e45a", "#d4e660", "#bfe365", "#a8df6b", "#8fdb77", "#72d98b", "#4dd6a3", "#25d3bb",
    };
    if (!audio_detail_palette_applied_) {
        std::vector<esp_brookesia::gui::BindingValueUpdate> palette_updates;
        palette_updates.reserve(kAudioSpectrumBandCount * 2U);
        for (size_t band = 0; band < kAudioSpectrumBandCount; ++band) {
            for (size_t channel = 0; channel < 2U; ++channel) {
                std::array<char, 96> path{};
                const char *row =
                    channel == 0U ? "mic0_bands" : "mic1_bands";
                std::snprintf(
                    path.data(),
                    path.size(),
                    "/LUNASHELL_audio_detail/surface/%s/band%02u",
                    row,
                    static_cast<unsigned>(band)
                );
                const auto &colors =
                    channel == 0U ? kMic0Colors : kMic1Colors;
                palette_updates.push_back({
                    .absolute_path = path.data(),
                    .key = "color",
                    .value = std::string(colors[band]),
                });
            }
        }
        if (auto result = set_binding_values(context, palette_updates);
            !result) {
            return std::unexpected(
                "Failed to update LUNASHELL detail FFT palette: " +
                result.error()
            );
        }
        audio_detail_palette_applied_ = true;
    }
    std::array<int16_t, kAudioUiSpectrumBandCount * 2U> next_heights{};
    size_t height_change_count = 0;
    for (size_t band = 0; band < kAudioSpectrumBandCount; ++band) {
        for (size_t channel = 0; channel < 2U; ++channel) {
            const int32_t height = std::max<int32_t>(
                1,
                (static_cast<int32_t>(
                    snapshot.audio.channel_spectrum[channel][band]
                ) * 64 + 99) / 100
            );
            const size_t index = band * 2U + channel;
            next_heights[index] = static_cast<int16_t>(height);
            if (!audio_detail_heights_valid_ ||
                audio_detail_heights_[index] != next_heights[index]) {
                ++height_change_count;
            }
        }
    }

    const bool visibility_changed =
        !audio_detail_hidden_known_ || audio_detail_hidden_;
    if (height_change_count == 0U && !visibility_changed) {
        return {};
    }

    std::vector<esp_brookesia::gui::BindingValueUpdate> spectrum_updates;
    spectrum_updates.reserve(
        height_change_count + (visibility_changed ? 2U : 0U)
    );
    for (size_t band = 0; band < kAudioSpectrumBandCount; ++band) {
        for (size_t channel = 0; channel < 2U; ++channel) {
            const size_t index = band * 2U + channel;
            if (audio_detail_heights_valid_ &&
                audio_detail_heights_[index] == next_heights[index]) {
                continue;
            }
            std::array<char, 96> path{};
            const char *row =
                channel == 0U ? "mic0_bands" : "mic1_bands";
            std::snprintf(
                path.data(),
                path.size(),
                "/LUNASHELL_audio_detail/surface/%s/band%02u",
                row,
                static_cast<unsigned>(band)
            );
            spectrum_updates.push_back({
                .absolute_path = path.data(),
                .key = "height",
                .value = std::to_string(next_heights[index]) + "dp",
            });
        }
    }
    if (visibility_changed) {
        spectrum_updates.push_back({
            .absolute_path = "/LUNASHELL_audio_detail/surface/mic0_bands",
            .key = "hidden",
            .value = "false",
        });
        spectrum_updates.push_back({
            .absolute_path = "/LUNASHELL_audio_detail/surface/mic1_bands",
            .key = "hidden",
            .value = "false",
        });
    }
    if (auto result = set_binding_values(context, spectrum_updates);
        !result) {
        return std::unexpected(
            "Failed to update LUNASHELL detail FFT band bindings: " + result.error()
        );
    }
    audio_detail_heights_ = next_heights;
    audio_detail_heights_valid_ = true;
    if (visibility_changed) {
        audio_detail_hidden_ = false;
        audio_detail_hidden_known_ = true;
    }
    return {};
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
    if (performance_mode_enabled_) {
        std::snprintf(
            status.data(),
            status.size(),
            "QMI8658 DISABLED (PERFORMANCE MODE)"
        );
    } else if (!snapshot_available(snapshot)) {
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

    if (performance_mode_enabled_) {
        std::snprintf(
            calibration.data(),
            calibration.size(),
            "CALIB DISABLED | PERFORMANCE MODE"
        );
    } else if (!snapshot_available(snapshot)) {
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

    if (auto result = set_cached_text(
            context,
            imu_ui_cache_.status,
            "/LUNASHELL_imu/surface/imu_status",
            status.data()
        ); !result) {
        return result;
    }
    if (auto result = set_cached_text(
            context,
            imu_ui_cache_.calibration,
            "/LUNASHELL_imu/surface/calibration",
            calibration.data()
        ); !result) {
        return result;
    }

    static constexpr std::array<const char *, 3> kAccelPaths = {
        "/LUNASHELL_imu/surface/accel_x",
        "/LUNASHELL_imu/surface/accel_y",
        "/LUNASHELL_imu/surface/accel_z",
    };
    static constexpr std::array<const char *, 3> kGyroPaths = {
        "/LUNASHELL_imu/surface/gyro_x",
        "/LUNASHELL_imu/surface/gyro_y",
        "/LUNASHELL_imu/surface/gyro_z",
    };
    static constexpr std::array<const char *, 3> kGyroArcPaths = {
        "/LUNASHELL_imu/surface/gyro_x_arc",
        "/LUNASHELL_imu/surface/gyro_y_arc",
        "/LUNASHELL_imu/surface/gyro_z_arc",
    };

    if (!sample_valid) {
        for (size_t axis = 0; axis < kAccelPaths.size(); ++axis) {
            if (auto result = set_cached_text(
                    context,
                    imu_ui_cache_.acceleration[axis],
                    kAccelPaths[axis],
                    "--"
                );
                !result) {
                return result;
            }
            if (auto result = set_cached_text(
                    context,
                    imu_ui_cache_.angular_velocity[axis],
                    kGyroPaths[axis],
                    "--"
                );
                !result) {
                return result;
            }
            if (!imu_ui_cache_.arc_hidden_known[axis] ||
                !imu_ui_cache_.arc_hidden[axis]) {
                if (auto result = set_hidden(
                        context,
                        kGyroArcPaths[axis],
                        true
                    ); !result) {
                    return result;
                }
                imu_ui_cache_.arc_hidden[axis] = true;
                imu_ui_cache_.arc_hidden_known[axis] = true;
            }
        }
        return set_cached_text(
            context,
            imu_ui_cache_.temperature,
            "/LUNASHELL_imu/surface/temperature",
            "-- C"
        );
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
        if (auto result = set_cached_text(
                context,
                imu_ui_cache_.acceleration[axis],
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
        if (auto result = set_cached_text(
                context,
                imu_ui_cache_.angular_velocity[axis],
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
        if (auto result = set_cached_binding_text(
                context,
                imu_ui_cache_.arc_opacity[axis],
                kGyroArcPaths[axis],
                "opacity",
                opacity.data()
            ); !result) {
            return result;
        }
        if (!imu_ui_cache_.arc_hidden_known[axis] ||
            imu_ui_cache_.arc_hidden[axis]) {
            if (auto result = set_hidden(
                    context,
                    kGyroArcPaths[axis],
                    false
                ); !result) {
                return result;
            }
            imu_ui_cache_.arc_hidden[axis] = false;
            imu_ui_cache_.arc_hidden_known[axis] = true;
        }
    }

    std::snprintf(
        value.data(),
        value.size(),
        "%.1f C",
        snapshot.imu.temperature_c
    );
    return set_cached_text(
        context,
        imu_ui_cache_.temperature,
        "/LUNASHELL_imu/surface/temperature",
        value.data()
    );
}

// Render only snapshot-owned GNSS data and hide every numeric fix field when
// the receiver sample is unavailable, stale, or internally inconsistent.
std::expected<void, std::string> App::refresh_gps(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }

    const GpsSnapshot &gps = snapshot.gps;
    const bool receiver_ready =
        snapshot_available(snapshot) && component_ready(gps);
    const bool valid_fix = receiver_ready && gps_fix_valid(gps);

    std::string_view accent = palette::kStatusMuted;
    if (receiver_ready) {
        accent = valid_fix
            ? palette::kStatusReady
            : gps.has_fix
                ? palette::kStatusFault
                : palette::kStatusWarning;
    } else if (snapshot_available(snapshot) &&
               (gps.last_error != ESP_OK ||
                gps.health == DeviceHealth::Degraded)) {
        accent = palette::kStatusFault;
    } else if (gps.data_stale) {
        accent = palette::kStatusWarning;
    }

    std::array<char, 160> device_state{};
    std::array<char, 64> fix_state{};
    if (!snapshot_available(snapshot)) {
        std::snprintf(
            device_state.data(),
            device_state.size(),
            "LC76G UNAVAILABLE | SNAPSHOT NOT READY"
        );
        std::snprintf(fix_state.data(), fix_state.size(), "NO DATA");
    } else if (!receiver_ready) {
        std::snprintf(
            device_state.data(),
            device_state.size(),
            "LC76G %s | %s",
            component_state(gps),
            component_reason(gps)
        );
        std::snprintf(
            fix_state.data(),
            fix_state.size(),
            "%s",
            component_state(gps)
        );
    } else if (!gps.has_fix) {
        std::snprintf(
            device_state.data(),
            device_state.size(),
            "LC76G READY | RECEIVER ONLINE"
        );
        std::snprintf(fix_state.data(), fix_state.size(), "NO FIX");
    } else if (!valid_fix) {
        std::snprintf(
            device_state.data(),
            device_state.size(),
            "LC76G ERROR | INVALID FIX REJECTED"
        );
        std::snprintf(fix_state.data(), fix_state.size(), "FIX REJECTED");
    } else {
        std::snprintf(
            device_state.data(),
            device_state.size(),
            "LC76G READY | SAMPLE %lld us",
            static_cast<long long>(gps.last_success_at_us)
        );
        std::snprintf(fix_state.data(), fix_state.size(), "FIX ACTIVE");
    }

    const char *unavailable_value = receiver_ready
        ? gps.has_fix ? "INVALID" : "NO FIX"
        : "UNAVAILABLE";
    std::array<char, 48> latitude{};
    std::array<char, 48> longitude{};
    std::array<char, 24> satellites{};
    std::array<char, 24> hdop{};
    std::array<char, 32> altitude{};
    std::array<char, 32> speed{};
    std::array<char, 32> course{};
    std::array<char, 64> utc{};
    std::array<char, 64> quality{};

    if (valid_fix) {
        std::snprintf(
            latitude.data(),
            latitude.size(),
            "%.6f %c",
            std::fabs(gps.latitude_degrees),
            gps.latitude_degrees >= 0.0 ? 'N' : 'S'
        );
        std::snprintf(
            longitude.data(),
            longitude.size(),
            "%.6f %c",
            std::fabs(gps.longitude_degrees),
            gps.longitude_degrees >= 0.0 ? 'E' : 'W'
        );
        std::snprintf(
            hdop.data(),
            hdop.size(),
            "%.1f",
            static_cast<double>(gps.hdop)
        );
        std::snprintf(
            altitude.data(),
            altitude.size(),
            "%.1f m",
            static_cast<double>(gps.altitude_m)
        );
        std::snprintf(
            speed.data(),
            speed.size(),
            "%.2f m/s",
            static_cast<double>(gps.speed_mps)
        );
        std::snprintf(
            course.data(),
            course.size(),
            "%.1f deg",
            static_cast<double>(gps.course_degrees)
        );
        if (gps.utc_date[0] != '\0' && gps.utc_time[0] != '\0') {
            std::snprintf(
                utc.data(),
                utc.size(),
                "UTC %s | %s",
                gps.utc_date,
                gps.utc_time
            );
        } else {
            std::snprintf(
                utc.data(),
                utc.size(),
                "UTC VALID | VALUE UNAVAILABLE"
            );
        }
    } else {
        std::snprintf(latitude.data(), latitude.size(), "%s", unavailable_value);
        std::snprintf(longitude.data(), longitude.size(), "%s", unavailable_value);
        std::snprintf(hdop.data(), hdop.size(), "%s", unavailable_value);
        std::snprintf(altitude.data(), altitude.size(), "%s", unavailable_value);
        std::snprintf(speed.data(), speed.size(), "%s", unavailable_value);
        std::snprintf(course.data(), course.size(), "%s", unavailable_value);
        std::snprintf(utc.data(), utc.size(), "UTC %s", unavailable_value);
    }

    if (receiver_ready) {
        std::snprintf(
            satellites.data(),
            satellites.size(),
            "%u",
            static_cast<unsigned>(gps.satellites)
        );
        std::snprintf(
            quality.data(),
            quality.size(),
            "QUALITY %u | NMEA QUEUE %lu B",
            static_cast<unsigned>(gps.fix_quality),
            static_cast<unsigned long>(gps.pending_nmea_bytes)
        );
    } else {
        std::snprintf(
            satellites.data(),
            satellites.size(),
            "UNAVAILABLE"
        );
        std::snprintf(
            quality.data(),
            quality.size(),
            "QUALITY UNAVAILABLE"
        );
    }

    const auto update_text = [this, &context](
        auto &cache,
        std::string_view path,
        std::string_view text
    ) -> std::expected<void, std::string> {
        return set_cached_text(context, cache, path, text);
    };
    if (auto result = update_text(
            gps_ui_cache_.device_state,
            "/LUNASHELL_gps/surface/device_state",
            device_state.data()
        ); !result) {
        return result;
    }
    if (auto result = update_text(
            gps_ui_cache_.fix_state,
            "/LUNASHELL_gps/surface/fix_state",
            fix_state.data()
        ); !result) {
        return result;
    }
    if (auto result = update_text(
            gps_ui_cache_.latitude,
            "/LUNASHELL_gps/surface/position_card/latitude",
            latitude.data()
        ); !result) {
        return result;
    }
    if (auto result = update_text(
            gps_ui_cache_.longitude,
            "/LUNASHELL_gps/surface/position_card/longitude",
            longitude.data()
        ); !result) {
        return result;
    }
    if (auto result = update_text(
            gps_ui_cache_.satellites,
            "/LUNASHELL_gps/surface/satellites",
            satellites.data()
        ); !result) {
        return result;
    }
    if (auto result = update_text(
            gps_ui_cache_.hdop,
            "/LUNASHELL_gps/surface/hdop",
            hdop.data()
        ); !result) {
        return result;
    }
    if (auto result = update_text(
            gps_ui_cache_.altitude,
            "/LUNASHELL_gps/surface/altitude",
            altitude.data()
        ); !result) {
        return result;
    }
    if (auto result = update_text(
            gps_ui_cache_.speed,
            "/LUNASHELL_gps/surface/speed",
            speed.data()
        ); !result) {
        return result;
    }
    if (auto result = update_text(
            gps_ui_cache_.course,
            "/LUNASHELL_gps/surface/course",
            course.data()
        ); !result) {
        return result;
    }
    if (auto result = update_text(
            gps_ui_cache_.utc,
            "/LUNASHELL_gps/surface/utc",
            utc.data()
        ); !result) {
        return result;
    }
    if (auto result = update_text(
            gps_ui_cache_.quality,
            "/LUNASHELL_gps/surface/quality",
            quality.data()
        ); !result) {
        return result;
    }

    static constexpr std::array<std::string_view, 7> kMetricPaths = {
        "/LUNASHELL_gps/surface/satellites_card/value",
        "/LUNASHELL_gps/surface/hdop_card/value",
        "/LUNASHELL_gps/surface/altitude_card/value",
        "/LUNASHELL_gps/surface/speed_card/value",
        "/LUNASHELL_gps/surface/course_card/value",
        "/LUNASHELL_gps/surface/utc_card/detail",
        "/LUNASHELL_gps/surface/quality_card/detail",
    };
    const std::array<std::string_view, 7> metric_values = {
        satellites.data(),
        hdop.data(),
        altitude.data(),
        speed.data(),
        course.data(),
        utc.data(),
        quality.data(),
    };
    for (size_t index = 0; index < kMetricPaths.size(); ++index) {
        if (auto result = update_text(
                gps_ui_cache_.metric_values[index],
                kMetricPaths[index],
                metric_values[index]
            ); !result) {
            return result;
        }
    }

    static constexpr std::array<std::string_view, 5> kAccentPaths = {
        "/LUNASHELL_gps/surface/device_state",
        "/LUNASHELL_gps/surface/fix_state",
        "/LUNASHELL_gps/surface/position_card",
        "/LUNASHELL_gps/surface/position_card/radar_fix",
        "/LUNASHELL_gps/surface/satellite_signal",
    };
    for (size_t index = 0; index < kAccentPaths.size(); ++index) {
        if (auto result = set_cached_binding_text(
                context,
                gps_ui_cache_.accent[index],
                kAccentPaths[index],
                "accent",
                accent
            ); !result) {
            return result;
        }
    }

    const bool signal_hidden = !receiver_ready;
    if (!gps_ui_cache_.satellite_signal_hidden_known ||
        gps_ui_cache_.satellite_signal_hidden != signal_hidden) {
        if (auto result = set_hidden(
                context,
                "/LUNASHELL_gps/surface/satellite_signal",
                signal_hidden
            ); !result) {
            return result;
        }
        gps_ui_cache_.satellite_signal_hidden = signal_hidden;
        gps_ui_cache_.satellite_signal_hidden_known = true;
    }
    if (receiver_ready) {
        const int32_t satellite_signal = std::min<int32_t>(
            static_cast<int32_t>(gps.satellites),
            32
        );
        if (!gps_ui_cache_.satellite_signal_known ||
            gps_ui_cache_.satellite_signal != satellite_signal) {
            if (auto result = set_value(
                    context,
                    "/LUNASHELL_gps/surface/satellite_signal",
                    satellite_signal
                ); !result) {
                return result;
            }
            gps_ui_cache_.satellite_signal = satellite_signal;
            gps_ui_cache_.satellite_signal_known = true;
        }
    }
    return {};
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
std::expected<void, std::string> App::refresh_tof(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }

    const bool sensor_1_ready =
        component_ready(snapshot.distance.sensors[0]);
    const bool sensor_2_ready =
        component_ready(snapshot.distance.sensors[1]);
    const bool sensor_1_value_valid =
        sensor_1_ready &&
        snapshot.distance.sensors[0].filtered_distance_mm > 0U &&
        snapshot.distance.sensors[0].window_sample_count > 0U &&
        snapshot.distance.sensors[0].inlier_count > 0U &&
        snapshot.distance.sensors[0].inlier_count <=
            snapshot.distance.sensors[0].window_sample_count;
    const bool sensor_2_value_valid =
        sensor_2_ready &&
        snapshot.distance.sensors[1].filtered_distance_mm > 0U &&
        snapshot.distance.sensors[1].window_sample_count > 0U &&
        snapshot.distance.sensors[1].inlier_count > 0U &&
        snapshot.distance.sensors[1].inlier_count <=
            snapshot.distance.sensors[1].window_sample_count;
    const bool distance_valid =
        snapshot_available(snapshot) &&
        component_ready(snapshot.distance) &&
        snapshot.distance.vertical_distance_mm > 0U &&
        snapshot.distance.slant_distance_mm > 0U &&
        snapshot.distance.samples_used > 0U &&
        snapshot.distance.confidence_percent <= 100U &&
        snapshot.distance.tilt_compensated &&
        std::isfinite(snapshot.distance.tilt_degrees);
    const bool calibration_running =
        snapshot.distance.calibration.state ==
        LUNASHELL::DistanceCalibrationState::Running;
    const bool calibration_running_valid =
        calibration_running &&
        component_ready(snapshot.distance.calibration) &&
        snapshot.distance.calibration.progress_percent <= 100U;

    const char *status_color = "#ff8a66";
    std::array<char, 176> status{};
    if (!snapshot_available(snapshot)) {
        std::snprintf(
            status.data(),
            status.size(),
            "DUAL TOF UNAVAILABLE (SNAPSHOT NOT READY)"
        );
    } else if (calibration_running_valid) {
        status_color = "#f0b44d";
        std::snprintf(
            status.data(),
            status.size(),
            "DUAL TOF CALIBRATING | %u%%",
            static_cast<unsigned>(
                snapshot.distance.calibration.progress_percent
            )
        );
    } else if (calibration_running) {
        std::snprintf(
            status.data(),
            status.size(),
            "CALIBRATION %s (%s)",
            component_state(snapshot.distance.calibration),
            component_reason(snapshot.distance.calibration)
        );
    } else if (distance_valid) {
        status_color = "#20dfc1";
        std::snprintf(
            status.data(),
            status.size(),
            "DUAL TOF READY | t=%lld us",
            static_cast<long long>(snapshot.distance.last_success_at_us)
        );
    } else if (!sensor_1_ready) {
        std::snprintf(
            status.data(),
            status.size(),
            "TOF 1 %s (%s)",
            component_state(snapshot.distance.sensors[0]),
            component_reason(snapshot.distance.sensors[0])
        );
    } else if (!sensor_2_ready) {
        std::snprintf(
            status.data(),
            status.size(),
            "TOF 2 %s (%s)",
            component_state(snapshot.distance.sensors[1]),
            component_reason(snapshot.distance.sensors[1])
        );
    } else {
        std::snprintf(
            status.data(),
            status.size(),
            "DISTANCE %s (%s)",
            component_state(snapshot.distance),
            component_reason(snapshot.distance)
        );
    }

    std::array<char, 40> distance{};
    std::array<char, 40> sensor_1{};
    std::array<char, 40> sensor_2{};
    std::array<char, 96> spread{};
    std::array<char, 96> tilt{};
    std::array<char, 96> confidence{};
    if (distance_valid) {
        std::snprintf(
            distance.data(),
            distance.size(),
            "%u mm",
            static_cast<unsigned>(snapshot.distance.vertical_distance_mm)
        );
        std::snprintf(
            spread.data(),
            spread.size(),
            "SPREAD %u mm | SLANT %u mm",
            static_cast<unsigned>(snapshot.distance.sensor_spread_mm),
            static_cast<unsigned>(snapshot.distance.slant_distance_mm)
        );
        std::snprintf(
            tilt.data(),
            tilt.size(),
            "TILT %.1f deg | COMPENSATED",
            static_cast<double>(snapshot.distance.tilt_degrees)
        );
        std::snprintf(
            confidence.data(),
            confidence.size(),
            "CONFIDENCE %u%% | SAMPLES %u",
            static_cast<unsigned>(snapshot.distance.confidence_percent),
            static_cast<unsigned>(snapshot.distance.samples_used)
        );
    } else {
        std::snprintf(distance.data(), distance.size(), "-- mm");
        std::snprintf(spread.data(), spread.size(), "SPREAD -- | SLANT --");
        std::snprintf(tilt.data(), tilt.size(), "TILT UNAVAILABLE");
        std::snprintf(
            confidence.data(),
            confidence.size(),
            "CONFIDENCE -- | SAMPLES --"
        );
    }
    if (sensor_1_value_valid) {
        std::snprintf(
            sensor_1.data(),
            sensor_1.size(),
            "%u mm",
            static_cast<unsigned>(
                snapshot.distance.sensors[0].filtered_distance_mm
            )
        );
    } else {
        std::snprintf(sensor_1.data(), sensor_1.size(), "-- mm");
    }
    if (sensor_2_value_valid) {
        std::snprintf(
            sensor_2.data(),
            sensor_2.size(),
            "%u mm",
            static_cast<unsigned>(
                snapshot.distance.sensors[1].filtered_distance_mm
            )
        );
    } else {
        std::snprintf(sensor_2.data(), sensor_2.size(), "-- mm");
    }

    const char *calibration_color = "#ff8a66";
    std::array<char, 176> calibration{};
    if (!snapshot_available(snapshot)) {
        std::snprintf(
            calibration.data(),
            calibration.size(),
            "CALIBRATION UNAVAILABLE"
        );
    } else {
        const auto &state = snapshot.distance.calibration;
        switch (state.state) {
        case LUNASHELL::DistanceCalibrationState::Required:
            calibration_color = "#f0b44d";
            std::snprintf(
                calibration.data(),
                calibration.size(),
                "CALIBRATION REQUIRED (%s)",
                component_reason(state)
            );
            break;
        case LUNASHELL::DistanceCalibrationState::Ready:
            if (component_ready(state)) {
                calibration_color = "#20dfc1";
                std::snprintf(
                    calibration.data(),
                    calibration.size(),
                    "CALIBRATION READY | OFFSETS %ld / %ld mm",
                    static_cast<long>(state.offsets_mm[0]),
                    static_cast<long>(state.offsets_mm[1])
                );
            } else {
                calibration_color = "#f0b44d";
                std::snprintf(
                    calibration.data(),
                    calibration.size(),
                    "CALIBRATION READY | LAST ERROR %s",
                    component_reason(state)
                );
            }
            break;
        case LUNASHELL::DistanceCalibrationState::Running:
            if (component_ready(state) &&
                state.progress_percent <= 100U) {
                calibration_color = "#f0b44d";
                std::snprintf(
                    calibration.data(),
                    calibration.size(),
                    "CALIBRATING %u%% | S1 %u S2 %u",
                    static_cast<unsigned>(state.progress_percent),
                    static_cast<unsigned>(state.accepted_samples[0]),
                    static_cast<unsigned>(state.accepted_samples[1])
                );
            } else {
                std::snprintf(
                    calibration.data(),
                    calibration.size(),
                    "CALIBRATION ERROR (%s)",
                    component_reason(state)
                );
            }
            break;
        case LUNASHELL::DistanceCalibrationState::Succeeded:
            if (component_ready(state)) {
                calibration_color = "#20dfc1";
                std::snprintf(
                    calibration.data(),
                    calibration.size(),
                    "CALIBRATION SUCCEEDED | OFFSETS %ld / %ld mm",
                    static_cast<long>(state.offsets_mm[0]),
                    static_cast<long>(state.offsets_mm[1])
                );
            } else {
                calibration_color = "#f0b44d";
                std::snprintf(
                    calibration.data(),
                    calibration.size(),
                    "CALIBRATION SUCCEEDED (%s)",
                    component_reason(state)
                );
            }
            break;
        case LUNASHELL::DistanceCalibrationState::Failed:
            std::snprintf(
                calibration.data(),
                calibration.size(),
                "CALIBRATION FAILED (%s)",
                component_reason(state)
            );
            break;
        }
    }

    const std::array<std::pair<const char *, const char *>, 8> updates = {{
        {"/LUNASHELL_tof/surface/status", status.data()},
        {"/LUNASHELL_tof/surface/distance", distance.data()},
        {
            "/LUNASHELL_tof/surface/sensor_1_panel/sensor_1",
            sensor_1.data(),
        },
        {
            "/LUNASHELL_tof/surface/sensor_2_panel/sensor_2",
            sensor_2.data(),
        },
        {"/LUNASHELL_tof/surface/spread", spread.data()},
        {"/LUNASHELL_tof/surface/tilt", tilt.data()},
        {"/LUNASHELL_tof/surface/confidence", confidence.data()},
        {"/LUNASHELL_tof/surface/calibration", calibration.data()},
    }};
    for (const auto &[path, text] : updates) {
        if (auto result = set_text(context, path, text); !result) {
            return result;
        }
    }
    if (auto result = set_binding_values(
            context,
            {
                {
                    .absolute_path = "/LUNASHELL_tof/surface/status",
                    .key = "color",
                    .value = status_color,
                },
                {
                    .absolute_path = "/LUNASHELL_tof/surface/calibration",
                    .key = "color",
                    .value = calibration_color,
                },
            }
        ); !result) {
        return std::unexpected(
            "Failed to refresh distance status colors: " +
            result.error()
        );
    }
    return {};
}

std::expected<void, std::string> App::refresh_tof_settings(
    AppContext &context
)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }

    const auto sample_count = std::find(
        kTofSampleCounts.begin(),
        kTofSampleCounts.end(),
        snapshot.distance.settings.sample_count
    );
    const auto sample_interval = std::find(
        kTofSampleIntervalsMs.begin(),
        kTofSampleIntervalsMs.end(),
        snapshot.distance.settings.sample_interval_ms
    );
    const bool reference_valid =
        snapshot.distance.settings.reference_distance_mm >=
            kTofReferenceMinimumMm &&
        snapshot.distance.settings.reference_distance_mm <=
            kTofReferenceMaximumMm &&
        ((snapshot.distance.settings.reference_distance_mm -
          kTofReferenceMinimumMm) %
         kTofReferenceStepMm) == 0;
    const bool settings_ready =
        snapshot_available(snapshot) &&
        component_ready(snapshot.distance.settings);
    const bool settings_valid =
        settings_ready &&
        sample_count != kTofSampleCounts.end() &&
        sample_interval != kTofSampleIntervalsMs.end() &&
        reference_valid;
    const bool calibration_running =
        snapshot.distance.calibration.state ==
        LUNASHELL::DistanceCalibrationState::Running;
    const bool calibration_running_valid =
        calibration_running &&
        component_ready(snapshot.distance.calibration) &&
        snapshot.distance.calibration.progress_percent <= 100U;

    if (!settings_valid) {
        tof_reference_editing_ = false;
        tof_reference_pending_mm_.reset();
    } else if (calibration_running && tof_reference_editing_) {
        tof_reference_editing_ = false;
        tof_reference_pending_mm_ =
            snapshot.distance.settings.reference_distance_mm;
    } else if (!tof_reference_editing_) {
        tof_reference_pending_mm_ =
            snapshot.distance.settings.reference_distance_mm;
    }

    const char *settings_status_color = "#ff8a66";
    std::array<char, 176> settings_status{};
    if (!snapshot_available(snapshot)) {
        std::snprintf(
            settings_status.data(),
            settings_status.size(),
            "SETTINGS UNAVAILABLE (SNAPSHOT NOT READY)"
        );
    } else if (!component_ready(snapshot.distance.settings)) {
        std::snprintf(
            settings_status.data(),
            settings_status.size(),
            "SETTINGS %s (%s)",
            component_state(snapshot.distance.settings),
            component_reason(snapshot.distance.settings)
        );
    } else if (sample_count == kTofSampleCounts.end()) {
        std::snprintf(
            settings_status.data(),
            settings_status.size(),
            "SAVED SAMPLE COUNT %u IS INVALID",
            static_cast<unsigned>(snapshot.distance.settings.sample_count)
        );
    } else if (sample_interval == kTofSampleIntervalsMs.end()) {
        std::snprintf(
            settings_status.data(),
            settings_status.size(),
            "SAVED INTERVAL %lu ms IS INVALID",
            static_cast<unsigned long>(
                snapshot.distance.settings.sample_interval_ms
            )
        );
    } else if (!reference_valid) {
        std::snprintf(
            settings_status.data(),
            settings_status.size(),
            "SAVED REFERENCE %u mm IS INVALID",
            static_cast<unsigned>(
                snapshot.distance.settings.reference_distance_mm
            )
        );
    } else if (calibration_running_valid) {
        settings_status_color = "#f0b44d";
        std::snprintf(
            settings_status.data(),
            settings_status.size(),
            "SETTINGS LOCKED | CALIBRATION RUNNING"
        );
    } else if (calibration_running) {
        std::snprintf(
            settings_status.data(),
            settings_status.size(),
            "SETTINGS LOCKED | CALIBRATION %s (%s)",
            component_state(snapshot.distance.calibration),
            component_reason(snapshot.distance.calibration)
        );
    } else {
        settings_status_color = "#20dfc1";
        std::snprintf(
            settings_status.data(),
            settings_status.size(),
            "SETTINGS READY | %u SAMPLES @ %lu ms",
            static_cast<unsigned>(snapshot.distance.settings.sample_count),
            static_cast<unsigned long>(
                snapshot.distance.settings.sample_interval_ms
            )
        );
    }

    std::array<char, 96> duration_hint{};
    if (!settings_valid) {
        std::snprintf(
            duration_hint.data(),
            duration_hint.size(),
            "ACQUISITION DURATION UNAVAILABLE"
        );
    } else {
        const uint64_t duration_ms =
            static_cast<uint64_t>(
                snapshot.distance.settings.sample_count
            ) *
            snapshot.distance.settings.sample_interval_ms;
        if (duration_ms < 60000ULL) {
            if ((duration_ms % 1000ULL) == 0ULL) {
                std::snprintf(
                    duration_hint.data(),
                    duration_hint.size(),
                    "ESTIMATED WINDOW %llu s",
                    static_cast<unsigned long long>(duration_ms / 1000ULL)
                );
            } else {
                std::snprintf(
                    duration_hint.data(),
                    duration_hint.size(),
                    "ESTIMATED WINDOW %.1f s",
                    static_cast<double>(duration_ms) / 1000.0
                );
            }
        } else {
            const uint64_t minutes = duration_ms / 60000ULL;
            const uint64_t seconds =
                (duration_ms % 60000ULL) / 1000ULL;
            std::snprintf(
                duration_hint.data(),
                duration_hint.size(),
                "ESTIMATED WINDOW %llu min %llu s",
                static_cast<unsigned long long>(minutes),
                static_cast<unsigned long long>(seconds)
            );
        }
    }

    std::array<char, 48> reference_value{};
    if (settings_valid && tof_reference_pending_mm_.has_value()) {
        std::snprintf(
            reference_value.data(),
            reference_value.size(),
            "%ld mm",
            static_cast<long>(*tof_reference_pending_mm_)
        );
    } else {
        std::snprintf(
            reference_value.data(),
            reference_value.size(),
            "UNAVAILABLE"
        );
    }

    const char *calibration_status_color = "#ff8a66";
    std::array<char, 176> calibration_status{};
    std::array<char, 224> calibration_hint{};
    if (!snapshot_available(snapshot)) {
        std::snprintf(
            calibration_status.data(),
            calibration_status.size(),
            "CALIBRATION UNAVAILABLE"
        );
        std::snprintf(
            calibration_hint.data(),
            calibration_hint.size(),
            "Wait for a valid device snapshot before calibrating."
        );
    } else {
        const auto &calibration = snapshot.distance.calibration;
        switch (calibration.state) {
        case LUNASHELL::DistanceCalibrationState::Required:
            calibration_status_color = "#f0b44d";
            std::snprintf(
                calibration_status.data(),
                calibration_status.size(),
                "CALIBRATION REQUIRED (%s)",
                component_reason(calibration)
            );
            std::snprintf(
                calibration_hint.data(),
                calibration_hint.size(),
                "Set the measured reference, hold the device still, then "
                "use the bottom action."
            );
            break;
        case LUNASHELL::DistanceCalibrationState::Ready:
            if (component_ready(calibration)) {
                calibration_status_color = "#20dfc1";
                std::snprintf(
                    calibration_status.data(),
                    calibration_status.size(),
                    "CALIBRATION READY | REFERENCE %u mm",
                    static_cast<unsigned>(
                        calibration.reference_distance_mm
                    )
                );
                std::snprintf(
                    calibration_hint.data(),
                    calibration_hint.size(),
                    "Use the bottom action to recalibrate against the "
                    "selected physical reference."
                );
            } else {
                calibration_status_color = "#f0b44d";
                std::snprintf(
                    calibration_status.data(),
                    calibration_status.size(),
                    "CALIBRATION READY | LAST ERROR %s",
                    component_reason(calibration)
                );
                std::snprintf(
                    calibration_hint.data(),
                    calibration_hint.size(),
                    "The previous calibration remains stored. Correct "
                    "alignment before retrying."
                );
            }
            break;
        case LUNASHELL::DistanceCalibrationState::Running:
            if (component_ready(calibration) &&
                calibration.progress_percent <= 100U) {
                calibration_status_color = "#f0b44d";
                std::snprintf(
                    calibration_status.data(),
                    calibration_status.size(),
                    "CALIBRATING %u%% | S1 %u S2 %u",
                    static_cast<unsigned>(calibration.progress_percent),
                    static_cast<unsigned>(
                        calibration.accepted_samples[0]
                    ),
                    static_cast<unsigned>(
                        calibration.accepted_samples[1]
                    )
                );
                std::snprintf(
                    calibration_hint.data(),
                    calibration_hint.size(),
                    "Keep the device still at %u mm until both sensors "
                    "complete the acquisition.",
                    static_cast<unsigned>(
                        calibration.reference_distance_mm
                    )
                );
            } else {
                std::snprintf(
                    calibration_status.data(),
                    calibration_status.size(),
                    "CALIBRATION ERROR (%s)",
                    component_reason(calibration)
                );
                std::snprintf(
                    calibration_hint.data(),
                    calibration_hint.size(),
                    "Calibration state is invalid; do not use the current "
                    "distance."
                );
            }
            break;
        case LUNASHELL::DistanceCalibrationState::Succeeded:
            if (component_ready(calibration)) {
                calibration_status_color = "#20dfc1";
                std::snprintf(
                    calibration_status.data(),
                    calibration_status.size(),
                    "CALIBRATION SUCCEEDED | S1 %+ld S2 %+ld mm",
                    static_cast<long>(calibration.offsets_mm[0]),
                    static_cast<long>(calibration.offsets_mm[1])
                );
                std::snprintf(
                    calibration_hint.data(),
                    calibration_hint.size(),
                    "Calibration is stored. Live distance becomes "
                    "available after the next valid sample window."
                );
            } else {
                calibration_status_color = "#f0b44d";
                std::snprintf(
                    calibration_status.data(),
                    calibration_status.size(),
                    "CALIBRATION SUCCEEDED (%s)",
                    component_reason(calibration)
                );
                std::snprintf(
                    calibration_hint.data(),
                    calibration_hint.size(),
                    "Calibration status is not currently valid."
                );
            }
            break;
        case LUNASHELL::DistanceCalibrationState::Failed:
            std::snprintf(
                calibration_status.data(),
                calibration_status.size(),
                "CALIBRATION FAILED (%s)",
                component_reason(calibration)
            );
            std::snprintf(
                calibration_hint.data(),
                calibration_hint.size(),
                "Verify the physical distance, alignment, and stability "
                "before retrying."
            );
            break;
        }
    }

    const std::array<std::pair<const char *, const char *>, 5> text_updates = {{
        {
            "/LUNASHELL_tof_settings/surface/tof_settings_scroller/"
            "settings_status",
            settings_status.data(),
        },
        {
            "/LUNASHELL_tof_settings/surface/tof_settings_scroller/"
            "duration_hint",
            duration_hint.data(),
        },
        {
            "/LUNASHELL_tof_settings/surface/tof_settings_scroller/"
            "reference_value",
            reference_value.data(),
        },
        {
            "/LUNASHELL_tof_settings/surface/tof_settings_scroller/"
            "calibration_status",
            calibration_status.data(),
        },
        {
            "/LUNASHELL_tof_settings/surface/tof_settings_scroller/"
            "calibration_hint",
            calibration_hint.data(),
        },
    }};
    for (const auto &[path, text] : text_updates) {
        if (auto result = set_text(context, path, text); !result) {
            return result;
        }
    }
    if (auto result = set_binding_values(
            context,
            {
                {
                    .absolute_path =
                        "/LUNASHELL_tof_settings/surface/"
                        "tof_settings_scroller/settings_status",
                    .key = "color",
                    .value = settings_status_color,
                },
                {
                    .absolute_path =
                        "/LUNASHELL_tof_settings/surface/"
                        "tof_settings_scroller/calibration_status",
                    .key = "color",
                    .value = calibration_status_color,
                },
            }
        ); !result) {
        return std::unexpected(
            "Failed to refresh distance settings status colors: " +
            result.error()
        );
    }

    TofSettingsControlsState next_controls{};
    if (settings_valid) {
        next_controls.sample_count_index = static_cast<int32_t>(
            std::distance(kTofSampleCounts.begin(), sample_count)
        );
        next_controls.sample_interval_index = static_cast<int32_t>(
            std::distance(kTofSampleIntervalsMs.begin(), sample_interval)
        );
        next_controls.reference_distance_mm =
            snapshot.distance.settings.reference_distance_mm;
    }
    next_controls.controls_disabled =
        !settings_valid || calibration_running;
    if (tof_settings_controls_state_ &&
        *tof_settings_controls_state_ == next_controls) {
        return {};
    }

    std::vector<esp_brookesia::gui::BindingValueUpdate> control_updates;
    control_updates.reserve(6);
    if (next_controls.sample_count_index >= 0) {
        control_updates.push_back({
            .absolute_path =
                "/LUNASHELL_tof_settings/surface/tof_settings_scroller/"
                "sample_count",
            .key = "selectedIndex",
            .value = std::to_string(
                next_controls.sample_count_index
            ),
        });
    }
    if (next_controls.sample_interval_index >= 0) {
        control_updates.push_back({
            .absolute_path =
                "/LUNASHELL_tof_settings/surface/tof_settings_scroller/"
                "sample_interval",
            .key = "selectedIndex",
            .value = std::to_string(
                next_controls.sample_interval_index
            ),
        });
    }
    if (next_controls.reference_distance_mm >= 0 &&
        !tof_reference_editing_) {
        control_updates.push_back({
            .absolute_path =
                "/LUNASHELL_tof_settings/surface/tof_settings_scroller/"
                "reference_distance",
            .key = "value",
            .value = std::to_string(
                next_controls.reference_distance_mm
            ),
        });
    }
    const char *disabled =
        next_controls.controls_disabled ? "true" : "false";
    control_updates.insert(
        control_updates.end(),
        {
            {
                .absolute_path =
                    "/LUNASHELL_tof_settings/surface/tof_settings_scroller/"
                    "sample_count",
                .key = "disabled",
                .value = disabled,
            },
            {
                .absolute_path =
                    "/LUNASHELL_tof_settings/surface/tof_settings_scroller/"
                    "sample_interval",
                .key = "disabled",
                .value = disabled,
            },
            {
                .absolute_path =
                    "/LUNASHELL_tof_settings/surface/tof_settings_scroller/"
                    "reference_distance",
                .key = "disabled",
                .value = disabled,
            },
        }
    );
    if (auto result = set_binding_values(context, control_updates);
        !result) {
        return std::unexpected(
            "Failed to refresh distance settings controls: " +
            result.error()
        );
    }
    tof_settings_controls_state_ = next_controls;
    return {};
}
#endif

std::expected<void, std::string> App::set_settings_text(
    AppContext &context,
    SettingsTextSlot slot,
    std::string_view path,
    std::string_view text
)
{
    const size_t index = static_cast<size_t>(slot);
    if (index >= settings_text_cache_.size()) {
        return std::unexpected("Settings text cache slot is out of range");
    }
    if (settings_text_cache_valid_ &&
        settings_text_cache_[index] == text) {
        return {};
    }
    if (auto result = set_text(context, path, text); !result) {
        return result;
    }
    settings_text_cache_[index].assign(text);
    return {};
}

std::expected<void, std::string> App::refresh_settings(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    if (auto result = configure_chrome(context); !result) {
        return result;
    }
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }
    std::array<char, 256> text{};
    std::snprintf(
        text.data(),
        text.size(),
        performance_mode_enabled_
            ? "Performance profile is active"
            : "Standard service profile"
    );
    if (auto result = set_settings_text(
            context,
            SettingsTextSlot::Performance,
            "/LUNASHELL_settings/surface/settings_scroller/performance_status",
            text.data()
        ); !result) {
        return result;
    }

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
            "Snapshot cadence unavailable: %s",
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
            "Saved snapshot cadence is invalid"
        );
    } else if (!component_ready(
                   snapshot.diagnostics.full_snapshot_monitor
               )) {
        std::snprintf(
            text.data(),
            text.size(),
            "Snapshot monitor unavailable: %s",
            component_reason(snapshot.diagnostics.full_snapshot_monitor)
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "%s updates are %s",
            full_snapshot_cadence.data(),
            snapshot.diagnostics.full_snapshot_monitor_active
                ? "active"
                : "paused"
        );
    }
    if (auto result = set_settings_text(
            context,
            SettingsTextSlot::FullSnapshot,
            "/LUNASHELL_settings/surface/settings_scroller/"
            "full_snapshot_status",
            text.data()
        ); !result) {
        return result;
    }

    if (!component_ready(snapshot.diagnostics.sample_period)) {
        std::snprintf(
            text.data(),
            text.size(),
            "Diagnostic refresh unavailable: %s",
            component_reason(snapshot.diagnostics.sample_period)
        );
    } else if (snapshot.diagnostics.sample_period_ms < 100U ||
               snapshot.diagnostics.sample_period_ms > 2000U) {
        std::snprintf(
            text.data(),
            text.size(),
            "Saved diagnostic refresh is invalid"
        );
    } else if (!component_ready(snapshot.diagnostics.monitor)) {
        std::snprintf(
            text.data(),
            text.size(),
            "Diagnostic monitor unavailable: %s",
            component_reason(snapshot.diagnostics.monitor)
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "Diagnostic refresh%s",
            snapshot.diagnostics.monitor_active ? "" : " (paused)"
        );
    }
    if (auto result = set_settings_text(
            context,
            SettingsTextSlot::DiagnosticPeriod,
            "/LUNASHELL_settings/surface/settings_scroller/period_value",
            text.data()
        ); !result) {
        return result;
    }

    if (!component_ready(snapshot.diagnostics.imu_smoothing)) {
        std::snprintf(
            text.data(),
            text.size(),
            "Motion smoothing unavailable: %s",
            component_reason(snapshot.diagnostics.imu_smoothing)
        );
    } else if (snapshot.diagnostics.imu_smoothing_percent > 90U) {
        std::snprintf(
            text.data(),
            text.size(),
            "Saved motion smoothing is invalid"
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "Motion smoothing"
        );
    }
    if (auto result = set_settings_text(
            context,
            SettingsTextSlot::ImuSmoothing,
            "/LUNASHELL_settings/surface/settings_scroller/smoothing_value",
            text.data()
        ); !result) {
        return result;
    }

    if (storage_cleanup_failed(snapshot.storage)) {
        std::snprintf(
            text.data(),
            text.size(),
            "SD card error - open SD Card for details"
        );
    } else if (storage_format_required(snapshot.storage)) {
        std::snprintf(
            text.data(),
            text.size(),
            "SD card needs formatting"
        );
    } else if (storage_mounted_ready(snapshot.storage)) {
        std::snprintf(
            text.data(),
            text.size(),
            "SD card ready"
        );
    } else if (!snapshot.storage.card_response_verified) {
        std::snprintf(
            text.data(),
            text.size(),
            "SD card not detected"
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "SD card unavailable"
        );
    }
    if (auto result = set_settings_text(
            context,
            SettingsTextSlot::Storage,
            "/LUNASHELL_settings/surface/settings_scroller/storage_value",
            text.data()
        ); !result) {
        return result;
    }

    std::array<char, 64> wifi_state{};
    std::array<char, 64> ble_state{};
    if (radio_deferred_by_policy(snapshot.radios.wifi) ||
        wifi_stopped_by_user(snapshot.radios.wifi)) {
        std::snprintf(wifi_state.data(), wifi_state.size(), "off");
    } else if (component_ready(snapshot.radios.wifi)) {
        std::snprintf(
            wifi_state.data(),
            wifi_state.size(),
            "%s",
            snapshot.radios.wifi.connected
                ? "connected"
                : snapshot.radios.wifi.started ? "ready" : "off"
        );
    } else {
        std::snprintf(wifi_state.data(), wifi_state.size(), "unavailable");
    }
    if (radio_deferred_by_policy(snapshot.radios.ble) ||
        ble_stopped_by_user(snapshot.radios.ble)) {
        std::snprintf(ble_state.data(), ble_state.size(), "off");
    } else if (component_ready(snapshot.radios.ble)) {
        std::snprintf(
            ble_state.data(),
            ble_state.size(),
            "%s",
            snapshot.radios.ble.connected
                ? "connected"
                : snapshot.radios.ble.provisioning
                    ? "pairing"
                    : snapshot.radios.ble.advertising ? "discoverable" : "ready"
        );
    } else {
        std::snprintf(ble_state.data(), ble_state.size(), "unavailable");
    }
    std::snprintf(
        text.data(),
        text.size(),
        "Wi-Fi %s  |  Bluetooth %s",
        wifi_state.data(),
        ble_state.data()
    );
    if (auto result = set_settings_text(
            context,
            SettingsTextSlot::Radios,
            "/LUNASHELL_settings/surface/settings_scroller/radio_value",
            text.data()
        ); !result) {
        return result;
    }
    if (radio_deferred_by_policy(snapshot.radios.wifi) ||
        wifi_stopped_by_user(snapshot.radios.wifi)) {
        std::snprintf(
            text.data(),
            text.size(),
            "Wi-Fi is off"
        );
    } else if (!component_ready(snapshot.radios.wifi)) {
        std::snprintf(
            text.data(),
            text.size(),
            "Wi-Fi is unavailable"
        );
    } else if (snapshot.radios.wifi.connected &&
               snapshot.radios.wifi.ssid[0] == '\0') {
        std::snprintf(
            text.data(),
            text.size(),
            "Wi-Fi connection details are unavailable"
        );
    } else if (snapshot.radios.wifi.ip_acquired &&
               snapshot.radios.wifi.ip_address[0] == '\0') {
        std::snprintf(
            text.data(),
            text.size(),
            "Wi-Fi address is still pending"
        );
    } else if (snapshot.radios.wifi.connected) {
        std::snprintf(
            text.data(),
            text.size(),
            "Connected to %s\nAddress %s",
            snapshot.radios.wifi.ssid,
            snapshot.radios.wifi.ip_acquired
                ? snapshot.radios.wifi.ip_address
                : "pending"
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "Wi-Fi is %s",
            snapshot.radios.wifi.started ? "ready" : "off"
        );
    }
    if (auto result = set_settings_text(
            context,
            SettingsTextSlot::Wifi,
            "/LUNASHELL_settings/surface/settings_scroller/wifi_detail",
            text.data()
        ); !result) {
        return result;
    }

    if (radio_deferred_by_policy(snapshot.radios.ble) ||
        ble_stopped_by_user(snapshot.radios.ble)) {
        std::snprintf(
            text.data(),
            text.size(),
            "Bluetooth is off"
        );
    } else if (!component_ready(snapshot.radios.ble)) {
        std::snprintf(
            text.data(),
            text.size(),
            "Bluetooth is unavailable"
        );
    } else if (snapshot.radios.ble.device_name[0] == '\0') {
        std::snprintf(
            text.data(),
            text.size(),
            "Bluetooth name is unavailable"
        );
    } else {
        const char *ble_mode =
            snapshot.radios.ble.connected
                ? "connected"
                : snapshot.radios.ble.provisioning
                    ? "pairing"
                    : snapshot.radios.ble.advertising
                        ? "discoverable"
                        : "ready";
        std::snprintf(
            text.data(),
            text.size(),
            "Bluetooth %s\n%s",
            ble_mode,
            snapshot.radios.ble.device_name
        );
    }
    if (auto result = set_settings_text(
            context,
            SettingsTextSlot::Bluetooth,
            "/LUNASHELL_settings/surface/settings_scroller/ble_detail",
            text.data()
        ); !result) {
        return result;
    }

    if (!component_ready(snapshot.gps)) {
        std::snprintf(
            text.data(),
            text.size(),
            "Location is unavailable"
        );
    } else if (!snapshot.gps.has_fix) {
        std::snprintf(text.data(), text.size(), "Location has no signal yet");
    } else if (!gps_fix_valid(snapshot.gps)) {
        std::snprintf(
            text.data(),
            text.size(),
            "Location is searching (%u satellites)",
            static_cast<unsigned>(snapshot.gps.satellites)
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "Location ready (%u satellites)",
            static_cast<unsigned>(snapshot.gps.satellites)
        );
    }
    if (auto result = set_settings_text(
            context,
            SettingsTextSlot::Gps,
            "/LUNASHELL_settings/surface/settings_scroller/gps_detail",
            text.data()
        ); !result) {
        return result;
    }

    if (!component_ready(snapshot.ui.brightness)) {
        std::snprintf(
            text.data(),
            text.size(),
            "Brightness unavailable: %s",
            component_reason(snapshot.ui.brightness)
        );
    } else if (!component_ready(snapshot.ui.display)) {
        std::snprintf(
            text.data(),
            text.size(),
            "Display unavailable: %s",
            component_reason(snapshot.ui.display)
        );
    } else if (snapshot.ui.brightness_percent < 10U ||
               snapshot.ui.brightness_percent > 100U) {
        std::snprintf(
            text.data(),
            text.size(),
            "Saved brightness is invalid"
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "Brightness"
        );
    }
    if (auto result = set_settings_text(
            context,
            SettingsTextSlot::Brightness,
            "/LUNASHELL_settings/surface/settings_scroller/brightness_value",
            text.data()
        ); !result) {
        return result;
    }

    const OrientationSnapshot &orientation = snapshot.ui.orientation;
    if (!component_ready(orientation)) {
        std::snprintf(
            text.data(),
            text.size(),
            "Screen orientation unavailable: %s",
            component_reason(orientation)
        );
    } else if (orientation.auto_rotation_enabled) {
        std::snprintf(
            text.data(),
            text.size(),
            orientation.rotation_pending
                ? "Screen rotation is updating"
                : "Screen follows device orientation"
        );
    } else {
        std::snprintf(
            text.data(),
            text.size(),
            "Screen orientation is locked"
        );
    }
    if (auto result = set_settings_text(
            context,
            SettingsTextSlot::Rotation,
            "/LUNASHELL_settings/surface/settings_scroller/rotation_value",
            text.data()
        ); !result) {
        return result;
    }
    const auto full_snapshot_period = std::find(
        kFullSnapshotPeriodsMs.begin(),
        kFullSnapshotPeriodsMs.end(),
        snapshot.diagnostics.full_snapshot_period_ms
    );
    const auto diagnostic_period = std::find(
        kDiagnosticPeriodsMs.begin(),
        kDiagnosticPeriodsMs.end(),
        snapshot.diagnostics.sample_period_ms
    );
    const auto smoothing = std::find(
        kImuSmoothingPercents.begin(),
        kImuSmoothingPercents.end(),
        snapshot.diagnostics.imu_smoothing_percent
    );
    const auto brightness = std::find(
        kBrightnessPercents.begin(),
        kBrightnessPercents.end(),
        snapshot.ui.brightness_percent
    );

    const auto resolved_index = [](
        bool available,
        const auto &values,
        const auto &entry
    ) -> int32_t {
        if (!available || entry == values.end()) {
            return -1;
        }
        return static_cast<int32_t>(std::distance(values.begin(), entry));
    };

    SettingsControlsState next_controls{};
    next_controls.selected_indexes = {
        static_cast<int32_t>(adaptive_chrome_.selected_index(
            AdaptiveChromeTiming::CommandArc
        )),
        static_cast<int32_t>(adaptive_chrome_.selected_index(
            AdaptiveChromeTiming::StatusArc
        )),
        static_cast<int32_t>(adaptive_chrome_.selected_index(
            AdaptiveChromeTiming::DisplayDim
        )),
        static_cast<int32_t>(adaptive_chrome_.selected_index(
            AdaptiveChromeTiming::ScreenSleep
        )),
        resolved_index(
            component_ready(snapshot.diagnostics.full_snapshot_period),
            kFullSnapshotPeriodsMs,
            full_snapshot_period
        ),
        resolved_index(
            component_ready(snapshot.diagnostics.sample_period),
            kDiagnosticPeriodsMs,
            diagnostic_period
        ),
        resolved_index(
            component_ready(snapshot.diagnostics.imu_smoothing),
            kImuSmoothingPercents,
            smoothing
        ),
        resolved_index(
            component_ready(snapshot.ui.brightness) &&
                component_ready(snapshot.ui.display),
            kBrightnessPercents,
            brightness
        ),
    };
    next_controls.performance_checked = performance_mode_enabled_;
    next_controls.mirror_checked =
        snapshot.storage.logger_mirror_requested;
    next_controls.mirror_disabled =
        !component_ready(snapshot.storage.logger_mirror);
    next_controls.rotation_checked = orientation.auto_rotation_enabled;
    next_controls.rotation_disabled = !component_ready(orientation);

    settings_text_cache_valid_ = true;
    if (settings_controls_state_ &&
        *settings_controls_state_ == next_controls) {
        return {};
    }

    std::vector<esp_brookesia::gui::BindingValueUpdate> settings_updates;
    settings_updates.reserve(13);
    const auto add_selected_index = [&settings_updates](
        const char *path,
        int32_t selected_index
    ) {
        if (selected_index < 0) {
            return;
        }
        settings_updates.push_back({
            .absolute_path = path,
            .key = "selectedIndex",
            .value = std::to_string(selected_index),
        });
    };
    add_selected_index(
        "/LUNASHELL_settings/surface/settings_scroller/command_arc_timeout",
        next_controls.selected_indexes[0]
    );
    add_selected_index(
        "/LUNASHELL_settings/surface/settings_scroller/status_arc_timeout",
        next_controls.selected_indexes[1]
    );
    add_selected_index(
        "/LUNASHELL_settings/surface/settings_scroller/display_dim_timeout",
        next_controls.selected_indexes[2]
    );
    add_selected_index(
        "/LUNASHELL_settings/surface/settings_scroller/screen_sleep_delay",
        next_controls.selected_indexes[3]
    );
    add_selected_index(
        "/LUNASHELL_settings/surface/settings_scroller/full_snapshot_period",
        next_controls.selected_indexes[4]
    );
    add_selected_index(
        "/LUNASHELL_settings/surface/settings_scroller/diagnostic_period",
        next_controls.selected_indexes[5]
    );
    add_selected_index(
        "/LUNASHELL_settings/surface/settings_scroller/imu_smoothing",
        next_controls.selected_indexes[6]
    );
    add_selected_index(
        "/LUNASHELL_settings/surface/settings_scroller/brightness",
        next_controls.selected_indexes[7]
    );
    settings_updates.insert(
        settings_updates.end(),
        {
            {
                .absolute_path =
                    "/LUNASHELL_settings/surface/settings_scroller/"
                    "performance_mode/toggle",
                .key = "checked",
                .value =
                    next_controls.performance_checked ? "true" : "false",
            },
            {
                .absolute_path =
                    "/LUNASHELL_settings/surface/settings_scroller/"
                    "storage_mirror/toggle",
                .key = "checked",
                .value = next_controls.mirror_checked ? "true" : "false",
            },
            {
                .absolute_path =
                    "/LUNASHELL_settings/surface/settings_scroller/"
                    "storage_mirror/toggle",
                .key = "disabled",
                .value = next_controls.mirror_disabled ? "true" : "false",
            },
            {
                .absolute_path =
                    "/LUNASHELL_settings/surface/settings_scroller/"
                    "auto_rotation/toggle",
                .key = "checked",
                .value = next_controls.rotation_checked ? "true" : "false",
            },
            {
                .absolute_path =
                    "/LUNASHELL_settings/surface/settings_scroller/"
                    "auto_rotation/toggle",
                .key = "disabled",
                .value = next_controls.rotation_disabled ? "true" : "false",
            },
        }
    );
    auto settings_result = set_binding_values(context, settings_updates);
    if (!settings_result) {
        return std::unexpected(
            "Failed to refresh Settings controls: " +
            settings_result.error()
        );
    }
    settings_controls_state_ = next_controls;
    return {};
}

std::expected<void, std::string> App::refresh_storage(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    if (auto result = refresh_chrome(context, snapshot); !result) {
        return result;
    }

    if (storage_format_request_pending_ &&
        snapshot.storage.mount_attempt_count >
            storage_format_request_attempt_) {
        storage_format_request_pending_ = false;
    }

    const StorageSnapshot &storage = snapshot.storage;
    const bool mounted_ready = storage_mounted_ready(storage);
    const bool space_valid =
        mounted_ready &&
        storage.filesystem_total_bytes > 0 &&
        storage.filesystem_total_bytes <= storage.capacity_bytes &&
        storage.free_bytes <= storage.filesystem_total_bytes;
    const bool format_available =
        !storage_format_request_pending_ &&
        storage_format_required(storage);

    const char *status = "UNAVAILABLE";
    const char *status_color = "#9fb1c1";
    const char *format_label = "FORMAT UNAVAILABLE";
    std::array<char, 192> detail{};

    if (storage_format_request_pending_) {
        status = "FORMATTING...";
        status_color = "#f0b44d";
        format_label = "FORMATTING...";
        std::snprintf(
            detail.data(),
            detail.size(),
            "Format request accepted. Keep the verified card inserted while "
            "the storage service completes the immediate attempt."
        );
    } else if (storage_cleanup_failed(storage)) {
        status = "CLEANUP ERROR";
        status_color = "#ff3859";
        std::snprintf(
            detail.data(),
            detail.size(),
            "Stage %s failed: %s. Formatting is blocked.",
            storage_cleanup_stage_name(storage.cleanup_stage),
            esp_err_to_name(storage.cleanup_error)
        );
    } else if (storage.mount_outcome == StorageMountOutcome::FormatFailed) {
        status = "FORMAT FAILED";
        status_color = "#ff3859";
        std::snprintf(
            detail.data(),
            detail.size(),
            "Stage %s failed: %s. No automatic retry will overwrite the card.",
            storage_mount_stage_name(storage.mount_stage),
            component_reason(storage)
        );
    } else if (storage_format_required(storage)) {
        status = "FORMAT REQUIRED";
        status_color = "#f0b44d";
        format_label = "FORMAT SD CARD";
        std::snprintf(
            detail.data(),
            detail.size(),
            "A verified card is present but no supported FAT filesystem was "
            "found. Formatting requires one explicit confirmation."
        );
    } else if (mounted_ready) {
        status = "READY";
        status_color = "#20dfc1";
        format_label = "FORMAT NOT REQUIRED";
        std::snprintf(
            detail.data(),
            detail.size(),
            storage.formatted
                ? "The card was formatted, mounted and measured successfully."
                : "The FAT filesystem is mounted and measured successfully."
        );
    } else if (!storage.card_response_verified) {
        status = "UNCONFIRMED";
        status_color = "#f0b44d";
        std::snprintf(
            detail.data(),
            detail.size(),
            "Card presence or transport response was not verified: %s.",
            component_reason(storage)
        );
    } else {
        status =
            storage.last_error == ESP_OK ? "UNAVAILABLE" : "ERROR";
        status_color =
            storage.last_error == ESP_OK ? "#9fb1c1" : "#ff3859";
        std::snprintf(
            detail.data(),
            detail.size(),
            "Stage %s: %s.",
            storage_mount_stage_name(storage.mount_stage),
            component_reason(storage)
        );
    }

    if (auto result = set_cached_text(
            context,
            storage_ui_cache_.status,
            "/LUNASHELL_storage/surface/storage_scroller/status_value",
            status
        ); !result) {
        return result;
    }
    if (auto result = set_cached_binding_text(
            context,
            storage_ui_cache_.status_color,
            "/LUNASHELL_storage/surface/storage_scroller/status_value",
            "status_color",
            status_color
        ); !result) {
        return result;
    }
    if (auto result = set_cached_text(
            context,
            storage_ui_cache_.detail,
            "/LUNASHELL_storage/surface/storage_scroller/status_detail",
            detail.data()
        ); !result) {
        return result;
    }

    std::array<char, 64> mount{};
    std::snprintf(
        mount.data(),
        mount.size(),
        mounted_ready
            ? storage.logger_mirror_enabled
                ? "MOUNTED | LOG MIRROR ON"
                : "MOUNTED | LOG MIRROR OFF"
            : "NOT MOUNTED"
    );
    if (auto result = set_cached_text(
            context,
            storage_ui_cache_.mount,
            "/LUNASHELL_storage/surface/storage_scroller/mount_value",
            mount.data()
        ); !result) {
        return result;
    }

    std::array<char, 32> total{};
    std::array<char, 32> free{};
    std::array<char, 64> usage{};
    int32_t usage_percent = 0;
    if (space_valid) {
        const uint64_t used_bytes =
            storage.filesystem_total_bytes - storage.free_bytes;
        usage_percent = static_cast<int32_t>(
            (used_bytes * 100ULL) / storage.filesystem_total_bytes
        );
        std::snprintf(
            total.data(),
            total.size(),
            "%llu MB",
            static_cast<unsigned long long>(
                storage.filesystem_total_bytes / (1024ULL * 1024ULL)
            )
        );
        std::snprintf(
            free.data(),
            free.size(),
            "%llu MB",
            static_cast<unsigned long long>(
                storage.free_bytes / (1024ULL * 1024ULL)
            )
        );
        std::snprintf(
            usage.data(),
            usage.size(),
            "USED %llu MB | %ld%%",
            static_cast<unsigned long long>(
                used_bytes / (1024ULL * 1024ULL)
            ),
            static_cast<long>(usage_percent)
        );
    } else {
        std::snprintf(total.data(), total.size(), "UNAVAILABLE");
        std::snprintf(free.data(), free.size(), "UNAVAILABLE");
        std::snprintf(
            usage.data(),
            usage.size(),
            "Space is unavailable until the card is mounted."
        );
    }

    if (auto result = set_cached_text(
            context,
            storage_ui_cache_.total,
            "/LUNASHELL_storage/surface/storage_scroller/total_row/value",
            total.data()
        ); !result) {
        return result;
    }
    if (auto result = set_cached_text(
            context,
            storage_ui_cache_.free,
            "/LUNASHELL_storage/surface/storage_scroller/free_row/value",
            free.data()
        ); !result) {
        return result;
    }
    if (auto result = set_cached_text(
            context,
            storage_ui_cache_.usage,
            "/LUNASHELL_storage/surface/storage_scroller/usage_value",
            usage.data()
        ); !result) {
        return result;
    }

    if (!storage_ui_cache_.usage_percent_known ||
        storage_ui_cache_.usage_percent != usage_percent) {
        if (space_valid) {
            if (auto result = set_value(
                    context,
                    "/LUNASHELL_storage/surface/storage_scroller/usage_bar",
                    usage_percent
                ); !result) {
                return result;
            }
        }
        storage_ui_cache_.usage_percent = usage_percent;
        storage_ui_cache_.usage_percent_known = true;
    }
    const bool usage_hidden = !space_valid;
    if (!storage_ui_cache_.usage_hidden_known ||
        storage_ui_cache_.usage_hidden != usage_hidden) {
        if (auto result = set_hidden(
                context,
                "/LUNASHELL_storage/surface/storage_scroller/usage_bar",
                usage_hidden
            ); !result) {
            return result;
        }
        storage_ui_cache_.usage_hidden = usage_hidden;
        storage_ui_cache_.usage_hidden_known = true;
    }

    if (auto result = set_cached_text(
            context,
            storage_ui_cache_.format_label,
            "/LUNASHELL_storage/surface/storage_scroller/format_action/label",
            format_label
        ); !result) {
        return result;
    }
    const bool format_disabled = !format_available;
    if (!storage_ui_cache_.format_disabled_known ||
        storage_ui_cache_.format_disabled != format_disabled) {
        if (auto result = set_binding_text(
                context,
                "/LUNASHELL_storage/surface/storage_scroller/format_action",
                "format_disabled",
                format_disabled ? "true" : "false"
            ); !result) {
            return result;
        }
        storage_ui_cache_.format_disabled = format_disabled;
        storage_ui_cache_.format_disabled_known = true;
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
    if (screen_ != Screen::Logs ||
        !active_screen_document_id_.has_value()) {
        return std::unexpected(
            "Log search requested without an active Logs document"
        );
    }
    const auto document_id = *active_screen_document_id_;
    const uint32_t generation = screen_document_generation_;

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
        [this,
         document_id,
         generation](
            const esp_brookesia::system::core::KeyboardResult &keyboard_result
        ) {
            search_keyboard_ =
                esp_brookesia::system::core::INVALID_KEYBOARD_REQUEST_ID;
            if (context_ == nullptr || !keyboard_result.confirmed) {
                return;
            }
            if (screen_ != Screen::Logs ||
                !is_current_screen_document(document_id, generation)) {
                ESP_LOGW(
                    kTag,
                    "Ignored log search result after the Logs document changed"
                );
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
    if (screen_ != Screen::Storage ||
        !active_screen_document_id_.has_value()) {
        return std::unexpected(
            "SD format dialog requested without an active Storage document"
        );
    }
    const auto document_id = *active_screen_document_id_;
    const uint32_t generation = screen_document_generation_;

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
        [this,
         document_id,
         generation,
         mount_attempt_count = storage.mount_attempt_count]() {
            const esp_err_t format_result = LUNASHELL_storage_request_format();
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
                // This is request progress only; completion still comes from
                // the next authoritative storage snapshot.
                storage_format_request_pending_ = true;
                storage_format_request_attempt_ = mount_attempt_count;
                ESP_LOGW(kTag, "Explicit user-authorized SD format requested");
            }
            if (context_ != nullptr &&
                screen_ == Screen::Storage &&
                is_current_screen_document(document_id, generation)) {
                auto refresh_result = refresh_storage(*context_);
                if (!refresh_result) {
                    ESP_LOGE(
                        kTag,
                        "Storage view refresh after format request failed: %s",
                        refresh_result.error().c_str()
                    );
                }
            } else {
                ESP_LOGI(
                    kTag,
                    "Skipped Storage refresh because its lazy "
                    "document changed"
                );
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
    "ai.LUNASHELL.device-console",
    LUNASHELL_app_provider_symbol
);

}  // namespace LUNASHELL
