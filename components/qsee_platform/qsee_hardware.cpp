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

#include "qsee_platform.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_settings.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_bit_defs.h"
#include "esp_chip_info.h"
#include "esp_codec_dev.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_psram.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "qmi8658.h"

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

#define I2C_TIMEOUT_MS 100
#define GPS_MAX_PAYLOAD 4096
#define GPS_MAX_PENDING_BYTES (1024U * 1024U)

#define AXP2101_ADDRESS 0x34

#define PCF85063_ADDRESS 0x51
#define PCF85063_REG_CTRL1 0x00
#define PCF85063_REG_RAM 0x03
#define PCF85063_REG_SECONDS 0x04

#define LC76G_WRITE_ADDRESS 0x50
#define LC76G_READ_ADDRESS 0x54

namespace {

const char *TAG = "qsee_snapshot";
constexpr char kDiagnosticPeriodSetting[] = "diag_ms";
constexpr char kImuSmoothingSetting[] = "imu_smooth";

i2c_master_bus_handle_t s_i2c_bus;
i2c_master_dev_handle_t s_rtc;
i2c_master_dev_handle_t s_gps_control;
i2c_master_dev_handle_t s_gps_read;
qmi8658_dev_t s_imu;
XPowersPMU s_power;

bool s_initialized;
bool s_pmu_ready;
bool s_rtc_ready;
bool s_imu_ready;
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
volatile uint32_t s_imu_smoothing_percent = 70;
bool s_gps_ready;
bool s_microphone_worker_started;
bool s_microphone_ready;
bool s_diagnostic_monitor_started;
volatile bool s_diagnostic_monitor_active;
volatile uint32_t s_diagnostic_period_ms = 300;

qsee::DeviceSnapshot s_snapshot;
StaticSemaphore_t s_snapshot_mutex_control;
SemaphoreHandle_t s_snapshot_mutex;
StaticSemaphore_t s_refresh_mutex_control;
SemaphoreHandle_t s_refresh_mutex;

StaticTask_t s_microphone_task_control;
StackType_t s_microphone_task_stack[4096];
StaticTask_t s_diagnostic_task_control;
StackType_t s_diagnostic_task_stack[3072];
char s_gps_payload[GPS_MAX_PAYLOAD + 1];
int16_t s_microphone_samples[1024];

SemaphoreHandle_t ensure_snapshot_mutex()
{
    if (s_snapshot_mutex == nullptr) {
        s_snapshot_mutex = xSemaphoreCreateMutexStatic(&s_snapshot_mutex_control);
    }
    return s_snapshot_mutex;
}

SemaphoreHandle_t ensure_refresh_mutex()
{
    if (s_refresh_mutex == nullptr) {
        s_refresh_mutex = xSemaphoreCreateMutexStatic(&s_refresh_mutex_control);
    }
    return s_refresh_mutex;
}

qsee::DeviceHealth health_for(bool ready, esp_err_t error)
{
    if (ready) {
        return qsee::DeviceHealth::Ready;
    }
    return error == ESP_ERR_NOT_FOUND ? qsee::DeviceHealth::Missing : qsee::DeviceHealth::Degraded;
}

void set_component(qsee::ComponentSnapshot &component, bool ready, esp_err_t error)
{
    component.health = health_for(ready, error);
    component.last_error = error;
}

template <typename Update>
void update_snapshot(Update update)
{
    SemaphoreHandle_t mutex = ensure_snapshot_mutex();
    if (mutex != nullptr && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        update(s_snapshot);
        xSemaphoreGive(mutex);
    }
}

esp_err_t add_i2c_device(
    i2c_master_bus_handle_t bus,
    uint8_t address,
    uint32_t frequency_hz,
    i2c_master_dev_handle_t *device
)
{
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = frequency_hz,
        .scl_wait_us = 20000,
        .flags = {},
    };
    return i2c_master_bus_add_device(bus, &config, device);
}

esp_err_t read_register(
    i2c_master_dev_handle_t device,
    uint8_t reg,
    uint8_t *data,
    size_t length
)
{
    return i2c_master_transmit_receive(device, &reg, 1, data, length, I2C_TIMEOUT_MS);
}

esp_err_t write_register(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value)
{
    const uint8_t payload[] = {reg, value};
    return i2c_master_transmit(device, payload, sizeof(payload), I2C_TIMEOUT_MS);
}

esp_err_t write_registers(
    i2c_master_dev_handle_t device,
    uint8_t reg,
    const uint8_t *data,
    size_t length
)
{
    if (device == nullptr || data == nullptr || length == 0 || length > 15) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t payload[16] = {reg};
    std::memcpy(payload + 1, data, length);
    return i2c_master_transmit(device, payload, length + 1, I2C_TIMEOUT_MS);
}

uint8_t bcd_to_decimal(uint8_t value)
{
    return static_cast<uint8_t>(((value >> 4) * 10U) + (value & 0x0FU));
}

uint8_t decimal_to_bcd(uint8_t value)
{
    return static_cast<uint8_t>(((value / 10U) << 4) | (value % 10U));
}

bool valid_calendar_time(const qsee_calendar_time_t &time)
{
    return time.year >= 2024 && time.year <= 2099 &&
           time.month >= 1 && time.month <= 12 &&
           time.day >= 1 && time.day <= 31 &&
           time.hour <= 23 && time.minute <= 59 && time.second <= 59;
}

esp_err_t sample_pmu(const char *phase)
{
    qsee::BatterySnapshot sample;
    const esp_err_t error =
        i2c_master_probe(s_i2c_bus, AXP2101_ADDRESS, I2C_TIMEOUT_MS);

    set_component(sample, error == ESP_OK, error);
    if (error != ESP_OK) {
        update_snapshot([&sample](qsee::DeviceSnapshot &snapshot) { snapshot.battery = sample; });
        ESP_LOGW(TAG, "PMU AXP2101: %s sample failed (%s)", phase, esp_err_to_name(error));
        return error;
    }

    sample.battery_present = s_power.isBatteryConnect();
    sample.usb_present = s_power.isVbusIn();
    const uint8_t percent = s_power.getBatteryPercent();
    sample.level_percent = sample.battery_present && percent <= 100 ? percent : -1;
    sample.battery_mv = s_power.getBattVoltage();
    sample.usb_mv = s_power.getVbusVoltage();
    sample.system_mv = s_power.getSystemVoltage();
    sample.die_temperature_c = s_power.getTemperature();
    const char *power_state = s_power.isCharging()
                                  ? "charging"
                                  : (s_power.isDischarge() ? "discharging" : "standby");
    strlcpy(sample.power_state, power_state, sizeof(sample.power_state));

    const uint64_t irq_status = s_power.getIrqStatus();
    if (irq_status != 0) {
        ESP_LOGI(
            TAG,
            "PMU AXP2101 events: raw=0x%llx bat+%d bat-%d usb+%d usb-%d key-short=%d key-long=%d charge-start=%d charge-done=%d",
            static_cast<unsigned long long>(irq_status),
            s_power.isBatInsertIrq(),
            s_power.isBatRemoveIrq(),
            s_power.isVbusInsertIrq(),
            s_power.isVbusRemoveIrq(),
            s_power.isPekeyShortPressIrq(),
            s_power.isPekeyLongPressIrq(),
            s_power.isBatChargeStartIrq(),
            s_power.isBatChargeDoneIrq()
        );
        s_power.clearIrqStatus();
    }
    update_snapshot([&sample](qsee::DeviceSnapshot &snapshot) { snapshot.battery = sample; });

    ESP_LOGI(
        TAG,
        "PMU AXP2101 %s: battery=%s %d%% %dmV, state=%s, USB=%s %dmV, system=%dmV, die=%.1fC",
        phase,
        sample.battery_present ? "present" : "absent",
        sample.level_percent,
        sample.battery_mv,
        sample.power_state,
        sample.usb_present ? "present" : "absent",
        sample.usb_mv,
        sample.system_mv,
        sample.die_temperature_c
    );
    return ESP_OK;
}

