// IMU acquisition owns QMI8658 profiles, calibration and measured sample filtering.
// Display orientation remains in the canonical acquisition orchestrator.
#include "LUNASHELL_acquisition_imu.hpp"
#include "LUNASHELL_acquisition_internal.hpp"
#include "LUNASHELL_platform.h"
#include "LUNASHELL_sensor_fusion.hpp"
#include "LUNASHELL_activity_internal.hpp"

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU && CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
// The Waveshare header defines the standard M_PI macro unconditionally.
#pragma push_macro("M_PI")
#undef M_PI
#include "qmi8658.h"
#pragma pop_macro("M_PI")
#endif

namespace {
using LUNASHELL::snapshot_detail::imu::RawSample;
using LUNASHELL::snapshot_detail::update_snapshot;
using LUNASHELL::snapshot_detail::set_component;
using LUNASHELL::snapshot_detail::mark_sample_success;
using LUNASHELL::snapshot_detail::mark_sample_failure;
using LUNASHELL::snapshot_detail::log_snapshot_publication_failure;
using LUNASHELL::snapshot_detail::sample_publication_result;
using LUNASHELL::snapshot_detail::record_first_error;
using LUNASHELL::snapshot_detail::high_frequency_sample_phase;
using LUNASHELL::snapshot_detail::ensure_refresh_mutex;
using LUNASHELL::snapshot_detail::give_refresh_mutex;
using LUNASHELL::snapshot_detail::process_orientation_sample;
using LUNASHELL::snapshot_detail::copy_orientation_snapshot;
using LUNASHELL::snapshot_detail::invalidate_orientation_sample;
constexpr const char *TAG = "LUNASHELL_imu";
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU && CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
qmi8658_dev_t s_imu;
#endif
bool s_imu_ready;
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU && CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
bool s_imu_profile_low_power;
bool s_imu_profile_valid;
bool s_imu_motion_wakeup_enabled;
esp_err_t s_imu_profile_error = ESP_ERR_INVALID_STATE;
#endif
bool s_imu_calibrated;
bool s_imu_calibration_stationary;
uint16_t s_imu_calibration_samples;
float s_imu_calibration_accel_max_span;
float s_imu_calibration_gyro_max_span;
float s_accel_bias[3];
float s_gyro_bias[3];
float s_accel_filtered[3];
float s_gyro_filtered[3];
bool s_imu_filter_ready;
std::atomic<uint32_t> s_imu_smoothing_percent{70U};
std::atomic_bool s_imu_smoothing_valid{false};
std::atomic<TaskHandle_t> s_imu_calibration_task{nullptr};
std::atomic_bool s_imu_calibration_request_pending{false};
std::mutex s_imu_calibration_mutex;
StaticTask_t s_imu_calibration_task_control;
StackType_t s_imu_calibration_task_stack[3072];
// Sampling and profile changes are serialized by the canonical refresh barrier.
int64_t s_last_error_log_us;

// data: Same decoded acquisition to validate before any publication.
bool finite_imu_sample(const RawSample &data)
{
    return std::isfinite(data.accelX) &&
           std::isfinite(data.accelY) &&
           std::isfinite(data.accelZ) &&
           std::isfinite(data.gyroX) &&
           std::isfinite(data.gyroY) &&
           std::isfinite(data.gyroZ) &&
           std::isfinite(data.temperature);
}

// No parameters: reset the component-owned filter under the acquisition barrier.
void reset_imu_filter()
{
    std::memset(s_accel_filtered, 0, sizeof(s_accel_filtered));
    std::memset(s_gyro_filtered, 0, sizeof(s_gyro_filtered));
    s_imu_filter_ready = false;
}

// error: Original rejected calibration operation.
// attempted_at_us: Original calibration attempt timestamp.
void publish_imu_calibration_failure(
    esp_err_t error,
    int64_t attempted_at_us
)
{
    update_snapshot([error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
        mark_sample_failure(
            snapshot.imu.calibration,
            error,
            attempted_at_us
        );
    });
}


// Reject samples after a failed profile transition instead of publishing partial data.
// sample: Required output; unavailable profiles never read or manufacture measurements.
// Exception: Returns unavailable profile or the original sensor-read error.
esp_err_t read_imu_sample(RawSample *sample)
{
    if (sample == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU && CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
    if (s_imu_motion_wakeup_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_imu_ready && !s_imu_profile_valid) {
        return s_imu_profile_error;
    }
    qmi8658_data_t raw = {};
    const esp_err_t error = qmi8658_read_sensor_data(&s_imu, &raw);
    if (error != ESP_OK) {
        return error;
    }
    *sample = {
        .accelX = raw.accelX,
        .accelY = raw.accelY,
        .accelZ = raw.accelZ,
        .gyroX = raw.gyroX,
        .gyroY = raw.gyroY,
        .gyroZ = raw.gyroZ,
        .temperature = raw.temperature,
    };
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU && CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
// Log the exact QMI8658 register operation that rejected a profile transition.
// profile: Selected hardware profile whose configuration is in progress.
// operation: Exact register operation that produced the result.
// error: Original driver result.
esp_err_t imu_profile_step_result(
    const char *profile,
    const char *operation,
    esp_err_t error
)
{
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "IMU QMI8658 %s profile %s failed (%s)",
            profile,
            operation,
            esp_err_to_name(error)
        );
    }
    return error;
}

// asserted: Required value of STATUSINT.CmdDone for one CTRL9 handshake phase.
// Exception: Returns the original command status read error or handshake timeout.
esp_err_t wait_imu_command_done(bool asserted)
{
    constexpr uint8_t kStatusInterruptRegister = 0x2D;
    constexpr uint8_t kCommandDoneMask = 0x80;
    const int64_t deadline_us = esp_timer_get_time() + 200000;
    do {
        uint8_t status = 0;
        ESP_RETURN_ON_ERROR(qmi8658_read_register(&s_imu, kStatusInterruptRegister, &status, 1),
                            TAG, "QMI8658 CTRL9 status read");
        if (((status & kCommandDoneMask) != 0) == asserted) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    } while (esp_timer_get_time() < deadline_us);
    ESP_LOGE(TAG, "QMI8658 CTRL9 CmdDone=%u timed out (%s)", asserted, esp_err_to_name(ESP_ERR_TIMEOUT));
    return ESP_ERR_TIMEOUT;
}

// No parameters: require the documented QMI8658C command protocol before mutation.
// Exception: Returns the original revision read error or unsupported chip protocol.
esp_err_t verify_imu_wake_revision()
{
    constexpr uint8_t kSupportedRevision = 0x7C;
    uint8_t revision = 0;
    ESP_RETURN_ON_ERROR(qmi8658_read_register(&s_imu, QMI8658_REVISION, &revision, 1), TAG, "QMI8658 wake revision read");
    if (revision != kSupportedRevision) {
        ESP_LOGE(TAG, "QMI8658 wake revision 0x%02x rejected: expected 0x%02x (%s)",
                 revision, kSupportedRevision, esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

// enabled: Arm accelerometer-only WoM, or explicitly exit it after a wake/rejection.
// Must run under the acquisition owner's refresh mutex, or during exclusive boot init.
// Exception: Returns the original register, handshake or profile readback error.
esp_err_t configure_imu_motion_wakeup(bool enabled)
{
    constexpr uint8_t kWomThresholdRegister = 0x0B;
    constexpr uint8_t kWomInterruptRegister = 0x0C;
    constexpr uint8_t kWomCommand = 0x08;
    constexpr uint8_t kCommandAcknowledge = 0x00;
    constexpr uint8_t kInt2InitiallyHigh = 0xC0;
    constexpr uint8_t kBlankingSamples = 3;
    ESP_RETURN_ON_ERROR(verify_imu_wake_revision(), TAG, "QMI8658 wake protocol");
    ESP_RETURN_ON_ERROR(qmi8658_enable_sensors(&s_imu, QMI8658_DISABLE_ALL), TAG, "QMI8658 WoM sensor disable");
    if (enabled) {
        ESP_RETURN_ON_ERROR(qmi8658_set_accel_range(&s_imu, QMI8658_ACCEL_RANGE_8G), TAG, "QMI8658 WoM range");
        ESP_RETURN_ON_ERROR(qmi8658_set_accel_odr(&s_imu, QMI8658_ACCEL_ODR_LOWPOWER_21HZ), TAG, "QMI8658 WoM low-power ODR");
    }
    // The vendor helper only writes CAL registers. It omits the required CTRL9
    // command/ACK and selects INT1, which is not wired to the ESP32 on this PCB.
    ESP_RETURN_ON_ERROR(qmi8658_write_register(&s_imu, QMI8658_CTRL9, kCommandAcknowledge), TAG, "QMI8658 clear prior command");
    ESP_RETURN_ON_ERROR(wait_imu_command_done(false), TAG, "QMI8658 command idle");
    ESP_RETURN_ON_ERROR(qmi8658_write_register(&s_imu, kWomThresholdRegister,
                        enabled ? LUNASHELL::power_policy::kMotionWakeThresholdMg : 0), TAG, "QMI8658 WoM threshold");
    ESP_RETURN_ON_ERROR(qmi8658_write_register(&s_imu, kWomInterruptRegister,
                        kInt2InitiallyHigh | kBlankingSamples), TAG, "QMI8658 WoM INT2 polarity");
    ESP_RETURN_ON_ERROR(qmi8658_write_register(&s_imu, QMI8658_CTRL9, kWomCommand), TAG, "QMI8658 WoM command");
    ESP_RETURN_ON_ERROR(wait_imu_command_done(true), TAG, "QMI8658 WoM command completion");
    ESP_RETURN_ON_ERROR(qmi8658_write_register(&s_imu, QMI8658_CTRL9, kCommandAcknowledge), TAG, "QMI8658 WoM command ACK");
    ESP_RETURN_ON_ERROR(wait_imu_command_done(false), TAG, "QMI8658 WoM ACK completion");
    if (enabled) {
        ESP_RETURN_ON_ERROR(qmi8658_enable_sensors(&s_imu, QMI8658_ENABLE_ACCEL), TAG, "QMI8658 WoM accelerometer enable");
        uint8_t controls[6] = {};
        ESP_RETURN_ON_ERROR(qmi8658_read_register(&s_imu, QMI8658_CTRL2, controls, sizeof(controls)), TAG, "QMI8658 WoM control readback");
        const uint8_t expected_accelerometer =
            (static_cast<uint8_t>(QMI8658_ACCEL_RANGE_8G) << 4U) | QMI8658_ACCEL_ODR_LOWPOWER_21HZ;
        if (controls[0] != expected_accelerometer || controls[5] != QMI8658_ENABLE_ACCEL) {
            ESP_LOGE(TAG, "QMI8658 WoM readback rejected: CTRL2=0x%02x CTRL7=0x%02x (%s)",
                     controls[0], controls[5], esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return ESP_OK;
}

// Apply and read back one complete measured QMI8658 sampling profile.
// low_power: Select the software-standby ODR instead of the active ODR.
// Exception: Returns the original register error or profile readback mismatch.
esp_err_t configure_imu_profile(bool low_power)
{
    const char *profile = low_power ? "low-power" : "active";
    const qmi8658_accel_odr_t accel_odr = low_power
        ? QMI8658_ACCEL_ODR_31_25HZ
        : QMI8658_ACCEL_ODR_500HZ;
    const qmi8658_gyro_odr_t gyro_odr = low_power
        ? QMI8658_GYRO_ODR_31_25HZ
        : QMI8658_GYRO_ODR_500HZ;

    esp_err_t error = imu_profile_step_result(
        profile,
        "sensor-disable",
        qmi8658_enable_sensors(&s_imu, QMI8658_DISABLE_ALL)
    );
    if (error != ESP_OK) {
        return error;
    }
    error = imu_profile_step_result(
        profile,
        "accelerometer-range",
        qmi8658_set_accel_range(&s_imu, QMI8658_ACCEL_RANGE_8G)
    );
    if (error != ESP_OK) {
        return error;
    }
    error = imu_profile_step_result(
        profile,
        "accelerometer-ODR",
        qmi8658_set_accel_odr(&s_imu, accel_odr)
    );
    if (error != ESP_OK) {
        return error;
    }
    error = imu_profile_step_result(
        profile,
        "gyroscope-range",
        qmi8658_set_gyro_range(&s_imu, QMI8658_GYRO_RANGE_512DPS)
    );
    if (error != ESP_OK) {
        return error;
    }
    error = imu_profile_step_result(
        profile,
        "gyroscope-ODR",
        qmi8658_set_gyro_odr(&s_imu, gyro_odr)
    );
    if (error != ESP_OK) {
        return error;
    }
    error = imu_profile_step_result(
        profile,
        "low-pass-filter",
        qmi8658_write_register(&s_imu, QMI8658_CTRL5, 0x03)
    );
    if (error != ESP_OK) {
        return error;
    }
    error = imu_profile_step_result(
        profile,
        "sensor-enable",
        qmi8658_enable_sensors(
            &s_imu,
            QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO
        )
    );
    if (error != ESP_OK) {
        return error;
    }

    uint8_t control_registers[6] = {};
    error = imu_profile_step_result(
        profile,
        "control-register-readback",
        qmi8658_read_register(
            &s_imu,
            QMI8658_CTRL2,
            control_registers,
            static_cast<uint8_t>(sizeof(control_registers))
        )
    );
    if (error != ESP_OK) {
        return error;
    }
    const uint8_t expected_ctrl2 =
        (static_cast<uint8_t>(QMI8658_ACCEL_RANGE_8G) << 4U) |
        static_cast<uint8_t>(accel_odr);
    const uint8_t expected_ctrl3 =
        (static_cast<uint8_t>(QMI8658_GYRO_RANGE_512DPS) << 4U) |
        static_cast<uint8_t>(gyro_odr);
    const uint8_t expected_ctrl5 = 0x03;
    const uint8_t expected_ctrl7 =
        QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO;
    if (control_registers[0] != expected_ctrl2 ||
        control_registers[1] != expected_ctrl3 ||
        control_registers[3] != expected_ctrl5 ||
        control_registers[5] != expected_ctrl7) {
        ESP_LOGE(
            TAG,
            "IMU QMI8658 %s profile readback rejected: CTRL2=0x%02x/0x%02x CTRL3=0x%02x/0x%02x CTRL5=0x%02x/0x%02x CTRL7=0x%02x/0x%02x",
            profile,
            control_registers[0],
            expected_ctrl2,
            control_registers[1],
            expected_ctrl3,
            control_registers[3],
            expected_ctrl5,
            control_registers[5],
            expected_ctrl7
        );
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
#endif

// Publish a failed IMU profile transition as unavailable measured telemetry.
// error: Original rejected profile operation.
// attempted_at_us: Original hardware transition attempt timestamp.
void publish_imu_profile_failure(esp_err_t error, int64_t attempted_at_us)
{
    const esp_err_t publication_error = update_snapshot(
        [error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
            mark_sample_failure(snapshot.imu, error, attempted_at_us);
            snapshot.imu.gesture_acceleration_valid = false;
        }
    );
    log_snapshot_publication_failure(
        "IMU profile transition",
        publication_error
    );
}

// Initialize the QMI8658 in the canonical active sampling profile.
// bus: Shared I2C bus owned by the hardware acquisition service.
// Exception: Returns the original probe, initialization or sampling-profile failure.
esp_err_t init_imu(i2c_master_bus_handle_t bus)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU && CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
    const uint8_t addresses[] = {QMI8658_ADDRESS_HIGH, QMI8658_ADDRESS_LOW};
    esp_err_t error = ESP_ERR_NOT_FOUND;
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        std::memset(&s_imu, 0, sizeof(s_imu));
        error = qmi8658_init(&s_imu, bus, addresses[i]);
        if (error == ESP_OK && esp_reset_reason() == ESP_RST_DEEPSLEEP) {
            // The shared rail stayed on: a CPU restart does not reset QMI8658 WoM.
            error = configure_imu_motion_wakeup(false);
        }
        if (error == ESP_OK) {
            error = configure_imu_profile(false);
        }
        if (error == ESP_OK) {
            qmi8658_set_accel_unit_mps2(&s_imu, true);
            qmi8658_set_gyro_unit_dps(&s_imu, true);
            // Let the first filtered samples settle before calibration.
            vTaskDelay(pdMS_TO_TICKS(150));
            for (unsigned discard = 0; discard < 16; ++discard) {
                RawSample ignored = {};
                error = read_imu_sample(&ignored);
                if (error != ESP_OK || !finite_imu_sample(ignored)) {
                    if (error == ESP_OK) {
                        error = ESP_ERR_INVALID_RESPONSE;
                    }
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        }
        if (error == ESP_OK) {
            s_imu_ready = true;
            s_imu_profile_low_power = false;
            s_imu_profile_valid = true;
            s_imu_profile_error = ESP_OK;
            ESP_LOGI(
                TAG,
                "IMU QMI8658: ready at 0x%02x; accel=8g/500Hz gyro=512dps/500Hz LPF enabled",
                addresses[i]
            );
            break;
        }
        if (s_imu.dev_handle != nullptr) {
            const esp_err_t remove_error =
                i2c_master_bus_rm_device(s_imu.dev_handle);
            if (remove_error != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "IMU probe cleanup failed at 0x%02x (%s)",
                    addresses[i],
                    esp_err_to_name(remove_error)
                );
                record_first_error(remove_error, &error);
                break;
            }
            s_imu.dev_handle = nullptr;
        }
    }
    update_snapshot([error](LUNASHELL::DeviceSnapshot &snapshot) {
        set_component(snapshot.imu, error == ESP_OK, error);
        snapshot.imu.gesture_acceleration_valid = false;
    });
    if (!s_imu_ready) {
        s_imu_profile_valid = false;
        s_imu_profile_error = error;
        ESP_LOGW(TAG, "IMU QMI8658: not detected");
    }
    return error;
#else
    (void)bus;
    const esp_err_t error = ESP_ERR_NOT_SUPPORTED;
    update_snapshot([error](LUNASHELL::DeviceSnapshot &snapshot) {
        set_component(snapshot.imu, false, error);
        snapshot.imu.gesture_acceleration_valid = false;
    });
    ESP_LOGI(TAG, "IMU: disabled by board hardware selection");
    return error;
#endif
}

// Exception: Returns acquisition failure or a measured nonstationary sample rejection.
esp_err_t calibrate_imu()
{
    const int64_t calibration_attempted_at_us = esp_timer_get_time();
    if (!s_imu_ready) {
        update_snapshot([calibration_attempted_at_us](
                            LUNASHELL::DeviceSnapshot &snapshot
                        ) {
            mark_sample_failure(
                snapshot.imu.calibration,
                ESP_ERR_INVALID_STATE,
                calibration_attempted_at_us
            );
        });
        return ESP_ERR_INVALID_STATE;
    }
    constexpr uint16_t sample_count = 96;
    constexpr float gravity = 9.80665F;
    float accel_sum[3] = {};
    float gyro_sum[3] = {};
    float accel_min[3] = {1000.0F, 1000.0F, 1000.0F};
    float accel_max[3] = {-1000.0F, -1000.0F, -1000.0F};
    float gyro_min[3] = {10000.0F, 10000.0F, 10000.0F};
    float gyro_max[3] = {-10000.0F, -10000.0F, -10000.0F};

    for (uint16_t index = 0; index < sample_count; ++index) {
        RawSample data = {};
        esp_err_t error = read_imu_sample(&data);
        if (error == ESP_OK && !finite_imu_sample(data)) {
            error = ESP_ERR_INVALID_RESPONSE;
        }
        if (error != ESP_OK) {
            const int64_t attempted_at_us = esp_timer_get_time();
            update_snapshot([error, attempted_at_us,
                             calibration_attempted_at_us](
                                LUNASHELL::DeviceSnapshot &snapshot
                            ) {
                mark_sample_failure(snapshot.imu, error, attempted_at_us);
                snapshot.imu.gesture_acceleration_valid = false;
                mark_sample_failure(
                    snapshot.imu.calibration,
                    error,
                    calibration_attempted_at_us
                );
            });
            ESP_LOGE(
                TAG,
                "IMU calibration sample %u failed (%s)",
                static_cast<unsigned>(index),
                esp_err_to_name(error)
            );
            return error;
        }
        const float acceleration[3] = {data.accelX, data.accelY, data.accelZ};
        const float angular_velocity[3] = {data.gyroX, data.gyroY, data.gyroZ};
        for (unsigned axis = 0; axis < 3; ++axis) {
            accel_sum[axis] += acceleration[axis];
            gyro_sum[axis] += angular_velocity[axis];
            if (acceleration[axis] < accel_min[axis]) {
                accel_min[axis] = acceleration[axis];
            }
            if (acceleration[axis] > accel_max[axis]) {
                accel_max[axis] = acceleration[axis];
            }
            if (angular_velocity[axis] < gyro_min[axis]) {
                gyro_min[axis] = angular_velocity[axis];
            }
            if (angular_velocity[axis] > gyro_max[axis]) {
                gyro_max[axis] = angular_velocity[axis];
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    float accel_mean[3] = {};
    float gyro_mean[3] = {};
    float magnitude_squared = 0.0F;
    float accel_max_span = 0.0F;
    float gyro_max_span = 0.0F;
    unsigned gravity_axis = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        accel_mean[axis] = accel_sum[axis] / sample_count;
        gyro_mean[axis] = gyro_sum[axis] / sample_count;
        magnitude_squared += accel_mean[axis] * accel_mean[axis];
        const float accel_span = accel_max[axis] - accel_min[axis];
        const float gyro_span = gyro_max[axis] - gyro_min[axis];
        if (accel_span > accel_max_span) {
            accel_max_span = accel_span;
        }
        if (gyro_span > gyro_max_span) {
            gyro_max_span = gyro_span;
        }
        const float axis_acceleration =
            accel_mean[axis] < 0.0F ? -accel_mean[axis] : accel_mean[axis];
        const float gravity_acceleration =
            accel_mean[gravity_axis] < 0.0F
                ? -accel_mean[gravity_axis]
                : accel_mean[gravity_axis];
        if (axis_acceleration > gravity_acceleration) {
            gravity_axis = axis;
        }
    }
    const float magnitude = __builtin_sqrtf(magnitude_squared);
    s_imu_calibration_samples = sample_count;
    s_imu_calibration_accel_max_span = accel_max_span;
    s_imu_calibration_gyro_max_span = gyro_max_span;
    s_imu_calibration_stationary =
        magnitude >= 8.0F && magnitude <= 12.0F &&
        accel_max_span <= 0.75F && gyro_max_span <= 8.0F;
    if (!s_imu_calibration_stationary) {
        update_snapshot([calibration_attempted_at_us](
                            LUNASHELL::DeviceSnapshot &snapshot
                        ) {
            snapshot.imu.calibration_stationary =
                s_imu_calibration_stationary;
            snapshot.imu.calibration_samples = s_imu_calibration_samples;
            snapshot.imu.calibration_accel_max_span_mps2 =
                s_imu_calibration_accel_max_span;
            snapshot.imu.calibration_gyro_max_span_dps =
                s_imu_calibration_gyro_max_span;
            mark_sample_failure(
                snapshot.imu.calibration,
                ESP_ERR_INVALID_STATE,
                calibration_attempted_at_us
            );
        });
        ESP_LOGW(
            TAG,
            "IMU calibration deferred: unstable sample set (|a|=%.2f accel-span=%.3f gyro-span=%.3f gyro-mean=[%.2f %.2f %.2f])",
            magnitude,
            accel_max_span,
            gyro_max_span,
            gyro_mean[0],
            gyro_mean[1],
            gyro_mean[2]
        );
        return ESP_ERR_INVALID_STATE;
    }

    for (unsigned axis = 0; axis < 3; ++axis) {
        s_accel_bias[axis] = accel_mean[axis];
        s_gyro_bias[axis] = gyro_mean[axis];
    }
    s_accel_bias[gravity_axis] -= accel_mean[gravity_axis] >= 0.0F ? gravity : -gravity;
    s_imu_calibrated = true;
    // A new bias must start a fresh filtered stream.
    reset_imu_filter();
    update_snapshot([calibration_attempted_at_us](
                        LUNASHELL::DeviceSnapshot &snapshot
                    ) {
        snapshot.imu.calibrated = s_imu_calibrated;
        snapshot.imu.calibration_stationary = s_imu_calibration_stationary;
        snapshot.imu.calibration_samples = s_imu_calibration_samples;
        snapshot.imu.calibration_accel_max_span_mps2 =
            s_imu_calibration_accel_max_span;
        snapshot.imu.calibration_gyro_max_span_dps =
            s_imu_calibration_gyro_max_span;
        for (unsigned axis = 0; axis < 3; ++axis) {
            snapshot.imu.acceleration_bias_mps2[axis] = s_accel_bias[axis];
            snapshot.imu.angular_velocity_bias_dps[axis] = s_gyro_bias[axis];
        }
        mark_sample_success(
            snapshot.imu.calibration,
            calibration_attempted_at_us
        );
    });
    ESP_LOGI(
        TAG,
        "IMU stationary calibration ready: samples=%u accel-span=%.3f gyro-span=%.3f accel-bias=[%.3f %.3f %.3f] gyro-bias=[%.3f %.3f %.3f]",
        sample_count,
        accel_max_span,
        gyro_max_span,
        s_accel_bias[0],
        s_accel_bias[1],
        s_accel_bias[2],
        s_gyro_bias[0],
        s_gyro_bias[1],
        s_gyro_bias[2]
    );
    return ESP_OK;
}

// Publish calibrated motion and optionally classify the same raw sample for rotation.
// phase: Original acquisition context used for publication and logging.
// verbose: Whether to log every sample or use bounded error reminders.
// track_orientation: Whether the root owner classifies the same acquired raw sample.
// activity_sample: Whether the owner scheduled this acquisition for the model window.
// Exception: Returns the original acquisition, publication or sensor-fusion error.
esp_err_t sample_imu(
    const char *phase,
    bool verbose = true,
    bool track_orientation = false,
    bool activity_sample = false
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    LUNASHELL::ImuSnapshot sample;
#if !CONFIG_LUNASHELL_BUILD_ENABLE_IMU
    LUNASHELL::snapshot_detail::activity::accept(sample, ESP_ERR_NOT_SUPPORTED, attempted_at_us);
    LUNASHELL::DeviceSnapshotService::instance().reportSensorFusion(
        {},
        ESP_ERR_NOT_SUPPORTED,
        attempted_at_us
    );
    const esp_err_t unavailable_publication_error = update_snapshot(
        [attempted_at_us, track_orientation](LUNASHELL::DeviceSnapshot &snapshot) {
            mark_sample_failure(
                snapshot.imu,
                ESP_ERR_NOT_SUPPORTED,
                attempted_at_us
            );
            snapshot.imu.gesture_acceleration_valid = false;
            if (track_orientation) {
                invalidate_orientation_sample(
                    snapshot, ESP_ERR_NOT_SUPPORTED, attempted_at_us
                );
            }
        }
    );
    return sample_publication_result(
        "IMU QMI8658",
        phase,
        ESP_ERR_NOT_SUPPORTED,
        unavailable_publication_error
    );
#endif
    if (!s_imu_smoothing_valid.load(std::memory_order_relaxed)) {
        LUNASHELL::snapshot_detail::activity::accept(sample, ESP_ERR_INVALID_STATE, attempted_at_us);
        LUNASHELL::DeviceSnapshotService::instance().reportSensorFusion(
            {},
            ESP_ERR_INVALID_STATE,
            attempted_at_us
        );
        const esp_err_t publication_error = update_snapshot(
            [attempted_at_us, track_orientation](
                LUNASHELL::DeviceSnapshot &snapshot
            ) {
                mark_sample_failure(
                    snapshot.imu,
                    ESP_ERR_INVALID_STATE,
                    attempted_at_us
                );
                snapshot.imu.gesture_acceleration_valid = false;
                if (track_orientation) {
                    invalidate_orientation_sample(
                        snapshot, ESP_ERR_INVALID_STATE, attempted_at_us
                    );
                }
            }
        );
        ESP_LOGE(TAG, "IMU QMI8658: smoothing setting is invalid");
        return sample_publication_result(
            "IMU QMI8658",
            phase,
            ESP_ERR_INVALID_STATE,
            publication_error
        );
    }

    RawSample data = {};
    esp_err_t error = read_imu_sample(&data);
    if (error == ESP_OK && !finite_imu_sample(data)) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK) {
        sample.calibrated = s_imu_calibrated;
        sample.calibration_stationary = s_imu_calibration_stationary;
        sample.calibration_samples = s_imu_calibration_samples;
        sample.calibration_accel_max_span_mps2 = s_imu_calibration_accel_max_span;
        sample.calibration_gyro_max_span_dps = s_imu_calibration_gyro_max_span;
        const float acceleration[] = {data.accelX, data.accelY, data.accelZ};
        const float angular_velocity[] = {data.gyroX, data.gyroY, data.gyroZ};
        const float smoothing =
            static_cast<float>(
                s_imu_smoothing_percent.load(std::memory_order_relaxed)
            ) / 100.0F;
        for (unsigned axis = 0; axis < 3; ++axis) {
            sample.acceleration_bias_mps2[axis] = s_accel_bias[axis];
            sample.angular_velocity_bias_dps[axis] = s_gyro_bias[axis];
            const float acceleration_corrected =
                acceleration[axis] - (s_imu_calibrated ? s_accel_bias[axis] : 0.0F);
            const float angular_velocity_corrected =
                angular_velocity[axis] - (s_imu_calibrated ? s_gyro_bias[axis] : 0.0F);
            // Gestures retain the measured impulse from this acquisition; the EMA
            // below remains the existing source for animation, power and fusion.
            sample.gesture_acceleration_mps2[axis] = acceleration_corrected;
            if (!s_imu_filter_ready) {
                s_accel_filtered[axis] = acceleration_corrected;
                s_gyro_filtered[axis] = angular_velocity_corrected;
            } else {
                s_accel_filtered[axis] =
                    s_accel_filtered[axis] * smoothing +
                    acceleration_corrected * (1.0F - smoothing);
                s_gyro_filtered[axis] =
                    s_gyro_filtered[axis] * smoothing +
                    angular_velocity_corrected * (1.0F - smoothing);
            }
            sample.acceleration_mps2[axis] = s_accel_filtered[axis];
            sample.angular_velocity_dps[axis] = s_gyro_filtered[axis];
        }
        s_imu_filter_ready = true;
        sample.gesture_acceleration_valid = true;
        sample.gesture_sampled_at_us = attempted_at_us;
        sample.temperature_c = data.temperature;
        if (track_orientation) {
            process_orientation_sample(data, attempted_at_us);
        }
    }
    if (error != ESP_OK) {
        LUNASHELL::snapshot_detail::activity::accept(sample, error, attempted_at_us);
        LUNASHELL::DeviceSnapshotService::instance().reportSensorFusion(
            {},
            error,
            attempted_at_us
        );
        const esp_err_t publication_error = update_snapshot(
            [error, attempted_at_us, track_orientation](
                LUNASHELL::DeviceSnapshot &snapshot
            ) {
                mark_sample_failure(snapshot.imu, error, attempted_at_us);
                snapshot.imu.gesture_acceleration_valid = false;
                if (track_orientation) {
                    invalidate_orientation_sample(
                        snapshot, error, attempted_at_us
                    );
                }
            }
        );
        const int64_t now = esp_timer_get_time();
        if (verbose || now - s_last_error_log_us >= 5000000) {
            s_last_error_log_us = now;
            ESP_LOGW(
                TAG,
                "IMU QMI8658: %s sample failed (%s)",
                phase,
                esp_err_to_name(error)
            );
        }
        return sample_publication_result(
            "IMU QMI8658",
            phase,
            error,
            publication_error
        );
    }
    mark_sample_success(sample, attempted_at_us);
    const esp_err_t publication_error = update_snapshot(
        [&sample, track_orientation](LUNASHELL::DeviceSnapshot &snapshot) {
            const LUNASHELL::ComponentSnapshot calibration =
                snapshot.imu.calibration;
            snapshot.imu = sample;
            snapshot.imu.calibration = calibration;
            if (track_orientation) {
                copy_orientation_snapshot(snapshot.ui.orientation);
            }
        }
    );
    // Successful unrelated UI/full-refresh reads must not shift the model phase.
    // Any acquisition/publication failure still invalidates the observed stream.
    if (activity_sample || publication_error != ESP_OK) {
        LUNASHELL::snapshot_detail::activity::accept(sample, publication_error, attempted_at_us);
    }
    if (verbose) {
        if (high_frequency_sample_phase(phase)) {
            ESP_LOGD(
                TAG,
                "IMU QMI8658 %s: accel=[%.2f %.2f %.2f]m/s2 gyro=[%.2f %.2f %.2f]dps temp=%.1fC",
                phase,
                sample.acceleration_mps2[0],
                sample.acceleration_mps2[1],
                sample.acceleration_mps2[2],
                sample.angular_velocity_dps[0],
                sample.angular_velocity_dps[1],
                sample.angular_velocity_dps[2],
                data.temperature
            );
        } else {
            ESP_LOGI(
                TAG,
                "IMU QMI8658 %s: accel=[%.2f %.2f %.2f]m/s2 gyro=[%.2f %.2f %.2f]dps temp=%.1fC",
                phase,
                sample.acceleration_mps2[0],
                sample.acceleration_mps2[1],
                sample.acceleration_mps2[2],
                sample.angular_velocity_dps[0],
                sample.angular_velocity_dps[1],
                sample.angular_velocity_dps[2],
                data.temperature
            );
        }
    }
    LUNASHELL::SensorFusionResult fusion_result;
    const esp_err_t fusion_error =
        LUNASHELL::SensorFusionService::instance().process(sample, &fusion_result);
    LUNASHELL::DeviceSnapshotService::instance().reportSensorFusion(
        fusion_result,
        fusion_error,
        attempted_at_us
    );
    const esp_err_t sample_result = sample_publication_result(
        "IMU QMI8658",
        phase,
        ESP_OK,
        publication_error
    );
    if (sample_result != ESP_OK) {
        return sample_result;
    }
    if (fusion_error != ESP_OK) {
        ESP_LOGW(
            TAG,
            "IMU sensor-fusion %s failed (%s)",
            phase,
            esp_err_to_name(fusion_error)
        );
        return fusion_error;
    }
    return ESP_OK;
}

// arg: Unused FreeRTOS task argument; all worker state belongs to this component.
void imu_calibration_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!s_imu_calibration_request_pending.load(std::memory_order_acquire)) {
            continue;
        }

        struct PendingCalibrationReset {
            std::atomic_bool &pending;
            ~PendingCalibrationReset()
            {
                pending.store(false, std::memory_order_release);
            }
        } pending_reset{s_imu_calibration_request_pending};

        const int64_t calibration_attempted_at_us = esp_timer_get_time();
        SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
        if (refresh_mutex == nullptr) {
            ESP_LOGE(
                TAG,
                "IMU calibration worker failed: refresh mutex allocation failed"
            );
            publish_imu_calibration_failure(
                ESP_ERR_NO_MEM,
                calibration_attempted_at_us
            );
            continue;
        }
        if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
            ESP_LOGW(
                TAG,
                "IMU calibration rejected: another refresh is still running"
            );
            publish_imu_calibration_failure(
                ESP_ERR_TIMEOUT,
                calibration_attempted_at_us
            );
            continue;
        }

        esp_err_t final_result = calibrate_imu();
        if (final_result == ESP_OK) {
            final_result = sample_imu("post-calibration");
        }
        const esp_err_t release_error = give_refresh_mutex(
            refresh_mutex,
            "IMU calibration"
        );
        record_first_error(release_error, &final_result);
    }
}

// Exception: Returns null if creating the unchanged bounded worker task fails.
TaskHandle_t ensure_imu_calibration_task()
{
    TaskHandle_t task = s_imu_calibration_task.load(std::memory_order_acquire);
    if (task != nullptr) {
        return task;
    }

    task = xTaskCreateStatic(
        imu_calibration_task,
        "LUNASHELL_imu_cal",
        sizeof(s_imu_calibration_task_stack) /
            sizeof(s_imu_calibration_task_stack[0]),
        nullptr,
        3,
        s_imu_calibration_task_stack,
        &s_imu_calibration_task_control
    );
    if (task != nullptr) {
        s_imu_calibration_task.store(task, std::memory_order_release);
    }
    return task;
}

}  // namespace

namespace LUNASHELL::snapshot_detail::imu {
// bus: Existing BSP-owned bus borrowed by the IMU acquisition component.
// Exception: Returns the original probe, profile or canonical publication error.
esp_err_t initialize(i2c_master_bus_handle_t bus)
{
    return init_imu(bus);
}

// No parameters: readiness observed by the acquisition orchestrator.
bool ready()
{
    return s_imu_ready;
}

// phase: Original acquisition context used for publication and logging.
// verbose: Whether to log every acquisition instead of bounded error reminders.
// track_orientation: Whether the same measured sample updates the root orientation owner.
// activity_sample: Whether this acquisition fulfills the owner's model sampling lease.
// Exception: Returns the original acquisition, publication or sensor-fusion error.
esp_err_t sample(const char *phase, bool verbose, bool track_orientation, bool activity_sample)
{
    return sample_imu(phase, verbose, track_orientation, activity_sample);
}

// percent: Already validated smoothing value admitted by the persistence owner.
// valid: Whether persisted smoothing passed validation.
// The caller holds the existing acquisition/settings barriers before runtime admission.
void set_smoothing(uint32_t percent, bool valid)
{
    s_imu_smoothing_percent.store(percent, std::memory_order_relaxed);
    s_imu_smoothing_valid.store(valid, std::memory_order_relaxed);
}

// No parameters: return the admitted runtime value without acquiring the sensor.
uint32_t smoothing_percent()
{
    return s_imu_smoothing_percent.load(std::memory_order_relaxed);
}

// No parameters: report whether persistence admitted the current smoothing value.
bool smoothing_valid()
{
    return s_imu_smoothing_valid.load(std::memory_order_relaxed);
}

// No parameters: reset filtering while the caller holds the acquisition barrier.
void reset_filter()
{
    reset_imu_filter();
}

// raw: Same acquisition already held by the orientation owner; no hardware read.
// The caller holds the refresh barrier so calibration cannot change this bias.
float corrected_gyro_z(const RawSample &raw)
{
    return raw.gyroZ - (s_imu_calibrated ? s_gyro_bias[2] : 0.0F);
}
}  // namespace LUNASHELL::snapshot_detail::imu

// enabled: Arm the physical wake profile, or restore normal software-standby sampling.
// Exception: Returns the original admission, locking, register or profile error.
extern "C" esp_err_t LUNASHELL_hardware_set_imu_motion_wakeup(bool enabled)
{
    const int64_t attempted_at_us = esp_timer_get_time();
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU && CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
    std::lock_guard<std::mutex> calibration_lock(s_imu_calibration_mutex);
    if (!LUNASHELL::snapshot_detail::hardware_initialized() || !s_imu_ready || s_imu.dev_handle == nullptr ||
        s_imu_calibration_request_pending.load(std::memory_order_acquire)) {
        ESP_LOGE(TAG, "QMI8658 WoM rejected: hardware unavailable or calibration pending (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        publish_imu_profile_failure(ESP_ERR_INVALID_STATE, attempted_at_us);
        return ESP_ERR_INVALID_STATE;
    }
    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr || xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        const esp_err_t error = refresh_mutex == nullptr ? ESP_ERR_NO_MEM : ESP_ERR_TIMEOUT;
        ESP_LOGE(TAG, "QMI8658 WoM acquisition lock failed (%s)", esp_err_to_name(error));
        publish_imu_profile_failure(error, attempted_at_us);
        return error;
    }
    if (!enabled && !s_imu_motion_wakeup_enabled) {
        return give_refresh_mutex(refresh_mutex, "IMU WoM already disabled");
    }
    const esp_err_t revision_error = verify_imu_wake_revision();
    if (revision_error != ESP_OK) {
        const esp_err_t release_error = give_refresh_mutex(refresh_mutex, "IMU WoM unsupported revision");
        if (release_error != ESP_OK) {
            ESP_LOGE(TAG, "QMI8658 rejected revision lock recovery failed (%s); preserving primary error %s",
                     esp_err_to_name(release_error), esp_err_to_name(revision_error));
        }
        publish_imu_profile_failure(revision_error, attempted_at_us);
        return revision_error;
    }
    // Block host sampling before the first register change, including partial failures.
    s_imu_motion_wakeup_enabled = true;
    s_imu_profile_valid = false;
    s_imu_profile_error = ESP_ERR_INVALID_STATE;
    esp_err_t error = configure_imu_motion_wakeup(enabled);
    if (error == ESP_OK && !enabled) {
        error = configure_imu_profile(true);
    }
    if (error == ESP_OK && !enabled) {
        s_imu_motion_wakeup_enabled = false;
        s_imu_profile_valid = true;
        s_imu_profile_low_power = true;
        s_imu_profile_error = ESP_OK;
        reset_imu_filter();
    } else if (error != ESP_OK) {
        s_imu_profile_error = error;
    }
    const esp_err_t release_error = give_refresh_mutex(refresh_mutex, "IMU WoM transition");
    if (release_error != ESP_OK) {
        ESP_LOGE(TAG, "QMI8658 WoM lock recovery failed (%s)", esp_err_to_name(release_error));
        if (error == ESP_OK) {
            error = release_error;
        }
        s_imu_profile_valid = false;
        s_imu_profile_error = error;
    }
    // WoM is an interrupt mode, not a fresh accelerometer/gyroscope measurement.
    publish_imu_profile_failure(error != ESP_OK ? error : ESP_ERR_INVALID_STATE, attempted_at_us);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "QMI8658 WoM %s failed (%s)", enabled ? "enable" : "disable", esp_err_to_name(error));
    } else {
        ESP_LOGI(TAG, "QMI8658 WoM %s: INT2 initially high, threshold=%u mg, accelerometer-only ~21 Hz",
                 enabled ? "armed" : "disabled", LUNASHELL::power_policy::kMotionWakeThresholdMg);
    }
    return error;
#else
    (void)enabled;
    ESP_LOGE(TAG, "QMI8658 WoM rejected: driver excluded (%s)", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    publish_imu_profile_failure(ESP_ERR_NOT_SUPPORTED, attempted_at_us);
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// Serialize QMI8658 profile changes with sampling and calibration ownership.
// enabled: Select the existing software-standby sampling profile.
// Exception: Returns the original admission, locking, transition or recovery error.
extern "C" esp_err_t LUNASHELL_hardware_set_imu_low_power(bool enabled)
{
    const int64_t attempted_at_us = esp_timer_get_time();
#if !CONFIG_LUNASHELL_BUILD_ENABLE_IMU || !CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
    publish_imu_profile_failure(ESP_ERR_NOT_SUPPORTED, attempted_at_us);
    ESP_LOGE(
        TAG,
        "IMU QMI8658 profile transition rejected: driver is unavailable"
    );
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!LUNASHELL::snapshot_detail::hardware_initialized() || !s_imu_ready || s_imu.dev_handle == nullptr) {
        publish_imu_profile_failure(ESP_ERR_INVALID_STATE, attempted_at_us);
        ESP_LOGE(
            TAG,
            "IMU QMI8658 profile transition rejected: hardware or IMU is not initialized"
        );
        return ESP_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> calibration_lock(s_imu_calibration_mutex);
    if (s_imu_calibration_request_pending.load(std::memory_order_acquire)) {
        publish_imu_profile_failure(ESP_ERR_INVALID_STATE, attempted_at_us);
        ESP_LOGE(
            TAG,
            "IMU QMI8658 profile transition rejected: calibration is pending"
        );
        return ESP_ERR_INVALID_STATE;
    }

    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        publish_imu_profile_failure(ESP_ERR_NO_MEM, attempted_at_us);
        ESP_LOGE(
            TAG,
            "IMU QMI8658 profile transition failed: refresh mutex allocation failed"
        );
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        publish_imu_profile_failure(ESP_ERR_TIMEOUT, attempted_at_us);
        ESP_LOGE(
            TAG,
            "IMU QMI8658 profile transition failed: sampler quiescence timed out"
        );
        return ESP_ERR_TIMEOUT;
    }

    if (s_imu_motion_wakeup_enabled) {
        const esp_err_t error = s_imu_profile_error;
        const esp_err_t release_error = give_refresh_mutex(refresh_mutex, "IMU sampling blocked by WoM");
        if (release_error != ESP_OK) {
            ESP_LOGE(TAG, "QMI8658 blocked profile lock recovery failed (%s); preserving primary error %s",
                     esp_err_to_name(release_error), esp_err_to_name(error));
        }
        ESP_LOGE(TAG, "QMI8658 sampling profile rejected while WoM remains armed (%s)", esp_err_to_name(error));
        publish_imu_profile_failure(error, attempted_at_us);
        return error;
    }

    if (s_imu_profile_valid && s_imu_profile_low_power == enabled) {
        give_refresh_mutex(refresh_mutex, "IMU profile transition");
        return ESP_OK;
    }

    const bool previous_low_power = s_imu_profile_low_power;
    const bool previous_valid = s_imu_profile_valid;
    const esp_err_t transition_error = configure_imu_profile(enabled);
    if (transition_error != ESP_OK) {
        esp_err_t recovery_error = ESP_ERR_INVALID_STATE;
        if (previous_valid) {
            recovery_error = configure_imu_profile(previous_low_power);
            if (recovery_error == ESP_OK) {
                s_imu_profile_low_power = previous_low_power;
                s_imu_profile_valid = true;
                s_imu_profile_error = ESP_OK;
                reset_imu_filter();
                ESP_LOGW(
                    TAG,
                    "IMU QMI8658 profile transition rolled back to %s",
                    previous_low_power ? "low-power" : "active"
                );
            }
        }
        if (!previous_valid || recovery_error != ESP_OK) {
            esp_err_t invalid_profile_error =
                previous_valid ? recovery_error : transition_error;
            const esp_err_t disable_error = qmi8658_enable_sensors(
                &s_imu,
                QMI8658_DISABLE_ALL
            );
            if (disable_error != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "IMU QMI8658 failed-profile sensor-disable failed (%s)",
                    esp_err_to_name(disable_error)
                );
                invalid_profile_error = disable_error;
            }
            s_imu_profile_valid = false;
            s_imu_profile_error = invalid_profile_error;
        }
        give_refresh_mutex(refresh_mutex, "IMU profile transition rollback");
        publish_imu_profile_failure(transition_error, attempted_at_us);
        return transition_error;
    }

    s_imu_profile_low_power = enabled;
    s_imu_profile_valid = true;
    s_imu_profile_error = ESP_OK;
    reset_imu_filter();
    give_refresh_mutex(refresh_mutex, "IMU profile transition");
    ESP_LOGI(
        TAG,
        "IMU QMI8658 profile: %s (accel=8g/%s gyro=512dps/%s)",
        enabled ? "low-power" : "active",
        enabled ? "31.25Hz" : "500Hz",
        enabled ? "31.25Hz" : "500Hz"
    );
    return ESP_OK;
#endif
}

// Reject calibration while the IMU is not in its canonical active profile.
// Exception: Returns invalid profile/admission or worker scheduling failure.
extern "C" esp_err_t LUNASHELL_imu_calibrate(void)
{
    const int64_t calibration_attempted_at_us = esp_timer_get_time();
#if !CONFIG_LUNASHELL_BUILD_ENABLE_IMU
    publish_imu_calibration_failure(
        ESP_ERR_NOT_SUPPORTED,
        calibration_attempted_at_us
    );
    return ESP_ERR_NOT_SUPPORTED;
#endif
    if (!LUNASHELL::snapshot_detail::hardware_initialized()) {
        ESP_LOGE(TAG, "IMU calibration rejected: hardware is not initialized");
        publish_imu_calibration_failure(
            ESP_ERR_INVALID_STATE,
            calibration_attempted_at_us
        );
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_imu_ready) {
        ESP_LOGE(TAG, "IMU calibration rejected: IMU is not ready");
        publish_imu_calibration_failure(
            ESP_ERR_INVALID_STATE,
            calibration_attempted_at_us
        );
        return ESP_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(s_imu_calibration_mutex);
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU && CONFIG_LUNASHELL_BUILD_IMU_DRIVER_QMI8658
    if (!s_imu_profile_valid || s_imu_profile_low_power) {
        publish_imu_calibration_failure(
            ESP_ERR_INVALID_STATE,
            calibration_attempted_at_us
        );
        ESP_LOGE(
            TAG,
            "IMU calibration rejected: active 500 Hz profile is unavailable"
        );
        return ESP_ERR_INVALID_STATE;
    }
#endif
    TaskHandle_t calibration_task = ensure_imu_calibration_task();
    if (calibration_task == nullptr) {
        publish_imu_calibration_failure(
            ESP_ERR_NO_MEM,
            calibration_attempted_at_us
        );
        ESP_LOGE(TAG, "IMU calibration task creation failed");
        return ESP_ERR_NO_MEM;
    }

    if (s_imu_calibration_request_pending.exchange(
            true,
            std::memory_order_acq_rel
        )) {
        publish_imu_calibration_failure(
            ESP_ERR_INVALID_STATE,
            calibration_attempted_at_us
        );
        ESP_LOGW(TAG, "IMU calibration rejected: another calibration is running");
        return ESP_ERR_INVALID_STATE;
    }

    xTaskNotifyGive(calibration_task);
    ESP_LOGI(TAG, "IMU calibration scheduled");
    return ESP_OK;
}
