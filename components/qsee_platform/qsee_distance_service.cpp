/*
 * Precision-distance acquisition for the QSee device firmware.
 *
 * One worker owns both TOF400F receivers. Runtime and calibration therefore
 * share the same serial transaction boundary and cannot race each other.
 */

#include "qsee_distance_service.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "qsee_tof400f.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_settings.hpp"

namespace {

constexpr char kTag[] = "qsee_distance";
constexpr char kSampleCountSetting[] = "tof_samples";
constexpr char kSampleIntervalSetting[] = "tof_interval";
constexpr char kReferenceSetting[] = "tof_ref_mm";
constexpr char kCalibrationSetting[] = "tof_cal";

constexpr uint16_t kDefaultSampleCount = qsee::kDistanceMaximumSampleCount;
constexpr uint32_t kDefaultSampleIntervalMs = 250;
constexpr uint16_t kDefaultReferenceMm = 1000;
constexpr uint32_t kMinimumSampleIntervalMs = 250;
constexpr uint32_t kMaximumSampleIntervalMs = 1000;
constexpr uint16_t kMinimumTofDistanceMm = 30;
constexpr uint16_t kMaximumTofDistanceMm = 4000;
constexpr uint16_t kMinimumEstimatorSamples = 20;
constexpr uint16_t kMinimumOutlierBandMm = 20;
constexpr uint16_t kMaximumCalibrationSensorSpreadMm = 100;
constexpr int32_t kMaximumCalibrationOffsetMm = 500;
constexpr float kMaximumCalibrationTiltDegrees = 5.0F;
constexpr float kMaximumRuntimeTiltDegrees = 15.0F;
constexpr uint32_t kMaximumTofPairSkewUs = 50000U;
constexpr uint32_t kMaximumImuFusionSkewUs = 50000U;
constexpr uint32_t kMaximumTofGeometrySkewUs = 500000U;
constexpr uint32_t kRecoveryInitialBackoffMs = 1000U;
constexpr uint32_t kRecoveryMaximumBackoffMs = 30000U;
constexpr uint32_t kCalibrationTransportFailureThreshold = 3U;
constexpr uint32_t kCalibrationDeadlineMarginMs = 30000U;
constexpr uint32_t kCalibrationRecordMagic = 0x544F4631U;  // "TOF1"
constexpr uint16_t kCalibrationRecordVersion = 2;
constexpr uint32_t kWorkerStackBytes = 12U * 1024U;
constexpr UBaseType_t kWorkerPriority = 4;
constexpr int64_t kWorkerWarningIntervalUs = 10'000'000;
constexpr float kPi = 3.14159265358979323846F;

struct CalibrationRecord {
    uint32_t magic = kCalibrationRecordMagic;
    uint16_t version = kCalibrationRecordVersion;
    uint16_t size = 24;
    uint16_t reference_distance_mm = 0;
    int8_t z_axis_sign = 0;
    uint8_t reserved = 0;
    int32_t offsets_mm[qsee::kTofSensorCount] = {};
    uint32_t crc32 = 0;
};

struct LegacyCalibrationRecordV1 {
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t reference_distance_mm = 0;
    int32_t offsets_mm[qsee::kTofSensorCount] = {};
};

static_assert(sizeof(CalibrationRecord) == 24);
static_assert(sizeof(LegacyCalibrationRecordV1) == 16);

struct SampleWindow {
    std::array<uint16_t, qsee::kDistanceMaximumSampleCount> values{};
    uint16_t count = 0;
    uint16_t next = 0;

    void clear()
    {
        count = 0;
        next = 0;
    }

    void push(uint16_t value, uint16_t capacity)
    {
        if (capacity == 0 || capacity > values.size()) {
            return;
        }
        if (count < capacity) {
            values[count++] = value;
            next = count == capacity ? 0 : count;
            return;
        }
        values[next] = value;
        next = static_cast<uint16_t>((next + 1U) % capacity);
    }
};

struct RobustEstimate {
    bool valid = false;
    uint16_t mean_mm = 0;
    uint16_t median_mm = 0;
    uint16_t mad_mm = 0;
    uint16_t inlier_count = 0;
};

struct SignedSampleWindow {
    std::array<int16_t, qsee::kDistanceMaximumSampleCount> values{};
    uint16_t count = 0;
    uint16_t next = 0;

    void clear()
    {
        count = 0;
        next = 0;
    }

    void push(int16_t value, uint16_t capacity)
    {
        if (capacity == 0 || capacity > values.size()) {
            return;
        }
        if (count < capacity) {
            values[count++] = value;
            next = count == capacity ? 0 : count;
            return;
        }
        values[next] = value;
        next = static_cast<uint16_t>((next + 1U) % capacity);
    }
};

struct RobustSignedEstimate {
    bool valid = false;
    int16_t mean_mm = 0;
    int16_t median_mm = 0;
    uint16_t mad_mm = 0;
    uint16_t inlier_count = 0;
};

struct PairTiming {
    bool valid = false;
    uint32_t tof_pair_skew_us = 0;
    uint32_t geometry_skew_us = 0;
    esp_err_t error = ESP_ERR_INVALID_STATE;
};

enum class WorkerWarningCategory : std::size_t {
    GeometryFailure,
    RuntimeTiming,
    RuntimePosture,
    RuntimeSign,
    RuntimeRange,
    CalibrationTiming,
    CalibrationPosture,
    CalibrationSign,
    CalibrationRange,
    Count,
};

struct WorkerWarningState {
    bool emitted = false;
    int64_t last_emitted_at_us = 0;
    uint32_t suppressed = 0;
};

std::mutex s_mutex;
bool s_initialized = false;
bool s_worker_started = false;
bool s_calibration_requested = false;
bool s_calibration_valid = false;
esp_err_t s_calibration_requirement_error = ESP_ERR_INVALID_STATE;
uint16_t s_sample_count = kDefaultSampleCount;
uint32_t s_sample_interval_ms = kDefaultSampleIntervalMs;
uint16_t s_reference_mm = kDefaultReferenceMm;
uint16_t s_calibration_reference_mm = 0;
int32_t s_offsets_mm[qsee::kTofSensorCount] = {};
int8_t s_calibration_z_axis_sign = 0;
int8_t s_pending_calibration_z_axis_sign = 0;
EXT_RAM_BSS_ATTR SampleWindow s_runtime_windows[qsee::kTofSensorCount];
EXT_RAM_BSS_ATTR SampleWindow
    s_runtime_vertical_windows[qsee::kTofSensorCount];
EXT_RAM_BSS_ATTR SampleWindow
    s_calibration_windows[qsee::kTofSensorCount];
EXT_RAM_BSS_ATTR SignedSampleWindow
    s_calibration_offset_windows[qsee::kTofSensorCount];
uint16_t s_calibration_rejected[qsee::kTofSensorCount] = {};
uint32_t s_calibration_attempts = 0;
uint32_t s_calibration_consecutive_transport_failures = 0;
int64_t s_calibration_deadline_at_us = 0;
uint32_t s_consecutive_transport_failures = 0;
qsee::DistanceSnapshot s_distance{};
TaskHandle_t s_worker = nullptr;
std::array<
    WorkerWarningState,
    static_cast<std::size_t>(WorkerWarningCategory::Count)
> s_worker_warning_states{};

qsee::DeviceHealth health_for_error(esp_err_t error)
{
    return error == ESP_ERR_NOT_FOUND || error == ESP_ERR_TIMEOUT
        ? qsee::DeviceHealth::Missing
        : qsee::DeviceHealth::Degraded;
}

void mark_success(qsee::ComponentSnapshot &component, int64_t sampled_at_us)
{
    component.health = qsee::DeviceHealth::Ready;
    component.last_error = ESP_OK;
    component.data_valid = true;
    component.data_stale = false;
    component.last_attempt_at_us = sampled_at_us;
    component.last_success_at_us = sampled_at_us;
}

void mark_failure(
    qsee::ComponentSnapshot &component,
    esp_err_t error,
    int64_t attempted_at_us
)
{
    component.health = health_for_error(error);
    component.last_error = error;
    component.data_valid = false;
    component.data_stale = component.last_success_at_us > 0;
    component.last_attempt_at_us = attempted_at_us;
}

void increment_saturated(uint32_t *value)
{
    if (value != nullptr && *value < UINT32_MAX) {
        ++(*value);
    }
}

bool take_worker_warning_slot(
    WorkerWarningCategory category,
    int64_t now_us,
    uint32_t *suppressed
)
{
    WorkerWarningState &state =
        s_worker_warning_states[static_cast<std::size_t>(category)];
    if (!state.emitted ||
        now_us < state.last_emitted_at_us ||
        now_us - state.last_emitted_at_us >= kWorkerWarningIntervalUs) {
        if (suppressed != nullptr) {
            *suppressed = state.suppressed;
        }
        state.emitted = true;
        state.last_emitted_at_us = now_us;
        state.suppressed = 0;
        return true;
    }

    // Only the single distance worker calls this; fixed state needs no lock.
    increment_saturated(&state.suppressed);
    return false;
}

uint64_t timestamp_delta_us(int64_t left, int64_t right)
{
    if (left <= 0 || right <= 0) {
        return UINT64_MAX;
    }
    const uint64_t unsigned_left = static_cast<uint64_t>(left);
    const uint64_t unsigned_right = static_cast<uint64_t>(right);
    return unsigned_left > unsigned_right
        ? unsigned_left - unsigned_right
        : unsigned_right - unsigned_left;
}

uint32_t bounded_diagnostic_us(uint64_t value)
{
    return static_cast<uint32_t>(
        std::min<uint64_t>(value, UINT32_MAX)
    );
}

uint32_t recovery_backoff_ms(uint32_t consecutive_failures)
{
    if (consecutive_failures == 0U) {
        return 0U;
    }
    const uint32_t shift = std::min<uint32_t>(
        consecutive_failures - 1U,
        5U
    );
    return std::min<uint32_t>(
        kRecoveryInitialBackoffMs << shift,
        kRecoveryMaximumBackoffMs
    );
}

int64_t calibration_deadline_at_us(
    int64_t started_at_us,
    uint16_t sample_count,
    uint32_t interval_ms
)
{
    const uint64_t attempt_budget_ms =
        static_cast<uint64_t>(sample_count) * 2ULL * interval_ms;
    const uint64_t total_budget_us =
        (attempt_budget_ms + kCalibrationDeadlineMarginMs) * 1000ULL;
    return started_at_us + static_cast<int64_t>(total_budget_us);
}

bool transport_pair_valid(
    const qsee_tof400f_pair_result_t &pair,
    esp_err_t read_error
)
{
    if (read_error != ESP_OK || pair.requested_at_us <= 0) {
        return false;
    }
    for (const qsee_tof400f_sensor_result_t &sensor : pair.sensors) {
        if (!sensor.valid || sensor.error != ESP_OK ||
            sensor.sampled_at_us <= 0) {
            return false;
        }
    }
    return true;
}

uint32_t update_transport_activity(bool available)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (available) {
        s_consecutive_transport_failures = 0;
        s_distance.consecutive_transport_failure_count = 0;
        s_distance.current_backoff_ms = 0;
        return 0;
    }
    increment_saturated(&s_consecutive_transport_failures);
    s_distance.consecutive_transport_failure_count =
        s_consecutive_transport_failures;
    s_distance.current_backoff_ms =
        recovery_backoff_ms(s_consecutive_transport_failures);
    return s_distance.current_backoff_ms;
}

