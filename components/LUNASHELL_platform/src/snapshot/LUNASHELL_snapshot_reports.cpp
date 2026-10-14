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
 *
 * VBWR E */

#include "LUNASHELL_snapshot_internal.hpp"
#include "LUNASHELL_snapshot_cadence.hpp"
#include "LUNASHELL_sensor_fusion.hpp"
#include "LUNASHELL_logger.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "esp_log.h"
#include "esp_timer.h"

namespace {
constexpr char TAG[] = "LUNASHELL_snapshot";
constexpr int64_t kLoggerLossReminderUs = 60000000;
// The full acquisition mutex serializes this optional diagnostic reminder.
int64_t s_logger_loss_notice_at_us;
}

namespace LUNASHELL::snapshot_detail {
// ready: Hardware readiness confirmed by the component owner.
// error: Original operation error; publication never hides a failure.
LUNASHELL::DeviceHealth health_for(bool ready, esp_err_t error)
{
    if (ready) {
        return LUNASHELL::DeviceHealth::Ready;
    }
    if (error == ESP_ERR_NOT_SUPPORTED) {
        return LUNASHELL::DeviceHealth::Unknown;
    }
    if (error == ESP_ERR_NOT_FOUND) {
        return LUNASHELL::DeviceHealth::Missing;
    }
    return LUNASHELL::DeviceHealth::Degraded;
}

// component: Canonical component state whose validity is being updated.
// ready: Hardware readiness confirmed by the component owner.
// error: Original operation error; publication never hides a failure.
void set_component(LUNASHELL::ComponentSnapshot &component, bool ready, esp_err_t error)
{
    component.health = health_for(ready, error);
    component.last_error = error;
}

// component: Canonical component state whose validity is being updated.
// attempted_at_us: Real time at which acquisition or publication was attempted.
// completed_at_us: Real time at which the hardware read completed successfully.
void mark_sample_success(
    LUNASHELL::ComponentSnapshot &component,
    int64_t attempted_at_us,
    int64_t completed_at_us
)
{
    set_component(component, true, ESP_OK);
    component.data_valid = true;
    component.data_stale = false;
    component.last_attempt_at_us = attempted_at_us;
    component.last_success_at_us = completed_at_us;
}

// component: Canonical component state whose validity is being updated.
// sampled_at_us: Hardware sample timestamp used for both attempt and success.
void mark_sample_success(
    LUNASHELL::ComponentSnapshot &component,
    int64_t sampled_at_us
)
{
    mark_sample_success(component, sampled_at_us, sampled_at_us);
}

// component: Canonical component state whose validity is being updated.
// error: Original operation error; publication never hides a failure.
// attempted_at_us: Real time at which acquisition or publication was attempted.
void mark_sample_failure(
    LUNASHELL::ComponentSnapshot &component,
    esp_err_t error,
    int64_t attempted_at_us
)
{
    set_component(component, false, error);
    component.data_valid = false;
    component.data_stale = component.last_success_at_us > 0;
    component.last_attempt_at_us = attempted_at_us;
}

// operation: Component operation named in a fatal publication error.
// error: Original operation error; publication never hides a failure.
void log_snapshot_publication_failure(
    const char *operation,
    esp_err_t error
)
{
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "%s snapshot publication failed (%s)",
            operation,
            esp_err_to_name(error)
        );
        std::abort();
    }
}

}  // namespace LUNASHELL::snapshot_detail

namespace LUNASHELL {
using namespace snapshot_detail;

// preferences: Readback-confirmed settings or an explicitly invalid failure observation.
// Exception: Returns the original canonical publication failure.
esp_err_t DeviceSnapshotService::reportCodyPreferences(const CodyPreferencesSnapshot &preferences)
{
    // snapshot: Canonical storage, exclusively held by update_snapshot.
    const esp_err_t error = update_snapshot([&](DeviceSnapshot &snapshot) {
        const int64_t previous_success = snapshot.cody_preferences.last_success_at_us;
        snapshot.cody_preferences = preferences;
        if (!preferences.data_valid) {
            // A failed reload has no new success timestamp; retain real prior provenance.
            snapshot.cody_preferences.last_success_at_us = previous_success;
            snapshot.cody_preferences.data_stale = previous_success > 0;
        }
    });
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Cody preferences publication failed (%s)", esp_err_to_name(error));
    }
    return error;
}