esp_err_t init_pmu(i2c_master_bus_handle_t bus)
{
    esp_err_t error = i2c_master_probe(bus, AXP2101_ADDRESS, I2C_TIMEOUT_MS);
    if (error == ESP_OK && !s_power.begin(bus, AXP2101_ADDRESS)) {
        error = ESP_FAIL;
    }
    if (error != ESP_OK) {
        update_snapshot([error](qsee::DeviceSnapshot &snapshot) {
            set_component(snapshot.battery, false, error);
        });
        ESP_LOGW(TAG, "PMU AXP2101: not detected (%s)", esp_err_to_name(error));
        return error;
    }

    s_power.enableVbusVoltageMeasure();
    s_power.enableBattVoltageMeasure();
    s_power.enableSystemVoltageMeasure();
    s_power.enableTemperatureMeasure();
    // The board has no battery NTC on TS; leaving TS measurement enabled can inhibit charging.
    s_power.disableTSPinMeasure();

    s_power.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    s_power.clearIrqStatus();
    s_power.enableIRQ(
        XPOWERS_AXP2101_BAT_INSERT_IRQ |
        XPOWERS_AXP2101_BAT_REMOVE_IRQ |
        XPOWERS_AXP2101_VBUS_INSERT_IRQ |
        XPOWERS_AXP2101_VBUS_REMOVE_IRQ |
        XPOWERS_AXP2101_PKEY_SHORT_IRQ |
        XPOWERS_AXP2101_PKEY_LONG_IRQ |
        XPOWERS_AXP2101_BAT_CHG_DONE_IRQ |
        XPOWERS_AXP2101_BAT_CHG_START_IRQ
    );
    s_power.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
    s_power.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_400MA);
    s_power.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
    s_power.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
    // ADC channels need one conversion interval before their first coherent sample.
    vTaskDelay(pdMS_TO_TICKS(50));

    s_pmu_ready = true;
    update_snapshot([](qsee::DeviceSnapshot &snapshot) {
        set_component(snapshot.battery, true, ESP_OK);
    });
    ESP_LOGI(
        TAG,
        "PMU AXP2101: ADCs enabled, TS disabled, charge policy=50/400/25mA target=4.20V, events enabled"
    );
    return ESP_OK;
}