void record_recovery_attempt()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    increment_saturated(&s_distance.recovery_attempt_count);
}

bool supported_interval(uint32_t interval_ms)
{
    return interval_ms == 250U || interval_ms == 500U ||
           interval_ms == 1000U;
}

bool valid_sample_count(uint16_t sample_count)
{
    return sample_count >= qsee::kDistanceMinimumSampleCount &&
           sample_count <= qsee::kDistanceMaximumSampleCount;
}

bool valid_reference(uint16_t reference_mm)
{
    return reference_mm >= qsee::kDistanceMinimumReferenceMm &&
           reference_mm <= qsee::kDistanceMaximumReferenceMm &&
           reference_mm % 10U == 0U;
}

bool plausible_distance(uint16_t distance_mm)
{
    return distance_mm >= kMinimumTofDistanceMm &&
           distance_mm <= kMaximumTofDistanceMm;
}

uint32_t calibration_record_crc32(const CalibrationRecord &record)
{
    const uint8_t *bytes =
        reinterpret_cast<const uint8_t *>(&record);
    uint32_t crc = UINT32_MAX;
    for (std::size_t index = 0;
         index < offsetof(CalibrationRecord, crc32);
         ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            const uint32_t mask =
                static_cast<uint32_t>(
                    -static_cast<int32_t>(crc & 1U)
                );
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

int8_t geometry_z_axis_sign(
    const qsee::DistanceGeometrySnapshot &geometry
)
{
    const float z = geometry.acceleration_mps2[2];
    if (!std::isfinite(z) || z == 0.0F) {
        return 0;
    }
    return z > 0.0F ? 1 : -1;
}

RobustEstimate robust_estimate(const SampleWindow &window)
{
    RobustEstimate estimate;
    if (window.count < kMinimumEstimatorSamples) {
        return estimate;
    }

    std::array<uint16_t, qsee::kDistanceMaximumSampleCount> sorted{};
    std::copy_n(window.values.begin(), window.count, sorted.begin());
    std::sort(sorted.begin(), sorted.begin() + window.count);
    estimate.median_mm = sorted[window.count / 2U];

    std::array<uint16_t, qsee::kDistanceMaximumSampleCount> deviations{};
    for (uint16_t index = 0; index < window.count; ++index) {
        const int32_t delta =
            static_cast<int32_t>(sorted[index]) -
            static_cast<int32_t>(estimate.median_mm);
        deviations[index] = static_cast<uint16_t>(
            delta < 0 ? -delta : delta
        );
    }
    std::sort(deviations.begin(), deviations.begin() + window.count);
    estimate.mad_mm = deviations[window.count / 2U];
    const uint32_t threshold_mm = std::max<uint32_t>(
        kMinimumOutlierBandMm,
        static_cast<uint32_t>(estimate.mad_mm) * 3U
    );

    uint32_t sum_mm = 0;
    for (uint16_t index = 0; index < window.count; ++index) {
        const uint32_t delta = sorted[index] > estimate.median_mm
            ? sorted[index] - estimate.median_mm
            : estimate.median_mm - sorted[index];
        if (delta <= threshold_mm) {
            sum_mm += sorted[index];
            ++estimate.inlier_count;
        }
    }
    const uint16_t minimum_inliers = std::max<uint16_t>(
        kMinimumEstimatorSamples,
        static_cast<uint16_t>((window.count * 80U + 99U) / 100U)
    );
    if (estimate.inlier_count < minimum_inliers) {
        return estimate;
    }
    estimate.mean_mm = static_cast<uint16_t>(
        (sum_mm + estimate.inlier_count / 2U) / estimate.inlier_count
    );
    estimate.valid = true;
    return estimate;
}

RobustSignedEstimate robust_signed_estimate(
    const SignedSampleWindow &window
)
{
    RobustSignedEstimate estimate;
    if (window.count < kMinimumEstimatorSamples) {
        return estimate;
    }

    std::array<int16_t, qsee::kDistanceMaximumSampleCount> sorted{};
    std::copy_n(window.values.begin(), window.count, sorted.begin());
    std::sort(sorted.begin(), sorted.begin() + window.count);
    estimate.median_mm = sorted[window.count / 2U];

    std::array<uint16_t, qsee::kDistanceMaximumSampleCount> deviations{};
    for (uint16_t index = 0; index < window.count; ++index) {
        const int32_t delta =
            static_cast<int32_t>(sorted[index]) -
            static_cast<int32_t>(estimate.median_mm);
        deviations[index] = static_cast<uint16_t>(
            delta < 0 ? -delta : delta
        );
    }
    std::sort(deviations.begin(), deviations.begin() + window.count);
    estimate.mad_mm = deviations[window.count / 2U];
    const uint32_t threshold_mm = std::max<uint32_t>(
        kMinimumOutlierBandMm,
        static_cast<uint32_t>(estimate.mad_mm) * 3U
    );

    int32_t sum_mm = 0;
    for (uint16_t index = 0; index < window.count; ++index) {
        const int32_t delta =
            static_cast<int32_t>(sorted[index]) -
            static_cast<int32_t>(estimate.median_mm);
        const uint32_t absolute_delta = static_cast<uint32_t>(
            delta < 0 ? -delta : delta
        );
        if (absolute_delta <= threshold_mm) {
            sum_mm += sorted[index];
            ++estimate.inlier_count;
        }
    }
    const uint16_t minimum_inliers = std::max<uint16_t>(
        kMinimumEstimatorSamples,
        static_cast<uint16_t>((window.count * 80U + 99U) / 100U)
    );
    if (estimate.inlier_count < minimum_inliers) {
        return estimate;
    }
    const int32_t rounded_mean =
        sum_mm >= 0
            ? (sum_mm + estimate.inlier_count / 2U) /
                  estimate.inlier_count
            : (sum_mm - estimate.inlier_count / 2U) /
                  estimate.inlier_count;
    if (rounded_mean < -kMaximumCalibrationOffsetMm ||
        rounded_mean > kMaximumCalibrationOffsetMm) {
        return estimate;
    }
    estimate.mean_mm = static_cast<int16_t>(rounded_mean);
    estimate.valid = true;
    return estimate;
}

esp_err_t load_or_create_uint32(
    qsee::UserSettingsService &settings,
    const char *key,
    const char *label,
    uint32_t first_boot_default,
    uint32_t *value
)
{
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t persisted = 0;
    esp_err_t error = settings.getUInt32(key, &persisted);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(
            kTag,
            "%s: first boot key '%s' absent; persisting explicit default %lu",
            label,
            key,
            static_cast<unsigned long>(first_boot_default)
        );
        error = settings.setUInt32(key, first_boot_default);
        if (error != ESP_OK) {
            ESP_LOGE(
                kTag,
                "%s: first-boot default persistence failed (%s)",
                label,
                esp_err_to_name(error)
            );
            return error;
        }
        *value = first_boot_default;
        return ESP_OK;
    }
    if (error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "%s: persisted read failed (%s)",
            label,
            esp_err_to_name(error)
        );
        return error;
    }
    *value = persisted;
    return ESP_OK;
}