// operation: Actual request or terminal write result; contains no sensor values or secrets.
// Exception: Returns publication/ordering errors separately from the persisted operation error.
esp_err_t DeviceSnapshotService::reportCharacterLookSave(const CharacterLookSaveSnapshot &operation)
{
    using State = CharacterLookSaveSnapshot::State;
    const int64_t now_us = esp_timer_get_time();
    const size_t length = strnlen(operation.requested_id, sizeof(operation.requested_id));
    const bool starting = operation.state == State::Saving;
    const bool terminal = operation.state == State::Saved || operation.state == State::Failed;
    if ((!starting && !terminal) || operation.request_id == 0 || length == 0 ||
        length == sizeof(operation.requested_id) || operation.started_at_us <= 0 ||
        operation.started_at_us > now_us ||
        (starting && (operation.error != ESP_OK || operation.completed_at_us != 0)) ||
        (terminal && (operation.completed_at_us < operation.started_at_us || operation.completed_at_us > now_us)) ||
        (operation.state == State::Saved && operation.error != ESP_OK) ||
        (operation.state == State::Failed && operation.error == ESP_OK)) {
        ESP_LOGE(TAG, "Look save publication rejected: request=%lu state=%u (%s)",
                 static_cast<unsigned long>(operation.request_id), static_cast<unsigned>(operation.state),
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t index = 0; index < length; ++index) {
        const auto character = static_cast<unsigned char>(operation.requested_id[index]);
        if (character < 32 || character > 126) {
            ESP_LOGE(TAG, "Look save ID rejected: byte=%u value=%u (%s)",
                     static_cast<unsigned>(index), static_cast<unsigned>(character),
                     esp_err_to_name(ESP_ERR_INVALID_ARG));
            return ESP_ERR_INVALID_ARG;
        }
    }
    esp_err_t ordering_error = ESP_OK;
    // snapshot: Canonical committed state, mutated only while its owner holds the lock.
    const esp_err_t publication_error = update_snapshot([&](DeviceSnapshot &snapshot) {
        const auto &previous = snapshot.character_look_save;
        if (starting) {
            if (previous.state == State::Saving || operation.request_id <= previous.request_id) {
                ordering_error = ESP_ERR_INVALID_STATE;
                return;
            }
        } else if (previous.state != State::Saving || previous.request_id != operation.request_id ||
                   previous.started_at_us != operation.started_at_us ||
                   std::strcmp(previous.requested_id, operation.requested_id) != 0) {
            ordering_error = ESP_ERR_INVALID_STATE;
            return;
        }
        snapshot.character_look_save = operation;
    });
    if (publication_error != ESP_OK || ordering_error != ESP_OK || operation.error != ESP_OK) {
        ESP_LOGE(TAG, "Look save request=%lu: write=%s ordering=%s publication=%s",
                 static_cast<unsigned long>(operation.request_id), esp_err_to_name(operation.error),
                 esp_err_to_name(ordering_error), esp_err_to_name(publication_error));
    }
    return publication_error != ESP_OK ? publication_error : ordering_error;
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
// input: Capture/transport result; its original timestamps and error remain intact.
// Exception: Returns the canonical publication error without changing sensor freshness.
esp_err_t DeviceSnapshotService::reportVoiceInput(const VoiceInputObservation &input)
{
    const bool known_phase = input.phase == VoiceInputPhase::Idle || input.phase == VoiceInputPhase::Listening ||
        input.phase == VoiceInputPhase::Sending || input.phase == VoiceInputPhase::Cancelling || input.phase == VoiceInputPhase::Received ||
        input.phase == VoiceInputPhase::Delivered || input.phase == VoiceInputPhase::Cancelled ||
        input.phase == VoiceInputPhase::Unconfirmed || input.phase == VoiceInputPhase::Failed;
    if (!known_phase || (input.trigger != VoiceInputTrigger::WakeWord && input.trigger != VoiceInputTrigger::DoubleTap) ||
        input.acknowledged_samples > input.captured_samples || input.started_at_us < 0 || input.changed_at_us <= 0 ||
        input.changed_at_us > esp_timer_get_time() || (input.phase == VoiceInputPhase::Failed && input.error == ESP_OK)) {
        ESP_LOGE(TAG, "Voice input publication rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    return update_snapshot([&](DeviceSnapshot &snapshot) {
        snapshot.voice.input = input;
    });
}

// state: Confirmed AFE state, including optional startup failures.
// error: Original operation error; a failed state never appears healthy.
// detected_at_us: Confirmed Sophia event timestamp, zero for a heartbeat.
// Exception: Returns rejected observations or the original snapshot publication failure.
esp_err_t DeviceSnapshotService::reportVoice(VoiceState state, esp_err_t error,
                                           int64_t detected_at_us)
{
    const int64_t now_us = esp_timer_get_time();
    const bool known_state = state == VoiceState::Disabled || state == VoiceState::Starting ||
        state == VoiceState::Listening || state == VoiceState::Paused || state == VoiceState::Failed;
    if (!known_state || detected_at_us < 0 || detected_at_us > now_us ||
        (detected_at_us != 0 && (state != VoiceState::Listening || error != ESP_OK)) ||
        (state == VoiceState::Failed && error == ESP_OK)) {
        ESP_LOGE(TAG, "Voice publication rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t event_error = ESP_OK;
    const esp_err_t publication_error = update_snapshot([&](DeviceSnapshot &snapshot) {
        VoiceSnapshot &voice = snapshot.voice;
        if (detected_at_us != 0 && detected_at_us <= voice.detected_at_us) {
            event_error = ESP_ERR_INVALID_ARG;
            return;
        }
        voice.state = state;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
        voice.configured_word = WakeWordId::Sophia;
        std::strcpy(voice.model_name, "wn9_sophia_tts_v1");
#endif
        if (error == ESP_OK) {
            mark_sample_success(voice, now_us);
        } else {
            mark_sample_failure(voice, error, now_us);
        }
        if (detected_at_us != 0) {
            voice.detected_word = WakeWordId::Sophia;
            voice.detected_at_us = detected_at_us;
            ++voice.detection_sequence;
        }
    });
    if (publication_error != ESP_OK || event_error != ESP_OK || error != ESP_OK) {
        ESP_LOGE(TAG, "Voice state publication: operation=%s event=%s publication=%s",
                 esp_err_to_name(error), esp_err_to_name(event_error), esp_err_to_name(publication_error));
    }
    return publication_error != ESP_OK ? publication_error : event_error;
}

// audio: Statistics computed from one successfully captured real microphone block.
// sampled_at_us: Original capture completion timestamp.
// error: Capture failure preserved independently of snapshot publication errors.
// Exception: Returns rejected observations or the original snapshot publication failure.
esp_err_t DeviceSnapshotService::reportVoiceMicrophones(const AudioSnapshot &audio,
                                                       int64_t sampled_at_us,
                                                       esp_err_t error)
{
    if (sampled_at_us <= 0 || sampled_at_us > esp_timer_get_time() ||
        (error == ESP_OK && (audio.channel_count == 0 || audio.channel_count > 2 ||
                            audio.sample_rate_hz != 16000))) {
        ESP_LOGE(TAG, "Voice microphone publication rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (error == ESP_OK) {
        for (uint8_t channel = 0; channel < audio.channel_count; ++channel) {
            if (audio.channel_mean_level[channel] > audio.channel_peak_level[channel] ||
                audio.channel_peak_level[channel] > 32768U) {
                ESP_LOGE(TAG, "Voice microphone level rejected: channel=%u mean=%lu peak=%lu (%s)",
                         static_cast<unsigned>(channel),
                         static_cast<unsigned long>(audio.channel_mean_level[channel]),
                         static_cast<unsigned long>(audio.channel_peak_level[channel]),
                         esp_err_to_name(ESP_ERR_INVALID_ARG));
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    const esp_err_t publication_error = update_snapshot([&](DeviceSnapshot &snapshot) {
        AudioSnapshot &target = snapshot.audio;
        if (error != ESP_OK) {
            mark_sample_failure(target.microphone, error, sampled_at_us);
            return;
        }
        target.channel_count = audio.channel_count;
        target.sample_rate_hz = audio.sample_rate_hz;
        target.microphone_mean_level = audio.microphone_mean_level;
        target.microphone_peak_level = audio.microphone_peak_level;
        for (uint8_t channel = 0; channel < audio.channel_count; ++channel) {
            target.channel_mean_level[channel] = audio.channel_mean_level[channel];
            target.channel_peak_level[channel] = audio.channel_peak_level[channel];
        }
        mark_sample_success(target.microphone, sampled_at_us);
    });
    if (error != ESP_OK || publication_error != ESP_OK) {
        ESP_LOGE(TAG, "Voice microphone publication: capture=%s publication=%s",
                 esp_err_to_name(error), esp_err_to_name(publication_error));
    }
    return publication_error;
}

#endif

// report: Storage-owner heartbeat; timestamps distinguish old I/O from live state.
// Exception: Returns the original optional publication failure without resetting the device.
esp_err_t DeviceSnapshotService::reportStorageDebug(const StorageDebugReport &report)
{
    const esp_err_t publication_error = update_snapshot(
        [report](DeviceSnapshot &snapshot) {
            // This diagnostic never upgrades mount health or refreshes card data.
            snapshot.storage.debug = report;
        },
        snapshot_detail::SnapshotPublicationPolicy::Optional
    );
    if (publication_error != ESP_OK) {
        ESP_LOGE(TAG, "SD debug heartbeat publication failed (%s)",
            esp_err_to_name(publication_error));
    }
    return publication_error;
}

// report: Hardware owner result, including partial cleanup and its real errors.
void DeviceSnapshotService::reportStorage(
    const StorageMountReport &report
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const bool coherent_capacity =
        report.capacity_bytes > 0 &&
        report.filesystem_total_bytes > 0 &&
        report.filesystem_total_bytes <= report.capacity_bytes &&
        report.free_bytes <= report.filesystem_total_bytes;
    const bool coherent_mounted =
        report.outcome == StorageMountOutcome::Mounted &&
        report.error == ESP_OK &&
        report.cleanup_error == ESP_OK &&
        report.cleanup_complete &&
        !report.poisoned &&
        report.mounted &&
        report.card_response_verified &&
        !report.format_required &&
        coherent_capacity;
    esp_err_t effective_error = report.error;
    if (!coherent_mounted && effective_error == ESP_OK) {
        effective_error =
            report.cleanup_error != ESP_OK
                ? report.cleanup_error
                : (report.outcome == StorageMountOutcome::NotAttempted
                       ? ESP_ERR_INVALID_STATE
                       : ESP_ERR_INVALID_RESPONSE);
    }

    const esp_err_t publication_error = update_snapshot(
        [report, coherent_mounted, effective_error, attempted_at_us](
            DeviceSnapshot &snapshot
        ) {
            snapshot.storage.mount_outcome = report.outcome;
            snapshot.storage.mount_stage = report.stage;
            snapshot.storage.cleanup_stage = report.cleanup_stage;
            snapshot.storage.cleanup_error = report.cleanup_error;
            snapshot.storage.initial_fatfs_result_valid =
                report.initial_fatfs_result_valid;
            snapshot.storage.initial_fatfs_result =
                report.initial_fatfs_result;
            snapshot.storage.fatfs_result_valid =
                report.fatfs_result_valid;
            snapshot.storage.fatfs_result = report.fatfs_result;
            snapshot.storage.cleanup_fatfs_result_valid =
                report.cleanup_fatfs_result_valid;
            snapshot.storage.cleanup_fatfs_result =
                report.cleanup_fatfs_result;
            snapshot.storage.cleanup_complete = report.cleanup_complete;
            snapshot.storage.poisoned = report.poisoned;
            snapshot.storage.mounted = report.mounted;
            snapshot.storage.card_response_verified =
                report.card_response_verified;
            snapshot.storage.format_required = report.format_required;
            snapshot.storage.authorization_consumed =
                report.authorization_consumed;
            snapshot.storage.authorization_identity_matched =
                report.authorization_identity_matched;
            snapshot.storage.format_attempted =
                report.format_attempted;
            snapshot.storage.partition_written =
                report.partition_written;
            snapshot.storage.formatted = report.formatted;
            snapshot.storage.mount_attempt_count =
                report.mount_attempt_count;
            snapshot.storage.logger_mirror_requested =
                report.logger_mirror_requested;
            snapshot.storage.logger_mirror_enabled =
                report.logger_mirror_enabled;

            if (coherent_mounted) {
                snapshot.storage.capacity_bytes =
                    report.capacity_bytes;
                snapshot.storage.filesystem_total_bytes =
                    report.filesystem_total_bytes;
                snapshot.storage.free_bytes = report.free_bytes;
                mark_sample_success(
                    snapshot.storage,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.storage,
                    effective_error,
                    attempted_at_us
                );
                snapshot.storage.health = DeviceHealth::Degraded;
            }
        }
    );
    log_snapshot_publication_failure(
        "Storage report",
        publication_error
    );

    if (!coherent_mounted) {
        ESP_LOGW(
            TAG,
            "Storage report rejected as current data: outcome=%u mounted=%s card-response=%s error=%s cleanup=%s",
            static_cast<unsigned>(report.outcome),
            report.mounted ? "yes" : "no",
            report.card_response_verified ? "verified" : "unconfirmed",
            esp_err_to_name(effective_error),
            esp_err_to_name(report.cleanup_error)
        );
    }
}

// requested: Whether the logger mirror was explicitly requested.
// enabled: Whether the mirror owner actually enabled writes.
// error: Original operation or publication failure.
// last_errno: Original filesystem errno from the mirror owner.
// changed_at_us: Real time of the last mirror state change.
// failure_count: Cumulative failures counted by the mirror owner.
void DeviceSnapshotService::reportStorageMirror(
    bool requested,
    bool enabled,
    esp_err_t error,
    int last_errno,
    int64_t changed_at_us,
    uint32_t failure_count
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            snapshot.storage.logger_mirror_requested = requested;
            snapshot.storage.logger_mirror_enabled = enabled;
            snapshot.storage.logger_mirror_errno = last_errno;
            snapshot.storage.logger_mirror_changed_at_us = changed_at_us;
            snapshot.storage.logger_mirror_failure_count = failure_count;
            if (error == ESP_OK) {
                mark_sample_success(
                    snapshot.storage.logger_mirror,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.storage.logger_mirror,
                    error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Storage-mirror report",
        publication_error
    );
}

// No parameters: only the acquisition owner samples this optional runtime component.
// Exception: Returns the actual query/publication error; retained losses remain structured state.
esp_err_t DeviceSnapshotService::refreshLoggerCapture()
{
    LUNASHELL_logger_sd_status_t status{};
    LoggerCaptureReport capture;
    const esp_err_t query_error = LUNASHELL_logger_get_sd_status(&status);
    capture.query_error = query_error;
    capture.sampled_at_us = query_error == ESP_OK ? status.sampled_at_us : esp_timer_get_time();
    if (query_error == ESP_OK) {
        capture.boot_dropped_lines = status.boot_dropped_lines;
        capture.usb_dropped_lines = status.usb_dropped_lines;
        capture.format_dropped_lines = status.format_dropped_lines;
        capture.queue_dropped_lines = status.queue_dropped_lines;
        capture.truncated_lines = status.truncated_lines;
        capture.boot_retained_bytes = status.boot_retained_bytes;
        capture.boot_archive_bytes = status.boot_archive_bytes;
        capture.capture_error = status.capture_error;
        capture.capture_error_at_us = status.capture_error_at_us;
    }
    const esp_err_t publication_error = reportLoggerCapture(capture);
    const bool capture_incomplete = query_error == ESP_OK &&
        (capture.boot_dropped_lines != 0U || capture.usb_dropped_lines != 0U ||
         capture.format_dropped_lines != 0U || capture.queue_dropped_lines != 0U ||
         capture.truncated_lines != 0U || capture.capture_error != ESP_OK);
    if (capture_incomplete && (s_logger_loss_notice_at_us == 0 ||
        capture.sampled_at_us - s_logger_loss_notice_at_us >= kLoggerLossReminderUs)) {
        s_logger_loss_notice_at_us = capture.sampled_at_us;
        // Count every loss, but avoid a recursive warning for every full queue.
        // These are actual cumulative observations, not synthetic recovery data.
        ESP_LOGW(TAG,
            "Log losses: boot=%lu usb=%lu fmt=%lu queue=%lu trunc=%lu "
            "capture_error=%s at=%lld internal=%lu psram=%lu",
            static_cast<unsigned long>(capture.boot_dropped_lines),
            static_cast<unsigned long>(capture.usb_dropped_lines),
            static_cast<unsigned long>(capture.format_dropped_lines),
            static_cast<unsigned long>(capture.queue_dropped_lines),
            static_cast<unsigned long>(capture.truncated_lines),
            esp_err_to_name(capture.capture_error),
            static_cast<long long>(capture.capture_error_at_us),
            static_cast<unsigned long>(capture.boot_retained_bytes),
            static_cast<unsigned long>(capture.boot_archive_bytes));
    }
    return query_error != ESP_OK ? query_error : publication_error;
}

// report: Original owner query timestamp, retained memory and cumulative sink losses.
// Exception: Returns malformed observation or the original snapshot publication error.
esp_err_t DeviceSnapshotService::reportLoggerCapture(const LoggerCaptureReport &report)
{
    if (report.sampled_at_us <= 0 || report.sampled_at_us > esp_timer_get_time()) {
        ESP_LOGE(TAG, "Logger capture report rejected: timestamp=%lld (%s)",
            static_cast<long long>(report.sampled_at_us), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    // snapshot: The current acquisition stage or exclusively owned canonical object.
    const esp_err_t error = update_snapshot([&report](DeviceSnapshot &snapshot) {
        auto &capture = snapshot.storage.logger_capture;
        if (report.query_error == ESP_OK) {
            capture.observation = report;
            mark_sample_success(capture, report.sampled_at_us);
            // A successful counter query is current data, even if a sink lost
            // earlier records. Its historical error remains in the observation;
            // Ready describes the query, never a guarantee of complete logging.
        } else {
            mark_sample_failure(capture, report.query_error, report.sampled_at_us);
        }
    }, SnapshotPublicationPolicy::Optional);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Logger capture publication failed (%s)", esp_err_to_name(error));
    }
    return error;
}

// sample_period_ms: Requested diagnostic cadence, accepted only within its defined bounds.
// error: Original operation or publication failure.
void DeviceSnapshotService::reportDiagnosticPeriod(
    uint32_t sample_period_ms,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        error == ESP_OK &&
                (sample_period_ms < kDiagnosticPeriodMinMs ||
                 sample_period_ms > kDiagnosticPeriodMaxMs)
            ? ESP_ERR_INVALID_ARG
            : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (effective_error == ESP_OK) {
                snapshot.diagnostics.sample_period_ms =
                    sample_period_ms;
                mark_sample_success(
                    snapshot.diagnostics.sample_period,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.diagnostics.sample_period,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Diagnostic-period report",
        publication_error
    );
}

// imu_smoothing_percent: Validated IMU smoothing setting; corrupt values are rejected.
// error: Original operation or publication failure.
void DeviceSnapshotService::reportImuSmoothing(
    uint32_t imu_smoothing_percent,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        error == ESP_OK &&
                imu_smoothing_percent > kImuSmoothingMaxPercent
            ? ESP_ERR_INVALID_ARG
            : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (effective_error == ESP_OK) {
                snapshot.diagnostics.imu_smoothing_percent =
                    imu_smoothing_percent;
                mark_sample_success(
                    snapshot.diagnostics.imu_smoothing,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.diagnostics.imu_smoothing,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "IMU-smoothing report",
        publication_error
    );
}

// full_snapshot_period_ms: Requested cadence for complete hardware snapshots.
// error: Original operation or publication failure.
void DeviceSnapshotService::reportFullSnapshotPeriod(
    uint32_t full_snapshot_period_ms,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        error == ESP_OK &&
                !isFullSnapshotPeriodSupported(full_snapshot_period_ms)
            ? ESP_ERR_INVALID_ARG
            : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (effective_error == ESP_OK) {
                snapshot.diagnostics.full_snapshot_period_ms =
                    full_snapshot_period_ms;
                mark_sample_success(
                    snapshot.diagnostics.full_snapshot_period,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.diagnostics.full_snapshot_period,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Full-snapshot-period report",
        publication_error
    );
}

// monitor_active: Whether the full-snapshot sampling worker is active.
// error: Original operation or publication failure.
void DeviceSnapshotService::reportFullSnapshotMonitor(
    bool monitor_active,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            snapshot.diagnostics.full_snapshot_monitor_active =
                monitor_active;
            if (error == ESP_OK) {
                mark_sample_success(
                    snapshot.diagnostics.full_snapshot_monitor,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.diagnostics.full_snapshot_monitor,
                    error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Full-snapshot-monitor report",
        publication_error
    );
}

// result: Measured sensor-fusion result published only on success.
// error: Original operation or publication failure.
// sampled_at_us: Original sensor acquisition timestamp, when provided.
void DeviceSnapshotService::reportSensorFusion(
    const SensorFusionResult &result,
    esp_err_t error,
    int64_t sampled_at_us
)
{
    const int64_t attempted_at_us =
        sampled_at_us > 0 ? sampled_at_us : esp_timer_get_time();
    const bool result_is_finite =
        std::isfinite(result.acceleration_magnitude_mps2) &&
        std::isfinite(result.angular_speed_dps);
    const esp_err_t effective_error =
        error == ESP_OK && !result_is_finite
            ? ESP_ERR_INVALID_RESPONSE
            : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (effective_error == ESP_OK) {
                snapshot.sensor_fusion.gravity_dominant =
                    result.gravity_dominant;
                snapshot.sensor_fusion.stationary = result.stationary;
                snapshot.sensor_fusion.acceleration_magnitude_mps2 =
                    result.acceleration_magnitude_mps2;
                snapshot.sensor_fusion.angular_speed_dps =
                    result.angular_speed_dps;
                mark_sample_success(
                    snapshot.sensor_fusion,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.sensor_fusion,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Sensor-fusion report",
        publication_error
    );
}

// distance: Distance owner snapshot with original sample validity.
void DeviceSnapshotService::reportDistance(const DistanceSnapshot &distance)
{
    const esp_err_t publication_error = update_snapshot(
        [&distance](DeviceSnapshot &snapshot) {
            snapshot.distance = distance;
        }
    );
    log_snapshot_publication_failure(
        "Distance report",
        publication_error
    );
}

// imu_monitor_active: Whether current IMU samples are being collected.
// microphone_monitor_active: Whether microphone level sampling is active.
// microphone_spectrum_monitor_active: Whether measured spectrum sampling is active.
// error: Original operation or publication failure.
void DeviceSnapshotService::reportDiagnosticMonitor(
    bool imu_monitor_active,
    bool microphone_monitor_active,
    bool microphone_spectrum_monitor_active,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (error == ESP_OK) {
                snapshot.diagnostics.monitor_active =
                    imu_monitor_active || microphone_monitor_active;
                snapshot.diagnostics.imu_monitor_active =
                    imu_monitor_active;
                snapshot.diagnostics.microphone_monitor_active =
                    microphone_monitor_active;
                snapshot.diagnostics.microphone_spectrum_monitor_active =
                    microphone_spectrum_monitor_active;
                mark_sample_success(
                    snapshot.diagnostics.monitor,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.diagnostics.monitor,
                    error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Diagnostic-monitor report",
        publication_error
    );
}

// brightness_percent: Brightness confirmed by the display/settings owner.
// error: Original operation or publication failure.
void DeviceSnapshotService::reportDisplayBrightness(
    uint32_t brightness_percent,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        error == ESP_OK && brightness_percent > 100U
            ? ESP_ERR_INVALID_ARG
            : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (effective_error == ESP_OK) {
                snapshot.ui.brightness_percent =
                    brightness_percent;
                mark_sample_success(
                    snapshot.ui.brightness,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.ui.brightness,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Display-brightness report",
        publication_error
    );
}

// brightness_percent: Brightness confirmed by the display/settings owner.
// error: Original operation or publication failure.
void DeviceSnapshotService::reportDisplayConfiguredBrightness(
    uint32_t brightness_percent,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        error == ESP_OK && brightness_percent > 100U
            ? ESP_ERR_INVALID_ARG
            : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (effective_error == ESP_OK) {
                snapshot.ui.configured_brightness_percent = brightness_percent;
                mark_sample_success(
                    snapshot.ui.brightness,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.ui.brightness,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Configured-display-brightness report",
        publication_error
    );
}

// display_power_mode: Validated persisted display power policy.
// error: Original operation or publication failure.
void DeviceSnapshotService::reportDisplayPowerMode(
    UiHardwareSnapshot::DisplayPowerMode display_power_mode,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        error == ESP_OK && display_power_mode == UiHardwareSnapshot::DisplayPowerMode::Unknown
            ? ESP_ERR_INVALID_ARG
            : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (effective_error == ESP_OK) {
                snapshot.ui.display_power_mode_state = display_power_mode;
                mark_sample_success(
                    snapshot.ui.display_power_mode,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.ui.display_power_mode,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Display-power-mode report",
        publication_error
    );
}

// pairing: Original controller result, including failure and confirmed cleanup.
void DeviceSnapshotService::reportPairingSession(const PairingSessionSnapshot &pairing)
{
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            snapshot.radios.pairing = pairing;
        }
    );
    log_snapshot_publication_failure("Pairing session report", publication_error);
}

// codex_link: Synchronously borrowed CCP1 result without code or cryptographic material.
// Exception: Returns and logs the original canonical publication failure.
esp_err_t DeviceSnapshotService::reportCodexLink(const CodexLinkSnapshot &codex_link)
{
    const esp_err_t publication_error = update_snapshot(
        [&codex_link](DeviceSnapshot &snapshot) {
            snapshot.radios.codex_link = codex_link;
        }
    );
    log_snapshot_publication_failure("Codex link report", publication_error);
    return publication_error;
}

// event: Complete owner-produced event; the synchronous update never retains this reference.
// Exception: Returns and logs the original snapshot publication failure.
esp_err_t DeviceSnapshotService::reportCodexEvent(const CodexEventSnapshot &event)
{
    const esp_err_t error = update_snapshot([&event](DeviceSnapshot &snapshot) {
        snapshot.codex_event = event;
    });
    log_snapshot_publication_failure("Codex event report", error);
    return error;
}

// technical: Synchronously borrowed owner buffer; the snapshot never retains the reference.
// Exception: Returns the original canonical publication error.
esp_err_t DeviceSnapshotService::reportCodexTechnical(const CodexTechnicalSnapshot &technical)
{
    const esp_err_t error = update_snapshot([&technical](DeviceSnapshot &snapshot) {
        snapshot.codex_technical = technical;
    });
    log_snapshot_publication_failure("Codex technical report", error);
    return error;
}

// ready: Hardware readiness confirmed by a successful operation.
// error: Original operation or publication failure.
void DeviceSnapshotService::reportDisplay(bool ready, esp_err_t error)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        !ready && error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (ready && effective_error == ESP_OK) {
                mark_sample_success(snapshot.ui.display, attempted_at_us);
            } else {
                mark_sample_failure(
                    snapshot.ui.display,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure("Display report", publication_error);
}

// frame: Measured completed-frame result, preserving consumer retention from its own owner.
void DeviceSnapshotService::reportDisplayFrame(const DisplayFrameSnapshot &frame)
{
    const esp_err_t publication_error = update_snapshot([&frame](DeviceSnapshot &snapshot) {
        const uint32_t retained_consumer_bytes = snapshot.ui.rendering.retained_consumer_bytes;
        const esp_err_t consumer_error = snapshot.ui.rendering.retained_consumer_error;
        snapshot.ui.rendering = frame;
        snapshot.ui.rendering.retained_consumer_error = consumer_error;
        snapshot.ui.rendering.retained_consumer_bytes = retained_consumer_bytes;
        snapshot.ui.rendering.reset_required = frame.reset_required || retained_consumer_bytes != 0;
        // A healthy transfer cannot erase another owner's unresolved cleanup failure.
        esp_err_t display_error = frame.last_error;
        if (display_error == ESP_OK && retained_consumer_bytes != 0) {
            display_error = consumer_error != ESP_OK ? consumer_error : ESP_ERR_INVALID_STATE;
        }
        if (display_error == ESP_OK) {
            mark_sample_success(snapshot.ui.display, frame.completed_at_us);
        } else {
            mark_sample_failure(snapshot.ui.display, display_error, frame.completed_at_us);
        }
    });
    log_snapshot_publication_failure("Display frame report", publication_error);
}

// bytes: Exact consumer-owned allocation bytes retained after an ambiguous cleanup.
// error: Real cleanup failure; no memory is claimed to have been released.
void DeviceSnapshotService::reportDisplayConsumerRetention(uint32_t bytes, esp_err_t error)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t publication_error = update_snapshot([=](DeviceSnapshot &snapshot) {
        snapshot.ui.rendering.retained_consumer_bytes = bytes;
        snapshot.ui.rendering.reset_required = bytes != 0 || snapshot.ui.rendering.retained_dma_bytes != 0;
        snapshot.ui.rendering.retained_consumer_error = error;
        mark_sample_failure(snapshot.ui.display, error, attempted_at_us);
    });
    log_snapshot_publication_failure("Display consumer retention", publication_error);
}

// ready: Hardware readiness confirmed by a successful operation.
// error: Original operation or publication failure.
void DeviceSnapshotService::reportTouch(bool ready, esp_err_t error)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        !ready && error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (ready && effective_error == ESP_OK) {
                mark_sample_success(snapshot.ui.touch, attempted_at_us);
            } else {
                mark_sample_failure(
                    snapshot.ui.touch,
                    effective_error,
                    attempted_at_us
                );
                // The acquisition failure interrupts any body gesture without
                // inventing coordinates or release. Retain its last good sample.
                mark_sample_failure(snapshot.character_touch, effective_error, attempted_at_us);
            }
        }
    );
    log_snapshot_publication_failure("Touch report", publication_error);
}



// ready: Hardware readiness confirmed by a successful operation.
// error: Original operation or publication failure.
void DeviceSnapshotService::reportSpeaker(bool ready, esp_err_t error)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t effective_error =
        !ready && error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (ready && effective_error == ESP_OK) {
                mark_sample_success(snapshot.audio.speaker, attempted_at_us);
            } else {
                mark_sample_failure(
                    snapshot.audio.speaker,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure("Speaker report", publication_error);
}

// wifi: Wi-Fi owner result, including connectivity and the real error.
void DeviceSnapshotService::reportWifi(const WifiSnapshot &wifi)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            snapshot.radios.wifi = wifi;
            if (wifi.health == DeviceHealth::Ready &&
                wifi.last_error == ESP_OK) {
                mark_sample_success(
                    snapshot.radios.wifi,
                    attempted_at_us
                );
            } else {
                const esp_err_t effective_error =
                    wifi.last_error == ESP_OK
                        ? ESP_ERR_INVALID_STATE
                        : wifi.last_error;
                mark_sample_failure(
                    snapshot.radios.wifi,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure("Wi-Fi report", publication_error);
}

// provisioning: Original lifecycle state and timestamp, including observed failures.
void DeviceSnapshotService::reportProvisioning(const ProvisioningSnapshot &provisioning)
{
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            snapshot.radios.provisioning = provisioning;
        }
    );
    log_snapshot_publication_failure("Provisioning report", publication_error);
}

// ready: Hardware readiness confirmed by a successful operation.
// error: Original operation or publication failure.
// advertising: Whether the BLE driver confirmed active advertising.
// connected: Whether the BLE driver confirmed a live connection.
// connection_handle: Driver connection identity; no list position is used as identity.
// connection_count: Cumulative confirmed BLE connections.
// disconnection_count: Cumulative confirmed BLE disconnections.
// device_name: Verified BLE device identity, or null when unavailable.
// provisioning: Whether a provisioning session is active.
void DeviceSnapshotService::reportBle(
    bool ready,
    esp_err_t error,
    bool advertising,
    bool connected,
    uint16_t connection_handle,
    uint32_t connection_count,
    uint32_t disconnection_count,
    const char *device_name,
    bool provisioning
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const bool device_name_valid =
        device_name != nullptr &&
        device_name[0] != '\0' &&
        std::strlen(device_name) < sizeof(BleSnapshot::device_name);
    const esp_err_t effective_error =
        error == ESP_OK && ready && !device_name_valid
            ? ESP_ERR_INVALID_ARG
            : (!ready && error == ESP_OK
                   ? ESP_ERR_INVALID_STATE
                   : error);
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            if (ready && effective_error == ESP_OK) {
                mark_sample_success(
                    snapshot.radios.ble,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.radios.ble,
                    effective_error,
                    attempted_at_us
                );
            }
            snapshot.radios.ble.provisioning = provisioning;
            snapshot.radios.ble.advertising = advertising;
            snapshot.radios.ble.connected = connected;
            snapshot.radios.ble.connection_handle = connection_handle;
            snapshot.radios.ble.connection_count = connection_count;
            snapshot.radios.ble.disconnection_count = disconnection_count;
            if (device_name_valid) {
                strlcpy(
                    snapshot.radios.ble.device_name,
                    device_name,
                    sizeof(snapshot.radios.ble.device_name)
                );
            }
        }
    );
    log_snapshot_publication_failure("BLE report", publication_error);
}

// system_time_valid: Whether the time owner verified the current clock.
// synchronized: Whether an external clock synchronization completed.
// source: Verified system-time source name.
// timezone: Configured timezone identifier.
// error: Original operation or publication failure.
void DeviceSnapshotService::reportTime(
    bool system_time_valid,
    bool synchronized,
    const char *source,
    const char *timezone,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const bool source_valid =
        source != nullptr &&
        source[0] != '\0' &&
        std::strlen(source) < sizeof(RtcSnapshot::source);
    const bool timezone_valid =
        timezone != nullptr &&
        timezone[0] != '\0' &&
        std::strlen(timezone) < sizeof(RtcSnapshot::timezone);
    esp_err_t effective_error = error;
    if (effective_error == ESP_OK && !system_time_valid) {
        effective_error = ESP_ERR_INVALID_STATE;
    } else if (
        effective_error == ESP_OK &&
        (!source_valid || !timezone_valid)
    ) {
        effective_error = ESP_ERR_INVALID_ARG;
    }
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            snapshot.rtc.system_time_valid = system_time_valid;
            snapshot.rtc.synchronized = synchronized;
            if (source_valid) {
                strlcpy(
                    snapshot.rtc.source,
                    source,
                    sizeof(snapshot.rtc.source)
                );
            }
            if (timezone_valid) {
                strlcpy(
                    snapshot.rtc.timezone,
                    timezone,
                    sizeof(snapshot.rtc.timezone)
                );
            }
            if (system_time_valid && effective_error == ESP_OK) {
                mark_sample_success(
                    snapshot.rtc.system_time,
                    attempted_at_us
                );
            } else {
                mark_sample_failure(
                    snapshot.rtc.system_time,
                    effective_error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure("Time report", publication_error);
}

// clock: Time-owner observation; failed reads keep explicit unavailable routine state.
esp_err_t DeviceSnapshotService::reportClock(const ClockSnapshot &clock)
{
    const esp_err_t error = update_snapshot(
        [&clock](DeviceSnapshot &snapshot) { snapshot.clock = clock; }
    );
    log_snapshot_publication_failure("Clock report", error);
    return error;
}

// drift: Measured comparison with trusted UTC, including original sample/error state.
esp_err_t DeviceSnapshotService::reportRtcDrift(const RtcDriftSnapshot &drift)
{
    const esp_err_t error = update_snapshot(
        [&drift](DeviceSnapshot &snapshot) { snapshot.rtc_drift = drift; }
    );
    log_snapshot_publication_failure("RTC drift report", error);
    return error;
}

// inputs: Hardware input owner snapshot.
void DeviceSnapshotService::reportInputs(const InputSnapshot &inputs)
{
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) { snapshot.input = inputs; }
    );
    log_snapshot_publication_failure("Input report", publication_error);
}

// state: Power state confirmed by the orchestrator.
// reason: Explicit reason for the most recent power transition.
// audio_suspended: Whether the audio owner confirmed quiescence.
// fullscreen_active: Whether central content currently occupies the full canvas.
// command_arc_visible: Published visibility of the command arc.
// status_arc_visible: Published visibility of the status arc.
// display_idle_stage: Current typed stage of the display idle policy.
// command_arc_timeout_seconds: Configured command-arc inactivity threshold.
// status_arc_timeout_seconds: Configured status-arc inactivity threshold.
// display_dim_timeout_seconds: Configured delay before display dimming.
// screen_sleep_delay_seconds: Configured delay before display sleep.
// screen_idle_timeout_seconds: Timeout from the last activity, independent of dimming.
// transition_sequence: Monotonic power-transition identity.
// last_activity_at_us: Real timestamp of the latest accepted user activity.
// error: Original operation or publication failure.
void DeviceSnapshotService::reportPowerManagement(
    DeviceOperatingState state,
    DeviceStateReason reason,
    bool audio_suspended,
    bool fullscreen_active,
    bool command_arc_visible,
    bool status_arc_visible,
    UiHardwareSnapshot::DisplayPowerMode display_idle_stage,
    uint32_t command_arc_timeout_seconds,
    uint32_t status_arc_timeout_seconds,
    uint32_t display_dim_timeout_seconds,
    uint32_t screen_sleep_delay_seconds,
    uint32_t screen_idle_timeout_seconds,
    uint32_t transition_sequence,
    int64_t last_activity_at_us,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t publication_error = update_snapshot(
        [=](DeviceSnapshot &snapshot) {
            snapshot.power.state = state;
            snapshot.power.reason = reason;
            snapshot.power.audio_suspended = audio_suspended;
            snapshot.power.fullscreen_active = fullscreen_active;
            snapshot.power.command_arc_visible = command_arc_visible;
            snapshot.power.status_arc_visible = status_arc_visible;
            snapshot.power.display_idle_stage = display_idle_stage;
            snapshot.power.command_arc_timeout_seconds =
                command_arc_timeout_seconds;
            snapshot.power.status_arc_timeout_seconds =
                status_arc_timeout_seconds;
            snapshot.power.display_dim_timeout_seconds =
                display_dim_timeout_seconds;
            snapshot.power.screen_sleep_delay_seconds =
                screen_sleep_delay_seconds;
            snapshot.power.screen_idle_timeout_seconds =
                screen_idle_timeout_seconds;
            snapshot.power.transition_sequence = transition_sequence;
            snapshot.power.last_activity_at_us = last_activity_at_us;
            snapshot.power.changed_at_us = attempted_at_us;
            if (error == ESP_OK) {
                mark_sample_success(snapshot.power, attempted_at_us);
            } else {
                mark_sample_failure(
                    snapshot.power,
                    error,
                    attempted_at_us
                );
            }
        }
    );
    log_snapshot_publication_failure(
        "Power-management report",
        publication_error
    );
}


}  // namespace LUNASHELL

#if CONFIG_LUNASHELL_BUILD_ENABLE_TTS
// speech: Owner-produced state with original request identity and actual timestamps.
// Exception: Canonical publication failure follows the platform fatal invariant.
esp_err_t LUNASHELL::DeviceSnapshotService::reportSpeech(const SpeechSnapshot &speech)
{
    const esp_err_t error = snapshot_detail::update_snapshot([&speech](DeviceSnapshot &snapshot) {
        snapshot.speech = speech;
    });
    snapshot_detail::log_snapshot_publication_failure("speech", error);
    return error;
}
#endif
