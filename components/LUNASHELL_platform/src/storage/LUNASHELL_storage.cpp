/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"
#if CONFIG_LUNASHELL_TTS_DATA_SD
#include "LUNASHELL_tts_storage.h"
#include "psa/crypto.h"
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#if CONFIG_LUNASHELL_TTS_DATA_SD
#include "freertos/semphr.h"
#endif
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "LUNASHELL_liveness.h"
#include "LUNASHELL_logger.h"
#include "LUNASHELL_settings.hpp"

namespace {

const char *TAG = "LUNASHELL_storage";
constexpr char kLoggerMirrorSetting[] = "sd_mirror";
// Keep the protocol-level card check responsive, but coalesce expensive FAT
// space walks after writes and refresh an otherwise idle measurement at 60 s.
constexpr uint32_t kStorageHealthPollMs = 1000;
constexpr uint32_t kStorageSpaceWriteRefreshMs = 10000;
constexpr uint32_t kStorageSpaceRefreshMs = 60000;
constexpr uint32_t kInitialRetrySeconds = 15;
constexpr uint32_t kMaximumRetrySeconds = 120;
constexpr uint32_t kPowerSuspendTimeoutMs = 5000;
constexpr uint32_t kStorageFormatWatchdogDeadlineMs = 12000;
static_assert(
    CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000U ==
        kStorageFormatWatchdogDeadlineMs,
    "The identity-bound SD format policy requires its reviewed hard deadline"
);
constexpr EventBits_t kWorkerQuiescentBit =
    static_cast<EventBits_t>(1U << 0);
constexpr EventBits_t kWorkerResumeCompleteBit =
    static_cast<EventBits_t>(1U << 1);
constexpr uint16_t kScreenshotWidth = 466;
constexpr uint16_t kScreenshotHeight = 466;
constexpr size_t kBmpHeaderBytes = 54;
constexpr size_t kBmpConversionPixels = 128;

struct ScreenshotWriteRequest {
    uint16_t *pixels = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t stride = 0;
    bool byte_swapped = false;
    uint32_t capture_id = 0;
    LUNASHELL_screenshot_completion_t completion = nullptr;
    void *context = nullptr;
};

std::mutex s_storage_mutex;
std::mutex s_power_transition_mutex;
TaskHandle_t s_storage_task;
EventGroupHandle_t s_storage_events;
StaticEventGroup_t s_storage_event_storage{};
uint32_t s_mount_attempt_count;
bool s_mounted;
bool s_terminal_failure;
bool s_waiting_for_format_authorization;
bool s_logger_mirror_requested = true;
bool s_performance_suspended;
bool s_power_suspended;
bool s_power_resume_requested;
esp_err_t s_power_resume_error = ESP_ERR_INVALID_STATE;
bool s_logger_mirror_enabled;
esp_err_t s_last_logged_mirror_error = ESP_ERR_INVALID_STATE;
uint32_t s_last_logged_mirror_failure_count;
int64_t s_last_logged_mirror_changed_at_us;
bool s_format_authorization_armed;
bsp_sdcard_identity_t s_authorized_identity;
bool s_format_candidate_valid;
bsp_sdcard_identity_t s_format_candidate_identity;
uint32_t s_format_candidate_attempt;
LUNASHELL::StorageMountReport s_last_mount_report;
bool s_space_refresh_tick_valid;
TickType_t s_last_space_refresh_tick;
uint32_t s_last_space_write_sequence;
bool s_space_refresh_requested;
ScreenshotWriteRequest s_screenshot_request;
bool s_screenshot_pending;
bool s_screenshot_active;

// destination: Writable little-endian byte range.
// value: Unsigned 16-bit value to encode.
void write_little_endian_16(uint8_t *destination, uint16_t value)
{
    destination[0] = static_cast<uint8_t>(value & 0xFFU);
    destination[1] = static_cast<uint8_t>(value >> 8U);
}

// destination: Writable little-endian byte range.
// value: Unsigned 32-bit value to encode.
void write_little_endian_32(uint8_t *destination, uint32_t value)
{
    destination[0] = static_cast<uint8_t>(value & 0xFFU);
    destination[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    destination[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    destination[3] = static_cast<uint8_t>(value >> 24U);
}

// path: Exact storage-owner directory to create or verify.
// Exception: Returns ESP_FAIL and logs the original errno when the path is unusable.
esp_err_t ensure_screenshot_directory(const char *path)
{
    if (mkdir(path, 0775) == 0) {
        return ESP_OK;
    }
    const int mkdir_errno = errno;
    if (mkdir_errno != EEXIST) {
        ESP_LOGE(TAG, "SD screenshot directory '%s' failed: errno=%d (%s)",
                 path, mkdir_errno, esp_err_to_name(ESP_FAIL));
        return ESP_FAIL;
    }
    struct stat status = {};
    const int stat_result = stat(path, &status);
    if (stat_result != 0 || !S_ISDIR(status.st_mode)) {
        const int stat_errno = stat_result != 0 ? errno : ENOTDIR;
        ESP_LOGE(TAG, "SD screenshot path '%s' is not a directory: errno=%d (%s)",
                 path, stat_errno, esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

// file: Open screenshot temporary file.
// data: Readable bytes to append.
// size: Exact byte count required for success.
// operation: Bounded stage name included in failure logs.
// Exception: Returns ESP_FAIL and logs the original errno or short-write state.
esp_err_t write_screenshot_bytes(FILE *file, const void *data, size_t size,
                                 const char *operation)
{
    const size_t written = std::fwrite(data, 1, size, file);
    if (written == size) {
        return ESP_OK;
    }
    const int write_errno = errno;
    const int stream_error = std::ferror(file);
    ESP_LOGE(TAG, "SD screenshot %s failed: errno=%d ferror=%d written=%u requested=%u (%s)",
             operation, write_errno, stream_error, static_cast<unsigned>(written),
             static_cast<unsigned>(size),
             esp_err_to_name(ESP_FAIL));
    return ESP_FAIL;
}

// request: Storage-owner screenshot request with a complete RGB565 frame.
// Exception: Returns the real directory, file, write, flush, sync or rename failure.
esp_err_t write_screenshot_file(const ScreenshotWriteRequest &request)
{
    char app_directory[96] = {};
    char screenshot_directory[128] = {};
    char temporary_path[192] = {};
    char final_path[192] = {};
    const long long timestamp_us = static_cast<long long>(esp_timer_get_time());
    const int app_length = std::snprintf(
        app_directory, sizeof(app_directory), "%s/LUNASHELL", BSP_SD_MOUNT_POINT
    );
    const int directory_length = std::snprintf(
        screenshot_directory, sizeof(screenshot_directory), "%s/screenshots", app_directory
    );
    const int final_length = std::snprintf(
        final_path, sizeof(final_path), "%s/CODY-%013lld-%06lu.bmp",
        screenshot_directory, timestamp_us, static_cast<unsigned long>(request.capture_id)
    );
    const int temporary_length = std::snprintf(
        temporary_path, sizeof(temporary_path), "%s/.CODY-%013lld-%06lu.tmp",
        screenshot_directory, timestamp_us, static_cast<unsigned long>(request.capture_id)
    );
    if (app_length <= 0 || static_cast<size_t>(app_length) >= sizeof(app_directory) ||
        directory_length <= 0 || static_cast<size_t>(directory_length) >= sizeof(screenshot_directory) ||
        final_length <= 0 || static_cast<size_t>(final_length) >= sizeof(final_path) ||
        temporary_length <= 0 || static_cast<size_t>(temporary_length) >= sizeof(temporary_path)) {
        ESP_LOGE(TAG, "SD screenshot path formatting failed (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t result = ensure_screenshot_directory(app_directory);
    if (result == ESP_OK) {
        result = ensure_screenshot_directory(screenshot_directory);
    }
    if (result != ESP_OK) {
        return result;
    }

    errno = 0;
    const int descriptor = open(temporary_path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (descriptor < 0) {
        const int open_errno = errno;
        ESP_LOGE(TAG, "SD screenshot temporary file '%s' failed: errno=%d (%s)",
                 temporary_path, open_errno, esp_err_to_name(ESP_FAIL));
        return ESP_FAIL;
    }
    FILE *file = fdopen(descriptor, "wb");
    if (file == nullptr) {
        const int stream_errno = errno;
        close(descriptor);
        unlink(temporary_path);
        ESP_LOGE(TAG, "SD screenshot stream failed: errno=%d (%s)",
                 stream_errno, esp_err_to_name(ESP_FAIL));
        return ESP_FAIL;
    }

    const uint32_t row_bytes = (static_cast<uint32_t>(request.width) * 3U + 3U) & ~3U;
    const uint32_t image_bytes = row_bytes * request.height;
    std::array<uint8_t, kBmpHeaderBytes> header{};
    header[0] = 'B';
    header[1] = 'M';
    write_little_endian_32(header.data() + 2, static_cast<uint32_t>(kBmpHeaderBytes) + image_bytes);
    write_little_endian_32(header.data() + 10, static_cast<uint32_t>(kBmpHeaderBytes));
    write_little_endian_32(header.data() + 14, 40U);
    write_little_endian_32(header.data() + 18, request.width);
    write_little_endian_32(
        header.data() + 22,
        static_cast<uint32_t>(-static_cast<int32_t>(request.height))
    );
    write_little_endian_16(header.data() + 26, 1U);
    write_little_endian_16(header.data() + 28, 24U);
    write_little_endian_32(header.data() + 34, image_bytes);
    result = write_screenshot_bytes(file, header.data(), header.size(), "header write");

    std::array<uint8_t, kBmpConversionPixels * 3U> converted{};
    const std::array<uint8_t, 3> padding{};
    for (uint16_t row = 0; result == ESP_OK && row < request.height; ++row) {
        const uint16_t *source = request.pixels + static_cast<size_t>(row) * request.stride;
        for (uint16_t first = 0; first < request.width; first += kBmpConversionPixels) {
            const uint16_t count = std::min<uint16_t>(
                kBmpConversionPixels, request.width - first
            );
            for (uint16_t index = 0; index < count; ++index) {
                uint16_t native = source[first + index];
                if (request.byte_swapped) {
                    native = static_cast<uint16_t>((native >> 8U) | (native << 8U));
                }
                const uint8_t red = static_cast<uint8_t>((((native >> 11U) & 0x1FU) * 255U + 15U) / 31U);
                const uint8_t green = static_cast<uint8_t>((((native >> 5U) & 0x3FU) * 255U + 31U) / 63U);
                const uint8_t blue = static_cast<uint8_t>(((native & 0x1FU) * 255U + 15U) / 31U);
                converted[index * 3U] = blue;
                converted[index * 3U + 1U] = green;
                converted[index * 3U + 2U] = red;
            }
            result = write_screenshot_bytes(
                file, converted.data(), static_cast<size_t>(count) * 3U, "pixel write"
            );
            if (result != ESP_OK) {
                break;
            }
        }
        const size_t padding_bytes = row_bytes - static_cast<uint32_t>(request.width) * 3U;
        if (result == ESP_OK && padding_bytes != 0) {
            result = write_screenshot_bytes(file, padding.data(), padding_bytes, "row padding");
        }
    }
    if (result == ESP_OK && std::fflush(file) != 0) {
        ESP_LOGE(TAG, "SD screenshot flush failed: errno=%d (%s)", errno, esp_err_to_name(ESP_FAIL));
        result = ESP_FAIL;
    }
    if (result == ESP_OK && fsync(fileno(file)) != 0) {
        ESP_LOGE(TAG, "SD screenshot sync failed: errno=%d (%s)", errno, esp_err_to_name(ESP_FAIL));
        result = ESP_FAIL;
    }
    if (std::fclose(file) != 0 && result == ESP_OK) {
        ESP_LOGE(TAG, "SD screenshot close failed: errno=%d (%s)", errno, esp_err_to_name(ESP_FAIL));
        result = ESP_FAIL;
    }
    if (result == ESP_OK && rename(temporary_path, final_path) != 0) {
        ESP_LOGE(TAG, "SD screenshot publish '%s' failed: errno=%d (%s)",
                 final_path, errno, esp_err_to_name(ESP_FAIL));
        result = ESP_FAIL;
    }
    if (result != ESP_OK) {
        if (unlink(temporary_path) != 0 && errno != ENOENT) {
            ESP_LOGE(TAG, "SD screenshot temporary cleanup failed: errno=%d", errno);
        }
        return result;
    }
    ESP_LOGI(TAG, "SD screenshot saved: %s", final_path);
    return ESP_OK;
}

esp_err_t logger_watch_begin()
{
    return LUNASHELL_liveness_watch_begin(LUNASHELL_LIVENESS_LOGGER);
}

esp_err_t logger_watch_end(esp_err_t operation_result)
{
    return LUNASHELL_liveness_watch_end(
        LUNASHELL_LIVENESS_LOGGER,
        operation_result
    );
}

constexpr LUNASHELL_logger_io_watch_t kLoggerIoWatch{
    .begin = logger_watch_begin,
    .end = logger_watch_end,
};

template <typename Operation>
esp_err_t run_watched_storage_io(
    LUNASHELL_liveness_point_t point,
    const char *operation_name,
    Operation operation
)
{
    const esp_err_t watch_error = LUNASHELL_liveness_watch_begin(point);
    if (watch_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "SD %s watchdog start failed (%s)",
            operation_name,
            esp_err_to_name(watch_error)
        );
        return watch_error;
    }
    const esp_err_t operation_error = operation();
    return LUNASHELL_liveness_watch_end(point, operation_error);
}

// Combine user intent with independent runtime suspension reasons.
bool logger_mirror_should_run_locked()
{
    return s_logger_mirror_requested &&
        !s_performance_suspended &&
        !s_power_suspended;
}

struct MountAttempt {
    uint32_t number;
    bsp_sdcard_mount_request_t request;
};

enum class MountFailureDisposition : uint8_t {
    Retry,
    WaitForFormatAuthorization,
    Terminal,
};

bool card_transport_error_is_explicitly_transient(esp_err_t error)
{
    switch (error) {
    case ESP_ERR_TIMEOUT:
    case ESP_ERR_INVALID_RESPONSE:
    case ESP_ERR_INVALID_CRC:
    case ESP_ERR_NOT_FOUND:
        return true;
    default:
        return false;
    }
}

bool filesystem_error_is_explicitly_transient(
    const LUNASHELL::StorageMountReport &report
)
{
    if (report.stage != LUNASHELL::StorageMountStage::FatMount ||
        !report.fatfs_result_valid) {
        return false;
    }

    switch (static_cast<FRESULT>(report.fatfs_result)) {
    case FR_DISK_ERR:
    case FR_NOT_READY:
    case FR_TIMEOUT:
        return true;
    default:
        return false;
    }
}

void record_first_error(esp_err_t error, esp_err_t *aggregate)
{
    if (aggregate != nullptr && *aggregate == ESP_OK && error != ESP_OK) {
        *aggregate = error;
    }
}

[[noreturn]] void invalid_bsp_enum(const char *kind, int value)
{
    ESP_LOGE(TAG, "Unknown BSP SD %s value %d", kind, value);
    std::abort();
}

LUNASHELL::StorageMountOutcome map_outcome(
    bsp_sdcard_mount_outcome_t outcome
)
{
    switch (outcome) {
    case BSP_SDCARD_MOUNT_OUTCOME_MOUNTED:
        return LUNASHELL::StorageMountOutcome::Mounted;
    case BSP_SDCARD_MOUNT_OUTCOME_CARD_OR_TRANSPORT_FAILED:
        return LUNASHELL::StorageMountOutcome::CardOrTransportFailed;
    case BSP_SDCARD_MOUNT_OUTCOME_FORMAT_REQUIRED:
        return LUNASHELL::StorageMountOutcome::FormatRequired;
    case BSP_SDCARD_MOUNT_OUTCOME_FILESYSTEM_FAILED:
        return LUNASHELL::StorageMountOutcome::FilesystemFailed;
    case BSP_SDCARD_MOUNT_OUTCOME_FORMAT_FAILED:
        return LUNASHELL::StorageMountOutcome::FormatFailed;
    case BSP_SDCARD_MOUNT_OUTCOME_INTERNAL_FAILED:
        return LUNASHELL::StorageMountOutcome::InternalFailed;
    }
    invalid_bsp_enum("outcome", static_cast<int>(outcome));
}

LUNASHELL::StorageMountStage map_stage(bsp_sdcard_mount_stage_t stage)
{
    switch (stage) {
    case BSP_SDCARD_STAGE_NONE:
        return LUNASHELL::StorageMountStage::None;
    case BSP_SDCARD_STAGE_REQUEST:
        return LUNASHELL::StorageMountStage::Request;
    case BSP_SDCARD_STAGE_HOST_INIT:
        return LUNASHELL::StorageMountStage::HostInit;
    case BSP_SDCARD_STAGE_SLOT_INIT:
        return LUNASHELL::StorageMountStage::SlotInit;
    case BSP_SDCARD_STAGE_CARD_INIT:
        return LUNASHELL::StorageMountStage::CardInit;
    case BSP_SDCARD_STAGE_CARD_IDENTITY:
        return LUNASHELL::StorageMountStage::CardIdentity;
    case BSP_SDCARD_STAGE_DRIVE_ALLOC:
        return LUNASHELL::StorageMountStage::DriveAlloc;
    case BSP_SDCARD_STAGE_DISKIO_REGISTER:
        return LUNASHELL::StorageMountStage::DiskIoRegister;
    case BSP_SDCARD_STAGE_VFS_REGISTER:
        return LUNASHELL::StorageMountStage::VfsRegister;
    case BSP_SDCARD_STAGE_FAT_MOUNT:
        return LUNASHELL::StorageMountStage::FatMount;
    case BSP_SDCARD_STAGE_PRE_FORMAT_UNMOUNT:
        return LUNASHELL::StorageMountStage::PreFormatUnmount;
    case BSP_SDCARD_STAGE_PARTITION:
        return LUNASHELL::StorageMountStage::Partition;
    case BSP_SDCARD_STAGE_FORMAT:
        return LUNASHELL::StorageMountStage::Format;
    case BSP_SDCARD_STAGE_POST_FORMAT_MOUNT:
        return LUNASHELL::StorageMountStage::PostFormatMount;
    }
    invalid_bsp_enum("stage", static_cast<int>(stage));
}

LUNASHELL::StorageCleanupStage map_cleanup_stage(
    bsp_sdcard_cleanup_stage_t stage
)
{
    switch (stage) {
    case BSP_SDCARD_CLEANUP_NONE:
        return LUNASHELL::StorageCleanupStage::None;
    case BSP_SDCARD_CLEANUP_FAT_UNMOUNT:
        return LUNASHELL::StorageCleanupStage::FatUnmount;
    case BSP_SDCARD_CLEANUP_VFS_UNREGISTER:
        return LUNASHELL::StorageCleanupStage::VfsUnregister;
    case BSP_SDCARD_CLEANUP_HOST_DEINIT:
        return LUNASHELL::StorageCleanupStage::HostDeinit;
    }
    invalid_bsp_enum("cleanup stage", static_cast<int>(stage));
}

const char *outcome_name(LUNASHELL::StorageMountOutcome outcome)
{
    switch (outcome) {
    case LUNASHELL::StorageMountOutcome::NotAttempted:
        return "not-attempted";
    case LUNASHELL::StorageMountOutcome::Mounted:
        return "mounted";
    case LUNASHELL::StorageMountOutcome::CardOrTransportFailed:
        return "card-or-transport-failed";
    case LUNASHELL::StorageMountOutcome::FormatRequired:
        return "format-required";
    case LUNASHELL::StorageMountOutcome::FilesystemFailed:
        return "filesystem-failed";
    case LUNASHELL::StorageMountOutcome::FormatFailed:
        return "format-failed";
    case LUNASHELL::StorageMountOutcome::InternalFailed:
        return "internal-failed";
    }
    invalid_bsp_enum("storage outcome", static_cast<int>(outcome));
}

MountFailureDisposition classify_mount_failure(
    const LUNASHELL::StorageMountReport &report
)
{
    if (report.poisoned ||
        !report.cleanup_complete ||
        report.error == ESP_ERR_INVALID_STATE) {
        return MountFailureDisposition::Terminal;
    }

    switch (report.outcome) {
    case LUNASHELL::StorageMountOutcome::CardOrTransportFailed:
        return report.stage == LUNASHELL::StorageMountStage::CardInit &&
                       card_transport_error_is_explicitly_transient(
                           report.error
                       )
                   ? MountFailureDisposition::Retry
                   : MountFailureDisposition::Terminal;
    case LUNASHELL::StorageMountOutcome::FilesystemFailed:
        return !report.format_attempted &&
                       !report.partition_written &&
                       !report.formatted &&
                       filesystem_error_is_explicitly_transient(report)
                   ? MountFailureDisposition::Retry
                   : MountFailureDisposition::Terminal;
    case LUNASHELL::StorageMountOutcome::FormatRequired:
        return MountFailureDisposition::WaitForFormatAuthorization;
    case LUNASHELL::StorageMountOutcome::FormatFailed:
    case LUNASHELL::StorageMountOutcome::InternalFailed:
    case LUNASHELL::StorageMountOutcome::NotAttempted:
    case LUNASHELL::StorageMountOutcome::Mounted:
        return MountFailureDisposition::Terminal;
    }
    invalid_bsp_enum(
        "storage failure disposition outcome",
        static_cast<int>(report.outcome)
    );
}

const char *stage_name(LUNASHELL::StorageMountStage stage)
{
    switch (stage) {
    case LUNASHELL::StorageMountStage::None:
        return "none";
    case LUNASHELL::StorageMountStage::Request:
        return "request";
    case LUNASHELL::StorageMountStage::HostInit:
        return "host-init";
    case LUNASHELL::StorageMountStage::SlotInit:
        return "slot-init";
    case LUNASHELL::StorageMountStage::CardInit:
        return "card-init";
    case LUNASHELL::StorageMountStage::CardIdentity:
        return "card-identity";
    case LUNASHELL::StorageMountStage::DriveAlloc:
        return "drive-alloc";
    case LUNASHELL::StorageMountStage::DiskIoRegister:
        return "diskio-register";
    case LUNASHELL::StorageMountStage::VfsRegister:
        return "vfs-register";
    case LUNASHELL::StorageMountStage::FatMount:
        return "fat-mount";
    case LUNASHELL::StorageMountStage::PreFormatUnmount:
        return "pre-format-unmount";
    case LUNASHELL::StorageMountStage::Partition:
        return "partition";
    case LUNASHELL::StorageMountStage::Format:
        return "format";
    case LUNASHELL::StorageMountStage::PostFormatMount:
        return "post-format-mount";
    case LUNASHELL::StorageMountStage::RuntimeStatus:
        return "runtime-status";
    case LUNASHELL::StorageMountStage::RuntimeFilesystem:
        return "runtime-filesystem";
    case LUNASHELL::StorageMountStage::RuntimeUnmount:
        return "runtime-unmount";
    }
    invalid_bsp_enum("storage stage", static_cast<int>(stage));
}

void clear_format_candidate_locked()
{
    s_format_candidate_valid = false;
    s_format_candidate_identity = {};
    s_format_candidate_attempt = 0;
}

void publish_mount_report_locked(LUNASHELL::StorageMountReport report)
{
    report.logger_mirror_requested = s_logger_mirror_requested;
    report.logger_mirror_enabled = s_logger_mirror_enabled;
    s_last_mount_report = report;
    LUNASHELL::DeviceSnapshotService::instance().reportStorage(report);
}

LUNASHELL::StorageMountReport make_mount_report(
    const bsp_sdcard_mount_result_t &result,
    uint32_t attempt_number
)
{
    LUNASHELL::StorageMountReport report;
    report.outcome = map_outcome(result.outcome);
    report.stage = map_stage(result.stage);
    report.cleanup_stage = map_cleanup_stage(result.cleanup_stage);
    report.error = result.error;
    report.cleanup_error = result.cleanup_error;
    report.initial_fatfs_result_valid =
        result.initial_mount_result_valid;
    report.initial_fatfs_result =
        static_cast<int32_t>(result.initial_mount_result);
    report.fatfs_result_valid = result.fatfs_result_valid;
    report.fatfs_result = static_cast<int32_t>(result.fatfs_result);
    report.cleanup_fatfs_result_valid =
        result.cleanup_fatfs_result_valid;
    report.cleanup_fatfs_result =
        static_cast<int32_t>(result.cleanup_fatfs_result);
    report.cleanup_complete = result.cleanup_complete;
    report.poisoned = result.poisoned;
    report.mounted = result.mounted;
    report.card_response_verified =
        result.card_initialized && result.observed_identity_valid;
    report.format_required =
        result.outcome == BSP_SDCARD_MOUNT_OUTCOME_FORMAT_REQUIRED &&
        report.card_response_verified &&
        result.cleanup_complete &&
        !result.poisoned;
    report.authorization_consumed = result.authorization_consumed;
    report.authorization_identity_matched =
        result.authorization_identity_matched;
    report.format_attempted = result.format_attempted;
    report.partition_written = result.partition_written;
    report.formatted = result.formatted;
    report.mount_attempt_count = attempt_number;
    if (result.observed_identity_valid) {
        report.capacity_bytes =
            result.observed_identity.capacity_bytes;
    }
    return report;
}

esp_err_t refresh_logger_status_locked(
    LUNASHELL_logger_sd_status_t *observed_status = nullptr
)
{
    LUNASHELL_logger_sd_status_t status = {};
    const esp_err_t query_error = LUNASHELL_logger_get_sd_status(&status);
    if (query_error != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportStorageMirror(
            s_logger_mirror_requested,
            s_logger_mirror_enabled,
            query_error,
            0,
            0,
            0
        );
        if (query_error != s_last_logged_mirror_error) {
            ESP_LOGE(
                TAG,
                "Reading asynchronous SD logger status failed (%s)",
                esp_err_to_name(query_error)
            );
            s_last_logged_mirror_error = query_error;
        }
        return query_error;
    }

    if (observed_status != nullptr) {
        *observed_status = status;
    }
    s_logger_mirror_enabled = status.enabled;
    esp_err_t reported_error = status.last_error;
    const bool should_run = logger_mirror_should_run_locked();
    esp_err_t action_error = should_run ? status.last_error : ESP_OK;
    if (s_mounted && status.enabled != should_run) {
        action_error = status.last_error != ESP_OK
            ? status.last_error
            : ESP_ERR_INVALID_STATE;
        if (reported_error == ESP_OK) {
            reported_error = action_error;
        }
    }
    LUNASHELL::DeviceSnapshotService::instance().reportStorageMirror(
        s_logger_mirror_requested,
        status.enabled,
        reported_error,
        status.last_errno,
        status.changed_at_us,
        status.failure_count
    );
    const bool status_changed =
        reported_error != s_last_logged_mirror_error ||
        status.failure_count != s_last_logged_mirror_failure_count ||
        status.changed_at_us != s_last_logged_mirror_changed_at_us;
    if (reported_error != ESP_OK && status_changed) {
        ESP_LOGE(
            TAG,
            "SD logger mirror failed: error=%s errno=%d failures=%lu",
            esp_err_to_name(reported_error),
            status.last_errno,
            static_cast<unsigned long>(status.failure_count)
        );
    }
    s_last_logged_mirror_error = reported_error;
    s_last_logged_mirror_failure_count = status.failure_count;
    s_last_logged_mirror_changed_at_us = status.changed_at_us;
    return action_error;
}

// Apply the effective mirror state and verify the asynchronous writer result.
esp_err_t apply_effective_logger_mirror_locked()
{
    const bool should_run = logger_mirror_should_run_locked();
    esp_err_t operation_error = ESP_OK;
    if (s_mounted && should_run) {
        operation_error = LUNASHELL_logger_enable_sd(BSP_SD_MOUNT_POINT);
    } else if (!should_run) {
        operation_error = LUNASHELL_logger_disable_sd();
    }
    if (operation_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Applying effective SD mirror lifecycle failed (%s)",
            esp_err_to_name(operation_error)
        );
        return operation_error;
    }
    return refresh_logger_status_locked();
}

// Complete a logger-mirror rollback while the storage ownership lock is held.
// Keep the transition failure as the primary storage error and publish the
// rollback failure separately as cleanup_error. A live mount whose logger
// ownership cannot be restored is terminal for this boot; an unmounted service
// remains explicitly unresolved so a later mount can reconcile the backend.
esp_err_t finish_logger_transition_rollback_locked(
    const char *owner,
    const char *failed_phase,
    esp_err_t transition_error,
    esp_err_t rollback_error
)
{
    if (rollback_error == ESP_OK) {
        publish_mount_report_locked(s_last_mount_report);
        ESP_LOGE(
            TAG,
            "%s SD mirror %s failed (%s); backend rollback completed",
            owner,
            failed_phase,
            esp_err_to_name(transition_error)
        );
        return transition_error;
    }

    // `apply_effective_logger_mirror_locked` returns before its status query
    // when the backend operation itself fails. Query once here so the mirror
    // component publishes the real asynchronous writer state as well.
    const esp_err_t status_error = refresh_logger_status_locked();
    if (status_error != ESP_OK && status_error != rollback_error) {
        ESP_LOGE(
            TAG,
            "%s SD mirror rollback status also failed (%s)",
            owner,
            esp_err_to_name(status_error)
        );
    }

    LUNASHELL::StorageMountReport report = s_last_mount_report;
    report.error = transition_error;
    report.cleanup_error = rollback_error;
    report.cleanup_complete = false;
    report.poisoned = s_mounted;
    if (s_mounted) {
        s_terminal_failure = true;
    }
    publish_mount_report_locked(report);
    ESP_LOGE(
        TAG,
        "%s SD mirror %s failed (%s); rollback failed (%s), storage ownership is %s",
        owner,
        failed_phase,
        esp_err_to_name(transition_error),
        esp_err_to_name(rollback_error),
        s_terminal_failure
            ? "terminal for this boot"
            : "unmounted and explicitly unresolved"
    );
    return rollback_error;
}

// Restore the exact backend state associated with the previous Performance
// Mode reason and apply the shared terminal/unresolved rollback policy.
esp_err_t rollback_performance_storage_locked(
    bool previous_suspended,
    const char *failed_phase,
    esp_err_t transition_error
)
{
    s_performance_suspended = previous_suspended;
    const esp_err_t rollback_error = apply_effective_logger_mirror_locked();
    return finish_logger_transition_rollback_locked(
        "Performance Mode",
        failed_phase,
        transition_error,
        rollback_error
    );
}

MountAttempt begin_mount_attempt()
{
    std::lock_guard<std::mutex> lock(s_storage_mutex);
    MountAttempt attempt = {};
    attempt.number = ++s_mount_attempt_count;
    s_waiting_for_format_authorization = false;
    if (s_format_authorization_armed) {
        attempt.request.format_authorized = true;
        attempt.request.expected_identity_valid = true;
        attempt.request.expected_identity = s_authorized_identity;
    }

    // Authorization and its source candidate are consumed by this immediate
    // probe. They are never retained for a retry.
    s_format_authorization_armed = false;
    s_authorized_identity = {};
    clear_format_candidate_locked();
    return attempt;
}

void publish_failed_mount(
    const bsp_sdcard_mount_result_t &result,
    const MountAttempt &attempt,
    esp_err_t call_error
)
{
    LUNASHELL::StorageMountReport report =
        make_mount_report(result, attempt.number);
    if (call_error == ESP_OK ||
        report.error != call_error ||
        report.mounted ||
        report.outcome == LUNASHELL::StorageMountOutcome::Mounted) {
        ESP_LOGE(
            TAG,
            "SD BSP failure contract violated: call_error=%s "
            "report_error=%s mounted=%s outcome=%s",
            esp_err_to_name(call_error),
            esp_err_to_name(report.error),
            report.mounted ? "yes" : "no",
            outcome_name(report.outcome)
        );
        std::abort();
    }
    if (attempt.request.format_authorized !=
        report.authorization_consumed) {
        ESP_LOGE(
            TAG,
            "SD authorization consumption invariant failed:"
            " requested=%s consumed=%s",
            attempt.request.format_authorized ? "yes" : "no",
            report.authorization_consumed ? "yes" : "no"
        );
        std::abort();
    }
    if (report.outcome == LUNASHELL::StorageMountOutcome::FormatRequired &&
        (!report.format_required ||
         !result.observed_identity_valid ||
         !report.card_response_verified)) {
        ESP_LOGE(
            TAG,
            "SD format-required report lacks a verified card identity"
        );
        std::abort();
    }

    const MountFailureDisposition disposition =
        classify_mount_failure(report);
    std::lock_guard<std::mutex> lock(s_storage_mutex);
    s_mounted = false;
    s_logger_mirror_enabled = false;
    s_space_refresh_requested = false;
    s_terminal_failure =
        disposition == MountFailureDisposition::Terminal;
    s_waiting_for_format_authorization =
        disposition == MountFailureDisposition::WaitForFormatAuthorization;
    if (s_waiting_for_format_authorization) {
        s_format_candidate_valid = true;
        s_format_candidate_identity = result.observed_identity;
        s_format_candidate_attempt = attempt.number;
    } else {
        clear_format_candidate_locked();
    }
    publish_mount_report_locked(report);

    ESP_LOGE(
        TAG,
        "SD attempt #%lu failed: outcome=%s stage=%s error=%s"
        " initial_fresult=%d current_fresult=%d cleanup=%s"
        " cleanup_error=%s poisoned=%s",
        static_cast<unsigned long>(attempt.number),
        outcome_name(report.outcome),
        stage_name(report.stage),
        esp_err_to_name(report.error),
        report.initial_fatfs_result_valid
            ? report.initial_fatfs_result
            : -1,
        report.fatfs_result_valid ? report.fatfs_result : -1,
        report.cleanup_complete ? "complete" : "incomplete",
        esp_err_to_name(report.cleanup_error),
        report.poisoned ? "yes" : "no"
    );
    if (s_terminal_failure) {
        ESP_LOGE(
            TAG,
            "SD retries stopped: failure is terminal for this boot"
        );
    } else if (s_waiting_for_format_authorization) {
        ESP_LOGW(
            TAG,
            "SD probes paused pending explicit identity-bound format "
            "authorization"
        );
    }
}

// Mount once and apply only the currently effective logger preference.
// No parameters: perform one storage-owned mount and publish every real failure.
esp_err_t mount_once()
{
    const MountAttempt attempt = begin_mount_attempt();
    ESP_LOGI(
        TAG,
        "SD attempt #%lu: 1-bit SDMMC probe at %s; format authorization=%s",
        static_cast<unsigned long>(attempt.number),
        BSP_SD_MOUNT_POINT,
        attempt.request.format_authorized ? "single-use" : "none"
    );

    bsp_sdcard_mount_result_t result = {};
    if (attempt.request.format_authorized) {
        ESP_LOGW(
            TAG,
            "Single-use SD format attempt uses dedicated watchdog domain; "
            "hard deadline=%lu ms",
            static_cast<unsigned long>(
                kStorageFormatWatchdogDeadlineMs
            )
        );
    }
    bool mount_invoked = false;
    const LUNASHELL_liveness_point_t watch_point =
        attempt.request.format_authorized
            ? LUNASHELL_LIVENESS_STORAGE_FORMAT
            : LUNASHELL_LIVENESS_STORAGE;
    const esp_err_t mount_error = run_watched_storage_io(
        watch_point,
        attempt.request.format_authorized ? "format/mount" : "mount",
        [&attempt, &result, &mount_invoked]() {
            mount_invoked = true;
            return bsp_sdcard_mount(&attempt.request, &result);
        }
    );
    if (!mount_invoked) {
        result.outcome = BSP_SDCARD_MOUNT_OUTCOME_INTERNAL_FAILED;
        result.stage = BSP_SDCARD_STAGE_REQUEST;
        result.error = mount_error;
        result.cleanup_error = ESP_OK;
        result.cleanup_complete = true;
        result.authorization_consumed =
            attempt.request.format_authorized;
    }
    if (mount_error != ESP_OK || !result.mounted) {
        publish_failed_mount(result, attempt, mount_error);
        return mount_error;
    }
    if (result.error != mount_error ||
        result.outcome != BSP_SDCARD_MOUNT_OUTCOME_MOUNTED ||
        !result.card_initialized ||
        !result.observed_identity_valid ||
        bsp_sdcard == nullptr) {
        ESP_LOGE(
            TAG,
            "SD BSP returned an incoherent successful mount"
        );
        std::abort();
    }

    uint64_t filesystem_total = 0;
    uint64_t filesystem_free = 0;
    const esp_err_t info_error = run_watched_storage_io(
        LUNASHELL_LIVENESS_STORAGE,
        "space query after mount",
        [&filesystem_total, &filesystem_free]() {
            return esp_vfs_fat_info(
                BSP_SD_MOUNT_POINT,
                &filesystem_total,
                &filesystem_free
            );
        }
    );
    if (info_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "SD space query after mount failed: %s",
            esp_err_to_name(info_error)
        );
    }

    LUNASHELL::StorageMountReport report =
        make_mount_report(result, attempt.number);
    report.error = info_error;
    if (info_error == ESP_OK) {
        report.filesystem_total_bytes = filesystem_total;
        report.free_bytes = filesystem_free;
    }

    esp_err_t aggregate = info_error;
    bool logger_mirror_requested = true;
    bool logger_mirror_enabled = false;
    bool power_suspended = false;
    bool performance_suspended = false;
    LUNASHELL_logger_sd_status_t logger_status = {};
    esp_err_t terminal_mount_error = ESP_OK;
    {
        std::lock_guard<std::mutex> lock(s_storage_mutex);
        if (s_terminal_failure) {
            terminal_mount_error = s_last_mount_report.error != ESP_OK
                ? s_last_mount_report.error
                : ESP_ERR_INVALID_STATE;
            report.error = terminal_mount_error;
            // The BSP completed and still owns this mount. Preserve that fact,
            // but do not enable a logger whose lifecycle is terminal.
            s_mounted = true;
            publish_mount_report_locked(report);
        } else {
            s_mounted = true;
            s_terminal_failure = false;
            s_waiting_for_format_authorization = false;
            clear_format_candidate_locked();
            logger_mirror_requested = logger_mirror_should_run_locked();
            power_suspended = s_power_suspended;
            performance_suspended = s_performance_suspended;

            // Bind reads even when the user's saved mirror preference is off.
            // A new card/mount invalidates any download's old file revision.
            const esp_err_t read_binding_error =
                LUNASHELL_logger_bind_sd(BSP_SD_MOUNT_POINT);
            esp_err_t logger_operation = read_binding_error;
            if (logger_operation == ESP_OK) {
                if (logger_mirror_requested) {
                    logger_operation = LUNASHELL_logger_enable_sd(BSP_SD_MOUNT_POINT);
                } else {
                    logger_operation = LUNASHELL_logger_disable_sd();
                }
            }
            if (logger_operation != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "Applying SD logger mirror preference failed (%s)",
                    esp_err_to_name(logger_operation)
                );
            }
            record_first_error(logger_operation, &aggregate);
            const esp_err_t logger_status_error =
                refresh_logger_status_locked(&logger_status);
            record_first_error(logger_status_error, &aggregate);
            logger_mirror_enabled = s_logger_mirror_enabled;
            if (info_error == ESP_OK) {
                s_last_space_refresh_tick = xTaskGetTickCount();
                s_space_refresh_tick_valid = true;
                s_last_space_write_sequence =
                    logger_status.persisted_write_sequence;
            } else {
                s_space_refresh_tick_valid = false;
            }
            publish_mount_report_locked(report);
        }
    }

    if (terminal_mount_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "SD mount completed after a terminal logger transition; mount retained without mirroring (%s)",
            esp_err_to_name(terminal_mount_error)
        );
        return terminal_mount_error;
    }

    ESP_LOGI(
        TAG,
        "SD mounted: capacity=%llu MB free=%s formatted=%s logger=%s",
        static_cast<unsigned long long>(
            report.capacity_bytes / (1024ULL * 1024ULL)
        ),
        info_error == ESP_OK ? "measured" : "unavailable",
        report.formatted ? "yes" : "no",
        !logger_mirror_requested
            ? power_suspended
                ? "suspended by power lifecycle"
                : performance_suspended
                    ? "suspended by Performance Mode"
                    : "disabled by user"
            : logger_mirror_enabled ? "mirrored" : "failed"
    );
    if (info_error == ESP_OK) {
        ESP_LOGI(
            TAG,
            "SD filesystem space: free=%llu MB total=%llu MB",
            static_cast<unsigned long long>(
                filesystem_free / (1024ULL * 1024ULL)
            ),
            static_cast<unsigned long long>(
                filesystem_total / (1024ULL * 1024ULL)
            )
        );
    }
    return aggregate;
}

esp_err_t invalidate_mounted_card(
    esp_err_t primary_error,
    LUNASHELL::StorageMountStage stage,
    bool card_response_verified
)
{
    const bool retry_allowed =
        stage == LUNASHELL::StorageMountStage::RuntimeStatus &&
        card_transport_error_is_explicitly_transient(primary_error);
    std::lock_guard<std::mutex> lock(s_storage_mutex);
    if (s_terminal_failure) {
        ESP_LOGE(
            TAG,
            "Mounted SD invalidation skipped because ownership is already terminal: stage=%s error=%s",
            stage_name(stage),
            esp_err_to_name(primary_error)
        );
        return primary_error;
    }
    LUNASHELL::StorageMountReport report = s_last_mount_report;
    report.outcome = card_response_verified
        ? LUNASHELL::StorageMountOutcome::FilesystemFailed
        : LUNASHELL::StorageMountOutcome::CardOrTransportFailed;
    report.stage = stage;
    report.error = primary_error;
    report.mounted = false;
    report.card_response_verified = card_response_verified;
    report.format_required = false;
    report.cleanup_stage = LUNASHELL::StorageCleanupStage::None;
    report.cleanup_error = ESP_OK;
    report.cleanup_complete = true;
    report.poisoned = false;

    s_format_authorization_armed = false;
    s_authorized_identity = {};
    s_waiting_for_format_authorization = false;
    clear_format_candidate_locked();

    const esp_err_t logger_stop_error = LUNASHELL_logger_disable_sd();
    s_logger_mirror_enabled = false;
    s_mounted = false;
    s_space_refresh_tick_valid = false;
    s_space_refresh_requested = false;
    const esp_err_t logger_status_error =
        refresh_logger_status_locked();

    esp_err_t unmount_error = ESP_OK;
    if (logger_stop_error != ESP_OK) {
        report.cleanup_error = logger_stop_error;
        report.cleanup_complete = false;
        report.poisoned = true;
        s_terminal_failure = true;
        ESP_LOGE(
            TAG,
            "SD unmount blocked: logger writer did not complete a verified "
            "clean close (%s)",
            esp_err_to_name(logger_stop_error)
        );
    } else {
        bsp_sdcard_unmount_result_t unmount_result = {};
        bool unmount_invoked = false;
        unmount_error = run_watched_storage_io(
            LUNASHELL_LIVENESS_STORAGE,
            "runtime unmount",
            [&unmount_result, &unmount_invoked]() {
                unmount_invoked = true;
                return bsp_sdcard_unmount(&unmount_result);
            }
        );
        if (!unmount_invoked) {
            unmount_result.error = unmount_error;
            unmount_result.complete = false;
            unmount_result.poisoned = true;
        }
        if (unmount_result.error != unmount_error ||
            (unmount_error == ESP_OK &&
             (!unmount_result.complete || unmount_result.poisoned)) ||
            (unmount_error != ESP_OK && unmount_result.complete)) {
            ESP_LOGE(
                TAG,
                "SD BSP runtime-unmount contract violated: call_error=%s "
                "report_error=%s complete=%s poisoned=%s",
                esp_err_to_name(unmount_error),
                esp_err_to_name(unmount_result.error),
                unmount_result.complete ? "yes" : "no",
                unmount_result.poisoned ? "yes" : "no"
            );
            std::abort();
        }
        report.cleanup_stage =
            map_cleanup_stage(unmount_result.stage);
        report.cleanup_error = unmount_result.error;
        report.cleanup_fatfs_result_valid =
            unmount_result.fatfs_result_valid;
        report.cleanup_fatfs_result =
            static_cast<int32_t>(unmount_result.fatfs_result);
        report.cleanup_complete = unmount_result.complete;
        report.poisoned = unmount_result.poisoned;
        if (unmount_error != ESP_OK) {
            report.stage = LUNASHELL::StorageMountStage::RuntimeUnmount;
            s_terminal_failure = true;
            ESP_LOGE(
                TAG,
                "SD runtime unmount failed: %s; retries stopped",
                esp_err_to_name(unmount_error)
            );
        } else {
            s_terminal_failure = !retry_allowed;
            if (s_terminal_failure) {
                ESP_LOGE(
                    TAG,
                    "SD retries stopped: runtime failure is not explicitly "
                    "transient (stage=%s error=%s)",
                    stage_name(stage),
                    esp_err_to_name(primary_error)
                );
            }
        }
    }

    publish_mount_report_locked(report);
    ESP_LOGE(
        TAG,
        "Mounted SD invalidated: stage=%s error=%s card_response=%s"
        " logger_stop=%s logger_status=%s unmount=%s",
        stage_name(stage),
        esp_err_to_name(primary_error),
        card_response_verified ? "verified" : "unconfirmed",
        esp_err_to_name(logger_stop_error),
        esp_err_to_name(logger_status_error),
        esp_err_to_name(unmount_error)
    );
    return primary_error;
}

esp_err_t poll_mounted_card(bool force_space_refresh = false)
{
    const esp_err_t status_error = run_watched_storage_io(
        LUNASHELL_LIVENESS_STORAGE,
        "runtime card status",
        []() { return bsp_sdcard_get_status(); }
    );
    if (status_error != ESP_OK) {
        return invalidate_mounted_card(
            status_error,
            LUNASHELL::StorageMountStage::RuntimeStatus,
            false
        );
    }

    LUNASHELL_logger_sd_status_t logger_status = {};
    esp_err_t logger_error = ESP_OK;
    bool refresh_space = force_space_refresh;
    {
        std::lock_guard<std::mutex> lock(s_storage_mutex);
        logger_error = refresh_logger_status_locked(&logger_status);
        const TickType_t now = xTaskGetTickCount();
        const TickType_t elapsed = now - s_last_space_refresh_tick;
        const bool slow_refresh_due =
            !s_space_refresh_tick_valid ||
            elapsed >= pdMS_TO_TICKS(kStorageSpaceRefreshMs);
        const bool persisted_write_observed =
            logger_error == ESP_OK &&
            logger_status.persisted_write_sequence !=
                s_last_space_write_sequence;
        const bool write_refresh_due =
            persisted_write_observed &&
            elapsed >= pdMS_TO_TICKS(kStorageSpaceWriteRefreshMs);
        refresh_space = refresh_space || slow_refresh_due || write_refresh_due;
    }
    if (!refresh_space) {
        // Card health is current, while the last filesystem-space timestamp is
        // intentionally left untouched because no FAT space read occurred.
        return logger_error;
    }

    uint64_t filesystem_total = 0;
    uint64_t filesystem_free = 0;
    const esp_err_t info_error = run_watched_storage_io(
        LUNASHELL_LIVENESS_STORAGE,
        "runtime filesystem space",
        [&filesystem_total, &filesystem_free]() {
            return esp_vfs_fat_info(
                BSP_SD_MOUNT_POINT,
                &filesystem_total,
                &filesystem_free
            );
        }
    );
    if (info_error != ESP_OK) {
        return invalidate_mounted_card(
            info_error,
            LUNASHELL::StorageMountStage::RuntimeFilesystem,
            true
        );
    }

    std::lock_guard<std::mutex> lock(s_storage_mutex);
    if (s_terminal_failure) {
        ESP_LOGE(
            TAG,
            "SD filesystem-space result discarded after terminal ownership transition (%s)",
            esp_err_to_name(ESP_ERR_INVALID_STATE)
        );
        return ESP_ERR_INVALID_STATE;
    }
    LUNASHELL::StorageMountReport report = s_last_mount_report;
    report.outcome = LUNASHELL::StorageMountOutcome::Mounted;
    report.stage = LUNASHELL::StorageMountStage::RuntimeFilesystem;
    report.error = ESP_OK;
    report.cleanup_error = ESP_OK;
    report.cleanup_stage = LUNASHELL::StorageCleanupStage::None;
    report.cleanup_complete = true;
    report.poisoned = false;
    report.mounted = true;
    report.card_response_verified = true;
    report.format_required = false;
    report.filesystem_total_bytes = filesystem_total;
    report.free_bytes = filesystem_free;
    s_last_space_refresh_tick = xTaskGetTickCount();
    s_space_refresh_tick_valid = true;
    if (logger_error == ESP_OK) {
        s_last_space_write_sequence =
            logger_status.persisted_write_sequence;
    }
    publish_mount_report_locked(report);
    return logger_error;
}

// resume_error: Original failed card validation, never replaced by retry eligibility.
#if CONFIG_LUNASHELL_TTS_DATA_SD
struct TtsReadRequest {
    lunashell_tts_storage_file_t *files;
    size_t count;
    bool verify_only;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    int io_errno = 0;
};
std::mutex s_tts_client_mutex;
StaticSemaphore_t s_tts_completion_control;
SemaphoreHandle_t s_tts_completion;
TtsReadRequest *s_tts_pending;

// relative_path: Exact package-owned path; never accepts arbitrary card paths.
bool valid_tts_path(const char *relative_path)
{
    constexpr const char *paths[] = {"phondata", "phontab", "phonindex", "intonations",
        "en_dict", "it_dict", "cmn_dict", "yue_dict", "lang/gmw/en", "lang/roa/it",
        "lang/sit/cmn", "lang/sit/yue"};
    if (relative_path == nullptr) return false;
    for (const char *path : paths) {
        if (strcmp(path, relative_path) == 0) return true;
    }
    return false;
}

// request: Caller-owned buffers retained until owner completion, including failures.
// Exception: Preserves read/close errno and SHA implementation failures.
esp_err_t read_tts_package(TtsReadRequest &request)
{
    const psa_status_t initialization = psa_crypto_init();
    if (initialization != PSA_SUCCESS) return static_cast<esp_err_t>(initialization);
    for (size_t index = 0; index < request.count; ++index) {
        const auto &asset = request.files[index];
        char path[128];
        const int length = snprintf(path, sizeof(path), "%s/LUNASHELL/tts/espeak-ng-data/%s",
                                    BSP_SD_MOUNT_POINT, asset.relative_path);
        if (length < 0 || static_cast<size_t>(length) >= sizeof(path)) return ESP_ERR_INVALID_SIZE;
        errno = 0;
        FILE *file = fopen(path, "rb");
        if (file == nullptr) {
            request.io_errno = errno;
            ESP_LOGE(TAG, "Speech package open %s failed: errno=%d", asset.relative_path, request.io_errno);
            return request.io_errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
        }
        struct stat info{};
        esp_err_t error = ESP_OK;
        if (fstat(fileno(file), &info) != 0) {
            request.io_errno = errno;
            error = ESP_FAIL;
        } else if (info.st_size < 0 || static_cast<size_t>(info.st_size) != asset.size) {
            error = ESP_ERR_INVALID_SIZE;
        }
        psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
        if (error == ESP_OK) {
            const psa_status_t status = psa_hash_setup(&hash, PSA_ALG_SHA_256);
            if (status != PSA_SUCCESS) error = static_cast<esp_err_t>(status);
        }
        uint8_t verification_buffer[512];
        size_t offset = 0;
        while (error == ESP_OK && offset < asset.size) {
            const size_t count = std::min<size_t>(sizeof(verification_buffer), asset.size - offset);
            uint8_t *destination = request.verify_only ? verification_buffer : asset.destination + offset;
            errno = 0;
            const size_t received = fread(destination, 1, count, file);
            if (received != count) {
                request.io_errno = errno;
                error = ferror(file) ? ESP_FAIL : ESP_ERR_INVALID_SIZE;
                break;
            }
            const psa_status_t hash_error = psa_hash_update(&hash, destination, received);
            if (hash_error != PSA_SUCCESS) error = static_cast<esp_err_t>(hash_error);
            offset += received;
        }
        errno = 0;
        if (fclose(file) != 0) {
            const int close_errno = errno;
            ESP_LOGE(TAG, "Speech package close %s failed: errno=%d", asset.relative_path, close_errno);
            if (error == ESP_OK) {
                request.io_errno = close_errno;
                error = ESP_FAIL;
            }
        }
        if (error == ESP_OK) {
            uint8_t digest[32];
            size_t digest_size = 0;
            const psa_status_t hash_error = psa_hash_finish(&hash, digest, sizeof(digest), &digest_size);
            if (hash_error != PSA_SUCCESS) error = static_cast<esp_err_t>(hash_error);
            else if (digest_size != sizeof(digest) || memcmp(digest, asset.sha256, sizeof(digest)) != 0)
                error = ESP_ERR_INVALID_CRC;
        }
        const psa_status_t abort_error = psa_hash_abort(&hash);
        if (abort_error != PSA_SUCCESS) {
            ESP_LOGE(TAG, "Speech package hash cleanup failed: psa=%ld", static_cast<long>(abort_error));
            if (error == ESP_OK) error = static_cast<esp_err_t>(abort_error);
        }
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "Speech package read/integrity %s failed (%s), errno=%d",
                     asset.relative_path, esp_err_to_name(error), request.io_errno);
            return error;
        }
    }
    return ESP_OK;
}

// No parameters: only the existing storage task executes removable-media reads.
// Exception: Every queued caller is completed even when standby or removal wins.
bool process_tts_package_request()
{
    TtsReadRequest *request = nullptr;
    bool allowed = false;
    {
        std::lock_guard<std::mutex> lock(s_storage_mutex);
        request = s_tts_pending;
        if (request == nullptr) return false;
        s_tts_pending = nullptr;
        allowed = s_mounted && !s_terminal_failure && !s_power_suspended &&
            !s_performance_suspended && s_last_mount_report.mounted &&
            s_last_mount_report.card_response_verified && !s_last_mount_report.poisoned;
        request->error = !s_mounted && s_last_mount_report.error != ESP_OK
            ? s_last_mount_report.error : ESP_ERR_INVALID_STATE;
    }
    if (allowed) {
        request->error = run_watched_storage_io(LUNASHELL_LIVENESS_STORAGE,
            "speech package read", [request]() { return read_tts_package(*request); });
    } else {
        ESP_LOGE(TAG, "Speech package owner rejected lifecycle (%s)", esp_err_to_name(request->error));
    }
    // Returning while the owner still holds borrowed buffers would be unsafe.
    if (xSemaphoreGive(s_tts_completion) != pdTRUE) {
        ESP_LOGE(TAG, "Speech package completion semaphore failed");
        ESP_ERROR_CHECK(ESP_FAIL);
    }
    return true;
}
#endif

// Caller holds s_storage_mutex; only a clean transient unmount can resume probing.
bool failed_power_resume_can_retry_locked(esp_err_t resume_error)
{
    const LUNASHELL::StorageMountReport &report = s_last_mount_report;
    return card_transport_error_is_explicitly_transient(resume_error) &&
        report.stage == LUNASHELL::StorageMountStage::RuntimeStatus &&
        report.error == resume_error &&
        !s_mounted && !report.mounted && !s_terminal_failure &&
        report.cleanup_complete && report.cleanup_error == ESP_OK &&
        !report.poisoned && !s_logger_mirror_enabled &&
        !report.logger_mirror_enabled;
}

// Mark every notification wait as quiescent for synchronous power handoff.
void wait_for_storage_notification(TickType_t timeout_ticks)
{
    (void)xEventGroupSetBits(s_storage_events, kWorkerQuiescentBit);
    (void)ulTaskNotifyTake(pdTRUE, timeout_ticks);
}

// argument: Unused FreeRTOS task context.
// Own SD/FatFs polling and park indefinitely while power is suspended.
// Exception: Publishes original I/O/resume failures; retry eligibility never clears them.
void storage_task(void *argument)
{
    (void)argument;
    uint32_t retry_seconds = kInitialRetrySeconds;
    while (true) {
        bool mounted = false;
        bool terminal_failure = false;
        bool waiting_for_format_authorization = false;
        bool power_suspended = false;
        bool performance_suspended = false;
        bool power_resume_requested = false;
        bool force_space_refresh = false;
        bool have_screenshot = false;
        bool screenshot_allowed = false;
        esp_err_t screenshot_rejection = ESP_ERR_INVALID_STATE;
        ScreenshotWriteRequest screenshot;
        (void)xEventGroupClearBits(
            s_storage_events,
            kWorkerQuiescentBit
        );
#if CONFIG_LUNASHELL_TTS_DATA_SD
        if (process_tts_package_request()) continue;
#endif
        {
            std::lock_guard<std::mutex> lock(s_storage_mutex);
            mounted = s_mounted;
            terminal_failure = s_terminal_failure;
            power_suspended = s_power_suspended;
            performance_suspended = s_performance_suspended;
            power_resume_requested = s_power_resume_requested;
            waiting_for_format_authorization =
                s_waiting_for_format_authorization;
            if (mounted && s_space_refresh_requested &&
                (!power_suspended || power_resume_requested)) {
                force_space_refresh = true;
                s_space_refresh_requested = false;
            }
            if (s_screenshot_pending) {
                screenshot = s_screenshot_request;
                s_screenshot_request = {};
                s_screenshot_pending = false;
                s_screenshot_active = true;
                have_screenshot = true;
                screenshot_allowed = mounted && !terminal_failure && !power_suspended &&
                    !performance_suspended && s_last_mount_report.mounted &&
                    s_last_mount_report.card_response_verified &&
                    !s_last_mount_report.poisoned;
                if (!screenshot_allowed && !mounted && s_last_mount_report.error != ESP_OK) {
                    screenshot_rejection = s_last_mount_report.error;
                }
            }
        }

        if (have_screenshot) {
            esp_err_t screenshot_error = screenshot_rejection;
            if (screenshot_allowed) {
                screenshot_error = run_watched_storage_io(
                    LUNASHELL_LIVENESS_STORAGE,
                    "screenshot write",
                    [&screenshot]() { return write_screenshot_file(screenshot); }
                );
            } else {
                ESP_LOGE(
                    TAG,
                    "SD screenshot rejected by owner: mounted=%u standby=%u performance=%u terminal=%u (%s)",
                    static_cast<unsigned>(mounted),
                    static_cast<unsigned>(power_suspended),
                    static_cast<unsigned>(performance_suspended),
                    static_cast<unsigned>(terminal_failure),
                    esp_err_to_name(screenshot_error)
                );
            }
            heap_caps_free(screenshot.pixels);
            {
                std::lock_guard<std::mutex> lock(s_storage_mutex);
                s_screenshot_active = false;
                if (screenshot_error == ESP_OK) {
                    s_space_refresh_requested = true;
                }
            }
            screenshot.completion(
                screenshot.capture_id,
                screenshot_error,
                screenshot.context
            );
            continue;
        }

        if (power_resume_requested) {
            esp_err_t resume_error =
                mounted ? poll_mounted_card(true) : ESP_OK;
            {
                std::lock_guard<std::mutex> lock(s_storage_mutex);
                if (!s_power_resume_requested) {
                    resume_error = ESP_ERR_TIMEOUT;
                } else if (resume_error == ESP_OK) {
                    s_power_suspended = false;
                    resume_error = apply_effective_logger_mirror_locked();
                    if (resume_error != ESP_OK) {
                        const esp_err_t transition_error = resume_error;
                        s_power_suspended = true;
                        const esp_err_t rollback_error =
                            apply_effective_logger_mirror_locked();
                        resume_error =
                            finish_logger_transition_rollback_locked(
                                "Power",
                                "resume transition",
                                transition_error,
                                rollback_error
                            );
                    } else {
                        publish_mount_report_locked(s_last_mount_report);
                    }
                } else if (failed_power_resume_can_retry_locked(resume_error)) {
                    // The device is waking, but the medium remains unavailable.
                    // A clean transient unmount has already stopped the mirror;
                    // release only the power gate so the existing mount/backoff
                    // path can run. Keep the published failure and resume result.
                    s_power_suspended = false;
                    ESP_LOGW(
                        TAG,
                        "SD wake validation failed (%s); clean transient unmount "
                        "permits owner retry with mirroring disabled",
                        esp_err_to_name(resume_error)
                    );
                }
                s_power_resume_requested = false;
                s_power_resume_error = resume_error;
            }
            (void)xEventGroupSetBits(
                s_storage_events,
                kWorkerResumeCompleteBit
            );
            continue;
        }
        if (power_suspended) {
            wait_for_storage_notification(portMAX_DELAY);
            continue;
        }
        if (terminal_failure) {
            wait_for_storage_notification(portMAX_DELAY);
            continue;
        }
        if (waiting_for_format_authorization) {
            wait_for_storage_notification(portMAX_DELAY);
            continue;
        }

        if (!mounted) {
            const esp_err_t result = mount_once();
            {
                std::lock_guard<std::mutex> lock(s_storage_mutex);
                mounted = s_mounted;
                terminal_failure = s_terminal_failure;
                waiting_for_format_authorization =
                    s_waiting_for_format_authorization;
            }
            if (terminal_failure) {
                continue;
            }
            if (!mounted) {
                if (waiting_for_format_authorization) {
                    continue;
                }
                ESP_LOGW(
                    TAG,
                    "SD remains unavailable (%s); next probe in %lu seconds",
                    esp_err_to_name(result),
                    static_cast<unsigned long>(retry_seconds)
                );
                wait_for_storage_notification(
                    pdMS_TO_TICKS(retry_seconds * 1000U)
                );
                if (retry_seconds < kMaximumRetrySeconds) {
                    retry_seconds *= 2;
                    if (retry_seconds > kMaximumRetrySeconds) {
                        retry_seconds = kMaximumRetrySeconds;
                    }
                }
                continue;
            }
            retry_seconds = kInitialRetrySeconds;
        }

        (void)poll_mounted_card(force_space_refresh);
        wait_for_storage_notification(
            pdMS_TO_TICKS(kStorageHealthPollMs)
        );
    }
}

// file selects only a logger-owned current/previous file.
// offset is the requested byte position.
// expected_revision pins the identity after the first block.
// buffer receives bounded raw bytes without any sensor acquisition.
// buffer_size is the available capacity (at most the protocol's 128 bytes).
// bytes_read receives the successful read length.
// total_size receives the persisted length observed under logger I/O ownership.
// revision receives the current file identity.
// system_errno receives the original filesystem error if one occurs.
// Returns the actual storage or logger read error, with no implicit wake/mount.
esp_err_t read_stored_log(
    LUNASHELL_logger_log_file_t file, uint32_t offset,
    uint32_t expected_revision, void *buffer, size_t buffer_size,
    size_t *bytes_read, uint32_t *total_size, uint32_t *revision,
    int *system_errno
)
{
    // Lock order is always storage -> logger I/O. The writer never enters the
    // storage mutex, and no mount lease survives into USB response transmission.
    std::lock_guard<std::mutex> lock(s_storage_mutex);
    if (s_terminal_failure || !s_mounted || s_power_suspended) {
        const esp_err_t error = !s_mounted && s_last_mount_report.error != ESP_OK
            ? s_last_mount_report.error : ESP_ERR_INVALID_STATE;
        ESP_LOGE(TAG, "SD log read rejected: mounted=%u standby=%u terminal=%u error=%s",
            static_cast<unsigned>(s_mounted), static_cast<unsigned>(s_power_suspended),
            static_cast<unsigned>(s_terminal_failure), esp_err_to_name(error));
        return error;
    }
    return LUNASHELL_logger_read_sd_log(file, offset, expected_revision,
        buffer, buffer_size, bytes_read, total_size, revision, system_errno);
}

}  // namespace

// No parameters: register SD readback and start the existing asynchronous owner.
// Returns real reader/settings/task failures while respecting a pre-existing power reason.
extern "C" esp_err_t LUNASHELL_storage_init(void)
{
    std::lock_guard<std::mutex> lock(s_storage_mutex);
    if (s_terminal_failure) {
        ESP_LOGE(TAG, "SD service is in a terminal ownership state");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_storage_task != nullptr) {
        return ESP_OK;
    }
    if (s_mounted) {
        ESP_LOGE(TAG, "SD service has a mount without an owner task");
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t reader_error = LUNASHELL_logger_set_sd_reader(read_stored_log);
    if (reader_error != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportStorageMirror(
            s_logger_mirror_requested, false, reader_error, 0, esp_timer_get_time(), 0);
        ESP_LOGE(TAG, "SD log reader registration failed (%s)", esp_err_to_name(reader_error));
        return reader_error;
    }

    const esp_err_t logger_watch_error =
        LUNASHELL_logger_set_io_watch(&kLoggerIoWatch);
    if (logger_watch_error != ESP_OK) {
        LUNASHELL::StorageMountReport report;
        report.outcome = LUNASHELL::StorageMountOutcome::InternalFailed;
        report.stage = LUNASHELL::StorageMountStage::Request;
        report.error = logger_watch_error;
        report.mount_attempt_count = s_mount_attempt_count;
        s_terminal_failure = true;
        publish_mount_report_locked(report);
        ESP_LOGE(
            TAG,
            "SD logger liveness registration failed (%s)",
            esp_err_to_name(logger_watch_error)
        );
        return logger_watch_error;
    }

    auto &settings = LUNASHELL::UserSettingsService::instance();
    if (!settings.isInitialized()) {
        const esp_err_t init_error = settings.initialize();
        if (init_error != ESP_OK) {
            LUNASHELL::DeviceSnapshotService::instance().reportStorageMirror(
                s_logger_mirror_requested,
                false,
                init_error,
                0,
                0,
                0
            );
            ESP_LOGE(
                TAG,
                "SD mirror settings initialization failed (%s)",
                esp_err_to_name(init_error)
            );
            return init_error;
        }
    }

    bool saved_mirror = false;
    esp_err_t read_error =
        settings.getBool(kLoggerMirrorSetting, &saved_mirror);
    if (read_error == ESP_ERR_NVS_NOT_FOUND) {
        saved_mirror = true;
        ESP_LOGI(
            TAG,
            "SD mirror: first boot key '%s' absent; "
            "persisting explicit default enabled",
            kLoggerMirrorSetting
        );
        read_error =
            settings.setBool(kLoggerMirrorSetting, saved_mirror);
        if (read_error != ESP_OK) {
            LUNASHELL::DeviceSnapshotService::instance().reportStorageMirror(
                s_logger_mirror_requested,
                false,
                read_error,
                0,
                0,
                0
            );
            ESP_LOGE(
                TAG,
                "Persisting first-boot SD mirror default failed (%s)",
                esp_err_to_name(read_error)
            );
            return read_error;
        }
    } else if (read_error != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportStorageMirror(
            s_logger_mirror_requested,
            false,
            read_error,
            0,
            0,
            0
        );
        ESP_LOGE(
            TAG,
            "Reading persisted SD mirror preference failed (%s)",
            esp_err_to_name(read_error)
        );
        return read_error;
    }

    s_logger_mirror_requested = saved_mirror;
    s_logger_mirror_enabled = false;
    s_waiting_for_format_authorization = false;
    s_space_refresh_requested = false;
    LUNASHELL::DeviceSnapshotService::instance().reportStorageMirror(
        s_logger_mirror_requested,
        false,
        ESP_OK,
        0,
        0,
        0
    );

    if (s_storage_events == nullptr) {
        s_storage_events = xEventGroupCreateStatic(
            &s_storage_event_storage
        );
        if (s_storage_events == nullptr) {
            LUNASHELL::StorageMountReport report;
            report.outcome = LUNASHELL::StorageMountOutcome::InternalFailed;
            report.error = ESP_ERR_NO_MEM;
            report.mount_attempt_count = s_mount_attempt_count;
            s_terminal_failure = true;
            publish_mount_report_locked(report);
            ESP_LOGE(TAG, "SD worker event group creation failed");
            return ESP_ERR_NO_MEM;
        }
    }

    // The owner task only executes SD/FatFs work; keep its long-lived stack out
    // of the scarce internal heap.
    if (xTaskCreateWithCaps(
            storage_task,
            "LUNASHELL_storage",
            7168,
            nullptr,
            3,
            &s_storage_task,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        ) != pdPASS) {
        s_storage_task = nullptr;
        LUNASHELL::StorageMountReport report;
        report.outcome = LUNASHELL::StorageMountOutcome::InternalFailed;
        report.error = ESP_ERR_NO_MEM;
        report.mount_attempt_count = s_mount_attempt_count;
        s_terminal_failure = true;
        publish_mount_report_locked(report);
        ESP_LOGE(TAG, "SD service task creation failed");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(
        TAG,
        "SD service started: exact staged mount, runtime status polling, "
        "automatic format disabled"
    );
    return ESP_OK;
}

// Accept format authorization only while the storage owner can act immediately.
extern "C" esp_err_t LUNASHELL_storage_request_format(void)
{
    std::lock_guard<std::mutex> lock(s_storage_mutex);
    if (s_terminal_failure ||
        s_power_suspended ||
        s_mounted ||
        s_storage_task == nullptr ||
        s_format_authorization_armed ||
        !s_waiting_for_format_authorization ||
        !s_format_candidate_valid) {
        ESP_LOGE(
            TAG,
            "SD format rejected: terminal=%s power_suspended=%s mounted=%s task=%s "
            "armed=%s waiting=%s candidate=%s candidate_attempt=%lu",
            s_terminal_failure ? "yes" : "no",
            s_power_suspended ? "yes" : "no",
            s_mounted ? "yes" : "no",
            s_storage_task != nullptr ? "ready" : "missing",
            s_format_authorization_armed ? "yes" : "no",
            s_waiting_for_format_authorization ? "yes" : "no",
            s_format_candidate_valid ? "verified" : "missing",
            static_cast<unsigned long>(s_format_candidate_attempt)
        );
        return ESP_ERR_INVALID_STATE;
    }

    s_authorized_identity = s_format_candidate_identity;
    s_format_authorization_armed = true;
    s_waiting_for_format_authorization = false;
    clear_format_candidate_locked();
    if (xTaskNotifyGive(s_storage_task) != pdPASS) {
        ESP_LOGE(
            TAG,
            "SD storage-task notification invariant failed after format "
            "authorization"
        );
        std::abort();
    }

    ESP_LOGW(
        TAG,
        "Explicit SD format authorization armed for one immediate probe: "
        "serial=%ld capacity=%llu",
        static_cast<long>(s_authorized_identity.serial),
        static_cast<unsigned long long>(
            s_authorized_identity.capacity_bytes
        )
    );
    return ESP_OK;
}

// Queue one FAT space measurement on the storage owner task. The request is
// intentionally asynchronous so UI callers never perform removable-media I/O.
extern "C" esp_err_t LUNASHELL_storage_request_space_refresh(void)
{
    std::lock_guard<std::mutex> lock(s_storage_mutex);
    if (s_terminal_failure ||
        s_storage_task == nullptr ||
        s_power_suspended ||
        s_power_resume_requested ||
        !s_mounted) {
        ESP_LOGE(
            TAG,
            "SD space refresh rejected: terminal=%s task=%s "
            "power_suspended=%s resume_pending=%s mounted=%s (%s)",
            s_terminal_failure ? "yes" : "no",
            s_storage_task != nullptr ? "ready" : "missing",
            s_power_suspended ? "yes" : "no",
            s_power_resume_requested ? "yes" : "no",
            s_mounted ? "yes" : "no",
            esp_err_to_name(ESP_ERR_INVALID_STATE)
        );
        return ESP_ERR_INVALID_STATE;
    }
    if (s_space_refresh_requested) {
        return ESP_OK;
    }

    s_space_refresh_requested = true;
    if (xTaskNotifyGive(s_storage_task) != pdPASS) {
        s_space_refresh_requested = false;
        ESP_LOGE(
            TAG,
            "Notifying SD owner for space refresh failed (%s)",
            esp_err_to_name(ESP_ERR_TIMEOUT)
        );
        return ESP_ERR_TIMEOUT;
    }
    ESP_LOGI(TAG, "Fresh SD filesystem-space measurement requested");
    return ESP_OK;
}

extern "C" esp_err_t LUNASHELL_storage_set_logger_mirror(bool enabled)
{
    esp_err_t aggregate = ESP_OK;
    std::lock_guard<std::mutex> lock(s_storage_mutex);
    auto &settings = LUNASHELL::UserSettingsService::instance();
    if (!settings.isInitialized()) {
        const esp_err_t init_error = settings.initialize();
        if (init_error != ESP_OK) {
            LUNASHELL::DeviceSnapshotService::instance().reportStorageMirror(
                s_logger_mirror_requested,
                s_logger_mirror_enabled,
                init_error,
                0,
                0,
                0
            );
            ESP_LOGE(
                TAG,
                "SD mirror settings initialization failed (%s)",
                esp_err_to_name(init_error)
            );
            return init_error;
        }
    }

    const esp_err_t persist_error =
        settings.setBool(kLoggerMirrorSetting, enabled);
    if (persist_error != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportStorageMirror(
            s_logger_mirror_requested,
            s_logger_mirror_enabled,
            persist_error,
            0,
            0,
            0
        );
        ESP_LOGE(
            TAG,
            "Persisting SD mirror preference failed (%s)",
            esp_err_to_name(persist_error)
        );
        return persist_error;
    }
    s_logger_mirror_requested = enabled;

    const bool should_run = logger_mirror_should_run_locked();
    esp_err_t apply_error = ESP_OK;
    if (should_run && s_mounted) {
        apply_error = LUNASHELL_logger_enable_sd(BSP_SD_MOUNT_POINT);
    } else if (!should_run) {
        apply_error = LUNASHELL_logger_disable_sd();
    }
    if (apply_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Applying SD mirror preference failed (%s)",
            esp_err_to_name(apply_error)
        );
    }
    record_first_error(apply_error, &aggregate);

    if (s_mounted || !should_run) {
        record_first_error(refresh_logger_status_locked(), &aggregate);
    } else {
        s_logger_mirror_enabled = false;
        LUNASHELL::DeviceSnapshotService::instance().reportStorageMirror(
            true,
            false,
            ESP_OK,
            0,
            0,
            0
        );
    }

    if (s_storage_task != nullptr &&
        xTaskNotifyGive(s_storage_task) != pdPASS) {
        ESP_LOGE(TAG, "Notifying SD service of mirror change failed");
        record_first_error(ESP_ERR_TIMEOUT, &aggregate);
    }
    ESP_LOGI(
        TAG,
        "SD log mirror preference: %s",
        enabled ? "enabled" : "disabled"
    );
    return aggregate;
}

// pixels: Heap-owned complete frame; ownership transfers only after successful queueing.
// width: Visible frame width in pixels.
// height: Visible frame height in pixels.
// stride: Source row stride in RGB565 pixels.
// byte_swapped: True when source words use panel byte order.
// capture_id: Caller-owned monotonic identity returned to completion.
// completion: Non-blocking completion invoked by the storage owner.
// context: Opaque caller value returned unchanged to completion.
// Exception: Returns the real state, geometry or owner-notification failure.
extern "C" esp_err_t LUNASHELL_storage_queue_screenshot_rgb565(
    uint16_t *pixels, uint16_t width, uint16_t height, uint16_t stride,
    bool byte_swapped, uint32_t capture_id,
    LUNASHELL_screenshot_completion_t completion, void *context
)
{
    if (pixels == nullptr || width != kScreenshotWidth || height != kScreenshotHeight ||
        stride < width || capture_id == 0 || completion == nullptr) {
        ESP_LOGE(
            TAG,
            "SD screenshot request rejected: pixels=%p size=%ux%u stride=%u id=%lu callback=%u (%s)",
            pixels,
            static_cast<unsigned>(width),
            static_cast<unsigned>(height),
            static_cast<unsigned>(stride),
            static_cast<unsigned long>(capture_id),
            static_cast<unsigned>(completion != nullptr),
            esp_err_to_name(ESP_ERR_INVALID_ARG)
        );
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(s_storage_mutex);
    if (s_storage_task == nullptr || !s_mounted || s_terminal_failure ||
        s_power_suspended || s_performance_suspended ||
        !s_last_mount_report.mounted || !s_last_mount_report.card_response_verified ||
        s_last_mount_report.poisoned) {
        const esp_err_t error = !s_mounted && s_last_mount_report.error != ESP_OK
            ? s_last_mount_report.error : ESP_ERR_INVALID_STATE;
        ESP_LOGE(
            TAG,
            "SD screenshot queue rejected: owner=%u mounted=%u standby=%u performance=%u terminal=%u verified=%u poisoned=%u (%s)",
            static_cast<unsigned>(s_storage_task != nullptr),
            static_cast<unsigned>(s_mounted),
            static_cast<unsigned>(s_power_suspended),
            static_cast<unsigned>(s_performance_suspended),
            static_cast<unsigned>(s_terminal_failure),
            static_cast<unsigned>(s_last_mount_report.card_response_verified),
            static_cast<unsigned>(s_last_mount_report.poisoned),
            esp_err_to_name(error)
        );
        return error;
    }
    if (s_screenshot_pending || s_screenshot_active) {
        ESP_LOGE(TAG, "SD screenshot queue already occupied (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }

    s_screenshot_request = {
        .pixels = pixels,
        .width = width,
        .height = height,
        .stride = stride,
        .byte_swapped = byte_swapped,
        .capture_id = capture_id,
        .completion = completion,
        .context = context,
    };
    s_screenshot_pending = true;
    if (xTaskNotifyGive(s_storage_task) != pdPASS) {
        s_screenshot_request = {};
        s_screenshot_pending = false;
        ESP_LOGE(TAG, "Notifying SD owner for screenshot failed (%s)",
                 esp_err_to_name(ESP_ERR_TIMEOUT));
        return ESP_ERR_TIMEOUT;
    }
    ESP_LOGI(TAG, "SD screenshot queued: capture=%lu", static_cast<unsigned long>(capture_id));
    return ESP_OK;
}

extern "C" esp_err_t LUNASHELL_storage_set_performance_suspended(bool suspended)
{
    std::lock_guard<std::mutex> lock(s_storage_mutex);
    if (s_terminal_failure) {
        ESP_LOGE(
            TAG,
            "Performance Mode storage change rejected: terminal ownership state"
        );
        return ESP_ERR_INVALID_STATE;
    }

    const bool previous = s_performance_suspended;
    s_performance_suspended = suspended;
    const bool should_run = logger_mirror_should_run_locked();
    esp_err_t operation_error = ESP_OK;
    if (s_mounted && should_run) {
        operation_error = LUNASHELL_logger_enable_sd(BSP_SD_MOUNT_POINT);
    } else if (!should_run) {
        operation_error = LUNASHELL_logger_disable_sd();
    }
    if (operation_error != ESP_OK) {
        return rollback_performance_storage_locked(
            previous,
            "transition",
            operation_error
        );
    }

    const esp_err_t status_error = refresh_logger_status_locked();
    if (status_error != ESP_OK) {
        return rollback_performance_storage_locked(
            previous,
            "status verification",
            status_error
        );
    }
    publish_mount_report_locked(s_last_mount_report);
    ESP_LOGI(
        TAG,
        "Performance Mode SD mirror suppression: %s (user preference=%s)",
        suspended ? "active" : "inactive",
        s_logger_mirror_requested ? "enabled" : "disabled"
    );
    return ESP_OK;
}

// Quiesce polling and mirroring transactionally without unmounting the card.
extern "C" esp_err_t LUNASHELL_storage_set_power_suspended(bool suspended)
{
    std::lock_guard<std::mutex> transition_lock(s_power_transition_mutex);
    TaskHandle_t storage_task_handle = nullptr;
    EventGroupHandle_t storage_events = nullptr;
    bool mounted_snapshot = false;
    bool mirror_requested_snapshot = false;
    if (suspended) {
        bool previous = false;
        bool format_state_expired = false;
        {
            std::lock_guard<std::mutex> lock(s_storage_mutex);
            if (s_terminal_failure) {
                ESP_LOGE(
                    TAG,
                    "Power storage change rejected: terminal ownership state (%s)",
                    esp_err_to_name(ESP_ERR_INVALID_STATE)
                );
                return ESP_ERR_INVALID_STATE;
            }
            if (s_storage_task == nullptr) {
                if (s_mounted) {
                    ESP_LOGE(
                        TAG,
                        "Power storage change rejected: mounted card has no owner task (%s)",
                        esp_err_to_name(ESP_ERR_INVALID_STATE)
                    );
                    return ESP_ERR_INVALID_STATE;
                }
                s_power_suspended = true;
                ESP_LOGI(TAG, "SD pre-initialization power suspension: active");
                return ESP_OK;
            }
            if (s_storage_events == nullptr || s_power_resume_requested) {
                ESP_LOGE(
                    TAG,
                    "Power storage suspension rejected: worker transition unavailable (%s)",
                    esp_err_to_name(ESP_ERR_INVALID_STATE)
                );
                return ESP_ERR_INVALID_STATE;
            }

            previous = s_power_suspended;
            s_power_suspended = true;
            if (s_format_authorization_armed ||
                s_waiting_for_format_authorization ||
                s_format_candidate_valid) {
                format_state_expired = true;
                s_format_authorization_armed = false;
                s_authorized_identity = {};
                s_waiting_for_format_authorization = false;
                clear_format_candidate_locked();
                ESP_LOGW(
                    TAG,
                    "SD format authorization/candidate expired for power suspension; a fresh verified probe is required"
                );
            }

            const esp_err_t transition_error =
                apply_effective_logger_mirror_locked();
            if (transition_error != ESP_OK) {
                s_power_suspended = previous;
                const esp_err_t rollback_error =
                    apply_effective_logger_mirror_locked();
                const esp_err_t rollback_result =
                    finish_logger_transition_rollback_locked(
                        "Power",
                        "suspension transition",
                        transition_error,
                        rollback_error
                    );
                if (format_state_expired &&
                    xTaskNotifyGive(s_storage_task) != pdPASS) {
                    ESP_LOGE(
                        TAG,
                        "Notifying SD service after format-state expiry failed (%s)",
                        esp_err_to_name(ESP_ERR_TIMEOUT)
                    );
                }
                return rollback_result;
            }
            publish_mount_report_locked(s_last_mount_report);
            storage_task_handle = s_storage_task;
            storage_events = s_storage_events;
        }

        esp_err_t quiescence_error = ESP_OK;
        if (xTaskNotifyGive(storage_task_handle) != pdPASS) {
            quiescence_error = ESP_ERR_TIMEOUT;
        } else {
            const EventBits_t bits = xEventGroupWaitBits(
                storage_events,
                kWorkerQuiescentBit,
                pdFALSE,
                pdTRUE,
                pdMS_TO_TICKS(kPowerSuspendTimeoutMs)
            );
            if ((bits & kWorkerQuiescentBit) == 0U) {
                quiescence_error = ESP_ERR_TIMEOUT;
            }
        }
        if (quiescence_error != ESP_OK) {
            esp_err_t rollback_result = quiescence_error;
            {
                std::lock_guard<std::mutex> lock(s_storage_mutex);
                s_power_suspended = previous;
                const esp_err_t rollback_error =
                    apply_effective_logger_mirror_locked();
                rollback_result = finish_logger_transition_rollback_locked(
                    "Power",
                    "quiescence transition",
                    quiescence_error,
                    rollback_error
                );
            }
            (void)xTaskNotifyGive(storage_task_handle);
            ESP_LOGE(
                TAG,
                "SD power quiescence failed; timeout=%lu ms request rolled back (%s)",
                static_cast<unsigned long>(kPowerSuspendTimeoutMs),
                esp_err_to_name(rollback_result)
            );
            return rollback_result;
        }

        {
            std::lock_guard<std::mutex> lock(s_storage_mutex);
            mounted_snapshot = s_mounted;
            mirror_requested_snapshot = s_logger_mirror_requested;
        }
        ESP_LOGI(
            TAG,
            "SD power suspension active (mounted=%s user mirror=%s)",
            mounted_snapshot ? "yes" : "no",
            mirror_requested_snapshot ? "enabled" : "disabled"
        );
        return ESP_OK;
    }

    {
        std::lock_guard<std::mutex> lock(s_storage_mutex);
        if (s_terminal_failure) {
            ESP_LOGE(
                TAG,
                "Power storage resume rejected: terminal ownership state (%s)",
                esp_err_to_name(ESP_ERR_INVALID_STATE)
            );
            return ESP_ERR_INVALID_STATE;
        }
        if (s_storage_task == nullptr) {
            if (s_mounted) {
                ESP_LOGE(
                    TAG,
                    "Power storage resume rejected: mounted card has no owner task (%s)",
                    esp_err_to_name(ESP_ERR_INVALID_STATE)
                );
                return ESP_ERR_INVALID_STATE;
            }
            s_power_suspended = false;
            ESP_LOGI(TAG, "SD pre-initialization power suspension: inactive");
            return ESP_OK;
        }
        if (s_storage_events == nullptr || s_power_resume_requested) {
            ESP_LOGE(
                TAG,
                "Power storage resume rejected: worker transition unavailable (%s)",
                esp_err_to_name(ESP_ERR_INVALID_STATE)
            );
            return ESP_ERR_INVALID_STATE;
        }
        if (!s_power_suspended) {
            return ESP_OK;
        }

        s_power_resume_requested = true;
        s_power_resume_error = ESP_ERR_NOT_FINISHED;
        (void)xEventGroupClearBits(
            s_storage_events,
            kWorkerResumeCompleteBit
        );
        storage_task_handle = s_storage_task;
        storage_events = s_storage_events;
    }

    if (xTaskNotifyGive(storage_task_handle) != pdPASS) {
        std::lock_guard<std::mutex> lock(s_storage_mutex);
        s_power_resume_requested = false;
        s_power_resume_error = ESP_ERR_TIMEOUT;
        ESP_LOGE(
            TAG,
            "Notifying SD worker for power resume failed (%s)",
            esp_err_to_name(ESP_ERR_TIMEOUT)
        );
        return ESP_ERR_TIMEOUT;
    }

    const EventBits_t bits = xEventGroupWaitBits(
        storage_events,
        kWorkerResumeCompleteBit,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(kPowerSuspendTimeoutMs)
    );
    esp_err_t resume_error = ESP_ERR_TIMEOUT;
    {
        std::lock_guard<std::mutex> lock(s_storage_mutex);
        if ((bits & kWorkerResumeCompleteBit) != 0U ||
            !s_power_resume_requested) {
            resume_error = s_power_resume_error;
        } else {
            s_power_resume_requested = false;
            s_power_resume_error = ESP_ERR_TIMEOUT;
        }
        mounted_snapshot = s_mounted;
        mirror_requested_snapshot = s_logger_mirror_requested;
    }
    if (resume_error != ESP_OK) {
        (void)xTaskNotifyGive(storage_task_handle);
        ESP_LOGE(
            TAG,
            "SD power resume validation failed (%s)",
            esp_err_to_name(resume_error)
        );
        return resume_error;
    }

    ESP_LOGI(
        TAG,
        "SD power suspension inactive after card/FAT validation (mounted=%s user mirror=%s)",
        mounted_snapshot ? "yes" : "no",
        mirror_requested_snapshot ? "enabled" : "disabled"
    );
    return ESP_OK;
}

#if CONFIG_LUNASHELL_TTS_DATA_SD
// files: Exact manifest paths and caller buffers; retained until owner completion.
// file_count: Reviewed package contains exactly twelve files.
// verify_only: Verifies current card/package without overwriting borrowed tables.
// io_errno: Receives original filesystem cause, zero for non-filesystem errors.
// Exception: Returns actual admission, owner lifecycle, read and integrity failures.
extern "C" esp_err_t LUNASHELL_storage_load_tts(lunashell_tts_storage_file_t *files,
                                               size_t file_count, bool verify_only, int *io_errno)
{
    if (io_errno == nullptr) {
        ESP_LOGE(TAG, "Speech package admission rejected: missing errno destination");
        return ESP_ERR_INVALID_ARG;
    }
    *io_errno = 0;
    if (files == nullptr || file_count != 12) {
        ESP_LOGE(TAG, "Speech package manifest rejected: count=%u", static_cast<unsigned>(file_count));
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t index = 0; index < file_count; ++index) {
        if (!valid_tts_path(files[index].relative_path) || (!verify_only && files[index].destination == nullptr) ||
            files[index].sha256 == nullptr || files[index].size == 0) {
            ESP_LOGE(TAG, "Speech package descriptor rejected at %u", static_cast<unsigned>(index));
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t previous = 0; previous < index; ++previous) {
            if (strcmp(files[index].relative_path, files[previous].relative_path) == 0) {
                ESP_LOGE(TAG, "Speech package duplicate path rejected: %s", files[index].relative_path);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    std::lock_guard<std::mutex> client_lock(s_tts_client_mutex);
    TtsReadRequest request{files, file_count, verify_only};
    {
        std::lock_guard<std::mutex> lock(s_storage_mutex);
        if (s_storage_task == nullptr || !s_mounted || s_terminal_failure ||
            s_power_suspended || s_performance_suspended || s_tts_pending != nullptr) {
            const esp_err_t error = !s_mounted && s_last_mount_report.error != ESP_OK
                ? s_last_mount_report.error : ESP_ERR_INVALID_STATE;
            ESP_LOGE(TAG, "Speech package admission rejected (%s)", esp_err_to_name(error));
            return error;
        }
        if (s_tts_completion == nullptr) {
            s_tts_completion = xSemaphoreCreateBinaryStatic(&s_tts_completion_control);
            if (s_tts_completion == nullptr) {
                ESP_LOGE(TAG, "Speech package completion allocation failed");
                return ESP_ERR_NO_MEM;
            }
        }
        s_tts_pending = &request;
        if (xTaskNotifyGive(s_storage_task) != pdPASS) {
            s_tts_pending = nullptr;
            ESP_LOGE(TAG, "Speech package owner notification failed");
            return ESP_FAIL;
        }
    }
    // The watched owner bounds hardware I/O; no timeout may release borrowed buffers.
    if (xSemaphoreTake(s_tts_completion, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Speech package owner completion wait failed");
        ESP_ERROR_CHECK(ESP_FAIL);
    }
    *io_errno = request.io_errno;
    return request.error;
}
#endif