esp_err_t init_rtc(i2c_master_bus_handle_t bus)
{
    esp_err_t error = add_i2c_device(bus, PCF85063_ADDRESS, 400000, &s_rtc);
    uint8_t ram_original = 0;
    if (error == ESP_OK) {
        error = read_register(s_rtc, PCF85063_REG_RAM, &ram_original, 1);
    }
    uint8_t ram_test = ram_original;
    if (error == ESP_OK) {
        error = write_register(s_rtc, PCF85063_REG_RAM, static_cast<uint8_t>(ram_original ^ BIT(7)));
    }
    if (error == ESP_OK) {
        error = read_register(s_rtc, PCF85063_REG_RAM, &ram_test, 1);
    }
    if (s_rtc != nullptr) {
        (void)write_register(s_rtc, PCF85063_REG_RAM, ram_original);
    }
    if (error == ESP_OK && ((ram_test ^ ram_original) & BIT(7)) == 0) {
        error = ESP_ERR_NOT_FOUND;
    }

    uint8_t ctrl1 = 0;
    if (error == ESP_OK) {
        error = read_register(s_rtc, PCF85063_REG_CTRL1, &ctrl1, 1);
    }
    if (error == ESP_OK) {
        ctrl1 &= static_cast<uint8_t>(~(BIT(5) | BIT(1)));
        error = write_register(s_rtc, PCF85063_REG_CTRL1, ctrl1);
    }
    s_rtc_ready = error == ESP_OK;
    update_snapshot([error](qsee::DeviceSnapshot &snapshot) {
        set_component(snapshot.rtc, error == ESP_OK, error);
    });
    if (!s_rtc_ready) {
        ESP_LOGW(TAG, "RTC PCF85063: initialization failed (%s)", esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(TAG, "RTC PCF85063: detected, oscillator running in 24-hour mode");
    return ESP_OK;
}

esp_err_t sample_rtc(const char *phase)
{
    qsee::RtcSnapshot sample;
    uint8_t raw[7] = {};
    const esp_err_t error = read_register(s_rtc, PCF85063_REG_SECONDS, raw, sizeof(raw));
    set_component(sample, error == ESP_OK, error);
    if (error == ESP_OK) {
        sample.clock_integrity = (raw[0] & BIT(7)) == 0;
        sample.second = bcd_to_decimal(raw[0] & 0x7F);
        sample.minute = bcd_to_decimal(raw[1] & 0x7F);
        sample.hour = bcd_to_decimal(raw[2] & 0x3F);
        sample.day = bcd_to_decimal(raw[3] & 0x3F);
        sample.month = bcd_to_decimal(raw[5] & 0x1F);
        sample.year = static_cast<uint16_t>(2000U + bcd_to_decimal(raw[6]));
    }
    update_snapshot([&sample](qsee::DeviceSnapshot &snapshot) {
        const bool system_time_valid = snapshot.rtc.system_time_valid;
        const bool synchronized = snapshot.rtc.synchronized;
        char source[sizeof(snapshot.rtc.source)] = {};
        char timezone[sizeof(snapshot.rtc.timezone)] = {};
        strlcpy(source, snapshot.rtc.source, sizeof(source));
        strlcpy(timezone, snapshot.rtc.timezone, sizeof(timezone));
        snapshot.rtc = sample;
        snapshot.rtc.system_time_valid = system_time_valid;
        snapshot.rtc.synchronized = synchronized;
        strlcpy(snapshot.rtc.source, source, sizeof(snapshot.rtc.source));
        strlcpy(snapshot.rtc.timezone, timezone, sizeof(snapshot.rtc.timezone));
    });
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "RTC PCF85063: %s sample failed (%s)", phase, esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(
        TAG,
        "RTC PCF85063 %s: %04u-%02u-%02u %02u:%02u:%02u, integrity=%s",
        phase,
        sample.year,
        sample.month,
        sample.day,
        sample.hour,
        sample.minute,
        sample.second,
        sample.clock_integrity ? "valid" : "clock-reset"
    );
    return ESP_OK;
}

esp_err_t init_imu(i2c_master_bus_handle_t bus)
{
    const uint8_t addresses[] = {QMI8658_ADDRESS_HIGH, QMI8658_ADDRESS_LOW};
    esp_err_t error = ESP_ERR_NOT_FOUND;
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        std::memset(&s_imu, 0, sizeof(s_imu));
        error = qmi8658_init(&s_imu, bus, addresses[i]);
        if (error == ESP_OK) {
            error = qmi8658_set_accel_range(&s_imu, QMI8658_ACCEL_RANGE_8G);
        }
        if (error == ESP_OK) {
            error = qmi8658_set_accel_odr(&s_imu, QMI8658_ACCEL_ODR_500HZ);
        }
        if (error == ESP_OK) {
            error = qmi8658_set_gyro_range(&s_imu, QMI8658_GYRO_RANGE_512DPS);
        }
        if (error == ESP_OK) {
            error = qmi8658_set_gyro_odr(&s_imu, QMI8658_GYRO_ODR_500HZ);
        }
        if (error == ESP_OK) {
            error = qmi8658_write_register(&s_imu, QMI8658_CTRL5, 0x03);
        }
        if (error == ESP_OK) {
            error = qmi8658_enable_sensors(
                &s_imu,
                QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO
            );
        }
        if (error == ESP_OK) {
            qmi8658_set_accel_unit_mps2(&s_imu, true);
            qmi8658_set_gyro_unit_dps(&s_imu, true);
            // Let the first filtered samples settle before calibration.
            vTaskDelay(pdMS_TO_TICKS(150));
            for (unsigned discard = 0; discard < 16; ++discard) {
                qmi8658_data_t ignored = {};
                (void)qmi8658_read_sensor_data(&s_imu, &ignored);
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            s_imu_ready = true;
            ESP_LOGI(
                TAG,
                "IMU QMI8658: ready at 0x%02x; accel=8g/500Hz gyro=512dps/500Hz LPF enabled",
                addresses[i]
            );
            break;
        }
        if (s_imu.dev_handle != nullptr) {
            (void)i2c_master_bus_rm_device(s_imu.dev_handle);
            s_imu.dev_handle = nullptr;
        }
    }
    update_snapshot([error](qsee::DeviceSnapshot &snapshot) {
        set_component(snapshot.imu, error == ESP_OK, error);
    });
    if (!s_imu_ready) {
        ESP_LOGW(TAG, "IMU QMI8658: not detected");
    }
    return error;
}

esp_err_t calibrate_imu()
{
    if (!s_imu_ready) {
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
        qmi8658_data_t data = {};
        const esp_err_t error = qmi8658_read_sensor_data(&s_imu, &data);
        if (error != ESP_OK) {
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

esp_err_t sample_imu(const char *phase)
{
    qsee::ImuSnapshot sample;
    qmi8658_data_t data = {};
    const esp_err_t error = qmi8658_read_sensor_data(&s_imu, &data);
    set_component(sample, error == ESP_OK, error);
    if (error == ESP_OK) {
        sample.calibrated = s_imu_calibrated;
        sample.calibration_stationary = s_imu_calibration_stationary;
        sample.calibration_samples = s_imu_calibration_samples;
        sample.calibration_accel_max_span_mps2 = s_imu_calibration_accel_max_span;
        sample.calibration_gyro_max_span_dps = s_imu_calibration_gyro_max_span;
        const float acceleration[] = {data.accelX, data.accelY, data.accelZ};
        const float angular_velocity[] = {data.gyroX, data.gyroY, data.gyroZ};
        const float smoothing =
            static_cast<float>(s_imu_smoothing_percent) / 100.0F;
        for (unsigned axis = 0; axis < 3; ++axis) {
            sample.acceleration_bias_mps2[axis] = s_accel_bias[axis];
            sample.angular_velocity_bias_dps[axis] = s_gyro_bias[axis];
            const float acceleration_corrected =
                acceleration[axis] - (s_imu_calibrated ? s_accel_bias[axis] : 0.0F);
            const float angular_velocity_corrected =
                angular_velocity[axis] - (s_imu_calibrated ? s_gyro_bias[axis] : 0.0F);
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
        sample.temperature_c = data.temperature;
    }
    update_snapshot([&sample](qsee::DeviceSnapshot &snapshot) { snapshot.imu = sample; });
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "IMU QMI8658: %s sample failed (%s)", phase, esp_err_to_name(error));
        return error;
    }
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
    return ESP_OK;
}

esp_err_t init_gps(i2c_master_bus_handle_t bus)
{
    esp_err_t error = add_i2c_device(bus, LC76G_WRITE_ADDRESS, 100000, &s_gps_control);
    if (error == ESP_OK) {
        error = add_i2c_device(bus, LC76G_READ_ADDRESS, 100000, &s_gps_read);
    }
    if (error == ESP_OK) {
        const esp_err_t write_probe = i2c_master_probe(bus, LC76G_WRITE_ADDRESS, I2C_TIMEOUT_MS);
        ESP_LOGI(
            TAG,
            "GPS LC76G control endpoint 0x50: %s; read endpoint 0x54 is command-gated",
            esp_err_to_name(write_probe)
        );
        if (write_probe != ESP_OK) {
            error = ESP_ERR_NOT_FOUND;
        }
    }
    s_gps_ready = error == ESP_OK;
    update_snapshot([error](qsee::DeviceSnapshot &snapshot) {
        set_component(snapshot.gps, error == ESP_OK, error);
    });
    if (!s_gps_ready) {
        ESP_LOGW(TAG, "GPS LC76G: initialization failed (%s)", esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(
        TAG,
        "GPS LC76G: Waveshare bridge protocol configured (control=0x50 read=0x54)"
    );
    return ESP_OK;
}

esp_err_t gps_write(const uint8_t *data, size_t length)
{
    if (data == nullptr || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit(s_gps_control, data, length, 1000);
}

esp_err_t gps_exchange(
    const uint8_t *command,
    size_t command_length,
    uint8_t *data,
    size_t data_length,
    const char *step
)
{
    esp_err_t error = ESP_FAIL;
    for (unsigned attempt = 1; attempt <= 3; ++attempt) {
        error = gps_write(command, command_length);
        if (error == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            error = i2c_master_receive(s_gps_read, data, data_length, 1000);
        }
        if (error == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(
            TAG,
            "GPS LC76G: %s exchange attempt %u/3 failed (%s)",
            step,
            attempt,
            esp_err_to_name(error)
        );
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return error;
}

double nmea_coordinate(const char *value, char hemisphere)
{
    if (value == nullptr || value[0] == '\0') {
        return 0.0;
    }
    const double encoded = std::strtod(value, nullptr);
    const int degrees = static_cast<int>(encoded / 100.0);
    double result = static_cast<double>(degrees) + (encoded - degrees * 100.0) / 60.0;
    if (hemisphere == 'S' || hemisphere == 'W') {
        result = -result;
    }
    return result;
}

void parse_nmea_sentence(const char *sentence, size_t length, qsee::GpsSnapshot &sample)
{
    if (sentence == nullptr || length == 0) {
        return;
    }

    char line[192] = {};
    const size_t copy_length = length < sizeof(line) - 1U ? length : sizeof(line) - 1U;
    std::memcpy(line, sentence, copy_length);

    char *fields[20] = {line};
    size_t field_count = 1;
    for (size_t index = 0; index < copy_length &&
                           field_count < sizeof(fields) / sizeof(fields[0]);
         ++index) {
        if (line[index] == ',') {
            line[index] = '\0';
            fields[field_count++] = &line[index + 1U];
        }
    }
    if (field_count < 2 || fields[0] == nullptr) {
        return;
    }

    const size_t type_length = std::strlen(fields[0]);
    const char *type = type_length >= 3 ? fields[0] + type_length - 3 : fields[0];
    if (std::strcmp(type, "GGA") == 0 && field_count >= 10) {
        strlcpy(sample.utc_time, fields[1], sizeof(sample.utc_time));
        sample.latitude_degrees = nmea_coordinate(
            fields[2],
            fields[3] != nullptr ? fields[3][0] : '\0'
        );
        sample.longitude_degrees = nmea_coordinate(
            fields[4],
            fields[5] != nullptr ? fields[5][0] : '\0'
        );
        sample.fix_quality = static_cast<uint8_t>(std::strtoul(fields[6], nullptr, 10));
        sample.satellites = static_cast<uint8_t>(std::strtoul(fields[7], nullptr, 10));
        sample.hdop = std::strtof(fields[8], nullptr);
        sample.altitude_m = std::strtof(fields[9], nullptr);
        sample.has_fix = sample.fix_quality > 0;
    } else if (std::strcmp(type, "RMC") == 0 && field_count >= 10) {
        strlcpy(sample.utc_time, fields[1], sizeof(sample.utc_time));
        if (fields[9] != nullptr && std::strlen(fields[9]) >= 6) {
            const unsigned day = static_cast<unsigned>(
                (fields[9][0] - '0') * 10 + (fields[9][1] - '0')
            );
            const unsigned month = static_cast<unsigned>(
                (fields[9][2] - '0') * 10 + (fields[9][3] - '0')
            );
            const unsigned year = 2000U + static_cast<unsigned>(
                (fields[9][4] - '0') * 10 + (fields[9][5] - '0')
            );
            if (day >= 1 && day <= 31 && month >= 1 && month <= 12) {
                std::snprintf(
                    sample.utc_date,
                    sizeof(sample.utc_date),
                    "%04u-%02u-%02u",
                    year,
                    month,
                    day
                );
            }
        }
        const bool valid = fields[2] != nullptr && fields[2][0] == 'A';
        if (valid) {
            sample.latitude_degrees = nmea_coordinate(
                fields[3],
                fields[4] != nullptr ? fields[4][0] : '\0'
            );
            sample.longitude_degrees = nmea_coordinate(
                fields[5],
                fields[6] != nullptr ? fields[6][0] : '\0'
            );
            sample.speed_mps = std::strtof(fields[7], nullptr) * 0.514444F;
            sample.course_degrees = std::strtof(fields[8], nullptr);
            sample.has_fix = true;
        }
    }
}

void parse_nmea_payload(const char *payload, size_t length, qsee::GpsSnapshot &sample)
{
    size_t cursor = 0;
    bool copied_first = false;
    while (cursor < length) {
        while (cursor < length && payload[cursor] != '$') {
            ++cursor;
        }
        const size_t start = cursor;
        while (cursor < length && payload[cursor] != '\r' && payload[cursor] != '\n') {
            ++cursor;
        }
        if (start < cursor) {
            const size_t sentence_length = cursor - start;
            if (!copied_first) {
                const size_t output_length =
                    sentence_length < sizeof(sample.first_sentence) - 1U
                        ? sentence_length
                        : sizeof(sample.first_sentence) - 1U;
                std::memcpy(sample.first_sentence, payload + start, output_length);
                sample.first_sentence[output_length] = '\0';
                copied_first = true;
            }
            parse_nmea_sentence(payload + start, sentence_length, sample);
        }
        while (cursor < length && (payload[cursor] == '\r' || payload[cursor] == '\n')) {
            ++cursor;
        }
    }
}

esp_err_t sample_gps(const char *phase)
{
    static const uint8_t length_command[] = {0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00};
    qsee::GpsSnapshot sample;
    uint8_t length_bytes[4] = {};

    esp_err_t error = gps_exchange(
        length_command,
        sizeof(length_command),
        length_bytes,
        sizeof(length_bytes),
        "length"
    );
    const uint32_t data_length =
        static_cast<uint32_t>(length_bytes[0]) |
        (static_cast<uint32_t>(length_bytes[1]) << 8) |
        (static_cast<uint32_t>(length_bytes[2]) << 16) |
        (static_cast<uint32_t>(length_bytes[3]) << 24);
    sample.pending_nmea_bytes = data_length;
    const uint32_t read_length =
        data_length < GPS_MAX_PAYLOAD ? data_length : GPS_MAX_PAYLOAD;

    if (error == ESP_OK && data_length > GPS_MAX_PENDING_BYTES) {
        ESP_LOGW(
            TAG,
            "GPS LC76G: invalid pending length=%lu raw=[%02x %02x %02x %02x]",
            static_cast<unsigned long>(data_length),
            length_bytes[0],
            length_bytes[1],
            length_bytes[2],
            length_bytes[3]
        );
        error = ESP_ERR_INVALID_SIZE;
    }
    if (error == ESP_OK && data_length > GPS_MAX_PAYLOAD) {
        ESP_LOGI(
            TAG,
            "GPS LC76G: %lu bytes pending; draining a bounded %u-byte chunk",
            static_cast<unsigned long>(data_length),
            GPS_MAX_PAYLOAD
        );
    }
    if (error == ESP_OK && read_length > 0) {
        const uint8_t request[8] = {
            0x00, 0x20, 0x51, 0xAA,
            static_cast<uint8_t>(read_length),
            static_cast<uint8_t>(read_length >> 8),
            static_cast<uint8_t>(read_length >> 16),
            static_cast<uint8_t>(read_length >> 24),
        };
        error = gps_exchange(
            request,
            sizeof(request),
            reinterpret_cast<uint8_t *>(s_gps_payload),
            read_length,
            "payload"
        );
        if (error == ESP_OK) {
            s_gps_payload[read_length] = '\0';
            parse_nmea_payload(s_gps_payload, read_length, sample);
        }
    }

    set_component(sample, error == ESP_OK, error);
    update_snapshot([&sample](qsee::DeviceSnapshot &snapshot) { snapshot.gps = sample; });
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "GPS LC76G: %s sample failed (%s)", phase, esp_err_to_name(error));
        return error;
    }
    if (read_length == 0) {
        ESP_LOGI(TAG, "GPS LC76G %s: transport online, no NMEA bytes waiting", phase);
    } else {
        ESP_LOGI(
            TAG,
            "GPS LC76G %s: read=%lu pending-before=%lu byte(s), fix=%s quality=%u sats=%u lat=%.6f lon=%.6f alt=%.1fm speed=%.2fm/s hdop=%.1f first=\"%s\"",
            phase,
            static_cast<unsigned long>(read_length),
            static_cast<unsigned long>(data_length),
            sample.has_fix ? "yes" : "no",
            sample.fix_quality,
            sample.satellites,
            sample.latitude_degrees,
            sample.longitude_degrees,
            sample.altitude_m,
            sample.speed_mps,
            sample.hdop,
            sample.first_sentence
        );
    }
    if (sample.has_fix && sample.utc_date[0] != '\0' && std::strlen(sample.utc_time) >= 6) {
        const uint16_t year = static_cast<uint16_t>(std::strtoul(sample.utc_date, nullptr, 10));
        const uint8_t month = static_cast<uint8_t>(std::strtoul(sample.utc_date + 5, nullptr, 10));
        const uint8_t day = static_cast<uint8_t>(std::strtoul(sample.utc_date + 8, nullptr, 10));
        const uint8_t hour = static_cast<uint8_t>(
            (sample.utc_time[0] - '0') * 10 + (sample.utc_time[1] - '0')
        );
        const uint8_t minute = static_cast<uint8_t>(
            (sample.utc_time[2] - '0') * 10 + (sample.utc_time[3] - '0')
        );
        const uint8_t second = static_cast<uint8_t>(
            (sample.utc_time[4] - '0') * 10 + (sample.utc_time[5] - '0')
        );
        (void)qsee_time_sync_from_gps(year, month, day, hour, minute, second);
    }
    return ESP_OK;
}

esp_err_t sample_microphone(const char *phase)
{
    constexpr size_t channel_count = 2;
    constexpr size_t frame_count = sizeof(s_microphone_samples) / sizeof(s_microphone_samples[0]) / channel_count;
    const esp_err_t error = qsee_audio_capture_pcm16_channels(
        s_microphone_samples,
        frame_count,
        channel_count,
        22050,
        24.0F
    );

    uint32_t absolute_sum[2] = {};
    uint32_t peak[2] = {};
    if (error == ESP_OK) {
        for (size_t index = 0; index < frame_count; ++index) {
            for (size_t channel = 0; channel < channel_count; ++channel) {
                const int16_t raw = s_microphone_samples[index * channel_count + channel];
                const uint32_t value = raw < 0 ? static_cast<uint32_t>(-static_cast<int32_t>(raw))
                                               : static_cast<uint32_t>(raw);
                absolute_sum[channel] += value;
                if (value > peak[channel]) {
                    peak[channel] = value;
                }
            }
        }
    }
    const uint32_t mean = (absolute_sum[0] + absolute_sum[1]) / (frame_count * channel_count);
    const uint32_t aggregate_peak = peak[0] > peak[1] ? peak[0] : peak[1];
    const uint32_t channel_mean0 = absolute_sum[0] / frame_count;
    const uint32_t channel_mean1 = absolute_sum[1] / frame_count;
    const uint32_t channel_peak0 = peak[0];
    const uint32_t channel_peak1 = peak[1];
    update_snapshot([error, mean, aggregate_peak, channel_count, channel_mean0, channel_mean1,
                     channel_peak0, channel_peak1](qsee::DeviceSnapshot &snapshot) {
        set_component(snapshot.audio.microphone, error == ESP_OK, error);
        snapshot.audio.channel_count = error == ESP_OK ? static_cast<uint8_t>(channel_count) : 0;
        snapshot.audio.sample_rate_hz = error == ESP_OK ? 22050 : 0;
        snapshot.audio.microphone_mean_level = mean;
        snapshot.audio.microphone_peak_level = aggregate_peak;
        snapshot.audio.channel_mean_level[0] = channel_mean0;
        snapshot.audio.channel_mean_level[1] = channel_mean1;
        snapshot.audio.channel_peak_level[0] = channel_peak0;
        snapshot.audio.channel_peak_level[1] = channel_peak1;
    });
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "Microphones ES7210 %s: ch0=%lu/%lu ch1=%lu/%lu",
                 phase,
                 static_cast<unsigned long>(absolute_sum[0] / frame_count),
                 static_cast<unsigned long>(peak[0]),
                 static_cast<unsigned long>(absolute_sum[1] / frame_count),
                 static_cast<unsigned long>(peak[1]));
    } else {
        ESP_LOGW(TAG, "Microphones ES7210: %s capture failed (%s)",
                 phase, esp_err_to_name(error));
    }
    return error;
}

void microphone_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(2500));
    const esp_err_t init_error = qsee_audio_init();
    if (init_error != ESP_OK) {
        update_snapshot([](qsee::DeviceSnapshot &snapshot) {
            set_component(snapshot.audio.microphone, false, ESP_ERR_NOT_FOUND);
        });
        ESP_LOGW(TAG, "Microphones ES7210: codec initialization failed (%s)",
                 esp_err_to_name(init_error));
        vTaskDelete(nullptr);
        return;
    }

    s_microphone_ready = true;
    ESP_LOGI(TAG, "Microphones ES7210: ADC path enabled at 22.05kHz 2-channel PCM16");
    (void)sample_microphone("initial");
    vTaskDelete(nullptr);
}

void diagnostic_monitor_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_diagnostic_monitor_active) {
            SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
            if (refresh_mutex != nullptr &&
                xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(250)) == pdTRUE) {
                esp_err_t aggregate = ESP_OK;
                if (s_imu_ready && sample_imu("diagnostic-ui") != ESP_OK) {
                    aggregate = ESP_FAIL;
                }
                if (s_microphone_ready && sample_microphone("diagnostic-ui") != ESP_OK) {
                    aggregate = ESP_FAIL;
                }
                update_snapshot([](qsee::DeviceSnapshot &snapshot) {
                    ++snapshot.sequence;
                    snapshot.refreshed_at_us = esp_timer_get_time();
                    strlcpy(snapshot.reason, "diagnostic-ui", sizeof(snapshot.reason));
                });
                xSemaphoreGive(refresh_mutex);
                if (aggregate != ESP_OK) {
                    ESP_LOGW(TAG, "Diagnostic sample completed with degraded inputs");
                }
            }
        }
        const uint32_t delay_ms =
            s_diagnostic_monitor_active ? s_diagnostic_period_ms : 250U;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void log_summary(const qsee::DeviceSnapshot &snapshot)
{
    ESP_LOGI(
        TAG,
        "Snapshot #%lu (%s): PMU=%s RTC=%s IMU=%s GPS=%s MIC=%s SPK=%s SD=%s DISPLAY=%s TOUCH=%s WIFI=%s BLE=%s",
        static_cast<unsigned long>(snapshot.sequence),
        snapshot.reason,
        qsee::deviceHealthName(snapshot.battery.health),
        qsee::deviceHealthName(snapshot.rtc.health),
        qsee::deviceHealthName(snapshot.imu.health),
        qsee::deviceHealthName(snapshot.gps.health),
        qsee::deviceHealthName(snapshot.audio.microphone.health),
        qsee::deviceHealthName(snapshot.audio.speaker.health),
        qsee::deviceHealthName(snapshot.storage.health),
        qsee::deviceHealthName(snapshot.ui.display.health),
        qsee::deviceHealthName(snapshot.ui.touch.health),
        qsee::deviceHealthName(snapshot.radios.wifi.health),
        qsee::deviceHealthName(snapshot.radios.ble.health)
    );
    ESP_LOGI(
        TAG,
        "Flash: firmware=%luB partition=%luB capacity=%luB",
        static_cast<unsigned long>(snapshot.memory.firmware_image_bytes),
        static_cast<unsigned long>(snapshot.memory.app_partition_bytes),
        static_cast<unsigned long>(snapshot.memory.flash_capacity_bytes)
    );
    ESP_LOGI(
        TAG,
        "Memory: heap=%luB min=%luB internal=%luB largest=%luB pressure=%s PSRAM used=%luB free=%luB total=%luB physical=%luB largest=%luB",
        static_cast<unsigned long>(snapshot.memory.heap_free_bytes),
        static_cast<unsigned long>(snapshot.memory.heap_minimum_free_bytes),
        static_cast<unsigned long>(snapshot.memory.internal_free_bytes),
        static_cast<unsigned long>(snapshot.memory.internal_largest_block_bytes),
        snapshot.memory.internal_memory_pressure ? "high" : "normal",
        static_cast<unsigned long>(snapshot.memory.psram_used_bytes),
        static_cast<unsigned long>(snapshot.memory.psram_free_bytes),
        static_cast<unsigned long>(snapshot.memory.psram_heap_total_bytes),
        static_cast<unsigned long>(snapshot.memory.psram_capacity_bytes),
        static_cast<unsigned long>(snapshot.memory.psram_largest_block_bytes)
    );
    ESP_LOGI(
        TAG,
        "Cores: count=%u sampled-on=%u core0=%s(prio=%lu stack=%lu) core1=%s(prio=%lu stack=%lu)",
        snapshot.processor.core_count,
        snapshot.processor.sampling_core,
        snapshot.processor.cores[0].current_task,
        static_cast<unsigned long>(snapshot.processor.cores[0].priority),
        static_cast<unsigned long>(snapshot.processor.cores[0].stack_high_water_words),
        snapshot.processor.cores[1].current_task,
        static_cast<unsigned long>(snapshot.processor.cores[1].priority),
        static_cast<unsigned long>(snapshot.processor.cores[1].stack_high_water_words)
    );
    ESP_LOGI(
        TAG,
        "Time/GPS: UTC=%04u-%02u-%02u %02u:%02u:%02u source=%s sync=%s fix=%s sat=%u",
        snapshot.rtc.year,
        snapshot.rtc.month,
        snapshot.rtc.day,
        snapshot.rtc.hour,
        snapshot.rtc.minute,
        snapshot.rtc.second,
        snapshot.rtc.source,
        snapshot.rtc.synchronized ? "yes" : "no",
        snapshot.gps.has_fix ? "yes" : "no",
        static_cast<unsigned>(snapshot.gps.satellites)
    );
    ESP_LOGI(
        TAG,
        "Inputs/IMU/SD: BOOT=%s(%lu) PWR=%s(short=%lu long=%lu) TCA9554=%s IMU-cal=%s SD-card=%s attempts=%lu format-attempted=%s",
        snapshot.input.boot_pressed ? "pressed" : "released",
        static_cast<unsigned long>(snapshot.input.boot_press_count),
        snapshot.input.power_pressed ? "pressed" : "released",
        static_cast<unsigned long>(snapshot.input.power_short_press_count),
        static_cast<unsigned long>(snapshot.input.power_long_press_count),
        qsee::deviceHealthName(snapshot.input.io_expander.health),
        snapshot.imu.calibrated ? "yes" : "no",
        snapshot.storage.card_present ? "present" : "absent",
        static_cast<unsigned long>(snapshot.storage.mount_attempt_count),
        snapshot.storage.format_attempted ? "yes" : "no"
    );
}

void sample_system_state()
{
    qsee::MemorySnapshot memory;
    (void)esp_flash_get_size(nullptr, &memory.flash_capacity_bytes);

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition != nullptr) {
        memory.app_partition_bytes = running_partition->size;
        const esp_partition_pos_t partition_position = {
            .offset = running_partition->address,
            .size = running_partition->size,
        };
        esp_image_metadata_t image_metadata = {};
        if (esp_image_get_metadata(&partition_position, &image_metadata) == ESP_OK) {
            memory.firmware_image_bytes = image_metadata.image_len;
        }
    }

    memory.heap_free_bytes = esp_get_free_heap_size();
    memory.heap_minimum_free_bytes = esp_get_minimum_free_heap_size();
    memory.internal_free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    memory.internal_largest_block_bytes =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    memory.internal_memory_pressure =
        memory.internal_free_bytes < 32768U ||
        memory.internal_largest_block_bytes < 16384U;
    memory.psram_capacity_bytes = static_cast<uint32_t>(esp_psram_get_size());
    memory.psram_heap_total_bytes =
        static_cast<uint32_t>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    memory.psram_free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    memory.psram_used_bytes = memory.psram_heap_total_bytes - memory.psram_free_bytes;
    memory.psram_largest_block_bytes = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    qsee::ProcessorSnapshot processor;
    esp_chip_info_t chip = {};
    esp_chip_info(&chip);
    processor.core_count = static_cast<uint8_t>(chip.cores);
    processor.sampling_core = static_cast<uint8_t>(esp_cpu_get_core_id());

    const uint8_t cores_to_query = processor.core_count < 2 ? processor.core_count : 2;
    for (uint8_t core = 0; core < cores_to_query; ++core) {
        TaskHandle_t task = xTaskGetCurrentTaskHandleForCore(core);
        if (task == nullptr) {
            continue;
        }
        strlcpy(processor.cores[core].current_task, pcTaskGetName(task),
                sizeof(processor.cores[core].current_task));
        processor.cores[core].priority = uxTaskPriorityGet(task);
        processor.cores[core].stack_high_water_words = uxTaskGetStackHighWaterMark(task);
    }

    update_snapshot([&memory, &processor](qsee::DeviceSnapshot &snapshot) {
        snapshot.memory = memory;
        snapshot.processor = processor;
    });
}

}  // namespace

