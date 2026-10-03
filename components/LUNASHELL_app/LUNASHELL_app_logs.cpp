#include "LUNASHELL_app.hpp"
#include "LUNASHELL_app_internal.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_logger.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <utility>
#include "esp_log.h"

namespace {
using esp_brookesia::system::core::AppContext;
using LUNASHELL::app_detail::kTag;
using LUNASHELL::app_detail::LogFilter;
constexpr size_t kLogBufferBytes = 4096;
constexpr size_t kLogSnapshotLines = LUNASHELL_LOGGER_SNAPSHOT_MAX_LINES;
// line: One bounded logger line without its newline.
// query: Optional literal search text, never a regular expression.
// filter_mode: Typed severity selection; it keeps its meaning when controls are reordered.
bool log_line_matches(const char *line, const std::string &query, LogFilter filter_mode)
{
    const char *first = line;
    while (*first == ' ' || *first == '\t') {
        ++first;
    }
    if (filter_mode == LogFilter::Warnings && *first != 'W' && *first != 'w') {
        return false;
    }
    if (filter_mode == LogFilter::Errors && *first != 'E' && *first != 'e') {
        return false;
    }
    if (filter_mode == LogFilter::Info && *first != 'I' && *first != 'i') {
        return false;
    }
    return query.empty() || std::strstr(line, query.c_str()) != nullptr;
}

// source: Canonical logger snapshot, or null for an empty snapshot.
// query: Optional literal search text.
// filter_mode: Typed severity to match before copying a logger line.
// output: Caller-owned filtered text buffer.
// output_size: Writable capacity including the terminating zero.
void filter_logs(
    const char *source,
    const std::string &query,
    LogFilter filter_mode,
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
// Retain bounded buffers for the app lifetime, so navigation leaves the heap alone.
// Exceptions: std::bad_alloc if constructing an error message fails.
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

// Release only app-owned log buffers after producers have stopped.
void App::release_log_buffers()
{
    log_source_buffer_.reset();
    log_filtered_buffer_.reset();
}

// context: Active Brookesia document receiving the canonical logger text.
// Exceptions: std::bad_alloc if GUI/error text allocation fails.
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


}  // namespace LUNASHELL
