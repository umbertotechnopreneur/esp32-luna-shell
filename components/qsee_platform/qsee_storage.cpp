/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "qsee_platform.h"
#include "qsee_device_snapshot.hpp"

#include <cstdlib>
#include <mutex>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "qsee_logger.h"
#include "qsee_settings.hpp"

namespace {

const char *TAG = "qsee_storage";
constexpr char kLoggerMirrorSetting[] = "sd_mirror";
constexpr uint32_t kStorageStatusPollMs = 1000;
constexpr uint32_t kInitialRetrySeconds = 15;
constexpr uint32_t kMaximumRetrySeconds = 120;

std::mutex s_storage_mutex;
TaskHandle_t s_storage_task;
uint32_t s_mount_attempt_count;
bool s_mounted;
bool s_terminal_failure;
bool s_waiting_for_format_authorization;
bool s_logger_mirror_requested = true;
bool s_logger_mirror_enabled;
esp_err_t s_last_logged_mirror_error = ESP_ERR_INVALID_STATE;
uint32_t s_last_logged_mirror_failure_count;
int64_t s_last_logged_mirror_changed_at_us;
bool s_format_authorization_armed;
bsp_sdcard_identity_t s_authorized_identity;
bool s_format_candidate_valid;
bsp_sdcard_identity_t s_format_candidate_identity;
uint32_t s_format_candidate_attempt;
qsee::StorageMountReport s_last_mount_report;

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
    const qsee::StorageMountReport &report
)
{
    if (report.stage != qsee::StorageMountStage::FatMount ||
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

qsee::StorageMountOutcome map_outcome(
    bsp_sdcard_mount_outcome_t outcome
)
{
    switch (outcome) {
    case BSP_SDCARD_MOUNT_OUTCOME_MOUNTED:
        return qsee::StorageMountOutcome::Mounted;
    case BSP_SDCARD_MOUNT_OUTCOME_CARD_OR_TRANSPORT_FAILED:
        return qsee::StorageMountOutcome::CardOrTransportFailed;
    case BSP_SDCARD_MOUNT_OUTCOME_FORMAT_REQUIRED:
        return qsee::StorageMountOutcome::FormatRequired;
    case BSP_SDCARD_MOUNT_OUTCOME_FILESYSTEM_FAILED:
        return qsee::StorageMountOutcome::FilesystemFailed;
    case BSP_SDCARD_MOUNT_OUTCOME_FORMAT_FAILED:
        return qsee::StorageMountOutcome::FormatFailed;
    case BSP_SDCARD_MOUNT_OUTCOME_INTERNAL_FAILED:
        return qsee::StorageMountOutcome::InternalFailed;
    }
    invalid_bsp_enum("outcome", static_cast<int>(outcome));
}

qsee::StorageMountStage map_stage(bsp_sdcard_mount_stage_t stage)
{
    switch (stage) {
    case BSP_SDCARD_STAGE_NONE:
        return qsee::StorageMountStage::None;
    case BSP_SDCARD_STAGE_REQUEST:
        return qsee::StorageMountStage::Request;
    case BSP_SDCARD_STAGE_HOST_INIT:
        return qsee::StorageMountStage::HostInit;
    case BSP_SDCARD_STAGE_SLOT_INIT:
        return qsee::StorageMountStage::SlotInit;
    case BSP_SDCARD_STAGE_CARD_INIT:
        return qsee::StorageMountStage::CardInit;
    case BSP_SDCARD_STAGE_CARD_IDENTITY:
        return qsee::StorageMountStage::CardIdentity;
    case BSP_SDCARD_STAGE_DRIVE_ALLOC:
        return qsee::StorageMountStage::DriveAlloc;
    case BSP_SDCARD_STAGE_DISKIO_REGISTER:
        return qsee::StorageMountStage::DiskIoRegister;
    case BSP_SDCARD_STAGE_VFS_REGISTER:
        return qsee::StorageMountStage::VfsRegister;
    case BSP_SDCARD_STAGE_FAT_MOUNT:
        return qsee::StorageMountStage::FatMount;
    case BSP_SDCARD_STAGE_PRE_FORMAT_UNMOUNT:
        return qsee::StorageMountStage::PreFormatUnmount;
    case BSP_SDCARD_STAGE_PARTITION:
        return qsee::StorageMountStage::Partition;
    case BSP_SDCARD_STAGE_FORMAT:
        return qsee::StorageMountStage::Format;
    case BSP_SDCARD_STAGE_POST_FORMAT_MOUNT:
        return qsee::StorageMountStage::PostFormatMount;
    }
    invalid_bsp_enum("stage", static_cast<int>(stage));
}

qsee::StorageCleanupStage map_cleanup_stage(
    bsp_sdcard_cleanup_stage_t stage
)
{
    switch (stage) {
    case BSP_SDCARD_CLEANUP_NONE:
        return qsee::StorageCleanupStage::None;
    case BSP_SDCARD_CLEANUP_FAT_UNMOUNT:
        return qsee::StorageCleanupStage::FatUnmount;
    case BSP_SDCARD_CLEANUP_VFS_UNREGISTER:
        return qsee::StorageCleanupStage::VfsUnregister;
    case BSP_SDCARD_CLEANUP_HOST_DEINIT:
        return qsee::StorageCleanupStage::HostDeinit;
    }
    invalid_bsp_enum("cleanup stage", static_cast<int>(stage));
}

const char *outcome_name(qsee::StorageMountOutcome outcome)
{
    switch (outcome) {
    case qsee::StorageMountOutcome::NotAttempted:
        return "not-attempted";
    case qsee::StorageMountOutcome::Mounted:
        return "mounted";
    case qsee::StorageMountOutcome::CardOrTransportFailed:
        return "card-or-transport-failed";
    case qsee::StorageMountOutcome::FormatRequired:
        return "format-required";
    case qsee::StorageMountOutcome::FilesystemFailed:
        return "filesystem-failed";
    case qsee::StorageMountOutcome::FormatFailed:
        return "format-failed";
    case qsee::StorageMountOutcome::InternalFailed:
        return "internal-failed";
    }
    invalid_bsp_enum("storage outcome", static_cast<int>(outcome));
}

MountFailureDisposition classify_mount_failure(
    const qsee::StorageMountReport &report
)
{
    if (report.poisoned ||
        !report.cleanup_complete ||
        report.error == ESP_ERR_INVALID_STATE) {
        return MountFailureDisposition::Terminal;
    }

    switch (report.outcome) {
    case qsee::StorageMountOutcome::CardOrTransportFailed:
        return report.stage == qsee::StorageMountStage::CardInit &&
                       card_transport_error_is_explicitly_transient(
                           report.error
                       )
                   ? MountFailureDisposition::Retry
                   : MountFailureDisposition::Terminal;
    case qsee::StorageMountOutcome::FilesystemFailed:
        return !report.format_attempted &&
                       !report.partition_written &&
                       !report.formatted &&
                       filesystem_error_is_explicitly_transient(report)
                   ? MountFailureDisposition::Retry
                   : MountFailureDisposition::Terminal;
    case qsee::StorageMountOutcome::FormatRequired:
        return MountFailureDisposition::WaitForFormatAuthorization;
    case qsee::StorageMountOutcome::FormatFailed:
    case qsee::StorageMountOutcome::InternalFailed:
    case qsee::StorageMountOutcome::NotAttempted:
    case qsee::StorageMountOutcome::Mounted:
        return MountFailureDisposition::Terminal;
    }
    invalid_bsp_enum(
        "storage failure disposition outcome",
        static_cast<int>(report.outcome)
    );
}

const char *stage_name(qsee::StorageMountStage stage)
{
    switch (stage) {
    case qsee::StorageMountStage::None:
        return "none";
    case qsee::StorageMountStage::Request:
        return "request";
    case qsee::StorageMountStage::HostInit:
        return "host-init";
    case qsee::StorageMountStage::SlotInit:
        return "slot-init";
    case qsee::StorageMountStage::CardInit:
        return "card-init";
    case qsee::StorageMountStage::CardIdentity:
        return "card-identity";
    case qsee::StorageMountStage::DriveAlloc:
        return "drive-alloc";
    case qsee::StorageMountStage::DiskIoRegister:
        return "diskio-register";
    case qsee::StorageMountStage::VfsRegister:
        return "vfs-register";
    case qsee::StorageMountStage::FatMount:
        return "fat-mount";
    case qsee::StorageMountStage::PreFormatUnmount:
        return "pre-format-unmount";
    case qsee::StorageMountStage::Partition:
        return "partition";
    case qsee::StorageMountStage::Format:
        return "format";
    case qsee::StorageMountStage::PostFormatMount:
        return "post-format-mount";
    case qsee::StorageMountStage::RuntimeStatus:
        return "runtime-status";
    case qsee::StorageMountStage::RuntimeFilesystem:
        return "runtime-filesystem";
    case qsee::StorageMountStage::RuntimeUnmount:
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

void publish_mount_report_locked(qsee::StorageMountReport report)
{
    report.logger_mirror_requested = s_logger_mirror_requested;
    report.logger_mirror_enabled = s_logger_mirror_enabled;
    s_last_mount_report = report;
    qsee::DeviceSnapshotService::instance().reportStorage(report);
}

qsee::StorageMountReport make_mount_report(
    const bsp_sdcard_mount_result_t &result,
    uint32_t attempt_number
)
{
    qsee::StorageMountReport report;
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

esp_err_t refresh_logger_status_locked()
{
    qsee_logger_sd_status_t status = {};
    const esp_err_t query_error = qsee_logger_get_sd_status(&status);
    if (query_error != ESP_OK) {
        qsee::DeviceSnapshotService::instance().reportStorageMirror(
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

    s_logger_mirror_enabled = status.enabled;
    esp_err_t reported_error = status.last_error;
    esp_err_t action_error =
        s_logger_mirror_requested ? status.last_error : ESP_OK;
    if (s_mounted && status.enabled != s_logger_mirror_requested) {
        action_error = ESP_ERR_INVALID_STATE;
        if (reported_error == ESP_OK) {
            reported_error = action_error;
        }
    }
    qsee::DeviceSnapshotService::instance().reportStorageMirror(
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
    qsee::StorageMountReport report =
        make_mount_report(result, attempt.number);
    if (call_error == ESP_OK ||
        report.error != call_error ||
        report.mounted ||
        report.outcome == qsee::StorageMountOutcome::Mounted) {
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
    if (report.outcome == qsee::StorageMountOutcome::FormatRequired &&
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
    const esp_err_t mount_error =
        bsp_sdcard_mount(&attempt.request, &result);
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
    const esp_err_t info_error =
        esp_vfs_fat_info(
            BSP_SD_MOUNT_POINT,
            &filesystem_total,
            &filesystem_free
        );
    if (info_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "SD space query after mount failed: %s",
            esp_err_to_name(info_error)
        );
    }

    qsee::StorageMountReport report =
        make_mount_report(result, attempt.number);
    report.error = info_error;
    if (info_error == ESP_OK) {
        report.filesystem_total_bytes = filesystem_total;
        report.free_bytes = filesystem_free;
    }

    esp_err_t aggregate = info_error;
    bool logger_mirror_requested = true;
    bool logger_mirror_enabled = false;
    {
        std::lock_guard<std::mutex> lock(s_storage_mutex);
        s_mounted = true;
        s_terminal_failure = false;
        s_waiting_for_format_authorization = false;
        clear_format_candidate_locked();
        logger_mirror_requested = s_logger_mirror_requested;

        const esp_err_t logger_operation =
            logger_mirror_requested
                ? qsee_logger_enable_sd(BSP_SD_MOUNT_POINT)
                : qsee_logger_disable_sd();
        if (logger_operation != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Applying SD logger mirror preference failed (%s)",
                esp_err_to_name(logger_operation)
            );
        }
        record_first_error(logger_operation, &aggregate);
        const esp_err_t logger_status_error =
            refresh_logger_status_locked();
        record_first_error(logger_status_error, &aggregate);
        logger_mirror_enabled = s_logger_mirror_enabled;
        publish_mount_report_locked(report);
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
            ? "disabled by user"
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
    qsee::StorageMountStage stage,
    bool card_response_verified
)
{
    const bool retry_allowed =
        stage == qsee::StorageMountStage::RuntimeStatus &&
        card_transport_error_is_explicitly_transient(primary_error);
    std::lock_guard<std::mutex> lock(s_storage_mutex);
    qsee::StorageMountReport report = s_last_mount_report;
    report.outcome = card_response_verified
        ? qsee::StorageMountOutcome::FilesystemFailed
        : qsee::StorageMountOutcome::CardOrTransportFailed;
    report.stage = stage;
    report.error = primary_error;
    report.mounted = false;
    report.card_response_verified = card_response_verified;
    report.format_required = false;
    report.cleanup_stage = qsee::StorageCleanupStage::None;
    report.cleanup_error = ESP_OK;
    report.cleanup_complete = true;
    report.poisoned = false;

    s_format_authorization_armed = false;
    s_authorized_identity = {};
    s_waiting_for_format_authorization = false;
    clear_format_candidate_locked();

    const esp_err_t logger_stop_error = qsee_logger_disable_sd();
    s_logger_mirror_enabled = false;
    s_mounted = false;
    const esp_err_t logger_status_error =
        refresh_logger_status_locked();

    esp_err_t unmount_error = ESP_OK;
    if (logger_stop_error == ESP_ERR_TIMEOUT) {
        report.cleanup_error = logger_stop_error;
        report.cleanup_complete = false;
        report.poisoned = true;
        s_terminal_failure = true;
        ESP_LOGE(
            TAG,
            "SD unmount blocked: logger writer did not acknowledge close"
        );
    } else {
        bsp_sdcard_unmount_result_t unmount_result = {};
        unmount_error = bsp_sdcard_unmount(&unmount_result);
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
            report.stage = qsee::StorageMountStage::RuntimeUnmount;
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

esp_err_t poll_mounted_card()
{
    const esp_err_t status_error = bsp_sdcard_get_status();
    if (status_error != ESP_OK) {
        return invalidate_mounted_card(
            status_error,
            qsee::StorageMountStage::RuntimeStatus,
            false
        );
    }

    uint64_t filesystem_total = 0;
    uint64_t filesystem_free = 0;
    const esp_err_t info_error =
        esp_vfs_fat_info(
            BSP_SD_MOUNT_POINT,
            &filesystem_total,
            &filesystem_free
        );
    if (info_error != ESP_OK) {
        return invalidate_mounted_card(
            info_error,
            qsee::StorageMountStage::RuntimeFilesystem,
            true
        );
    }

    std::lock_guard<std::mutex> lock(s_storage_mutex);
    qsee::StorageMountReport report = s_last_mount_report;
    report.outcome = qsee::StorageMountOutcome::Mounted;
    report.stage = qsee::StorageMountStage::RuntimeFilesystem;
    report.error = ESP_OK;
    report.cleanup_error = ESP_OK;
    report.cleanup_stage = qsee::StorageCleanupStage::None;
    report.cleanup_complete = true;
    report.poisoned = false;
    report.mounted = true;
    report.card_response_verified = true;
    report.format_required = false;
    report.filesystem_total_bytes = filesystem_total;
    report.free_bytes = filesystem_free;
    const esp_err_t logger_error = refresh_logger_status_locked();
    publish_mount_report_locked(report);
    return logger_error;
}

void storage_task(void *argument)
{
    (void)argument;
    uint32_t retry_seconds = kInitialRetrySeconds;
    while (true) {
        bool mounted = false;
        bool terminal_failure = false;
        bool waiting_for_format_authorization = false;
        {
            std::lock_guard<std::mutex> lock(s_storage_mutex);
            mounted = s_mounted;
            terminal_failure = s_terminal_failure;
            waiting_for_format_authorization =
                s_waiting_for_format_authorization;
        }

        if (terminal_failure) {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        if (waiting_for_format_authorization) {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
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
            if (!mounted) {
                if (terminal_failure) {
                    continue;
                }
                if (waiting_for_format_authorization) {
                    continue;
                }
                ESP_LOGW(
                    TAG,
                    "SD remains unavailable (%s); next probe in %lu seconds",
                    esp_err_to_name(result),
                    static_cast<unsigned long>(retry_seconds)
                );
                (void)ulTaskNotifyTake(
                    pdTRUE,
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

        (void)poll_mounted_card();
        (void)ulTaskNotifyTake(
            pdTRUE,
            pdMS_TO_TICKS(kStorageStatusPollMs)
        );
    }
}

}  // namespace

extern "C" esp_err_t qsee_storage_init(void)
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

    auto &settings = qsee::UserSettingsService::instance();
    if (!settings.isInitialized()) {
        const esp_err_t init_error = settings.initialize();
        if (init_error != ESP_OK) {
            qsee::DeviceSnapshotService::instance().reportStorageMirror(
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
            qsee::DeviceSnapshotService::instance().reportStorageMirror(
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
        qsee::DeviceSnapshotService::instance().reportStorageMirror(
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
    qsee::DeviceSnapshotService::instance().reportStorageMirror(
        s_logger_mirror_requested,
        false,
        ESP_OK,
        0,
        0,
        0
    );

    if (xTaskCreate(
            storage_task,
            "qsee_storage",
            7168,
            nullptr,
            3,
            &s_storage_task
        ) != pdPASS) {
        s_storage_task = nullptr;
        qsee::StorageMountReport report;
        report.outcome = qsee::StorageMountOutcome::InternalFailed;
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

extern "C" esp_err_t qsee_storage_request_format(void)
{
    std::lock_guard<std::mutex> lock(s_storage_mutex);
    if (s_terminal_failure ||
        s_mounted ||
        s_storage_task == nullptr ||
        s_format_authorization_armed ||
        !s_waiting_for_format_authorization ||
        !s_format_candidate_valid) {
        ESP_LOGE(
            TAG,
            "SD format rejected: terminal=%s mounted=%s task=%s "
            "armed=%s waiting=%s candidate=%s candidate_attempt=%lu",
            s_terminal_failure ? "yes" : "no",
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

extern "C" esp_err_t qsee_storage_set_logger_mirror(bool enabled)
{
    esp_err_t aggregate = ESP_OK;
    std::lock_guard<std::mutex> lock(s_storage_mutex);
    auto &settings = qsee::UserSettingsService::instance();
    if (!settings.isInitialized()) {
        const esp_err_t init_error = settings.initialize();
        if (init_error != ESP_OK) {
            qsee::DeviceSnapshotService::instance().reportStorageMirror(
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
        qsee::DeviceSnapshotService::instance().reportStorageMirror(
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

    esp_err_t apply_error = ESP_OK;
    if (enabled && s_mounted) {
        apply_error = qsee_logger_enable_sd(BSP_SD_MOUNT_POINT);
    } else if (!enabled) {
        apply_error = qsee_logger_disable_sd();
    }
    if (apply_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Applying SD mirror preference failed (%s)",
            esp_err_to_name(apply_error)
        );
    }
    record_first_error(apply_error, &aggregate);

    if (s_mounted || !enabled) {
        record_first_error(refresh_logger_status_locked(), &aggregate);
    } else {
        s_logger_mirror_enabled = false;
        qsee::DeviceSnapshotService::instance().reportStorageMirror(
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