namespace qsee {

const char *deviceHealthName(DeviceHealth health)
{
    switch (health) {
    case DeviceHealth::Ready:
        return "ready";
    case DeviceHealth::Degraded:
        return "degraded";
    case DeviceHealth::Missing:
        return "missing";
    case DeviceHealth::Unknown:
    default:
        return "unknown";
    }
}

DeviceSnapshotService &DeviceSnapshotService::instance()
{
    static DeviceSnapshotService service;
    return service;
}

esp_err_t DeviceSnapshotService::initialize()
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (ensure_snapshot_mutex() == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (ensure_refresh_mutex() == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    auto &settings = UserSettingsService::instance();
    const esp_err_t settings_error = settings.initialize();
    if (settings_error == ESP_OK) {
        uint32_t saved_period_ms = 300;
        if (settings.getUInt32(kDiagnosticPeriodSetting, 300, &saved_period_ms) == ESP_OK) {
            s_diagnostic_period_ms = saved_period_ms < 100U ? 100U :
                                     saved_period_ms > 2000U ? 2000U :
                                     saved_period_ms;
        }
        uint32_t saved_smoothing = 70;
        if (settings.getUInt32(kImuSmoothingSetting, 70, &saved_smoothing) == ESP_OK) {
            s_imu_smoothing_percent = saved_smoothing > 90U ? 90U : saved_smoothing;
        }
    } else {
        ESP_LOGW(TAG, "User settings unavailable; diagnostic period uses default");
    }

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == nullptr) {
        ESP_LOGE(TAG, "Shared BSP I2C bus unavailable");
        return ESP_ERR_INVALID_STATE;
    }
    s_i2c_bus = bus;
    ESP_LOGI(TAG, "Shared BSP I2C: SDA=%d SCL=%d", BSP_I2C_SDA, BSP_I2C_SCL);

    const esp_err_t pmu = init_pmu(bus);
    const esp_err_t rtc = init_rtc(bus);
    const esp_err_t imu = init_imu(bus);
    if (imu == ESP_OK) {
        (void)calibrate_imu();
    }
    const esp_err_t gps = init_gps(bus);

    TaskHandle_t microphone = xTaskCreateStatic(
        microphone_task,
        "qsee_microphone",
        sizeof(s_microphone_task_stack) / sizeof(s_microphone_task_stack[0]),
        nullptr,
        3,
        s_microphone_task_stack,
        &s_microphone_task_control
    );
    s_microphone_worker_started = microphone != nullptr;
    if (!s_microphone_worker_started) {
        update_snapshot([](DeviceSnapshot &snapshot) {
            set_component(snapshot.audio.microphone, false, ESP_ERR_NO_MEM);
        });
    }
    ESP_LOGI(TAG, "Audio input worker: %s (delayed until boot SFX completes)",
             s_microphone_worker_started ? "started" : "failed");

    s_initialized = true;
    const esp_err_t diagnostic_task = qsee_hardware_set_diagnostic_monitor(false);
    if (diagnostic_task != ESP_OK) {
        ESP_LOGW(TAG, "Diagnostic monitor task unavailable (%s)",
                 esp_err_to_name(diagnostic_task));
    }
    if (pmu != ESP_OK || rtc != ESP_OK || imu != ESP_OK || gps != ESP_OK) {
        ESP_LOGW(TAG, "Hardware initialization completed with degraded devices");
    } else {
        ESP_LOGI(TAG, "Hardware initialization: PMU + RTC + IMU + GPS configured");
    }
    return refresh("boot");
}

esp_err_t DeviceSnapshotService::refresh(const char *reason)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "Hardware refresh rejected: another refresh is still running");
        return ESP_ERR_TIMEOUT;
    }
    const char *phase = reason != nullptr && reason[0] != '\0' ? reason : "manual";
    esp_err_t aggregate = ESP_OK;

    if (s_pmu_ready && sample_pmu(phase) != ESP_OK) {
        aggregate = ESP_FAIL;
    }
    if (s_rtc_ready && sample_rtc(phase) != ESP_OK) {
        aggregate = ESP_FAIL;
    }
    if (s_imu_ready && sample_imu(phase) != ESP_OK) {
        aggregate = ESP_FAIL;
    }
    if (s_gps_ready && sample_gps(phase) != ESP_OK) {
        aggregate = ESP_FAIL;
    }
    if (s_microphone_ready && sample_microphone(phase) != ESP_OK) {
        aggregate = ESP_FAIL;
    }
    sample_system_state();

    update_snapshot([phase](DeviceSnapshot &snapshot) {
        ++snapshot.sequence;
        snapshot.refreshed_at_us = esp_timer_get_time();
        strlcpy(snapshot.reason, phase, sizeof(snapshot.reason));
    });
    if (std::strcmp(phase, "diagnostic-ui") != 0) {
        log_summary(snapshot());
    }
    xSemaphoreGive(refresh_mutex);
    return aggregate;
}