esp_err_t load_settings()
{
    qsee::UserSettingsService &settings =
        qsee::UserSettingsService::instance();
    esp_err_t error = settings.initialize();
    if (error != ESP_OK) {
        return error;
    }

    uint32_t sample_count = 0;
    error = load_or_create_uint32(
        settings,
        kSampleCountSetting,
        "TOF robust sample count",
        kDefaultSampleCount,
        &sample_count
    );
    if (error != ESP_OK) {
        return error;
    }
    if (sample_count > UINT16_MAX ||
        !valid_sample_count(static_cast<uint16_t>(sample_count))) {
        ESP_LOGE(
            kTag,
            "TOF persisted sample count %lu outside [%u, %u]; explicit correction required",
            static_cast<unsigned long>(sample_count),
            qsee::kDistanceMinimumSampleCount,
            qsee::kDistanceMaximumSampleCount
        );
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t interval_ms = 0;
    error = load_or_create_uint32(
        settings,
        kSampleIntervalSetting,
        "TOF sample interval",
        kDefaultSampleIntervalMs,
        &interval_ms
    );
    if (error != ESP_OK) {
        return error;
    }
    if (!supported_interval(interval_ms)) {
        ESP_LOGE(
            kTag,
            "TOF persisted interval %lu ms is unsupported; expected 250, 500, or 1000 ms",
            static_cast<unsigned long>(interval_ms)
        );
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t reference_mm = 0;
    error = load_or_create_uint32(
        settings,
        kReferenceSetting,
        "TOF physical calibration reference",
        kDefaultReferenceMm,
        &reference_mm
    );
    if (error != ESP_OK) {
        return error;
    }
    if (reference_mm > UINT16_MAX ||
        !valid_reference(static_cast<uint16_t>(reference_mm))) {
        ESP_LOGE(
            kTag,
            "TOF persisted reference %lu mm outside 500..1500 mm in 10 mm steps; explicit correction required",
            static_cast<unsigned long>(reference_mm)
        );
        return ESP_ERR_INVALID_ARG;
    }

    s_calibration_valid = false;
    s_calibration_requirement_error = ESP_ERR_INVALID_STATE;
    s_calibration_reference_mm = 0;
    s_calibration_z_axis_sign = 0;
    std::fill(std::begin(s_offsets_mm), std::end(s_offsets_mm), 0);

    std::vector<uint8_t> calibration_data;
    error = settings.getBlob(kCalibrationSetting, &calibration_data);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(
            kTag,
            "TOF calibration record absent; measured distance remains explicitly uncalibrated"
        );
        s_calibration_valid = false;
        s_calibration_reference_mm = 0;
        s_calibration_z_axis_sign = 0;
        std::fill(std::begin(s_offsets_mm), std::end(s_offsets_mm), 0);
    } else if (error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "TOF calibration record read failed (%s)",
            esp_err_to_name(error)
        );
        return error;
    } else {
        if (calibration_data.size() ==
            sizeof(LegacyCalibrationRecordV1)) {
            LegacyCalibrationRecordV1 legacy{};
            std::memcpy(
                &legacy,
                calibration_data.data(),
                sizeof(legacy)
            );
            if (legacy.magic != kCalibrationRecordMagic ||
                legacy.version != 1U) {
                ESP_LOGE(
                    kTag,
                    "TOF legacy calibration header is corrupt; explicit reset is required"
                );
                return ESP_ERR_INVALID_RESPONSE;
            }
            ESP_LOGW(
                kTag,
                "TOF calibration version 1 has no mounting sign or CRC; explicit recalibration is required"
            );
            s_calibration_valid = false;
            s_calibration_requirement_error = ESP_ERR_NOT_SUPPORTED;
            s_calibration_reference_mm = 0;
            s_calibration_z_axis_sign = 0;
            std::fill(
                std::begin(s_offsets_mm),
                std::end(s_offsets_mm),
                0
            );
        } else if (
            calibration_data.size() != sizeof(CalibrationRecord)
        ) {
            ESP_LOGE(
                kTag,
                "TOF calibration record has invalid size %u; explicit recalibration/reset required",
                static_cast<unsigned>(calibration_data.size())
            );
            return ESP_ERR_INVALID_SIZE;
        } else {
            CalibrationRecord record{};
            std::memcpy(
                &record,
                calibration_data.data(),
                sizeof(record)
            );
            if (record.magic != kCalibrationRecordMagic ||
                record.version != kCalibrationRecordVersion ||
                record.size != static_cast<uint16_t>(
                    sizeof(CalibrationRecord)
                ) ||
                !valid_reference(record.reference_distance_mm) ||
                (record.z_axis_sign != -1 &&
                 record.z_axis_sign != 1) ||
                record.reserved != 0) {
                ESP_LOGE(
                    kTag,
                    "TOF calibration record header is invalid; explicit recalibration/reset required"
                );
                return ESP_ERR_INVALID_RESPONSE;
            }
            const uint32_t expected_crc =
                calibration_record_crc32(record);
            if (record.crc32 != expected_crc) {
                ESP_LOGE(
                    kTag,
                    "TOF calibration CRC mismatch: stored=0x%08lx expected=0x%08lx",
                    static_cast<unsigned long>(record.crc32),
                    static_cast<unsigned long>(expected_crc)
                );
                return ESP_ERR_INVALID_CRC;
            }
            for (std::size_t index = 0;
                 index < qsee::kTofSensorCount;
                 ++index) {
                if (record.offsets_mm[index] <
                        -kMaximumCalibrationOffsetMm ||
                    record.offsets_mm[index] >
                        kMaximumCalibrationOffsetMm) {
                    ESP_LOGE(
                        kTag,
                        "TOF calibration offset %d for sensor %u is invalid",
                        static_cast<int>(record.offsets_mm[index]),
                        static_cast<unsigned>(index + 1U)
                    );
                    return ESP_ERR_INVALID_RESPONSE;
                }
                s_offsets_mm[index] = record.offsets_mm[index];
            }
            s_calibration_reference_mm =
                record.reference_distance_mm;
            s_calibration_z_axis_sign = record.z_axis_sign;
            s_calibration_valid = true;
            s_calibration_requirement_error = ESP_OK;
        }
    }

    s_sample_count = static_cast<uint16_t>(sample_count);
    s_sample_interval_ms = interval_ms;
    s_reference_mm = static_cast<uint16_t>(reference_mm);
    return ESP_OK;
}

void initialize_published_state(
    int64_t attempted_at_us,
    esp_err_t settings_error,
    esp_err_t transport_error
)
{
    s_distance = {};
    s_distance.settings.sample_count = s_sample_count;
    s_distance.settings.sample_interval_ms = s_sample_interval_ms;
    s_distance.settings.reference_distance_mm = s_reference_mm;
    if (settings_error == ESP_OK) {
        mark_success(s_distance.settings, attempted_at_us);
    } else {
        mark_failure(
            s_distance.settings,
            settings_error,
            attempted_at_us
        );
    }

    for (std::size_t index = 0; index < qsee::kTofSensorCount; ++index) {
        s_distance.sensors[index].calibration_offset_mm =
            s_offsets_mm[index];
        mark_failure(
            s_distance.sensors[index],
            transport_error,
            attempted_at_us
        );
        s_distance.calibration.offsets_mm[index] = s_offsets_mm[index];
    }
    s_distance.calibration.state = s_calibration_valid
        ? qsee::DistanceCalibrationState::Ready
        : qsee::DistanceCalibrationState::Required;
    s_distance.calibration.record_valid = s_calibration_valid;
    s_distance.calibration.reference_distance_mm =
        s_calibration_reference_mm;
    s_distance.calibration.z_axis_sign =
        s_calibration_z_axis_sign;
    if (s_calibration_valid) {
        mark_success(s_distance.calibration, attempted_at_us);
    } else {
        mark_failure(
            s_distance.calibration,
            settings_error == ESP_OK
                ? s_calibration_requirement_error
                : settings_error,
            attempted_at_us
        );
    }
    mark_failure(s_distance, transport_error, attempted_at_us);
}

void update_worker_stack_telemetry_locked()
{
    // ESP-IDF reports this watermark in bytes on the supported ESP32-S3 port.
    const uint32_t free_bytes = static_cast<uint32_t>(
        uxTaskGetStackHighWaterMark(nullptr)
    );
    if (!s_distance.worker_stack_high_water_valid ||
        free_bytes < s_distance.worker_stack_min_free_bytes) {
        s_distance.worker_stack_min_free_bytes = free_bytes;
    }
    s_distance.worker_stack_high_water_valid = true;
}

void publish_locked(bool worker_context = false)
{
    if (worker_context) {
        update_worker_stack_telemetry_locked();
    }
    qsee::DeviceSnapshotService::instance().reportDistance(s_distance);
}

bool current_imu_geometry(
    const qsee::DistanceGeometrySnapshot &geometry,
    float *axis_factor,
    float *tilt_degrees
)
{
    if (axis_factor == nullptr || tilt_degrees == nullptr ||
        geometry.imu.health != qsee::DeviceHealth::Ready ||
        geometry.imu.last_error != ESP_OK ||
        !geometry.imu.data_valid || geometry.imu.data_stale ||
        !geometry.imu_calibrated ||
        geometry.sensor_fusion.health != qsee::DeviceHealth::Ready ||
        geometry.sensor_fusion.last_error != ESP_OK ||
        !geometry.sensor_fusion.data_valid ||
        geometry.sensor_fusion.data_stale ||
        !geometry.gravity_dominant ||
        !geometry.stationary ||
        timestamp_delta_us(
            geometry.imu.last_success_at_us,
            geometry.sensor_fusion.last_success_at_us
        ) > kMaximumImuFusionSkewUs) {
        return false;
    }
    const float x = geometry.acceleration_mps2[0];
    const float y = geometry.acceleration_mps2[1];
    const float z = geometry.acceleration_mps2[2];
    const float magnitude = std::sqrt(x * x + y * y + z * z);
    if (!std::isfinite(magnitude) || magnitude < 7.0F ||
        magnitude > 12.5F) {
        return false;
    }
    const float factor = std::clamp(std::abs(z) / magnitude, 0.0F, 1.0F);
    const float tilt = std::acos(factor) * 180.0F / kPi;
    if (!std::isfinite(tilt)) {
        return false;
    }
    *axis_factor = factor;
    *tilt_degrees = tilt;
    return true;
}

PairTiming evaluate_pair_timing(
    const qsee_tof400f_pair_result_t &pair,
    const qsee::DistanceGeometrySnapshot &geometry
)
{
    PairTiming timing;
    const uint64_t pair_skew_us = timestamp_delta_us(
        pair.sensors[0].sampled_at_us,
        pair.sensors[1].sampled_at_us
    );
    if (pair_skew_us != UINT64_MAX) {
        timing.tof_pair_skew_us =
            bounded_diagnostic_us(pair_skew_us);
    }
    if (pair.requested_at_us <= 0 ||
        pair_skew_us == UINT64_MAX) {
        timing.error = ESP_ERR_INVALID_RESPONSE;
        return timing;
    }

    const uint64_t imu_fusion_skew_us = timestamp_delta_us(
        geometry.imu.last_success_at_us,
        geometry.sensor_fusion.last_success_at_us
    );
    if (imu_fusion_skew_us == UINT64_MAX) {
        timing.error = ESP_ERR_INVALID_STATE;
        return timing;
    }

    const uint64_t geometry_skew_us = std::max({
        timestamp_delta_us(
            pair.requested_at_us,
            geometry.imu.last_success_at_us
        ),
        timestamp_delta_us(
            pair.requested_at_us,
            geometry.sensor_fusion.last_success_at_us
        ),
        timestamp_delta_us(
            pair.sensors[0].sampled_at_us,
            geometry.imu.last_success_at_us
        ),
        timestamp_delta_us(
            pair.sensors[0].sampled_at_us,
            geometry.sensor_fusion.last_success_at_us
        ),
        timestamp_delta_us(
            pair.sensors[1].sampled_at_us,
            geometry.imu.last_success_at_us
        ),
        timestamp_delta_us(
            pair.sensors[1].sampled_at_us,
            geometry.sensor_fusion.last_success_at_us
        ),
    });
    if (geometry_skew_us != UINT64_MAX) {
        timing.geometry_skew_us =
            bounded_diagnostic_us(geometry_skew_us);
    }
    if (pair_skew_us > kMaximumTofPairSkewUs) {
        timing.error = ESP_ERR_INVALID_RESPONSE;
        return timing;
    }
    if (imu_fusion_skew_us > kMaximumImuFusionSkewUs ||
        geometry_skew_us > kMaximumTofGeometrySkewUs) {
        timing.error = ESP_ERR_INVALID_STATE;
        return timing;
    }
    timing.valid = true;
    timing.error = ESP_OK;
    return timing;
}

