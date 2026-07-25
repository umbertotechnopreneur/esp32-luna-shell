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
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "esp_log.h"
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

bool storage_format_required(const qsee::StorageSnapshot &storage)
{
    return storage.card_present && !storage.formatted &&
           (storage.last_error == ESP_FAIL || storage.format_attempted);
}

const char *health_short(qsee::DeviceHealth health)
{
    switch (health) {
    case qsee::DeviceHealth::Ready:
        return "OK";
    case qsee::DeviceHealth::Degraded:
        return "!";
    case qsee::DeviceHealth::Missing:
        return "--";
    case qsee::DeviceHealth::Unknown:
    default:
        return "?";
    }
}

const char *log_filter_name(uint8_t mode)
{
    switch (mode) {
    case 1:
        return "WARN";
    case 2:
        return "ERROR";
    case 3:
        return "INFO";
    default:
        return "ALL";
    }
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
    auto result = context.gui().set_value(
        path,
        std::clamp(value, std::int32_t{0}, std::int32_t{100})
    );
    if (!result) {
        return std::unexpected(
            "Failed to update QSee value '" + std::string(path) + "': " + result.error()
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

    const std::vector<std::string> actions = {
        "qsee.open.welcome",
        "qsee.open.logs",
        "qsee.open.hardware",
        "qsee.open.audio",
        "qsee.open.imu",
        "qsee.open.about",
        "qsee.logs.back",
        "qsee.hardware.back",
        "qsee.audio.back",
        "qsee.imu.back",
        "qsee.about.back",
        "qsee.logs.pause",
        "qsee.logs.clear",
        "qsee.logs.follow",
        "qsee.logs.filter",
        "qsee.logs.search",
        "qsee.storage.format",
    };
    auto subscription_result = context.gui().subscribe_actions(actions);
    if (!subscription_result) {
        context_ = nullptr;
        return std::unexpected(
            "Failed to subscribe QSee UI actions: " + subscription_result.error()
        );
    }

    auto timer_result = context.timer().start_periodic(kRefreshTimer, 1000);
    if (!timer_result) {
        context_ = nullptr;
        return std::unexpected("Failed to start QSee refresh timer: " + timer_result.error());
    }
    refresh_timer_ = *timer_result;

    auto refresh_result = refresh(context);
    if (!refresh_result) {
        context.timer().stop(refresh_timer_);
        refresh_timer_ = esp_brookesia::system::core::INVALID_TIMER_ID;
        context_ = nullptr;
        return refresh_result;
    }

    ESP_LOGI(kTag, "QSee.ai native app started on Brookesia System 0.8");
    return {};
}

std::expected<void, std::string> App::on_stop(AppContext &context)
{
    if (refresh_timer_ != esp_brookesia::system::core::INVALID_TIMER_ID) {
        context.timer().stop(refresh_timer_);
        refresh_timer_ = esp_brookesia::system::core::INVALID_TIMER_ID;
    }
    if (search_keyboard_ != esp_brookesia::system::core::INVALID_KEYBOARD_REQUEST_ID) {
        (void)context.system_service().hide_keyboard(search_keyboard_);
        search_keyboard_ = esp_brookesia::system::core::INVALID_KEYBOARD_REQUEST_ID;
    }
    if (format_dialog_ != esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID) {
        (void)context.system_service().hide_message_dialog(format_dialog_);
        format_dialog_ = esp_brookesia::system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID;
    }

    context_ = nullptr;
    ESP_LOGI(kTag, "QSee.ai native app stopped");
    return {};
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
    return refresh(context);
}

std::expected<void, std::string> App::on_action(
    AppContext &context,
    std::string_view action
)
{
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
    if (action == "qsee.open.imu") {
        return open_screen(context, Screen::Imu, action);
    }
    if (action == "qsee.open.about") {
        return open_screen(context, Screen::About, action);
    }
    if (action == "qsee.logs.back" || action == "qsee.hardware.back" ||
        action == "qsee.audio.back" || action == "qsee.imu.back" ||
        action == "qsee.about.back") {
        return open_screen(context, Screen::Welcome, "qsee.open.welcome");
    }
    if (action == "qsee.logs.pause") {
        logs_paused_ = !logs_paused_;
        return refresh_logs(context);
    }
    if (action == "qsee.logs.clear") {
        qsee_logger_clear();
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

std::expected<void, std::string> App::refresh(AppContext &context)
{
    switch (screen_) {
    case Screen::Welcome:
        return refresh_welcome(context);
    case Screen::Logs:
        return refresh_logs(context);
    case Screen::Hardware:
        return refresh_hardware(context);
    case Screen::Audio:
        return refresh_audio(context);
    case Screen::Imu:
        return refresh_imu(context);
    case Screen::About:
        return {};
    }
    return {};
}

std::expected<void, std::string> App::refresh_welcome(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    std::array<char, 192> text{};

    std::snprintf(
        text.data(),
        text.size(),
        "RAM %lu KB free | min %lu KB",
        static_cast<unsigned long>(snapshot.memory.heap_free_bytes / 1024U),
        static_cast<unsigned long>(snapshot.memory.heap_minimum_free_bytes / 1024U)
    );
    if (auto result = set_text(
            context,
            "/qsee_welcome/surface/ram/ram_value",
            text.data()
        ); !result) {
        return result;
    }
    const int ram_percent = snapshot.memory.psram_heap_total_bytes > 0
        ? static_cast<int>(
              (static_cast<uint64_t>(snapshot.memory.psram_free_bytes) * 100U) /
              snapshot.memory.psram_heap_total_bytes
          )
        : 0;
    if (auto result = set_value(
            context,
            "/qsee_welcome/surface/ram/ram_bar",
            ram_percent
        ); !result) {
        return result;
    }

    int storage_percent = 0;
    if (snapshot.storage.health == DeviceHealth::Ready &&
        snapshot.storage.filesystem_total_bytes > 0) {
        storage_percent = static_cast<int>(
            (snapshot.storage.free_bytes * 100ULL) / snapshot.storage.filesystem_total_bytes
        );
        std::snprintf(
            text.data(),
            text.size(),
            "SD %llu / %llu MB free%s",
            static_cast<unsigned long long>(snapshot.storage.free_bytes / (1024ULL * 1024ULL)),
            static_cast<unsigned long long>(
                snapshot.storage.filesystem_total_bytes / (1024ULL * 1024ULL)
            ),
            snapshot.storage.logger_mirror_enabled ? " | LOG" : ""
        );
    } else if (storage_format_required(snapshot.storage)) {
        std::snprintf(text.data(), text.size(), "SD requires explicit format");
    } else if (!snapshot.storage.card_present) {
        std::snprintf(text.data(), text.size(), "SD card absent");
    } else {
        std::snprintf(text.data(), text.size(), "SD not mounted");
    }
    if (auto result = set_text(
            context,
            "/qsee_welcome/surface/sd/sd_value",
            text.data()
        ); !result) {
        return result;
    }
    if (auto result = set_value(
            context,
            "/qsee_welcome/surface/sd/sd_bar",
            storage_percent
        ); !result) {
        return result;
    }

    std::snprintf(
        text.data(),
        text.size(),
        "WiFi %s  BLE %s  GPS %s  SD %s",
        health_short(snapshot.radios.wifi.health),
        health_short(snapshot.radios.ble.health),
        health_short(snapshot.gps.health),
        storage_format_required(snapshot.storage) ? "!" : health_short(snapshot.storage.health)
    );
    if (auto result = set_text(
            context,
            "/qsee_welcome/surface/radio_status",
            text.data()
        ); !result) {
        return result;
    }

    std::snprintf(
        text.data(),
        text.size(),
        "BAT %s%s  |  snapshot #%lu",
        snapshot.battery.level_percent >= 0
            ? std::to_string(snapshot.battery.level_percent).c_str()
            : "--",
        snapshot.battery.level_percent >= 0 ? "%" : "",
        static_cast<unsigned long>(snapshot.sequence)
    );
    return set_text(context, "/qsee_welcome/surface/device_status", text.data());
}

std::expected<void, std::string> App::refresh_logs(AppContext &context)
{
    std::array<char, 8192> source{};
    std::array<char, 8192> filtered{};
    if (!logs_paused_) {
        qsee_logger_snapshot(source.data(), source.size(), 64);
        filter_logs(
            source.data(),
            log_query_,
            log_filter_mode_,
            filtered.data(),
            filtered.size()
        );
        if (auto result = set_text(
                context,
                "/qsee_logs/surface/log_scroller/log_text",
                filtered.data()
            ); !result) {
            return result;
        }
    }

    if (auto result = set_text(
            context,
            "/qsee_logs/surface/controls/pause/label",
            logs_paused_ ? "RESUME" : "PAUSE"
        );
        !result) {
        return result;
    }
    if (auto result = set_text(
            context,
            "/qsee_logs/surface/controls/follow/label",
            logs_follow_latest_ ? "FOLLOW ON" : "FOLLOW OFF"
        );
        !result) {
        return result;
    }

    std::array<char, 64> label{};
    std::snprintf(label.data(), label.size(), "FILTER %s", log_filter_name(log_filter_mode_));
    if (auto result = set_text(
            context,
            "/qsee_logs/surface/controls/filter/label",
            label.data()
        ); !result) {
        return result;
    }
    std::snprintf(
        label.data(),
        label.size(),
        "SEARCH %s",
        log_query_.empty() ? "ALL" : log_query_.c_str()
    );
    if (auto result = set_text(
            context,
            "/qsee_logs/surface/controls/search/label",
            label.data()
        ); !result) {
        return result;
    }

    if (logs_follow_latest_ && !logs_paused_) {
        (void)context.gui().scroll_to(
            "/qsee_logs/surface/log_scroller",
            0,
            32767,
            false
        );
    }
    return {};
}

std::expected<void, std::string> App::refresh_hardware(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    std::array<char, 2048> details{};
    std::snprintf(
        details.data(),
        details.size(),
        "BATTERY     %-8s %d%%  %dmV\n"
        "RTC         %-8s %04u-%02u-%02u %02u:%02u\n"
        "IMU         %-8s samples %u\n"
        "GPS         %-8s fix %s / sats %u\n"
        "WIFI        %-8s %s\n"
        "BLE         %-8s %s\n"
        "DISPLAY     %-8s\n"
        "TOUCH       %-8s\n"
        "SPEAKER     %-8s\n"
        "MICROPHONE  %-8s\n"
        "SD CARD     %-8s attempts %lu",
        deviceHealthName(snapshot.battery.health),
        snapshot.battery.level_percent,
        snapshot.battery.battery_mv,
        deviceHealthName(snapshot.rtc.health),
        static_cast<unsigned>(snapshot.rtc.year),
        static_cast<unsigned>(snapshot.rtc.month),
        static_cast<unsigned>(snapshot.rtc.day),
        static_cast<unsigned>(snapshot.rtc.hour),
        static_cast<unsigned>(snapshot.rtc.minute),
        deviceHealthName(snapshot.imu.health),
        static_cast<unsigned>(snapshot.imu.calibration_samples),
        deviceHealthName(snapshot.gps.health),
        snapshot.gps.has_fix ? "yes" : "no",
        static_cast<unsigned>(snapshot.gps.satellites),
        deviceHealthName(snapshot.radios.wifi.health),
        snapshot.radios.wifi.connected ? snapshot.radios.wifi.ssid : "not connected",
        deviceHealthName(snapshot.radios.ble.health),
        snapshot.radios.ble.connected
            ? "connected"
            : snapshot.radios.ble.advertising ? "advertising" : "idle",
        deviceHealthName(snapshot.ui.display.health),
        deviceHealthName(snapshot.ui.touch.health),
        deviceHealthName(snapshot.audio.speaker.health),
        deviceHealthName(snapshot.audio.microphone.health),
        storage_format_required(snapshot.storage)
            ? "FORMAT REQUIRED"
            : deviceHealthName(snapshot.storage.health),
        static_cast<unsigned long>(snapshot.storage.mount_attempt_count)
    );
    if (auto result = set_text(
            context,
            "/qsee_hardware/surface/hardware_scroller/hardware_text",
            details.data()
        ); !result) {
        return result;
    }

    const char *format_label = storage_format_required(snapshot.storage)
        ? "FORMAT SD"
        : snapshot.storage.health == DeviceHealth::Ready ? "SD READY" : "CHECK SD";
    return set_text(
        context,
        "/qsee_hardware/surface/actions/format/label",
        format_label
    );
}

std::expected<void, std::string> App::refresh_audio(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    std::array<char, 192> text{};
    std::snprintf(
        text.data(),
        text.size(),
        "MICROPHONES %s | PCM %u ch @ %lu Hz",
        deviceHealthName(snapshot.audio.microphone.health),
        static_cast<unsigned>(snapshot.audio.channel_count),
        static_cast<unsigned long>(snapshot.audio.sample_rate_hz)
    );
    if (auto result = set_text(
            context,
            "/qsee_audio/surface/audio_status",
            text.data()
        ); !result) {
        return result;
    }

    const int level0 =
        std::min(100, static_cast<int>(snapshot.audio.channel_mean_level[0] / 328U));
    const int level1 =
        std::min(100, static_cast<int>(snapshot.audio.channel_mean_level[1] / 328U));
    if (auto result = set_value(context, "/qsee_audio/surface/mic0_bar", level0); !result) {
        return result;
    }
    if (auto result = set_value(context, "/qsee_audio/surface/mic1_bar", level1); !result) {
        return result;
    }

    std::snprintf(
        text.data(),
        text.size(),
        "CH0 mean %lu peak %lu\nCH1 mean %lu peak %lu",
        static_cast<unsigned long>(snapshot.audio.channel_mean_level[0]),
        static_cast<unsigned long>(snapshot.audio.channel_peak_level[0]),
        static_cast<unsigned long>(snapshot.audio.channel_mean_level[1]),
        static_cast<unsigned long>(snapshot.audio.channel_peak_level[1])
    );
    return set_text(context, "/qsee_audio/surface/audio_values", text.data());
}

std::expected<void, std::string> App::refresh_imu(AppContext &context)
{
    const DeviceSnapshot snapshot = DeviceSnapshotService::instance().snapshot();
    std::array<char, 512> text{};
    std::snprintf(
        text.data(),
        text.size(),
        "QMI8658 %s\n\n"
        "ACC\nX %+.2f  Y %+.2f  Z %+.2f m/s2\n\n"
        "GYRO\nX %+.2f  Y %+.2f  Z %+.2f dps\n\n"
        "TEMP %.1f C\n"
        "CALIBRATION %s | %u samples",
        deviceHealthName(snapshot.imu.health),
        snapshot.imu.acceleration_mps2[0],
        snapshot.imu.acceleration_mps2[1],
        snapshot.imu.acceleration_mps2[2],
        snapshot.imu.angular_velocity_dps[0],
        snapshot.imu.angular_velocity_dps[1],
        snapshot.imu.angular_velocity_dps[2],
        snapshot.imu.temperature_c,
        snapshot.imu.calibrated ? "READY" : "PENDING",
        static_cast<unsigned>(snapshot.imu.calibration_samples)
    );
    return set_text(context, "/qsee_imu/surface/imu_values", text.data());
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
        MessageDialogOptions info = {
            .text = "SD format is not available",
            .informative_text = storage.health == DeviceHealth::Ready
                ? "The card is already mounted. No format is needed."
                : storage.card_present
                    ? "The card has not reported a format-required state."
                    : "No SD card is present.",
            .icon = MessageDialogIcon::Information,
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