DeviceSnapshot DeviceSnapshotService::snapshot() const
{
    DeviceSnapshot copy;
    SemaphoreHandle_t mutex = ensure_snapshot_mutex();
    if (mutex != nullptr && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        copy = s_snapshot;
        xSemaphoreGive(mutex);
    }
    return copy;
}

void DeviceSnapshotService::reportStorage(
    bool mounted,
    bool card_present,
    bool format_attempted,
    bool formatted,
    uint32_t mount_attempt_count,
    uint64_t capacity_bytes,
    uint64_t filesystem_total_bytes,
    uint64_t free_bytes,
    bool logger_mirror,
    esp_err_t error
)
{
    update_snapshot([=](DeviceSnapshot &snapshot) {
        set_component(snapshot.storage, mounted, error);
        snapshot.storage.card_present = card_present;
        snapshot.storage.format_attempted = format_attempted;
        snapshot.storage.formatted = formatted;
        snapshot.storage.mount_attempt_count = mount_attempt_count;
        snapshot.storage.capacity_bytes = capacity_bytes;
        snapshot.storage.filesystem_total_bytes = filesystem_total_bytes;
        snapshot.storage.free_bytes = free_bytes;
        snapshot.storage.logger_mirror_enabled = logger_mirror;
    });
}

void DeviceSnapshotService::reportDisplay(bool ready, esp_err_t error)
{
    update_snapshot([=](DeviceSnapshot &snapshot) { set_component(snapshot.ui.display, ready, error); });
}