uint8_t confidence_percent(
    uint16_t samples_used,
    uint16_t requested_samples,
    uint16_t maximum_mad_mm,
    uint16_t spread_mm,
    float tilt_degrees
)
{
    const uint32_t sample_score = requested_samples == 0
        ? 0U
        : std::min<uint32_t>(
              100U,
              static_cast<uint32_t>(samples_used) * 100U /
                  requested_samples
          );
    const uint32_t dispersion_score =
        maximum_mad_mm >= 50U ? 0U : 100U - maximum_mad_mm * 2U;
    const uint32_t agreement_score =
        spread_mm >= 50U ? 0U : 100U - spread_mm * 2U;
    const uint32_t tilt_penalty = static_cast<uint32_t>(
        std::min(tilt_degrees * 2.0F, 100.0F)
    );
    const uint32_t tilt_score = 100U - tilt_penalty;
    return static_cast<uint8_t>(
        std::min({sample_score, dispersion_score, agreement_score, tilt_score})
    );
}

void update_sensor_snapshot(
    std::size_t index,
    const qsee_tof400f_sensor_result_t &result,
    const SampleWindow &window,
    WorkerWarningCategory range_warning_category,
    int64_t attempted_at_us
)
{
    qsee::TofSensorSnapshot &sensor = s_distance.sensors[index];
    sensor.frame_sequence = result.frame_sequence;
    sensor.crc_error_count = result.crc_error_count;
    sensor.protocol_error_count = result.protocol_error_count;
    sensor.timeout_count = result.timeout_count;
    sensor.uart_error_count = result.uart_error_count;
    sensor.calibration_offset_mm = s_offsets_mm[index];
    sensor.window_sample_count = window.count;

    if (!result.valid || result.error != ESP_OK) {
        const esp_err_t error =
            result.error != ESP_OK ? result.error : ESP_ERR_INVALID_RESPONSE;
        mark_failure(sensor, error, attempted_at_us);
        return;
    }
    if (!plausible_distance(result.distance_mm)) {
        uint32_t suppressed = 0;
        if (take_worker_warning_slot(
                range_warning_category,
                attempted_at_us,
                &suppressed
            )) {
            ESP_LOGW(
                kTag,
                "TOF sensor %u rejected implausible %u mm sample; suppressed=%lu",
                static_cast<unsigned>(index + 1U),
                static_cast<unsigned>(result.distance_mm),
                static_cast<unsigned long>(suppressed)
            );
        }
        mark_failure(sensor, ESP_ERR_INVALID_RESPONSE, attempted_at_us);
        return;
    }

    sensor.raw_distance_mm = result.distance_mm;
    const RobustEstimate estimate = robust_estimate(window);
    if (estimate.valid) {
        sensor.filtered_distance_mm = estimate.mean_mm;
        sensor.inlier_count = estimate.inlier_count;
        sensor.median_absolute_deviation_mm = estimate.mad_mm;
    } else {
        sensor.filtered_distance_mm = 0;
        sensor.inlier_count = 0;
        sensor.median_absolute_deviation_mm = 0;
    }
    mark_success(
        sensor,
        result.sampled_at_us > 0
            ? result.sampled_at_us
            : attempted_at_us
    );
}

void mark_distance_unavailable(esp_err_t error, int64_t attempted_at_us)
{
    s_distance.slant_distance_mm = 0;
    s_distance.vertical_distance_mm = 0;
    s_distance.sensor_spread_mm = 0;
    s_distance.samples_used = 0;
    s_distance.tilt_degrees = 0.0F;
    s_distance.confidence_percent = 0;
    s_distance.tilt_compensated = false;
    mark_failure(s_distance, error, attempted_at_us);
}

void calculate_runtime_distance(
    const qsee::DistanceGeometrySnapshot &geometry,
    int64_t attempted_at_us
)
{
    if (!s_calibration_valid) {
        mark_distance_unavailable(ESP_ERR_INVALID_STATE, attempted_at_us);
        return;
    }
    for (const qsee::TofSensorSnapshot &sensor : s_distance.sensors) {
        if (sensor.health != qsee::DeviceHealth::Ready ||
            sensor.last_error != ESP_OK || !sensor.data_valid ||
            sensor.data_stale) {
            mark_distance_unavailable(
                sensor.last_error != ESP_OK
                    ? sensor.last_error
                    : ESP_ERR_INVALID_STATE,
                attempted_at_us
            );
            return;
        }
    }

    float axis_factor = 0.0F;
    float tilt_degrees = 0.0F;
    if (!current_imu_geometry(
            geometry,
            &axis_factor,
            &tilt_degrees
        ) ||
        tilt_degrees > kMaximumRuntimeTiltDegrees) {
        mark_distance_unavailable(ESP_ERR_INVALID_STATE, attempted_at_us);
        return;
    }
    const int8_t measured_z_axis_sign =
        geometry_z_axis_sign(geometry);
    if (measured_z_axis_sign == 0 ||
        measured_z_axis_sign != s_calibration_z_axis_sign) {
        mark_distance_unavailable(ESP_ERR_INVALID_STATE, attempted_at_us);
        return;
    }

    RobustEstimate vertical_estimates[qsee::kTofSensorCount] = {
        robust_estimate(s_runtime_vertical_windows[0]),
        robust_estimate(s_runtime_vertical_windows[1]),
    };
    for (std::size_t index = 0;
         index < qsee::kTofSensorCount;
         ++index) {
        if (s_runtime_vertical_windows[index].count < s_sample_count ||
            !vertical_estimates[index].valid) {
            mark_distance_unavailable(
                ESP_ERR_NOT_FINISHED,
                attempted_at_us
            );
            return;
        }
    }

    const uint16_t spread_mm = static_cast<uint16_t>(
        vertical_estimates[0].mean_mm > vertical_estimates[1].mean_mm
            ? vertical_estimates[0].mean_mm -
                  vertical_estimates[1].mean_mm
            : vertical_estimates[1].mean_mm -
                  vertical_estimates[0].mean_mm
    );
    const uint16_t vertical_mm = static_cast<uint16_t>(
        (
            static_cast<uint32_t>(vertical_estimates[0].mean_mm) +
            vertical_estimates[1].mean_mm +
            1U
        ) /
        2U
    );
    const uint16_t allowed_spread_mm = std::max<uint16_t>(
        40U,
        static_cast<uint16_t>(vertical_mm * 2U / 100U)
    );
    if (spread_mm > allowed_spread_mm) {
        uint32_t suppressed = 0;
        if (take_worker_warning_slot(
                WorkerWarningCategory::RuntimeRange,
                attempted_at_us,
                &suppressed
            )) {
            ESP_LOGW(
                kTag,
                "TOF pair rejected: verticalized sensors disagree by %u mm (limit %u mm); suppressed=%lu",
                static_cast<unsigned>(spread_mm),
                static_cast<unsigned>(allowed_spread_mm),
                static_cast<unsigned long>(suppressed)
            );
        }
        mark_distance_unavailable(ESP_ERR_INVALID_RESPONSE, attempted_at_us);
        return;
    }

    const uint32_t slant_mm = static_cast<uint32_t>(
        std::lround(
            static_cast<float>(vertical_mm) / axis_factor
        )
    );
    if (vertical_mm < kMinimumTofDistanceMm ||
        vertical_mm > kMaximumTofDistanceMm ||
        slant_mm < kMinimumTofDistanceMm ||
        slant_mm > kMaximumTofDistanceMm) {
        mark_distance_unavailable(ESP_ERR_INVALID_RESPONSE, attempted_at_us);
        return;
    }

    s_distance.slant_distance_mm = static_cast<uint16_t>(slant_mm);
    s_distance.vertical_distance_mm = vertical_mm;
    s_distance.sensor_spread_mm = spread_mm;
    s_distance.samples_used = std::min(
        vertical_estimates[0].inlier_count,
        vertical_estimates[1].inlier_count
    );
    s_distance.tilt_degrees = tilt_degrees;
    s_distance.confidence_percent = confidence_percent(
        s_distance.samples_used,
        s_sample_count,
        std::max(
            vertical_estimates[0].mad_mm,
            vertical_estimates[1].mad_mm
        ),
        spread_mm,
        tilt_degrees
    );
    s_distance.tilt_compensated = true;
    const int64_t derived_at_us = std::min({
        s_distance.sensors[0].last_success_at_us,
        s_distance.sensors[1].last_success_at_us,
        geometry.imu.last_success_at_us,
        geometry.sensor_fusion.last_success_at_us,
    });
    if (derived_at_us <= 0) {
        mark_distance_unavailable(ESP_ERR_INVALID_STATE, attempted_at_us);
        return;
    }
    mark_success(s_distance, derived_at_us);
    s_distance.last_attempt_at_us = attempted_at_us;
}

