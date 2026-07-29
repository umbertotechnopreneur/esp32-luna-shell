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

#include "esp_heap_caps.h"
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
constexpr char kReleaseContentAction[] = "qsee.content.release";
constexpr char kChromeFlow[] = "qsee_chrome_flow";
constexpr char kRefreshTimer[] = "qsee.refresh";
constexpr char kPowerStateSyncTimer[] = "qsee.power.state_sync";
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
// The logger contract keeps the bounded snapshot inside the persistent 4 KB buffer.
constexpr size_t kLogSnapshotLines = QSEE_LOGGER_SNAPSHOT_MAX_LINES;

template <size_t CommandCount>
constexpr bool valid_command_main_layout(
    const std::array<qsee::CommandSpec, CommandCount> &commands
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

}  // namespace

namespace qsee {

std::optional<App::LazyScreenDescriptor> App::lazy_screen_descriptor(
    Screen screen
)
{
    switch (screen) {
    case Screen::Welcome:
        return std::nullopt;
    case Screen::Logs:
        return LazyScreenDescriptor{
            .document_path = "res/screen_logs.json",
            .screen_path = "/qsee_logs",
        };
    case Screen::Hardware:
        return LazyScreenDescriptor{
            .document_path = "res/screen_hardware.json",
            .screen_path = "/qsee_hardware",
        };
    case Screen::HardwareDetail:
        return LazyScreenDescriptor{
            .document_path = "res/screen_hardware_detail.json",
            .screen_path = "/qsee_hardware_detail",
        };
    case Screen::Audio:
        return LazyScreenDescriptor{
            .document_path = "res/screen_audio.json",
            .screen_path = "/qsee_audio",
        };
    case Screen::AudioFft:
        return LazyScreenDescriptor{
            .document_path = "res/screen_audio_fft.json",
            .screen_path = "/qsee_audio_fft",
        };
    case Screen::AudioDetail:
        return LazyScreenDescriptor{
            .document_path = "res/screen_audio_detail.json",
            .screen_path = "/qsee_audio_detail",
        };
    case Screen::Imu:
        return LazyScreenDescriptor{
            .document_path = "res/screen_imu.json",
            .screen_path = "/qsee_imu",
        };
    case Screen::Settings:
        return LazyScreenDescriptor{
            .document_path = "res/screen_settings.json",
            .screen_path = "/qsee_settings",
        };
    case Screen::About:
        return LazyScreenDescriptor{
            .document_path = "res/screen_about.json",
            .screen_path = "/qsee_about",
        };
    case Screen::PairIntro:
    case Screen::PairBluetooth:
    case Screen::PairApp:
    case Screen::AppStoreQr:
    case Screen::PairQr:
    case Screen::PairResult:
        return LazyScreenDescriptor{
            .document_path = "res/screen_pairing.json",
            .screen_path = "/qsee_pairing",
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
        auto connection = context.gui().subscribe_action(
            document_id,
            action,
            std::move(handler)
        );
        if (!connection.connected()) {
            return std::unexpected(
                "Failed to subscribe lazy GUI action '" + std::string(action) + "'"
            );
        }
        connections.push_back(std::move(connection));
        return {};
    };

    static constexpr std::array<std::string_view, 1> kLogActions = {
        "qsee.chrome.activity.logs",
    };
    static constexpr std::array<std::string_view, 13> kHardwareActions = {
        "qsee.chrome.activity.hardware",
        "qsee.hardware.detail.0",
        "qsee.hardware.detail.1",
        "qsee.hardware.detail.2",
        "qsee.hardware.detail.3",
        "qsee.hardware.detail.4",
        "qsee.hardware.detail.5",
        "qsee.hardware.detail.6",
        "qsee.hardware.detail.7",
        "qsee.hardware.detail.8",
        "qsee.hardware.detail.9",
        "qsee.hardware.detail.10",
        "qsee.hardware.detail.11",
    };
    static constexpr std::array<std::string_view, 1> kHardwareDetailActions = {
        "qsee.chrome.activity.hardware_detail",
    };
    static constexpr std::array<std::string_view, 3> kSettingsActions = {
        "qsee.chrome.activity.settings",
        "qsee.open.about",
        "qsee.settings.radios.start",
    };

    std::span<const std::string_view> forwarded_actions;
    switch (screen) {
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
            "qsee.settings.command_arc.changed",
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
            "qsee.settings.status_arc.changed",
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
            "qsee.settings.display_dim.changed",
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
            "qsee.settings.screen_sleep.changed",
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
            "qsee.settings.full_snapshot.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_full_snapshot_period_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "qsee.settings.diagnostic.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_diagnostic_period_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "qsee.settings.imu_smoothing.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_imu_smoothing_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "qsee.settings.display.brightness.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_brightness_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "qsee.settings.performance.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_performance_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "qsee.settings.storage.mirror.changed",
            [this](const esp_brookesia::gui::Event &event) {
                handle_storage_mirror_event(event);
            }
        ); !result) {
        connections.clear();
        return result;
    }
    if (auto result = add_settings_connection(
            "qsee.settings.display.rotation.changed",
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
    if (screen == Screen::AudioDetail) {
        audio_detail_palette_applied_ = false;
    }
    if (screen == Screen::Settings) {
        settings_text_cache_valid_ = false;
        settings_controls_state_.reset();
    }
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
        return std::unexpected("Failed to unload active QSee screen document");
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
            "qsee.open.welcome"
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
            "Failed to release current QSee content: " + release_result.error()
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
            "qsee.open.welcome"
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
            "Failed to load QSee screen document '" +
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
            "Failed to mount QSee screen '" +
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
    auto welcome_result = context.gui().trigger_screen_flow(
        kScreenFlow,
        "qsee.open.welcome"
    );
    if (!welcome_result) {
        return std::unexpected(
            "Failed to restore Welcome after lazy GUI failure: " +
            welcome_result.error()
        );
    }
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
    if (auto unload_result = unload_active_screen_document(context);
        !unload_result) {
        remember_failure("lazy document unload", unload_result.error());
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
            "Failed to update QSee view '" + std::string(path) + "': " +
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
            "Failed to update QSee bindings: " + result.error()
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
            "Rejected out-of-range QSee value '" + std::string(path) +
            "': " + std::to_string(value)
        );
    }
    if (screen_ == Screen::Welcome ||
        !active_screen_document_id_.has_value()) {
        auto result = context.gui().set_value(path, value);
        if (!result) {
            return std::unexpected(
                "Failed to update QSee value '" + std::string(path) +
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
            "Failed to update QSee visibility '" + std::string(path) +
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
            "Failed to update QSee binding '" + std::string(path) + "/" +
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
    performance_mode_enabled_ = qsee_performance_mode_get_enabled();
    pairing_qr_registered_ = false;
    background_asset_registered_ = false;
    pairing_result_success_ = false;
    pairing_visual_state_.reset();
    settings_text_cache_valid_ = false;
    settings_controls_state_.reset();
    content_viewport_state_.reset();
    invalidate_audio_screen_cache(Screen::Audio);
    invalidate_audio_screen_cache(Screen::AudioFft);
    invalidate_audio_screen_cache(Screen::AudioDetail);
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
        "qsee.chrome.activity",
        "qsee.chrome.activity.wake",
        "qsee.command.0",
        "qsee.command.1",
        "qsee.command.2",
        "qsee.command.3",
        "qsee.command.4",
        "qsee.back",
        "qsee.next",
    };
    auto subscription_result = context.gui().subscribe_actions(actions);
    if (!subscription_result) {
        context_ = nullptr;
        return std::unexpected(
            "Failed to subscribe QSee UI actions: " + subscription_result.error()
        );
    }
    if (auto result = configure_chrome(context); !result) {
        (void)adaptive_chrome_.shutdown(context.gui());
        screen_document_connections_.clear();
        context_ = nullptr;
        return result;
    }

    const esp_err_t monitor_result =
        qsee_hardware_set_diagnostic_streams(false, false);
    if (monitor_result != ESP_OK) {
        (void)adaptive_chrome_.shutdown(context.gui());
        screen_document_connections_.clear();
        context_ = nullptr;
        return std::unexpected(
            "Failed to disable QSee diagnostic monitor at startup: " +
            std::string(esp_err_to_name(monitor_result))
        );
    }
    const esp_err_t full_snapshot_result =
        qsee_hardware_start_full_snapshot_monitor();
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
        (void)qsee_hardware_stop_full_snapshot_monitor();
        (void)adaptive_chrome_.shutdown(context.gui());
        screen_document_connections_.clear();
        context_ = nullptr;
        return result;
    }

    if (auto timer_result =
            sync_refresh_timer(context, DisplayIdleStage::Active);
        !timer_result) {
        (void)qsee_hardware_stop_full_snapshot_monitor();
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
        (void)qsee_hardware_stop_full_snapshot_monitor();
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
            ESP_LOGE(kTag, "Failed to stop QSee refresh timer after startup failure");
        }
        refresh_timer_ = esp_brookesia::system::core::INVALID_TIMER_ID;
        refresh_period_ms_ = 0;
        if (!context.timer().stop(power_state_sync_timer_)) {
            ESP_LOGE(kTag, "Failed to stop power-state sync timer after startup failure");
        }
        power_state_sync_timer_ =
            esp_brookesia::system::core::INVALID_TIMER_ID;
        (void)qsee_hardware_stop_full_snapshot_monitor();
        (void)adaptive_chrome_.shutdown(context.gui());
        screen_document_connections_.clear();
        context_ = nullptr;
        return refresh_result;
    }

    ESP_LOGI(kTag, "QSee.ai native app started on Brookesia System 0.8");
    return {};
}

std::expected<void, std::string> App::on_pause(AppContext &context)
{
    audio_detail_palette_applied_ = false;
    std::string pause_error;
    if (auto result = adaptive_chrome_.shutdown(context.gui()); !result) {
        pause_error = result.error();
        ESP_LOGE(kTag, "%s", pause_error.c_str());
    }
    const esp_err_t diagnostic_result =
        qsee_hardware_set_diagnostic_streams(false, false);
    if (diagnostic_result != ESP_OK) {
        const std::string error =
            "Failed to pause QSee diagnostic monitor: " +
            std::string(esp_err_to_name(diagnostic_result));
        ESP_LOGE(kTag, "%s", error.c_str());
        if (pause_error.empty()) {
            pause_error = error;
        }
    }
    if (audio_diagnostic_capture_active_) {
        const esp_err_t audio_result =
            qsee_voice_afe_set_diagnostic_suspended(false);
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
    settings_text_cache_valid_ = false;
    settings_controls_state_.reset();
    invalidate_audio_screen_cache(Screen::Audio);
    invalidate_audio_screen_cache(Screen::AudioFft);
    invalidate_audio_screen_cache(Screen::AudioDetail);
    const auto rollback_diagnostics = [this]() {
        const esp_err_t monitor_result =
            qsee_hardware_set_diagnostic_streams(false, false);
        if (monitor_result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Diagnostic monitor resume rollback failed: %s",
                esp_err_to_name(monitor_result)
            );
        }
        if (audio_diagnostic_capture_active_) {
            const esp_err_t audio_result =
                qsee_voice_afe_set_diagnostic_suspended(false);
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
    const esp_err_t full_snapshot_result =
        qsee_hardware_start_full_snapshot_monitor();
    if (full_snapshot_result != ESP_OK) {
        return std::unexpected(
            "Failed to resume Full Snapshot monitor: " +
            std::string(esp_err_to_name(full_snapshot_result))
        );
    }
    if (auto result = apply_diagnostic_monitor(context, screen_); !result) {
        rollback_diagnostics();
        (void)qsee_hardware_stop_full_snapshot_monitor();
        return result;
    }
    if (auto result = configure_chrome(context); !result) {
        rollback_diagnostics();
        (void)qsee_hardware_stop_full_snapshot_monitor();
        return result;
    }
    if (auto result = adaptive_chrome_.resume(context.gui()); !result) {
        rollback_diagnostics();
        (void)qsee_hardware_stop_full_snapshot_monitor();
        return result;
    }
    if (auto result =
            sync_refresh_timer(context, DisplayIdleStage::Active);
        !result) {
        (void)adaptive_chrome_.shutdown(context.gui());
        rollback_diagnostics();
        (void)qsee_hardware_stop_full_snapshot_monitor();
        return result;
    }
    if (auto result = refresh(context); !result) {
        (void)adaptive_chrome_.shutdown(context.gui());
        rollback_diagnostics();
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
    ++screen_document_generation_;
    screen_document_connections_.clear();
    const esp_err_t monitor_result =
        qsee_hardware_set_diagnostic_streams(false, false);
    if (monitor_result != ESP_OK) {
        const std::string error =
            "Failed to stop QSee diagnostic monitor: " +
            std::string(esp_err_to_name(monitor_result));
        ESP_LOGE(kTag, "%s", error.c_str());
        if (stop_error.empty()) {
            stop_error = error;
        }
    }
    if (audio_diagnostic_capture_active_) {
        const esp_err_t audio_result =
            qsee_voice_afe_set_diagnostic_suspended(false);
        if (audio_result == ESP_OK) {
            audio_diagnostic_capture_active_ = false;
        } else {
            const std::string error =
                "Failed to restore voice audio while stopping QSee: " +
                std::string(esp_err_to_name(audio_result));
            ESP_LOGE(kTag, "%s", error.c_str());
            if (stop_error.empty()) {
                stop_error = error;
            }
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
    if (auto unload_result = unload_active_screen_document(context);
        !unload_result) {
        ESP_LOGE(kTag, "%s", unload_result.error().c_str());
        if (stop_error.empty()) {
            stop_error = std::move(unload_result.error());
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
            qsee_voice_afe_set_diagnostic_suspended(true);
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
    const esp_err_t result = qsee_hardware_set_diagnostic_streams(
        imu_enabled,
        audio_enabled
    );
    if (result != ESP_OK && entering_audio) {
        const esp_err_t rollback_result =
            qsee_voice_afe_set_diagnostic_suspended(false);
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
            qsee_voice_afe_set_diagnostic_suspended(false);
        if (audio_result != ESP_OK) {
            const esp_err_t rollback_result =
                qsee_hardware_set_diagnostic_streams(false, true);
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
            "\xef\x81\xae",
            "qsee.logs.filter",
            log_filter_mode_ == 0
                ? ActionRole::Primary
                : ActionRole::Warning,
            false,
        },
        {"\xef\x8c\x84", "qsee.logs.search", ActionRole::Primary, false},
    }};
    static constexpr std::array<CommandSpec, 1> kAudioCommands = {{
        {"\xef\x83\x89", "qsee.open.audio_fft", ActionRole::Success, true},
    }};
    static constexpr std::array<CommandSpec, 2> kHardwareCommands = {{
        {"\xef\x80\xa1", "qsee.hardware.refresh", ActionRole::Success, true},
        {"\xef\x8b\xad", "qsee.storage.format", ActionRole::Danger, true},
    }};
    static constexpr std::array<CommandSpec, 1> kAudioFftCommands = {{
        {"\xef\x81\xae", "qsee.open.audio_detail", ActionRole::Success, true},
    }};
    static constexpr std::array<CommandSpec, 2> kSettingsCommands = {{
        {"\xef\x8a\x93", "qsee.open.pairing", ActionRole::Success, true},
        {"\xef\x85\x9b", "qsee.open.about", ActionRole::Primary, true},
    }};
    static_assert(valid_command_main_layout(kNoCommands));
    static_assert(valid_command_main_layout(kWelcomeCommands));
    static_assert(valid_command_main_layout(kAudioCommands));
    static_assert(valid_command_main_layout(kHardwareCommands));
    static_assert(valid_command_main_layout(kAudioFftCommands));
    static_assert(valid_command_main_layout(kSettingsCommands));

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
            return adaptive_chrome_.configure_commands(
                context.gui(),
                kHardwareCommands
            );
        case Screen::HardwareDetail:
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

struct HardwareDescriptor {
    std::string_view id;
    std::string_view name;
    std::string_view part;
    std::string_view description;
};

constexpr std::array<HardwareDescriptor, 12> kHardwareComponents = {{
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
}};

const qsee::ComponentSnapshot &hardware_component(
    const qsee::DeviceSnapshot &snapshot,
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
    default:
        ESP_LOGE(kTag, "Rejected hardware component index %u",
                 static_cast<unsigned>(index));
        std::abort();
    }
}

void format_hardware_status(
    std::array<char, 64> &output,
    const qsee::DeviceSnapshot &snapshot,
    size_t index
)
{
    const qsee::ComponentSnapshot &component =
        hardware_component(snapshot, index);
    if ((index == 4 || index == 5) &&
        radio_deferred_by_policy(component)) {
        std::snprintf(output.data(), output.size(), "OFF BY POLICY");
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
    std::snprintf(
        output.data(),
        output.size(),
        "%s",
        component_state(component)
    );
}

const char *hardware_status_color(
    const qsee::DeviceSnapshot &snapshot,
    size_t index
)
{
    const qsee::ComponentSnapshot &component =
        hardware_component(snapshot, index);
    if ((index == 4 || index == 5) &&
        radio_deferred_by_policy(component)) {
        return "#9fb1c1";
    }
    if ((index == 3 && component_ready(snapshot.gps) &&
         !snapshot.gps.has_fix) ||
        (index == 10 &&
         (storage_format_required(snapshot.storage) ||
          !snapshot.storage.card_response_verified))) {
        return "#f0b44d";
    }
    if (component_ready(component)) {
        return "#63f4e0";
    }
    if (component.last_error != ESP_OK ||
        component.health == qsee::DeviceHealth::Degraded) {
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
            if (qsee_provisioning_active()) {
                const esp_err_t stop_result = qsee_provisioning_stop();
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
    if (leaving_pairing_qr) {
        if (auto release_result = release_pairing_qr(context); !release_result) {
            return release_result;
        }
    }
    if (auto result = activate_screen_document(context, screen); !result) {
        return fail_navigation(
            "QSee navigation '" + std::string(action) + "' failed: " +
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
            "QSee route changed but navigation history rejected the transition"
        );
    }
    screen_ = screen;
    pairing_visual_state_.reset();
    if (auto chrome_result = configure_chrome(context); !chrome_result) {
        return fail_navigation(
            "QSee chrome configuration failed after navigation: " +
            chrome_result.error()
        );
    }
    if (auto monitor_result = apply_diagnostic_monitor(context, screen); !monitor_result) {
        return fail_navigation(
            "QSee diagnostic policy failed after navigation: " +
            monitor_result.error()
        );
    }
    if (auto refresh_result = refresh(context); !refresh_result) {
        return fail_navigation(
            "QSee screen refresh failed after navigation: " +
            refresh_result.error()
        );
    }
    return {};
}

std::expected<void, std::string> App::on_action(
    AppContext &context,
    std::string_view action
)
{
    if (auto result = note_activity(context); !result) {
        return result;
    }
    if (const auto resolved =
            adaptive_chrome_.resolve_action(action);
        resolved.has_value()) {
        action = *resolved;
    }
    if (action == "qsee.chrome.activity" ||
        action == "qsee.chrome.activity.wake" ||
        action == "qsee.chrome.activity.logs" ||
        action == "qsee.chrome.activity.hardware" ||
        action == "qsee.chrome.activity.hardware_detail" ||
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
    constexpr std::string_view kHardwareDetailPrefix =
        "qsee.hardware.detail.";
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

    if (action == "qsee.settings.radios.start") {
        esp_err_t result = qsee_radios_init();
        if (result == ESP_OK && !qsee_provisioning_active()) {
            result = qsee_wifi_start_scan();
        }
        return finish_settings_command(result, "Radio startup");
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
        qsee_hardware_set_diagnostic_period_ms(
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
        qsee_hardware_set_imu_smoothing_percent(
            kImuSmoothingPercents[index]
        ),
        "Motion smoothing"
    );
}

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
        qsee_display_set_brightness_percent(kBrightnessPercents[index]),
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
        qsee_performance_mode_set_enabled(*checked);
    if (result != ESP_OK) {
        finish_settings_event(result, "Performance mode");
        return;
    }
    performance_mode_enabled_ = qsee_performance_mode_get_enabled();
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
        qsee_storage_set_logger_mirror(*checked),
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
        qsee_display_set_auto_rotation_enabled(*checked),
        "Automatic rotation"
    );
}

std::expected<void, std::string> App::on_timer(
    AppContext &context,
    esp_brookesia::system::core::TimerId timer_id,
    std::string_view name
)
{
    if (timer_id == power_state_sync_timer_ &&
        name == kPowerStateSyncTimer) {
        const DisplayIdleStage previous_stage =
            adaptive_chrome_.display_idle_stage();
        const DeviceSnapshot snapshot =
            DeviceSnapshotService::instance().snapshot();
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
        if (previous_stage == DisplayIdleStage::Off &&
            current_stage == DisplayIdleStage::Active) {
            return refresh(context);
        }
        return {};
    }
    if (timer_id == refresh_timer_ && name == kRefreshTimer) {
        if (adaptive_chrome_.display_idle_stage() ==
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
        screen_ == Screen::Imu;
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
                "Failed to stop QSee refresh timer while changing "
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
            "Failed to start QSee refresh timer at " +
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
                "/qsee_welcome/surface",
                ViewportLayout::CircularScroll,
                48,
                70,
            };
        case Screen::Logs:
            return {
                "/qsee_logs/surface",
                ViewportLayout::CircularScroll,
                48,
                8,
            };
        case Screen::Hardware:
            return {
                "/qsee_hardware/surface",
                ViewportLayout::CircularScroll,
                48,
                8,
            };
        case Screen::HardwareDetail:
            return {
                "/qsee_hardware_detail/surface",
                ViewportLayout::CircularScroll,
                48,
                12,
            };
        case Screen::Audio:
            return {
                "/qsee_audio/surface",
                ViewportLayout::FixedCanvas,
                0,
                0,
            };
        case Screen::AudioFft:
            return {
                "/qsee_audio_fft/surface",
                ViewportLayout::FixedCanvas,
                0,
                0,
            };
        case Screen::AudioDetail:
            return {
                "/qsee_audio_detail/surface",
                ViewportLayout::FixedCanvas,
                0,
                0,
            };
        case Screen::Imu:
            return {
                "/qsee_imu/surface",
                ViewportLayout::FixedCanvas,
                0,
                0,
            };
        case Screen::Settings:
            return {
                "/qsee_settings/surface",
                ViewportLayout::CircularScroll,
                48,
                8,
            };
        case Screen::About:
            return {
                "/qsee_about/surface",
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
                "/qsee_pairing/surface",
                ViewportLayout::Pairing,
                50,
                6,
            };
        }
        return {};
    }();
    if (spec.surface_path.empty()) {
        return std::unexpected(
            "Unknown QSee route for adaptive content viewport"
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
            "Failed to resize QSee content viewport: " +
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
            "/qsee_welcome/surface/ram/value",
            text.data()
        ); !result) {
        return result;
    }
    if (heap_valid) {
        if (auto result = set_value(
                context,
                "/qsee_welcome/surface/ram/bar",
                heap_percent
            ); !result) {
            return result;
        }
    }
    if (auto result = set_hidden(
            context,
            "/qsee_welcome/surface/ram/bar",
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
            "/qsee_welcome/surface/psram/value",
            text.data()
        ); !result) {
        return result;
    }
    if (psram_valid) {
        if (auto result = set_value(
                context,
                "/qsee_welcome/surface/psram/bar",
                psram_percent
            ); !result) {
            return result;
        }
    }
    if (auto result = set_hidden(
            context,
            "/qsee_welcome/surface/psram/bar",
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
            "/qsee_welcome/surface/flash/value",
            text.data()
        ); !result) {
        return result;
    }
    if (flash_valid) {
        if (auto result = set_value(
                context,
                "/qsee_welcome/surface/flash/bar",
                flash_percent
            ); !result) {
            return result;
        }
    }
    if (auto result = set_hidden(
            context,
            "/qsee_welcome/surface/flash/bar",
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
            "/qsee_welcome/surface/sd/value",
            text.data()
        ); !result) {
        return result;
    }
    if (storage_percent_valid) {
        if (auto result = set_value(
                context,
                "/qsee_welcome/surface/sd/bar",
                storage_percent
            ); !result) {
            return result;
        }
    }
    if (auto result = set_hidden(
            context,
            "/qsee_welcome/surface/sd/bar",
            !storage_percent_valid
        ); !result) {
        return result;
    }

    for (size_t core = 0; core < 2; ++core) {
        const std::string path = core == 0
            ? "/qsee_welcome/surface/core0"
            : "/qsee_welcome/surface/core1";
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

    const qsee::WifiSnapshot &wifi = snapshot.radios.wifi;
    const char *ip_state =
        wifi.ip_acquired && wifi.ip_address[0] != '\0'
            ? wifi.ip_address
            : wifi.started
                ? "CONNECTING"
                : "OFFLINE";
    std::snprintf(
        text.data(),
        text.size(),
        "QSee device console\nIP ADDRESS | %s",
        ip_state
    );
    return set_text(
        context,
        "/qsee_welcome/surface/device_status",
        text.data()
    );
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
        auto scroll_result = scroll_to(
            context,
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

    for (size_t index = 0; index < kHardwareComponents.size(); ++index) {
        std::array<char, 64> status{};
        format_hardware_status(status, snapshot, index);
        const std::string base_path =
            "/qsee_hardware/surface/hardware_scroller/" +
            std::string(kHardwareComponents[index].id);
        if (auto result = set_text(
                context,
                base_path + "/status",
                status.data()
            ); !result) {
            return result;
        }
        const char *color = hardware_status_color(snapshot, index);
        if (auto result = set_binding_text(
                context,
                base_path + "/status",
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
    return set_text(
        context,
        "/qsee_hardware/surface/hardware_scroller/summary",
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
        "qsee.open.hardware_detail"
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
            "/qsee_hardware_detail/surface/title",
            hardware_detail_title_
        ); !result) {
        return result;
    }
    return set_text(
        context,
        "/qsee_hardware_detail/surface/detail_scroller/body",
        hardware_detail_body_
    );
}

std::expected<void, std::string> App::set_cached_text(
    AppContext &context,
    CachedText &cache,
    std::string_view path,
    std::string_view text
)
{
    if (text.size() >= cache.value.size()) {
        return std::unexpected(
            "Rejected oversized cached QSee text for '" +
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

void App::invalidate_audio_screen_cache(Screen screen)
{
    switch (screen) {
    case Screen::Audio:
        audio_status_cache_.valid = false;
        audio_values_cache_.valid = false;
        audio_meter_cache_.level_known.fill(false);
        audio_meter_cache_.hidden_known.fill(false);
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
            "/qsee_audio/surface/audio_status",
            status.data()
        ); !result) {
        return result;
    }

    static constexpr std::array<std::string_view, 2> kMeterPaths = {
        "/qsee_audio/surface/mic0_bar",
        "/qsee_audio/surface/mic1_bar",
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
                "Failed to update QSee audio meter visibility: " +
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
        "/qsee_audio/surface/audio_values",
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
    case Screen::HardwareDetail:
        return "qsee.open.hardware_detail";
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
    static_assert(
        kAudioUiSpectrumBandCount == kAudioSpectrumBandCount,
        "QSee FFT cache must match the canonical snapshot band count"
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

    if (auto result = set_cached_text(
            context,
            audio_fft_status_cache_,
            "/qsee_audio_fft/surface/spectrum_status",
            status.data()
        ); !result) {
        return result;
    }
    if (!spectrum_valid) {
        if (!audio_fft_hidden_known_ || !audio_fft_hidden_) {
            if (auto result = set_hidden(
                    context,
                    "/qsee_audio_fft/surface/bands",
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
            (static_cast<int32_t>(snapshot.audio.channel_spectrum[0][band]) +
             static_cast<int32_t>(snapshot.audio.channel_spectrum[1][band]) +
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
            "/qsee_audio_fft/surface/bands/band%02u",
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
            .absolute_path = "/qsee_audio_fft/surface/bands",
            .key = "hidden",
            .value = "false",
        });
    }
    if (auto result = set_binding_values(context, spectrum_updates);
        !result) {
        return std::unexpected(
            "Failed to update QSee FFT band bindings: " + result.error()
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
        "QSee detail FFT cache must match the canonical snapshot band count"
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
            "/qsee_audio_detail/surface/spectrum_status",
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
                            "/qsee_audio_detail/surface/mic0_bands",
                        .key = "hidden",
                        .value = "true",
                    },
                    {
                        .absolute_path =
                            "/qsee_audio_detail/surface/mic1_bands",
                        .key = "hidden",
                        .value = "true",
                    },
                };
            if (auto result =
                    set_binding_values(context, visibility_updates);
                !result) {
                return std::unexpected(
                    "Failed to hide QSee detail FFT bands: " +
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
                    "/qsee_audio_detail/surface/%s/band%02u",
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
                "Failed to update QSee detail FFT palette: " +
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
                "/qsee_audio_detail/surface/%s/band%02u",
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
            .absolute_path = "/qsee_audio_detail/surface/mic0_bands",
            .key = "hidden",
            .value = "false",
        });
        spectrum_updates.push_back({
            .absolute_path = "/qsee_audio_detail/surface/mic1_bands",
            .key = "hidden",
            .value = "false",
        });
    }
    if (auto result = set_binding_values(context, spectrum_updates);
        !result) {
        return std::unexpected(
            "Failed to update QSee detail FFT band bindings: " + result.error()
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
            "/qsee_settings/surface/settings_scroller/performance_status",
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
            "/qsee_settings/surface/settings_scroller/period_value",
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
            "/qsee_settings/surface/settings_scroller/smoothing_value",
            text.data()
        ); !result) {
        return result;
    }

    if (storage_cleanup_failed(snapshot.storage)) {
        std::snprintf(
            text.data(),
            text.size(),
            "SD card error - open Hardware for details"
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
            "/qsee_settings/surface/settings_scroller/storage_value",
            text.data()
        ); !result) {
        return result;
    }

    std::array<char, 64> wifi_state{};
    std::array<char, 64> ble_state{};
    if (radio_deferred_by_policy(snapshot.radios.wifi)) {
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
    if (radio_deferred_by_policy(snapshot.radios.ble)) {
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
            "/qsee_settings/surface/settings_scroller/radio_value",
            text.data()
        ); !result) {
        return result;
    }
    if (radio_deferred_by_policy(snapshot.radios.wifi)) {
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
            "/qsee_settings/surface/settings_scroller/wifi_detail",
            text.data()
        ); !result) {
        return result;
    }

    if (radio_deferred_by_policy(snapshot.radios.ble)) {
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
            "/qsee_settings/surface/settings_scroller/ble_detail",
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
            "/qsee_settings/surface/settings_scroller/gps_detail",
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
            "/qsee_settings/surface/settings_scroller/rotation_value",
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
        "/qsee_settings/surface/settings_scroller/command_arc_timeout",
        next_controls.selected_indexes[0]
    );
    add_selected_index(
        "/qsee_settings/surface/settings_scroller/status_arc_timeout",
        next_controls.selected_indexes[1]
    );
    add_selected_index(
        "/qsee_settings/surface/settings_scroller/display_dim_timeout",
        next_controls.selected_indexes[2]
    );
    add_selected_index(
        "/qsee_settings/surface/settings_scroller/screen_sleep_delay",
        next_controls.selected_indexes[3]
    );
    add_selected_index(
        "/qsee_settings/surface/settings_scroller/full_snapshot_period",
        next_controls.selected_indexes[4]
    );
    add_selected_index(
        "/qsee_settings/surface/settings_scroller/diagnostic_period",
        next_controls.selected_indexes[5]
    );
    add_selected_index(
        "/qsee_settings/surface/settings_scroller/imu_smoothing",
        next_controls.selected_indexes[6]
    );
    add_selected_index(
        "/qsee_settings/surface/settings_scroller/brightness",
        next_controls.selected_indexes[7]
    );
    settings_updates.insert(
        settings_updates.end(),
        {
            {
                .absolute_path =
                    "/qsee_settings/surface/settings_scroller/"
                    "performance_mode/toggle",
                .key = "checked",
                .value =
                    next_controls.performance_checked ? "true" : "false",
            },
            {
                .absolute_path =
                    "/qsee_settings/surface/settings_scroller/"
                    "storage_mirror/toggle",
                .key = "checked",
                .value = next_controls.mirror_checked ? "true" : "false",
            },
            {
                .absolute_path =
                    "/qsee_settings/surface/settings_scroller/"
                    "storage_mirror/toggle",
                .key = "disabled",
                .value = next_controls.mirror_disabled ? "true" : "false",
            },
            {
                .absolute_path =
                    "/qsee_settings/surface/settings_scroller/"
                    "auto_rotation/toggle",
                .key = "checked",
                .value = next_controls.rotation_checked ? "true" : "false",
            },
            {
                .absolute_path =
                    "/qsee_settings/surface/settings_scroller/"
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
    if (screen_ != Screen::Hardware ||
        !active_screen_document_id_.has_value()) {
        return std::unexpected(
            "SD format dialog requested without an active Hardware document"
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
        [this, document_id, generation]() {
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
            if (context_ != nullptr &&
                screen_ == Screen::Hardware &&
                is_current_screen_document(document_id, generation)) {
                if (auto refresh_result = refresh_hardware(*context_); !refresh_result) {
                    ESP_LOGE(
                        kTag,
                        "Hardware view refresh failed: %s",
                        refresh_result.error().c_str()
                    );
                }
            } else {
                ESP_LOGI(
                    kTag,
                    "Skipped Hardware refresh because its lazy document changed"
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
    "ai.qsee.device-console",
    qsee_app_provider_symbol
);

}  // namespace qsee