void DeviceSnapshotService::reportTouch(bool ready, esp_err_t error)
{
    update_snapshot([=](DeviceSnapshot &snapshot) { set_component(snapshot.ui.touch, ready, error); });
}

void DeviceSnapshotService::reportSpeaker(bool ready, esp_err_t error)
{
    update_snapshot([=](DeviceSnapshot &snapshot) { set_component(snapshot.audio.speaker, ready, error); });
}

void DeviceSnapshotService::reportWifi(const WifiSnapshot &wifi)
{
    update_snapshot([=](DeviceSnapshot &snapshot) { snapshot.radios.wifi = wifi; });
}

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
    update_snapshot([=](DeviceSnapshot &snapshot) {
        set_component(snapshot.radios.ble, ready, error);
        snapshot.radios.ble.provisioning = provisioning;
        snapshot.radios.ble.advertising = advertising;
        snapshot.radios.ble.connected = connected;
        snapshot.radios.ble.connection_handle = connection_handle;
        snapshot.radios.ble.connection_count = connection_count;
        snapshot.radios.ble.disconnection_count = disconnection_count;
        if (device_name != nullptr) {
            strlcpy(
                snapshot.radios.ble.device_name,
                device_name,
                sizeof(snapshot.radios.ble.device_name)
            );
        }
    });
}