esp_err_t persist_calibration(
    const RobustSignedEstimate offsets[qsee::kTofSensorCount],
    int8_t z_axis_sign
)
{
    if (z_axis_sign != -1 && z_axis_sign != 1) {
        ESP_LOGE(
            kTag,
            "TOF calibration mounting sign %d is invalid",
            static_cast<int>(z_axis_sign)
        );
        return ESP_ERR_INVALID_ARG;
    }
    CalibrationRecord record{};
    record.size = static_cast<uint16_t>(sizeof(record));
    record.reference_distance_mm = s_reference_mm;
    record.z_axis_sign = z_axis_sign;
    for (std::size_t index = 0; index < qsee::kTofSensorCount; ++index) {
        if (!offsets[index].valid) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        const int32_t offset = offsets[index].mean_mm;
        if (offset < -kMaximumCalibrationOffsetMm ||
            offset > kMaximumCalibrationOffsetMm) {
            ESP_LOGE(
                kTag,
                "TOF calibration rejected sensor %u offset %d mm",
                static_cast<unsigned>(index + 1U),
                static_cast<int>(offset)
            );
            return ESP_ERR_INVALID_RESPONSE;
        }
        record.offsets_mm[index] = offset;
    }
    record.crc32 = calibration_record_crc32(record);

    qsee::UserSettingsService &settings =
        qsee::UserSettingsService::instance();
    const esp_err_t error = settings.setBlob(
        kCalibrationSetting,
        &record,
        sizeof(record)
    );
    if (error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "TOF calibration persistence failed (%s)",
            esp_err_to_name(error)
        );
        return error;
    }
    for (std::size_t index = 0; index < qsee::kTofSensorCount; ++index) {
        s_offsets_mm[index] = record.offsets_mm[index];
    }
    s_calibration_reference_mm = record.reference_distance_mm;
    s_calibration_z_axis_sign = record.z_axis_sign;
    s_calibration_valid = true;
    s_calibration_requirement_error = ESP_OK;
    return ESP_OK;
}

void begin_calibration_locked(int64_t started_at_us)
{
    for (SampleWindow &window : s_calibration_windows) {
        window.clear();
    }
    for (SignedSampleWindow &window : s_calibration_offset_windows) {
        window.clear();
    }
    for (SampleWindow &window : s_runtime_windows) {
        window.clear();
    }
    for (SampleWindow &window : s_runtime_vertical_windows) {
        window.clear();
    }
    for (qsee::TofSensorSnapshot &sensor : s_distance.sensors) {
        sensor.window_sample_count = 0;
        sensor.inlier_count = 0;
        sensor.filtered_distance_mm = 0;
        sensor.median_absolute_deviation_mm = 0;
    }
    std::fill(
        std::begin(s_calibration_rejected),
        std::end(s_calibration_rejected),
        0
    );
    s_calibration_attempts = 0;
    s_calibration_consecutive_transport_failures = 0;
    s_calibration_deadline_at_us = calibration_deadline_at_us(
        started_at_us,
        s_sample_count,
        s_sample_interval_ms
    );
    s_pending_calibration_z_axis_sign = 0;
    s_calibration_requested = false;
    s_distance.calibration.state =
        qsee::DistanceCalibrationState::Running;
    s_distance.calibration.record_valid = s_calibration_valid;
    s_distance.calibration.requested_samples = s_sample_count;
    s_distance.calibration.reference_distance_mm = s_reference_mm;
    s_distance.calibration.progress_percent = 0;
    s_distance.calibration.started_at_us = started_at_us;
    s_distance.calibration.completed_at_us = 0;
    std::fill(
        std::begin(s_distance.calibration.accepted_samples),
        std::end(s_distance.calibration.accepted_samples),
        0
    );
    std::fill(
        std::begin(s_distance.calibration.rejected_samples),
        std::end(s_distance.calibration.rejected_samples),
        0
    );
    mark_success(s_distance.calibration, started_at_us);
    mark_distance_unavailable(ESP_ERR_INVALID_STATE, started_at_us);
}

void finish_calibration_failure_locked(
    esp_err_t error,
    int64_t attempted_at_us
)
{
    s_calibration_consecutive_transport_failures = 0;
    s_calibration_deadline_at_us = 0;
    s_distance.calibration.state = s_calibration_valid
        ? qsee::DistanceCalibrationState::Ready
        : qsee::DistanceCalibrationState::Failed;
    s_distance.calibration.record_valid = s_calibration_valid;
    s_distance.calibration.completed_at_us = attempted_at_us;
    if (s_calibration_valid) {
        s_distance.calibration.reference_distance_mm =
            s_calibration_reference_mm;
        for (std::size_t index = 0;
             index < qsee::kTofSensorCount;
             ++index) {
            s_distance.calibration.offsets_mm[index] =
                s_offsets_mm[index];
        }
        s_distance.calibration.z_axis_sign =
            s_calibration_z_axis_sign;
        s_distance.calibration.health = qsee::DeviceHealth::Degraded;
        s_distance.calibration.last_error = error;
        s_distance.calibration.data_valid = true;
        s_distance.calibration.data_stale = false;
        s_distance.calibration.last_attempt_at_us = attempted_at_us;
    } else {
        s_distance.calibration.z_axis_sign = 0;
        mark_failure(s_distance.calibration, error, attempted_at_us);
    }
    mark_distance_unavailable(error, attempted_at_us);
    ESP_LOGE(
        kTag,
        "TOF calibration failed after %lu attempts (%s)",
        static_cast<unsigned long>(s_calibration_attempts),
        esp_err_to_name(error)
    );
}

