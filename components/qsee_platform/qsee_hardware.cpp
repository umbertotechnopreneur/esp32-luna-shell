#include "qsee_platform.h"
#include "qsee_device_snapshot.hpp"

#include <cstdint>
#include <cstring>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_bit_defs.h"
#include "esp_chip_info.h"
#include "esp_codec_dev.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "qmi8658.h"

#define I2C_TIMEOUT_MS 100
#define GPS_MAX_PAYLOAD 4096

#define AXP2101_ADDRESS 0x34
#define AXP2101_REG_STATUS1 0x00
#define AXP2101_REG_STATUS2 0x01
#define AXP2101_REG_CHIP_ID 0x03
#define AXP2101_REG_ADC_CONTROL 0x30
#define AXP2101_REG_ADC_DATA 0x34
#define AXP2101_REG_BATTERY_PERCENT 0xA4
#define AXP2101_EXPECTED_CHIP_ID 0x4A

#define PCF85063_ADDRESS 0x51
#define PCF85063_REG_CTRL1 0x00
#define PCF85063_REG_RAM 0x03
#define PCF85063_REG_SECONDS 0x04

#define LC76G_WRITE_ADDRESS 0x50
#define LC76G_READ_ADDRESS 0x54

namespace {

const char *TAG = "qsee_snapshot";

i2c_master_dev_handle_t s_pmu;
i2c_master_dev_handle_t s_rtc;
i2c_master_dev_handle_t s_gps_write;
i2c_master_dev_handle_t s_gps_read;
qmi8658_dev_t s_imu;
esp_codec_dev_handle_t s_microphone;

bool s_initialized;
bool s_pmu_ready;
bool s_rtc_ready;
bool s_imu_ready;
bool s_gps_ready;
bool s_microphone_worker_started;

qsee::DeviceSnapshot s_snapshot;
StaticSemaphore_t s_snapshot_mutex_control;
SemaphoreHandle_t s_snapshot_mutex;
StaticSemaphore_t s_microphone_mutex_control;
SemaphoreHandle_t s_microphone_mutex;

StaticTask_t s_microphone_task_control;
StackType_t s_microphone_task_stack[4096];
char s_gps_payload[GPS_MAX_PAYLOAD + 1];
int16_t s_microphone_samples[512];

SemaphoreHandle_t ensure_snapshot_mutex()
{
    if (s_snapshot_mutex == nullptr) {
        s_snapshot_mutex = xSemaphoreCreateMutexStatic(&s_snapshot_mutex_control);
    }
    return s_snapshot_mutex;
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

uint8_t bcd_to_decimal(uint8_t value)
{
    return static_cast<uint8_t>(((value >> 4) * 10U) + (value & 0x0FU));
}

esp_err_t sample_pmu(const char *phase)
{
    qsee::BatterySnapshot sample;
    uint8_t status1 = 0;
    uint8_t status2 = 0;
    uint8_t percent = 0xFF;
    uint8_t adc[10] = {};

    esp_err_t error = read_register(s_pmu, AXP2101_REG_STATUS1, &status1, 1);
    if (error == ESP_OK) {
        error = read_register(s_pmu, AXP2101_REG_STATUS2, &status2, 1);
    }
    if (error == ESP_OK) {
        error = read_register(s_pmu, AXP2101_REG_BATTERY_PERCENT, &percent, 1);
    }
    if (error == ESP_OK) {
        error = read_register(s_pmu, AXP2101_REG_ADC_DATA, adc, sizeof(adc));
    }

    set_component(sample, error == ESP_OK, error);
    if (error != ESP_OK) {
        update_snapshot([&sample](qsee::DeviceSnapshot &snapshot) { snapshot.battery = sample; });
        ESP_LOGW(TAG, "PMU AXP2101: %s sample failed (%s)", phase, esp_err_to_name(error));
        return error;
    }

    sample.battery_present = (status1 & BIT(3)) != 0;
    sample.usb_present = (status1 & BIT(5)) != 0 && (status2 & BIT(3)) == 0;
    sample.level_percent = percent <= 100 ? percent : -1;
    sample.battery_mv = ((adc[0] & 0x1F) << 8) | adc[1];
    sample.usb_mv = ((adc[4] & 0x3F) << 8) | adc[5];
    sample.system_mv = ((adc[6] & 0x3F) << 8) | adc[7];
    const int temperature_raw = ((adc[8] & 0x3F) << 8) | adc[9];
    sample.die_temperature_c = 22.0F + (7274.0F - temperature_raw) / 20.0F;
    const char *power_state =
        (status2 >> 5) == 1 ? "charging" : ((status2 >> 5) == 2 ? "discharging" : "standby");
    strlcpy(sample.power_state, power_state, sizeof(sample.power_state));
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
    esp_err_t error = add_i2c_device(bus, AXP2101_ADDRESS, 400000, &s_pmu);
    uint8_t chip_id = 0;
    if (error == ESP_OK) {
        error = read_register(s_pmu, AXP2101_REG_CHIP_ID, &chip_id, 1);
    }
    if (error == ESP_OK && chip_id != AXP2101_EXPECTED_CHIP_ID) {
        error = ESP_ERR_NOT_FOUND;
    }
    if (error != ESP_OK) {
        update_snapshot([error](qsee::DeviceSnapshot &snapshot) {
            set_component(snapshot.battery, false, error);
        });
        ESP_LOGW(TAG, "PMU AXP2101: not detected (id=0x%02x, %s)", chip_id, esp_err_to_name(error));
        return error;
    }

    uint8_t adc_control = 0;
    error = read_register(s_pmu, AXP2101_REG_ADC_CONTROL, &adc_control, 1);
    if (error == ESP_OK) {
        adc_control |= BIT(0) | BIT(2) | BIT(3) | BIT(4);
        adc_control &= static_cast<uint8_t>(~BIT(1));
        error = write_register(s_pmu, AXP2101_REG_ADC_CONTROL, adc_control);
    }
    s_pmu_ready = error == ESP_OK;
    update_snapshot([error](qsee::DeviceSnapshot &snapshot) {
        set_component(snapshot.battery, error == ESP_OK, error);
    });
    if (!s_pmu_ready) {
        ESP_LOGW(TAG, "PMU AXP2101: ADC enable failed (%s)", esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(TAG, "PMU AXP2101: detected id=0x%02x; power ADC enabled", chip_id);
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
    update_snapshot([&sample](qsee::DeviceSnapshot &snapshot) { snapshot.rtc = sample; });
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
            qmi8658_set_accel_unit_mps2(&s_imu, true);
            qmi8658_set_gyro_unit_dps(&s_imu, true);
            s_imu_ready = true;
            ESP_LOGI(TAG, "IMU QMI8658: ready at 0x%02x; accelerometer + gyroscope enabled", addresses[i]);
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

esp_err_t sample_imu(const char *phase)
{
    qsee::ImuSnapshot sample;
    qmi8658_data_t data = {};
    const esp_err_t error = qmi8658_read_sensor_data(&s_imu, &data);
    set_component(sample, error == ESP_OK, error);
    if (error == ESP_OK) {
        sample.acceleration_mps2[0] = data.accelX;
        sample.acceleration_mps2[1] = data.accelY;
        sample.acceleration_mps2[2] = data.accelZ;
        sample.angular_velocity_dps[0] = data.gyroX;
        sample.angular_velocity_dps[1] = data.gyroY;
        sample.angular_velocity_dps[2] = data.gyroZ;
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
        data.accelX,
        data.accelY,
        data.accelZ,
        data.gyroX,
        data.gyroY,
        data.gyroZ,
        data.temperature
    );
    return ESP_OK;
}

esp_err_t init_gps(i2c_master_bus_handle_t bus)
{
    esp_err_t write_error = add_i2c_device(bus, LC76G_WRITE_ADDRESS, 100000, &s_gps_write);
    esp_err_t read_error = add_i2c_device(bus, LC76G_READ_ADDRESS, 100000, &s_gps_read);
    esp_err_t error = write_error != ESP_OK ? write_error : read_error;
    if (error == ESP_OK) {
        const esp_err_t write_probe = i2c_master_probe(bus, LC76G_WRITE_ADDRESS, I2C_TIMEOUT_MS);
        const esp_err_t read_probe = i2c_master_probe(bus, LC76G_READ_ADDRESS, I2C_TIMEOUT_MS);
        if (write_probe != ESP_OK && read_probe != ESP_OK) {
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
    ESP_LOGI(TAG, "GPS LC76G: write=0x50 read=0x54 detected; NMEA available on demand");
    return ESP_OK;
}

esp_err_t sample_gps(const char *phase)
{
    static const uint8_t length_command[] = {0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00};
    qsee::GpsSnapshot sample;
    uint8_t length_bytes[4] = {};

    esp_err_t error = i2c_master_transmit(s_gps_write, length_command, sizeof(length_command), 1000);
    if (error == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        error = i2c_master_receive(s_gps_read, length_bytes, sizeof(length_bytes), 1000);
    }
    const uint32_t data_length =
        static_cast<uint32_t>(length_bytes[0]) |
        (static_cast<uint32_t>(length_bytes[1]) << 8) |
        (static_cast<uint32_t>(length_bytes[2]) << 16) |
        (static_cast<uint32_t>(length_bytes[3]) << 24);
    sample.pending_nmea_bytes = data_length;

    if (error == ESP_OK && data_length > GPS_MAX_PAYLOAD) {
        error = ESP_ERR_INVALID_SIZE;
    }
    if (error == ESP_OK && data_length > 0) {
        const uint8_t request[8] = {
            0x00, 0x20, 0x51, 0xAA,
            length_bytes[0], length_bytes[1], length_bytes[2], length_bytes[3],
        };
        error = i2c_master_transmit(s_gps_write, request, sizeof(request), 1000);
        if (error == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            error = i2c_master_receive(
                s_gps_read,
                reinterpret_cast<uint8_t *>(s_gps_payload),
                data_length,
                1000
            );
        }
        if (error == ESP_OK) {
            s_gps_payload[data_length] = '\0';
            const char *start = std::strchr(s_gps_payload, '$');
            if (start == nullptr) {
                start = s_gps_payload;
            }
            size_t output = 0;
            while (start[output] != '\0' && start[output] != '\r' && start[output] != '\n' &&
                   output < sizeof(sample.first_sentence) - 1U) {
                const unsigned char byte = static_cast<unsigned char>(start[output]);
                sample.first_sentence[output] = byte >= 0x20 && byte <= 0x7E ? static_cast<char>(byte) : '.';
                ++output;
            }
            sample.first_sentence[output] = '\0';
        }
    }

    set_component(sample, error == ESP_OK, error);
    update_snapshot([&sample](qsee::DeviceSnapshot &snapshot) { snapshot.gps = sample; });
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "GPS LC76G: %s sample failed (%s)", phase, esp_err_to_name(error));
        return error;
    }
    if (data_length == 0) {
        ESP_LOGI(TAG, "GPS LC76G %s: transport online, no NMEA bytes waiting", phase);
    } else {
        ESP_LOGI(
            TAG,
            "GPS LC76G %s: %lu NMEA byte(s), first=\"%s\"",
            phase,
            static_cast<unsigned long>(data_length),
            sample.first_sentence
        );
    }
    return ESP_OK;
}

esp_err_t sample_microphone(const char *phase)
{
    if (s_microphone == nullptr || s_microphone_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_microphone_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const int result = esp_codec_dev_read(s_microphone, s_microphone_samples, sizeof(s_microphone_samples));
    xSemaphoreGive(s_microphone_mutex);
    const esp_err_t error = result == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;

    uint32_t absolute_sum = 0;
    uint32_t peak = 0;
    if (error == ESP_OK) {
        for (int16_t raw : s_microphone_samples) {
            const uint32_t value = raw < 0 ? static_cast<uint32_t>(-static_cast<int32_t>(raw))
                                           : static_cast<uint32_t>(raw);
            absolute_sum += value;
            if (value > peak) {
                peak = value;
            }
        }
    }
    const uint32_t mean = absolute_sum / (sizeof(s_microphone_samples) / sizeof(s_microphone_samples[0]));
    update_snapshot([error, mean, peak](qsee::DeviceSnapshot &snapshot) {
        set_component(snapshot.audio.microphone, error == ESP_OK, error);
        snapshot.audio.microphone_mean_level = mean;
        snapshot.audio.microphone_peak_level = peak;
    });
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "Microphones ES7210 %s: mean=%lu peak=%lu",
                 phase, static_cast<unsigned long>(mean), static_cast<unsigned long>(peak));
    } else {
        ESP_LOGW(TAG, "Microphones ES7210: %s capture failed (%d)", phase, result);
    }
    return error;
}

void microphone_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(2500));
    s_microphone = bsp_audio_codec_microphone_init();
    if (s_microphone == nullptr) {
        update_snapshot([](qsee::DeviceSnapshot &snapshot) {
            set_component(snapshot.audio.microphone, false, ESP_ERR_NOT_FOUND);
        });
        ESP_LOGW(TAG, "Microphones ES7210: codec initialization failed");
        vTaskDelete(nullptr);
        return;
    }

    esp_codec_dev_sample_info_t sample = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = 22050,
        .mclk_multiple = 0,
    };
    const int result = esp_codec_dev_open(s_microphone, &sample);
    if (result != ESP_CODEC_DEV_OK) {
        update_snapshot([](qsee::DeviceSnapshot &snapshot) {
            set_component(snapshot.audio.microphone, false, ESP_FAIL);
        });
        ESP_LOGW(TAG, "Microphones ES7210: stream open failed (%d)", result);
        vTaskDelete(nullptr);
        return;
    }
    (void)esp_codec_dev_set_in_gain(s_microphone, 24.0F);
    ESP_LOGI(TAG, "Microphones ES7210: ADC path enabled at 22.05kHz mono PCM16");
    (void)sample_microphone("initial");
    vTaskDelete(nullptr);
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
        "Memory: heap=%luB min=%luB internal=%luB largest=%luB PSRAM=%luB largest=%luB",
        static_cast<unsigned long>(snapshot.memory.heap_free_bytes),
        static_cast<unsigned long>(snapshot.memory.heap_minimum_free_bytes),
        static_cast<unsigned long>(snapshot.memory.internal_free_bytes),
        static_cast<unsigned long>(snapshot.memory.internal_largest_block_bytes),
        static_cast<unsigned long>(snapshot.memory.psram_free_bytes),
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
}

void sample_system_state()
{
    qsee::MemorySnapshot memory;
    memory.heap_free_bytes = esp_get_free_heap_size();
    memory.heap_minimum_free_bytes = esp_get_minimum_free_heap_size();
    memory.internal_free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    memory.internal_largest_block_bytes =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    memory.psram_free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
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
    s_microphone_mutex = xSemaphoreCreateMutexStatic(&s_microphone_mutex_control);
    if (s_microphone_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == nullptr) {
        ESP_LOGE(TAG, "Shared BSP I2C bus unavailable");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Shared BSP I2C: SDA=%d SCL=%d", BSP_I2C_SDA, BSP_I2C_SCL);

    const esp_err_t pmu = init_pmu(bus);
    const esp_err_t rtc = init_rtc(bus);
    const esp_err_t imu = init_imu(bus);
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
    if (s_microphone != nullptr && sample_microphone(phase) != ESP_OK) {
        aggregate = ESP_FAIL;
    }
    sample_system_state();

    update_snapshot([phase](DeviceSnapshot &snapshot) {
        ++snapshot.sequence;
        snapshot.refreshed_at_us = esp_timer_get_time();
        strlcpy(snapshot.reason, phase, sizeof(snapshot.reason));
    });
    log_summary(snapshot());
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
    uint64_t capacity_bytes,
    bool logger_mirror,
    esp_err_t error
)
{
    update_snapshot([=](DeviceSnapshot &snapshot) {
        set_component(snapshot.storage, mounted, error);
        snapshot.storage.capacity_bytes = capacity_bytes;
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

void DeviceSnapshotService::reportWifi(bool ready, esp_err_t error)
{
    update_snapshot([=](DeviceSnapshot &snapshot) { set_component(snapshot.radios.wifi, ready, error); });
}

void DeviceSnapshotService::reportBle(bool ready, esp_err_t error)
{
    update_snapshot([=](DeviceSnapshot &snapshot) { set_component(snapshot.radios.ble, ready, error); });
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

extern "C" void qsee_hardware_report_speaker(bool ready, esp_err_t error)
{
    qsee::DeviceSnapshotService::instance().reportSpeaker(ready, error);
}