void DeviceSnapshotService::reportTime(
    bool system_time_valid,
    bool synchronized,
    const char *source,
    const char *timezone
)
{
    update_snapshot([=](DeviceSnapshot &snapshot) {
        snapshot.rtc.system_time_valid = system_time_valid;
        snapshot.rtc.synchronized = synchronized;
        strlcpy(snapshot.rtc.source, source != nullptr ? source : "none",
                sizeof(snapshot.rtc.source));
        strlcpy(snapshot.rtc.timezone, timezone != nullptr ? timezone : "UTC0",
                sizeof(snapshot.rtc.timezone));
    });
}

void DeviceSnapshotService::reportInputs(const InputSnapshot &inputs)
{
    update_snapshot([=](DeviceSnapshot &snapshot) { snapshot.input = inputs; });
}

}  // namespace qsee

extern "C" esp_err_t qsee_hardware_init(void)
{
    return qsee::DeviceSnapshotService::instance().initialize();
}

extern "C" esp_err_t qsee_hardware_refresh(const char *reason)
{
    return qsee::DeviceSnapshotService::instance().refresh(reason);
}

extern "C" esp_err_t qsee_hardware_set_diagnostic_monitor(bool enabled)
{
    if (s_diagnostic_monitor_started) {
        s_diagnostic_monitor_active = enabled;
        return ESP_OK;
    }
    s_diagnostic_monitor_active = false;
    TaskHandle_t task = xTaskCreateStatic(
        diagnostic_monitor_task,
        "qsee_diag_monitor",
        sizeof(s_diagnostic_task_stack) / sizeof(s_diagnostic_task_stack[0]),
        nullptr,
        3,
        s_diagnostic_task_stack,
        &s_diagnostic_task_control
    );
    s_diagnostic_monitor_started = task != nullptr;
    if (!s_diagnostic_monitor_started) {
        return ESP_ERR_NO_MEM;
    }
    s_diagnostic_monitor_active = enabled;
    ESP_LOGI(TAG, "Diagnostic monitor task ready (%s)",
             enabled ? "active" : "standby");
    return ESP_OK;
}