void process_calibration_sample(
    const qsee_tof400f_pair_result_t &pair,
    const qsee::DistanceGeometrySnapshot &geometry,
    esp_err_t geometry_error,
    bool transport_available,
    int64_t attempted_at_us
)
{
    float calibration_axis_factor = 0.0F;
    float calibration_tilt_degrees = 0.0F;
    const bool calibration_posture_valid =
        geometry_error == ESP_OK &&
        current_imu_geometry(
            geometry,
            &calibration_axis_factor,
            &calibration_tilt_degrees
        ) &&
        calibration_tilt_degrees <= kMaximumCalibrationTiltDegrees;
    const int8_t sample_z_axis_sign =
        geometry_z_axis_sign(geometry);
    const PairTiming timing = evaluate_pair_timing(pair, geometry);
    qsee_tof400f_sensor_result_t effective_sensors[
        qsee::kTofSensorCount
    ] = {
        pair.sensors[0],
        pair.sensors[1],
    };
    bool pair_usable =
        calibration_posture_valid && timing.valid &&
        sample_z_axis_sign != 0;
    esp_err_t rejection_error = geometry_error != ESP_OK
        ? geometry_error
        : timing.error;
    int16_t pair_offsets[qsee::kTofSensorCount] = {};

    for (std::size_t index = 0; index < qsee::kTofSensorCount; ++index) {
        const qsee_tof400f_sensor_result_t &sensor =
            effective_sensors[index];
        if (!sensor.valid || sensor.error != ESP_OK ||
            !plausible_distance(sensor.distance_mm)) {
            pair_usable = false;
            if (rejection_error == ESP_OK) {
                rejection_error = sensor.error != ESP_OK
                    ? sensor.error
                    : ESP_ERR_INVALID_RESPONSE;
            }
        }
    }
    if (pair_usable) {
        const int32_t expected_slant_mm = static_cast<int32_t>(
            std::lround(
                static_cast<float>(s_reference_mm) /
                calibration_axis_factor
            )
        );
        for (std::size_t index = 0;
             index < qsee::kTofSensorCount;
             ++index) {
            const int32_t offset_mm =
                expected_slant_mm -
                static_cast<int32_t>(
                    effective_sensors[index].distance_mm
                );
            if (offset_mm < -kMaximumCalibrationOffsetMm ||
                offset_mm > kMaximumCalibrationOffsetMm) {
                uint32_t suppressed = 0;
                if (take_worker_warning_slot(
                        WorkerWarningCategory::CalibrationRange,
                        attempted_at_us,
                        &suppressed
                    )) {
                    ESP_LOGW(
                        kTag,
                        "TOF calibration rejected pair: sensor %u sample %u mm derives offset %ld mm outside +/- %ld mm; suppressed=%lu",
                        static_cast<unsigned>(index + 1U),
                        static_cast<unsigned>(
                            effective_sensors[index].distance_mm
                        ),
                        static_cast<long>(offset_mm),
                        static_cast<long>(kMaximumCalibrationOffsetMm),
                        static_cast<unsigned long>(suppressed)
                    );
                }
                pair_usable = false;
                rejection_error = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            pair_offsets[index] = static_cast<int16_t>(offset_mm);
        }
    }
    if (!pair_usable && rejection_error == ESP_OK) {
        rejection_error = ESP_ERR_INVALID_STATE;
    }
    if (!timing.valid) {
        uint32_t suppressed = 0;
        if (take_worker_warning_slot(
                WorkerWarningCategory::CalibrationTiming,
                attempted_at_us,
                &suppressed
            )) {
            ESP_LOGW(
                kTag,
                "TOF calibration rejected unsynchronized pair: TOF skew=%lu us (limit %lu), geometry skew=%lu us (limit %lu), error=%s; suppressed=%lu",
                static_cast<unsigned long>(timing.tof_pair_skew_us),
                static_cast<unsigned long>(kMaximumTofPairSkewUs),
                static_cast<unsigned long>(timing.geometry_skew_us),
                static_cast<unsigned long>(kMaximumTofGeometrySkewUs),
                esp_err_to_name(timing.error),
                static_cast<unsigned long>(suppressed)
            );
        }
    } else if (!calibration_posture_valid) {
        uint32_t suppressed = 0;
        if (take_worker_warning_slot(
                WorkerWarningCategory::CalibrationPosture,
                attempted_at_us,
                &suppressed
            )) {
            ESP_LOGW(
                kTag,
                "TOF calibration rejected pair: fresh calibrated stationary IMU within %.1f degrees is required (%s); suppressed=%lu",
                static_cast<double>(kMaximumCalibrationTiltDegrees),
                esp_err_to_name(rejection_error),
                static_cast<unsigned long>(suppressed)
            );
        }
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    ++s_calibration_attempts;
    if (transport_available) {
        s_calibration_consecutive_transport_failures = 0;
    } else {
        increment_saturated(
            &s_calibration_consecutive_transport_failures
        );
    }
    const bool transport_failure_limit_reached =
        !transport_available &&
        s_calibration_consecutive_transport_failures >=
            kCalibrationTransportFailureThreshold;
    s_distance.acquisition_timing_valid = timing.valid;
    s_distance.tof_pair_skew_us = timing.tof_pair_skew_us;
    s_distance.geometry_skew_us = timing.geometry_skew_us;
    if (!timing.valid) {
        increment_saturated(&s_distance.timing_rejection_count);
    }
    if (pair_usable &&
        s_pending_calibration_z_axis_sign != 0 &&
        s_pending_calibration_z_axis_sign != sample_z_axis_sign) {
        uint32_t suppressed = 0;
        if (take_worker_warning_slot(
                WorkerWarningCategory::CalibrationSign,
                attempted_at_us,
                &suppressed
            )) {
            ESP_LOGW(
                kTag,
                "TOF calibration rejected mounting sign change from %d to %d; suppressed=%lu",
                static_cast<int>(s_pending_calibration_z_axis_sign),
                static_cast<int>(sample_z_axis_sign),
                static_cast<unsigned long>(suppressed)
            );
        }
        pair_usable = false;
        rejection_error = ESP_ERR_INVALID_STATE;
    }
    if (pair_usable) {
        if (s_pending_calibration_z_axis_sign == 0) {
            s_pending_calibration_z_axis_sign =
                sample_z_axis_sign;
        }
        for (std::size_t index = 0;
             index < qsee::kTofSensorCount;
             ++index) {
            s_calibration_windows[index].push(
                effective_sensors[index].distance_mm,
                s_sample_count
            );
            s_calibration_offset_windows[index].push(
                pair_offsets[index],
                s_sample_count
            );
        }
    } else {
        increment_saturated(&s_distance.rejected_pair_count);
        s_pending_calibration_z_axis_sign = 0;
        for (std::size_t index = 0;
             index < qsee::kTofSensorCount;
             ++index) {
            s_calibration_windows[index].clear();
            s_calibration_offset_windows[index].clear();
            if (s_calibration_rejected[index] < UINT16_MAX) {
                ++s_calibration_rejected[index];
            }
            effective_sensors[index].valid = false;
            if (effective_sensors[index].error == ESP_OK) {
                effective_sensors[index].error = rejection_error;
            }
        }
    }
    for (std::size_t index = 0; index < qsee::kTofSensorCount; ++index) {
        update_sensor_snapshot(
            index,
            effective_sensors[index],
            s_calibration_windows[index],
            WorkerWarningCategory::CalibrationRange,
            attempted_at_us
        );
        s_distance.calibration.accepted_samples[index] =
            s_calibration_windows[index].count;
        s_distance.calibration.rejected_samples[index] =
            s_calibration_rejected[index];
    }
    const uint16_t completed = std::min(
        s_calibration_windows[0].count,
        s_calibration_windows[1].count
    );
    s_distance.calibration.progress_percent = static_cast<uint8_t>(
        static_cast<uint32_t>(completed) * 100U / s_sample_count
    );
    s_distance.calibration.last_attempt_at_us = attempted_at_us;

    const int64_t evaluated_at_us = esp_timer_get_time();
    if (transport_failure_limit_reached) {
        const esp_err_t transport_error =
            rejection_error != ESP_OK
                ? rejection_error
                : ESP_ERR_INVALID_RESPONSE;
        ESP_LOGE(
            kTag,
            "TOF calibration aborted after %lu consecutive transport failures (%s)",
            static_cast<unsigned long>(
                s_calibration_consecutive_transport_failures
            ),
            esp_err_to_name(transport_error)
        );
        finish_calibration_failure_locked(
            transport_error,
            evaluated_at_us
        );
        publish_locked(true);
        return;
    }
    if (s_calibration_deadline_at_us > 0 &&
        evaluated_at_us >= s_calibration_deadline_at_us) {
        ESP_LOGE(
            kTag,
            "TOF calibration wall-clock deadline expired (%s)",
            esp_err_to_name(ESP_ERR_TIMEOUT)
        );
        finish_calibration_failure_locked(
            ESP_ERR_TIMEOUT,
            evaluated_at_us
        );
        publish_locked(true);
        return;
    }

    if (completed >= s_sample_count) {
        RobustEstimate estimates[qsee::kTofSensorCount] = {
            robust_estimate(s_calibration_windows[0]),
            robust_estimate(s_calibration_windows[1]),
        };
        RobustSignedEstimate offset_estimates[qsee::kTofSensorCount] = {
            robust_signed_estimate(s_calibration_offset_windows[0]),
            robust_signed_estimate(s_calibration_offset_windows[1]),
        };
        if (!estimates[0].valid || !estimates[1].valid ||
            !offset_estimates[0].valid || !offset_estimates[1].valid) {
            finish_calibration_failure_locked(
                ESP_ERR_INVALID_RESPONSE,
                attempted_at_us
            );
            publish_locked(true);
            return;
        }
        const uint16_t spread_mm =
            estimates[0].mean_mm > estimates[1].mean_mm
                ? estimates[0].mean_mm - estimates[1].mean_mm
                : estimates[1].mean_mm - estimates[0].mean_mm;
        if (spread_mm > kMaximumCalibrationSensorSpreadMm ||
            !calibration_posture_valid) {
            finish_calibration_failure_locked(
                ESP_ERR_INVALID_RESPONSE,
                attempted_at_us
            );
            publish_locked(true);
            return;
        }
        const esp_err_t persistence_error =
            persist_calibration(
                offset_estimates,
                s_pending_calibration_z_axis_sign
            );
        if (persistence_error != ESP_OK) {
            finish_calibration_failure_locked(
                persistence_error,
                attempted_at_us
            );
            publish_locked(true);
            return;
        }

        for (std::size_t index = 0; index < qsee::kTofSensorCount; ++index) {
            s_distance.sensors[index].calibration_offset_mm =
                s_offsets_mm[index];
            s_distance.calibration.offsets_mm[index] =
                s_offsets_mm[index];
            s_runtime_windows[index].clear();
            s_runtime_vertical_windows[index].clear();
        }
        s_distance.calibration.state =
            qsee::DistanceCalibrationState::Succeeded;
        s_distance.calibration.record_valid = true;
        s_distance.calibration.z_axis_sign =
            s_calibration_z_axis_sign;
        s_distance.calibration.progress_percent = 100;
        s_distance.calibration.completed_at_us = attempted_at_us;
        s_calibration_consecutive_transport_failures = 0;
        s_calibration_deadline_at_us = 0;
        mark_success(s_distance.calibration, attempted_at_us);
        ESP_LOGI(
            kTag,
            "TOF calibration complete: reference=%u mm offsets=[%d,%d] mm samples=%u",
            static_cast<unsigned>(s_reference_mm),
            static_cast<int>(s_offsets_mm[0]),
            static_cast<int>(s_offsets_mm[1]),
            static_cast<unsigned>(s_sample_count)
        );
        publish_locked(true);
        return;
    }

    const uint32_t maximum_attempts =
        static_cast<uint32_t>(s_sample_count) * 2U;
    if (s_calibration_attempts >= maximum_attempts) {
        finish_calibration_failure_locked(
            ESP_ERR_TIMEOUT,
            attempted_at_us
        );
    }
    publish_locked(true);
}

void process_runtime_sample(
    const qsee_tof400f_pair_result_t &pair,
    const qsee::DistanceGeometrySnapshot &geometry,
    esp_err_t geometry_error,
    int64_t attempted_at_us
)
{
    float axis_factor = 0.0F;
    float tilt_degrees = 0.0F;
    const bool geometry_valid =
        geometry_error == ESP_OK &&
        current_imu_geometry(
            geometry,
            &axis_factor,
            &tilt_degrees
        ) &&
        tilt_degrees <= kMaximumRuntimeTiltDegrees;
    const PairTiming timing = evaluate_pair_timing(pair, geometry);

    std::lock_guard<std::mutex> lock(s_mutex);
    qsee_tof400f_sensor_result_t effective_sensors[
        qsee::kTofSensorCount
    ] = {
        pair.sensors[0],
        pair.sensors[1],
    };
    uint16_t verticalized_mm[qsee::kTofSensorCount] = {};
    const int8_t measured_z_axis_sign =
        geometry_z_axis_sign(geometry);
    bool sensor_pair_valid = true;
    esp_err_t pair_error = ESP_OK;

    for (std::size_t index = 0; index < qsee::kTofSensorCount; ++index) {
        qsee_tof400f_sensor_result_t &sensor =
            effective_sensors[index];
        if (!sensor.valid || sensor.error != ESP_OK ||
            !plausible_distance(sensor.distance_mm)) {
            sensor_pair_valid = false;
            if (pair_error == ESP_OK) {
                pair_error = sensor.error != ESP_OK
                    ? sensor.error
                    : ESP_ERR_INVALID_RESPONSE;
            }
            continue;
        }
    }
    const bool mounting_sign_valid =
        measured_z_axis_sign != 0 &&
        measured_z_axis_sign == s_calibration_z_axis_sign;
    bool pair_usable =
        sensor_pair_valid && timing.valid && geometry_valid &&
        s_calibration_valid && mounting_sign_valid;
    s_distance.acquisition_timing_valid = timing.valid;
    s_distance.tof_pair_skew_us = timing.tof_pair_skew_us;
    s_distance.geometry_skew_us = timing.geometry_skew_us;
    if (!timing.valid) {
        increment_saturated(&s_distance.timing_rejection_count);
        pair_error = geometry_error != ESP_OK
            ? geometry_error
            : timing.error;
        uint32_t suppressed = 0;
        if (take_worker_warning_slot(
                WorkerWarningCategory::RuntimeTiming,
                attempted_at_us,
                &suppressed
            )) {
            ESP_LOGW(
                kTag,
                "TOF runtime rejected unsynchronized pair: TOF skew=%lu us (limit %lu), geometry skew=%lu us (limit %lu), error=%s; suppressed=%lu",
                static_cast<unsigned long>(timing.tof_pair_skew_us),
                static_cast<unsigned long>(kMaximumTofPairSkewUs),
                static_cast<unsigned long>(timing.geometry_skew_us),
                static_cast<unsigned long>(kMaximumTofGeometrySkewUs),
                esp_err_to_name(timing.error),
                static_cast<unsigned long>(suppressed)
            );
        }
    } else if (!geometry_valid) {
        pair_error = geometry_error != ESP_OK
            ? geometry_error
            : ESP_ERR_INVALID_STATE;
        uint32_t suppressed = 0;
        if (take_worker_warning_slot(
                WorkerWarningCategory::RuntimePosture,
                attempted_at_us,
                &suppressed
            )) {
            ESP_LOGW(
                kTag,
                "TOF runtime rejected pair: fresh calibrated stationary IMU within %.1f degrees is required (%s); suppressed=%lu",
                static_cast<double>(kMaximumRuntimeTiltDegrees),
                esp_err_to_name(pair_error),
                static_cast<unsigned long>(suppressed)
            );
        }
    }

    if (!sensor_pair_valid || !timing.valid) {
        if (pair_error == ESP_OK) {
            pair_error = ESP_ERR_INVALID_RESPONSE;
        }
        for (qsee_tof400f_sensor_result_t &sensor :
             effective_sensors) {
            sensor.valid = false;
            if (sensor.error == ESP_OK) {
                sensor.error = pair_error;
            }
        }
    }
    if (!sensor_pair_valid || !timing.valid || !geometry_valid) {
        increment_saturated(&s_distance.rejected_pair_count);
    }
    if (sensor_pair_valid && timing.valid && geometry_valid &&
        s_calibration_valid && !mounting_sign_valid) {
        increment_saturated(&s_distance.rejected_pair_count);
        uint32_t suppressed = 0;
        if (take_worker_warning_slot(
                WorkerWarningCategory::RuntimeSign,
                attempted_at_us,
                &suppressed
            )) {
            ESP_LOGW(
                kTag,
                "TOF runtime rejected mounting sign %d; calibration requires %d; suppressed=%lu",
                static_cast<int>(measured_z_axis_sign),
                static_cast<int>(s_calibration_z_axis_sign),
                static_cast<unsigned long>(suppressed)
            );
        }
    }

    if (pair_usable) {
        for (std::size_t index = 0;
             index < qsee::kTofSensorCount;
             ++index) {
            qsee_tof400f_sensor_result_t &sensor =
                effective_sensors[index];
            const int32_t corrected_slant_mm =
                static_cast<int32_t>(sensor.distance_mm) +
                s_offsets_mm[index];
            const int32_t corrected_vertical_mm = static_cast<int32_t>(
                std::lround(
                    static_cast<float>(corrected_slant_mm) *
                    axis_factor
                )
            );
            if (corrected_slant_mm < kMinimumTofDistanceMm ||
                corrected_slant_mm > kMaximumTofDistanceMm ||
                corrected_vertical_mm < kMinimumTofDistanceMm ||
                corrected_vertical_mm > kMaximumTofDistanceMm) {
                sensor.valid = false;
                sensor.error = ESP_ERR_INVALID_RESPONSE;
                pair_usable = false;
                uint32_t suppressed = 0;
                if (take_worker_warning_slot(
                        WorkerWarningCategory::RuntimeRange,
                        attempted_at_us,
                        &suppressed
                    )) {
                    ESP_LOGW(
                        kTag,
                        "TOF runtime rejected sensor %u sample %u mm: corrected slant=%ld mm vertical=%ld mm; suppressed=%lu",
                        static_cast<unsigned>(index + 1U),
                        static_cast<unsigned>(sensor.distance_mm),
                        static_cast<long>(corrected_slant_mm),
                        static_cast<long>(corrected_vertical_mm),
                        static_cast<unsigned long>(suppressed)
                    );
                }
                continue;
            }
            verticalized_mm[index] =
                static_cast<uint16_t>(corrected_vertical_mm);
        }
    }
    if (!pair_usable && sensor_pair_valid && timing.valid &&
        geometry_valid && s_calibration_valid) {
        increment_saturated(&s_distance.rejected_pair_count);
        for (qsee_tof400f_sensor_result_t &sensor :
             effective_sensors) {
            sensor.valid = false;
            if (sensor.error == ESP_OK) {
                sensor.error = ESP_ERR_INVALID_RESPONSE;
            }
        }
    }

    if (pair_usable) {
        for (std::size_t index = 0;
             index < qsee::kTofSensorCount;
             ++index) {
            s_runtime_windows[index].push(
                effective_sensors[index].distance_mm,
                s_sample_count
            );
            s_runtime_vertical_windows[index].push(
                verticalized_mm[index],
                s_sample_count
            );
        }
    } else {
        // A precision pair window is one synchronized acquisition. Any sensor
        // or posture gap invalidates both sides so old poses cannot reappear.
        for (std::size_t index = 0;
             index < qsee::kTofSensorCount;
             ++index) {
            s_runtime_windows[index].clear();
            s_runtime_vertical_windows[index].clear();
        }
    }

    for (std::size_t index = 0; index < qsee::kTofSensorCount; ++index) {
        update_sensor_snapshot(
            index,
            effective_sensors[index],
            s_runtime_windows[index],
            WorkerWarningCategory::RuntimeRange,
            attempted_at_us
        );
    }
    calculate_runtime_distance(geometry, attempted_at_us);
    publish_locked(true);
}

void publish_transport_failure(esp_err_t error, int64_t attempted_at_us)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    for (SampleWindow &window : s_runtime_windows) {
        window.clear();
    }
    for (SampleWindow &window : s_runtime_vertical_windows) {
        window.clear();
    }
    for (qsee::TofSensorSnapshot &sensor : s_distance.sensors) {
        sensor.window_sample_count = 0;
        sensor.inlier_count = 0;
        sensor.filtered_distance_mm = 0;
        sensor.median_absolute_deviation_mm = 0;
        mark_failure(sensor, error, attempted_at_us);
    }
    mark_distance_unavailable(error, attempted_at_us);
    s_distance.acquisition_timing_valid = false;
    publish_locked(true);
}

void delay_until_next_interval(int64_t cycle_started_at_us, uint32_t interval_ms)
{
    const int64_t deadline_us =
        cycle_started_at_us + static_cast<int64_t>(interval_ms) * 1000LL;
    const int64_t remaining_us = deadline_us - esp_timer_get_time();
    if (remaining_us <= 0) {
        vTaskDelay(1);
        return;
    }
    const uint32_t remaining_ms = static_cast<uint32_t>(
        (remaining_us + 999LL) / 1000LL
    );
    vTaskDelay(pdMS_TO_TICKS(remaining_ms));
}

void distance_worker(void *)
{
    while (true) {
        bool begin_calibration = false;
        bool calibration_running = false;
        uint32_t interval_ms = kDefaultSampleIntervalMs;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            begin_calibration = s_calibration_requested;
            if (begin_calibration) {
                begin_calibration_locked(esp_timer_get_time());
                publish_locked(true);
            }
            calibration_running =
                s_distance.calibration.state ==
                qsee::DistanceCalibrationState::Running;
            interval_ms = s_sample_interval_ms;
        }

        if (!qsee_tof400f_is_initialized()) {
            record_recovery_attempt();
            const esp_err_t init_error = qsee_tof400f_init();
            if (init_error != ESP_OK) {
                const uint32_t backoff_ms =
                    update_transport_activity(false);
                const int64_t failed_at_us = esp_timer_get_time();
                if (calibration_running) {
                    qsee_tof400f_pair_result_t failed_pair{};
                    for (qsee_tof400f_sensor_result_t &sensor :
                         failed_pair.sensors) {
                        sensor.error = init_error;
                    }
                    process_calibration_sample(
                        failed_pair,
                        {},
                        init_error,
                        false,
                        failed_at_us
                    );
                } else {
                    publish_transport_failure(init_error, failed_at_us);
                }
                vTaskDelay(pdMS_TO_TICKS(backoff_ms));
                continue;
            }
        }

        qsee_tof400f_pair_result_t pair{};
        const int64_t attempted_at_us = esp_timer_get_time();
        const esp_err_t read_error =
            qsee_tof400f_read_pair(&pair);
        if (read_error != ESP_OK) {
            ESP_LOGD(
                kTag,
                "TOF pair read completed with per-sensor errors (%s)",
                esp_err_to_name(read_error)
            );
        }
        const bool transport_available =
            transport_pair_valid(pair, read_error);
        const uint32_t backoff_ms =
            update_transport_activity(transport_available);
        qsee::DistanceGeometrySnapshot geometry{};
        esp_err_t geometry_error = read_error;
        if (transport_available) {
            geometry_error =
                qsee::DeviceSnapshotService::instance()
                    .sampleDistanceGeometry(&geometry);
            if (geometry_error != ESP_OK) {
                uint32_t suppressed = 0;
                if (take_worker_warning_slot(
                        WorkerWarningCategory::GeometryFailure,
                        attempted_at_us,
                        &suppressed
                    )) {
                    ESP_LOGW(
                        kTag,
                        "TOF geometry acquisition failed (%s); suppressed=%lu",
                        esp_err_to_name(geometry_error),
                        static_cast<unsigned long>(suppressed)
                    );
                }
            }
        } else {
            if (geometry_error == ESP_OK) {
                geometry_error = ESP_ERR_INVALID_RESPONSE;
            }
        }
        if (calibration_running) {
            process_calibration_sample(
                pair,
                geometry,
                geometry_error,
                transport_available,
                attempted_at_us
            );
        } else {
            process_runtime_sample(
                pair,
                geometry,
                geometry_error,
                attempted_at_us
            );
        }
        // The setting is a request-to-request cadence. Long-range conversion
        // time therefore consumes the interval instead of being added to it.
        delay_until_next_interval(
            attempted_at_us,
            transport_available ? interval_ms : backoff_ms
        );
    }
}