extern "C" esp_err_t qsee_hardware_set_diagnostic_period_ms(uint32_t period_ms)
{
    if (period_ms < 100U || period_ms > 2000U) {
        return ESP_ERR_INVALID_ARG;
    }
    s_diagnostic_period_ms = period_ms;
    auto &settings = qsee::UserSettingsService::instance();
    if (settings.isInitialized()) {
        const esp_err_t persist_error =
            settings.setUInt32(kDiagnosticPeriodSetting, period_ms);
        if (persist_error != ESP_OK) {
            ESP_LOGW(TAG, "Diagnostic period persistence failed (%s)",
                     esp_err_to_name(persist_error));
        }
    }
    ESP_LOGI(TAG, "Diagnostic sample period: %lu ms",
             static_cast<unsigned long>(period_ms));
    return ESP_OK;
}

extern "C" uint32_t qsee_hardware_get_diagnostic_period_ms(void)
{
    return s_diagnostic_period_ms;
}

extern "C" esp_err_t qsee_hardware_set_imu_smoothing_percent(uint32_t percent)
{
    if (percent > 90U) {
        return ESP_ERR_INVALID_ARG;
    }
    s_imu_smoothing_percent = percent;
    s_imu_filter_ready = false;
    auto &settings = qsee::UserSettingsService::instance();
    if (settings.isInitialized()) {
        const esp_err_t persist_error =
            settings.setUInt32(kImuSmoothingSetting, percent);
        if (persist_error != ESP_OK) {
            return persist_error;
        }
    }
    ESP_LOGI(TAG, "IMU input smoothing: %lu%%",
             static_cast<unsigned long>(percent));
    return ESP_OK;
}

extern "C" uint32_t qsee_hardware_get_imu_smoothing_percent(void)
{
    return s_imu_smoothing_percent;
}

extern "C" esp_err_t qsee_imu_calibrate(void)
{
    if (!s_initialized || !s_imu_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    SemaphoreHandle_t refresh_mutex = ensure_refresh_mutex();
    if (refresh_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(refresh_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t result = calibrate_imu();
    if (result == ESP_OK) {
        (void)sample_imu("post-calibration");
    }
    xSemaphoreGive(refresh_mutex);
    return result;
}

extern "C" void qsee_hardware_report_speaker(bool ready, esp_err_t error)
{
    qsee::DeviceSnapshotService::instance().reportSpeaker(ready, error);
}

extern "C" esp_err_t qsee_rtc_read(qsee_calendar_time_t *time)
{
    if (time == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_rtc_ready || s_rtc == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t raw[7] = {};
    const esp_err_t error = read_register(s_rtc, PCF85063_REG_SECONDS, raw, sizeof(raw));
    if (error != ESP_OK) {
        return error;
    }
    time->clock_integrity = (raw[0] & BIT(7)) == 0;
    time->second = bcd_to_decimal(raw[0] & 0x7F);
    time->minute = bcd_to_decimal(raw[1] & 0x7F);
    time->hour = bcd_to_decimal(raw[2] & 0x3F);
    time->day = bcd_to_decimal(raw[3] & 0x3F);
    time->month = bcd_to_decimal(raw[5] & 0x1F);
    time->year = static_cast<uint16_t>(2000U + bcd_to_decimal(raw[6]));
    return ESP_OK;
}

extern "C" esp_err_t qsee_rtc_write(const qsee_calendar_time_t *time)
{
    if (time == nullptr || !valid_calendar_time(*time)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_rtc_ready || s_rtc == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t raw[7] = {
        decimal_to_bcd(time->second),
        decimal_to_bcd(time->minute),
        decimal_to_bcd(time->hour),
        decimal_to_bcd(time->day),
        0,
        decimal_to_bcd(time->month),
        decimal_to_bcd(static_cast<uint8_t>(time->year - 2000U)),
    };
    const esp_err_t error = write_registers(s_rtc, PCF85063_REG_SECONDS, raw, sizeof(raw));
    if (error == ESP_OK) {
        ESP_LOGI(
            TAG,
            "RTC PCF85063 updated: %04u-%02u-%02u %02u:%02u:%02u UTC",
            time->year, time->month, time->day, time->hour, time->minute, time->second
        );
    }
    return error;
}