esp_err_t persist_setting(
    const char *key,
    uint32_t value,
    const char *label
)
{
    qsee::UserSettingsService &settings =
        qsee::UserSettingsService::instance();
    esp_err_t error = settings.initialize();
    if (error == ESP_OK) {
        error = settings.setUInt32(key, value);
    }
    if (error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "%s persistence failed (%s)",
            label,
            esp_err_to_name(error)
        );
    }
    return error;
}

void mark_settings_persistence_failure_locked(
    esp_err_t error,
    int64_t attempted_at_us
)
{
    s_distance.settings.health = qsee::DeviceHealth::Degraded;
    s_distance.settings.last_error = error;
    s_distance.settings.data_valid =
        s_distance.settings.last_success_at_us > 0;
    s_distance.settings.data_stale = false;
    s_distance.settings.last_attempt_at_us = attempted_at_us;
}

void reset_runtime_windows_locked()
{
    for (SampleWindow &window : s_runtime_windows) {
        window.clear();
    }
    for (SampleWindow &window : s_runtime_vertical_windows) {
        window.clear();
    }
    for (qsee::TofSensorSnapshot &sensor : s_distance.sensors) {
        sensor.window_sample_count = 0;
        sensor.inlier_count = 0;
        sensor.filtered_distance_mm = 0;
        sensor.median_absolute_deviation_mm = 0;
    }
    mark_distance_unavailable(ESP_ERR_NOT_FINISHED, esp_timer_get_time());
}

}  // namespace

namespace qsee {

esp_err_t distanceServiceInit()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_initialized) {
        if (qsee_tof400f_is_initialized()) {
            return ESP_OK;
        }
        const esp_err_t transport_error =
            s_distance.last_error != ESP_OK
                ? s_distance.last_error
                : ESP_ERR_INVALID_STATE;
        ESP_LOGE(
            kTag,
            "TOF service is running but its transport is unavailable (%s)",
            esp_err_to_name(transport_error)
        );
        return transport_error;
    }
    esp_err_t error = load_settings();
    if (error != ESP_OK) {
        initialize_published_state(
            esp_timer_get_time(),
            error,
            error
        );
        publish_locked();
        return error;
    }

    const esp_err_t transport_error = qsee_tof400f_init();
    initialize_published_state(
        esp_timer_get_time(),
        ESP_OK,
        transport_error == ESP_OK ? ESP_ERR_NOT_FINISHED : transport_error
    );
    publish_locked();

    s_worker_started =
        xTaskCreateWithCaps(
            distance_worker,
            "qsee_distance",
            kWorkerStackBytes,
            nullptr,
            kWorkerPriority,
            &s_worker,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        ) == pdPASS;
    if (!s_worker_started) {
        const esp_err_t task_error = ESP_ERR_NO_MEM;
        const esp_err_t cleanup_error = qsee_tof400f_deinit();
        const int64_t failed_at_us = esp_timer_get_time();
        for (qsee::TofSensorSnapshot &sensor : s_distance.sensors) {
            mark_failure(
                sensor,
                cleanup_error != ESP_OK ? cleanup_error : task_error,
                failed_at_us
            );
        }
        mark_distance_unavailable(task_error, failed_at_us);
        publish_locked();
        ESP_LOGE(kTag, "TOF acquisition worker creation failed");
        if (cleanup_error != ESP_OK) {
            ESP_LOGE(
                kTag,
                "TOF UART rollback after worker creation failure failed (%s)",
                esp_err_to_name(cleanup_error)
            );
        }
        return task_error;
    }
    s_initialized = true;
    ESP_LOGI(
        kTag,
        "TOF precision service ready: %u samples every %lu ms; reference=%u mm",
        static_cast<unsigned>(s_sample_count),
        static_cast<unsigned long>(s_sample_interval_ms),
        static_cast<unsigned>(s_reference_mm)
    );
    return transport_error;
}

esp_err_t distanceStartCalibration()
{
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_initialized || !s_worker_started) {
            ESP_LOGE(
                kTag,
                "TOF calibration rejected: service is not initialized"
            );
            return ESP_ERR_INVALID_STATE;
        }
        if (!qsee_tof400f_is_initialized()) {
            ESP_LOGE(
                kTag,
                "TOF calibration rejected: both configured receivers are required"
            );
            return ESP_ERR_INVALID_STATE;
        }
        if (s_calibration_requested ||
            s_distance.calibration.state ==
                DistanceCalibrationState::Running) {
            ESP_LOGW(
                kTag,
                "TOF calibration rejected: calibration already running"
            );
            return ESP_ERR_INVALID_STATE;
        }
    }

    qsee::DistanceGeometrySnapshot geometry{};
    const esp_err_t geometry_error =
        qsee::DeviceSnapshotService::instance()
            .sampleDistanceGeometry(&geometry);
    if (geometry_error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "TOF calibration rejected: fresh IMU/fusion sample failed (%s)",
            esp_err_to_name(geometry_error)
        );
        return geometry_error;
    }
    float axis_factor = 0.0F;
    float tilt_degrees = 0.0F;
    if (!current_imu_geometry(
            geometry,
            &axis_factor,
            &tilt_degrees
        ) ||
        tilt_degrees > kMaximumCalibrationTiltDegrees) {
        ESP_LOGE(
            kTag,
            "TOF calibration rejected: calibrated stationary IMU within %.1f degrees of level is required",
            static_cast<double>(kMaximumCalibrationTiltDegrees)
        );
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t sample_count = 0;
    uint32_t sample_interval_ms = 0;
    uint16_t reference_mm = 0;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_initialized || !s_worker_started ||
            !qsee_tof400f_is_initialized() ||
            s_calibration_requested ||
            s_distance.calibration.state ==
                DistanceCalibrationState::Running) {
            ESP_LOGW(
                kTag,
                "TOF calibration rejected: service state changed during IMU validation"
            );
            return ESP_ERR_INVALID_STATE;
        }
        s_calibration_requested = true;
        sample_count = s_sample_count;
        sample_interval_ms = s_sample_interval_ms;
        reference_mm = s_reference_mm;
    }
    ESP_LOGI(
        kTag,
        "TOF calibration accepted: %u samples, %lu ms interval, reference=%u mm",
        static_cast<unsigned>(sample_count),
        static_cast<unsigned long>(sample_interval_ms),
        static_cast<unsigned>(reference_mm)
    );
    return ESP_OK;
}

esp_err_t distanceSetSampleCount(uint16_t sample_count)
{
    if (!valid_sample_count(sample_count)) {
        ESP_LOGE(
            kTag,
            "TOF sample count %u rejected; expected %u..%u",
            static_cast<unsigned>(sample_count),
            kDistanceMinimumSampleCount,
            kDistanceMaximumSampleCount
        );
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_distance.calibration.state == DistanceCalibrationState::Running ||
        s_calibration_requested) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t error = persist_setting(
        kSampleCountSetting,
        sample_count,
        "TOF sample count"
    );
    if (error != ESP_OK) {
        mark_settings_persistence_failure_locked(
            error,
            esp_timer_get_time()
        );
        publish_locked();
        return error;
    }
    s_sample_count = sample_count;
    s_distance.settings.sample_count = sample_count;
    mark_success(s_distance.settings, esp_timer_get_time());
    reset_runtime_windows_locked();
    publish_locked();
    return ESP_OK;
}

esp_err_t distanceSetSampleIntervalMs(uint32_t interval_ms)
{
    if (interval_ms < kMinimumSampleIntervalMs ||
        interval_ms > kMaximumSampleIntervalMs ||
        !supported_interval(interval_ms)) {
        ESP_LOGE(
            kTag,
            "TOF interval %lu ms rejected; expected 250, 500, or 1000 ms",
            static_cast<unsigned long>(interval_ms)
        );
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_distance.calibration.state == DistanceCalibrationState::Running ||
        s_calibration_requested) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t error = persist_setting(
        kSampleIntervalSetting,
        interval_ms,
        "TOF sample interval"
    );
    if (error != ESP_OK) {
        mark_settings_persistence_failure_locked(
            error,
            esp_timer_get_time()
        );
        publish_locked();
        return error;
    }
    s_sample_interval_ms = interval_ms;
    s_distance.settings.sample_interval_ms = interval_ms;
    mark_success(s_distance.settings, esp_timer_get_time());
    reset_runtime_windows_locked();
    publish_locked();
    return ESP_OK;
}

esp_err_t distanceSetReferenceMm(uint16_t reference_mm)
{
    if (!valid_reference(reference_mm)) {
        ESP_LOGE(
            kTag,
            "TOF reference %u mm rejected; expected 500..1500 mm in 10 mm steps",
            static_cast<unsigned>(reference_mm)
        );
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_distance.calibration.state == DistanceCalibrationState::Running ||
        s_calibration_requested) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t error = persist_setting(
        kReferenceSetting,
        reference_mm,
        "TOF physical reference"
    );
    if (error != ESP_OK) {
        mark_settings_persistence_failure_locked(
            error,
            esp_timer_get_time()
        );
        publish_locked();
        return error;
    }
    s_reference_mm = reference_mm;
    s_distance.settings.reference_distance_mm = reference_mm;
    mark_success(s_distance.settings, esp_timer_get_time());
    publish_locked();
    return ESP_OK;
}

}  // namespace qsee

extern "C" esp_err_t qsee_distance_start_calibration(void)
{
    return qsee::distanceStartCalibration();
}

extern "C" esp_err_t qsee_distance_set_sample_count(uint16_t sample_count)
{
    return qsee::distanceSetSampleCount(sample_count);
}

extern "C" esp_err_t qsee_distance_set_sample_interval_ms(uint32_t interval_ms)
{
    return qsee::distanceSetSampleIntervalMs(interval_ms);
}

extern "C" esp_err_t qsee_distance_set_reference_mm(uint16_t reference_mm)
{
    return qsee::distanceSetReferenceMm(reference_mm);
}
