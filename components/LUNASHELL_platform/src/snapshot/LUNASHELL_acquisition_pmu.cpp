// PMU acquisition owns its handles, interrupt decoding and PMU serialization.
// Canonical publication and refresh staging remain owned by DeviceSnapshotService.
#include "LUNASHELL_acquisition_pmu.hpp"
#include "LUNASHELL_acquisition_internal.hpp"
#include "LUNASHELL_acquisition_i2c.hpp"
#include "LUNASHELL_snapshot_internal.hpp"
#include "LUNASHELL_platform.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "esp_bit_defs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
#endif

#define AXP2101_ADDRESS 0x34

namespace {
using LUNASHELL::snapshot_detail::update_snapshot;
using LUNASHELL::snapshot_detail::set_component;
using LUNASHELL::snapshot_detail::mark_sample_success;
using LUNASHELL::snapshot_detail::mark_sample_failure;
using LUNASHELL::snapshot_detail::record_first_error;
using LUNASHELL::snapshot_detail::sample_publication_result;
using LUNASHELL::snapshot_detail::high_frequency_sample_phase;
using LUNASHELL::snapshot_detail::acquisition_i2c::add_i2c_device;
using LUNASHELL::snapshot_detail::acquisition_i2c::read_register;
using LUNASHELL::snapshot_detail::acquisition_i2c::write_register;
using LUNASHELL::snapshot_detail::acquisition_i2c::write_registers;
constexpr const char *TAG = "LUNASHELL_pmu";
StaticSemaphore_t s_pmu_mutex_control;
SemaphoreHandle_t s_pmu_mutex;
bool s_pmu_ready;
#if CONFIG_LUNASHELL_BUILD_ENABLE_BATTERY
i2c_master_bus_handle_t s_i2c_bus;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
i2c_master_dev_handle_t s_pmu_device;
XPowersPMU s_power;
std::atomic<uint32_t> s_pkey_short_press_pending{0};
std::atomic<uint32_t> s_pkey_long_press_pending{0};
uint64_t s_pmu_queued_pkey_status;
LUNASHELL_power_key_sample_t s_pkey_sample{};
constexpr uint64_t kPmuIrqReadFailure = 0xFFFFFFULL;
constexpr uint64_t kPmuIrqMask = 0xFFFFFFULL;
constexpr uint64_t kPmuRuntimeIrqMask =
#if CONFIG_LUNASHELL_BUILD_ENABLE_BATTERY
    XPOWERS_AXP2101_BAT_INSERT_IRQ |
    XPOWERS_AXP2101_BAT_REMOVE_IRQ |
    XPOWERS_AXP2101_VBUS_INSERT_IRQ |
    XPOWERS_AXP2101_VBUS_REMOVE_IRQ |
#endif
    XPOWERS_AXP2101_PKEY_SHORT_IRQ |
    XPOWERS_AXP2101_PKEY_LONG_IRQ |
    XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ |
    XPOWERS_AXP2101_PKEY_POSITIVE_IRQ
#if CONFIG_LUNASHELL_BUILD_ENABLE_BATTERY
    |
    XPOWERS_AXP2101_BAT_CHG_DONE_IRQ |
    XPOWERS_AXP2101_BAT_CHG_START_IRQ
#endif
    ;
#endif

// Exception: Returns null if the PMU's static mutex cannot be initialized.
SemaphoreHandle_t ensure_pmu_mutex()
{
    if (s_pmu_mutex == nullptr) {
        s_pmu_mutex = xSemaphoreCreateMutexStatic(&s_pmu_mutex_control);
    }
    return s_pmu_mutex;
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
// device_address: Address supplied by XPowers, required to match the PMU.
// reg: First register requested by the library.
// data: Library-owned bounded destination.
// length: Number of requested register bytes.
// Exception: Reports library failure after logging the real I2C cause.
int xpowers_read_register(
    uint8_t device_address,
    uint8_t reg,
    uint8_t *data,
    uint8_t length
)
{
    if (device_address != AXP2101_ADDRESS ||
        s_pmu_device == nullptr || data == nullptr || length == 0U) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101 callback read rejected: address=0x%02x register=0x%02x length=%u",
            device_address,
            reg,
            static_cast<unsigned>(length)
        );
        return -1;
    }
    const esp_err_t error =
        read_register(s_pmu_device, reg, data, length);
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101 callback read failed: register=0x%02x length=%u (%s)",
            reg,
            static_cast<unsigned>(length),
            esp_err_to_name(error)
        );
        return -1;
    }
    return 0;
}

// device_address: Address supplied by XPowers, required to match the PMU.
// reg: First register supplied by the library.
// data: Library-owned bytes borrowed for the synchronous write.
// length: Number of requested register bytes.
// Exception: Reports library failure after logging the real I2C cause.
int xpowers_write_register(
    uint8_t device_address,
    uint8_t reg,
    uint8_t *data,
    uint8_t length
)
{
    if (device_address != AXP2101_ADDRESS ||
        s_pmu_device == nullptr || data == nullptr || length == 0U) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101 callback write rejected: address=0x%02x register=0x%02x length=%u",
            device_address,
            reg,
            static_cast<unsigned>(length)
        );
        return -1;
    }
    const esp_err_t error =
        write_registers(s_pmu_device, reg, data, length);
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101 callback write failed: register=0x%02x length=%u (%s)",
            reg,
            static_cast<unsigned>(length),
            esp_err_to_name(error)
        );
        return -1;
    }
    return 0;
}

// reg: First PMU register to read.
// data: Caller-owned bounded destination.
// length: Register count representable by the driver.
// Exception: Returns invalid argument, unavailable device or original I2C failure.
esp_err_t read_pmu_registers(
    uint8_t reg,
    uint8_t *data,
    size_t length
)
{
    if (data == nullptr || length == 0 || length > UINT8_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_pmu_device == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return read_register(s_pmu_device, reg, data, length);
}

// reg: Documented configuration register, never an action or IRQ register.
// mask: Bits owned by this operation.
// value: Requested values for the owned bits.
// Exception: Returns original I2C errors or a failed configuration readback.
esp_err_t update_and_verify_power_configuration(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t previous = 0;
    esp_err_t error = read_pmu_registers(reg, &previous, 1);
    if (error == ESP_OK) {
        const uint8_t requested = static_cast<uint8_t>((previous & ~mask) | (value & mask));
        if (requested != previous) {
            error = write_register(s_pmu_device, reg, requested);
        }
    }
    uint8_t observed = 0;
    if (error == ESP_OK) {
        error = read_pmu_registers(reg, &observed, 1);
    }
    if (error == ESP_OK && (observed & mask) != (value & mask)) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Power configuration failed: register=0x%02x mask=0x%02x expected=0x%02x observed=0x%02x (%s)",
                 reg, mask, value, observed, esp_err_to_name(error));
    }
    return error;
}

// timing: Successful REG27 readback, not a physical button-duration measurement.
uint16_t power_on_hold_ms(uint8_t timing)
{
    constexpr uint16_t durations[] = {128, 512, 1000, 2000};
    return durations[timing & 3U];
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_BATTERY
// Exception: Returns the original register error or detector readback mismatch.
esp_err_t enable_and_verify_pmu_battery_detection()
{
    uint8_t value = 0;
    esp_err_t error = read_pmu_registers(
        XPOWERS_AXP2101_BAT_DET_CTRL,
        &value,
        1
    );
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: battery detection control read failed at register=0x%02x (%s)",
            XPOWERS_AXP2101_BAT_DET_CTRL,
            esp_err_to_name(error)
        );
        return error;
    }
    if ((value & BIT(0)) == 0U) {
        error = write_register(
            s_pmu_device,
            XPOWERS_AXP2101_BAT_DET_CTRL,
            static_cast<uint8_t>(value | BIT(0))
        );
        if (error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "PMU AXP2101: battery detection enable failed at register=0x%02x (%s)",
                XPOWERS_AXP2101_BAT_DET_CTRL,
                esp_err_to_name(error)
            );
            return error;
        }
    }

    uint8_t readback = 0;
    error = read_pmu_registers(
        XPOWERS_AXP2101_BAT_DET_CTRL,
        &readback,
        1
    );
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: battery detection enable readback failed at register=0x%02x (%s)",
            XPOWERS_AXP2101_BAT_DET_CTRL,
            esp_err_to_name(error)
        );
        return error;
    }
    if ((readback & BIT(0)) == 0U) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: battery detection enable readback mismatch at register=0x%02x value=0x%02x (%s)",
            XPOWERS_AXP2101_BAT_DET_CTRL,
            readback,
            esp_err_to_name(ESP_ERR_INVALID_RESPONSE)
        );
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
#endif

// operation: Original IRQ acquisition context for failure logging.
// status: Receives coherent status only after a successful burst read.
// Exception: Returns the original read error or invalid all-ones response.
esp_err_t read_pmu_irq_status(const char *operation, uint64_t *status)
{
    if (status == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t status_bytes[3] = {};
    // INTSTS1..3 are contiguous; one burst keeps the status coherent and avoids
    // three polling transactions on the shared bus every 100 ms.
    const esp_err_t error = read_pmu_registers(
        XPOWERS_AXP2101_INTSTS1,
        status_bytes,
        sizeof(status_bytes)
    );
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: %s failed at registers=0x%02x..0x%02x (%s)",
            operation,
            XPOWERS_AXP2101_INTSTS1,
            XPOWERS_AXP2101_INTSTS3,
            esp_err_to_name(error)
        );
        return error;
    }
    const uint64_t readback =
        static_cast<uint64_t>(status_bytes[0]) |
        (static_cast<uint64_t>(status_bytes[1]) << 8U) |
        (static_cast<uint64_t>(status_bytes[2]) << 16U);
    if (readback == kPmuIrqReadFailure) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: %s failed (raw=0x%06llx)",
            operation,
            static_cast<unsigned long long>(readback)
        );
        return ESP_FAIL;
    }
    *status = readback;
    return ESP_OK;
}

// expected: Exact enabled IRQ mask selected for this build.
// Exception: Returns the original read error or a real readback mismatch.
esp_err_t verify_pmu_irq_enable_mask(uint64_t expected)
{
    uint8_t enabled_bytes[3] = {};
    const esp_err_t error = read_pmu_registers(
        XPOWERS_AXP2101_INTEN1,
        enabled_bytes,
        sizeof(enabled_bytes)
    );
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: runtime IRQ enable readback failed (%s)",
            esp_err_to_name(error)
        );
        return error;
    }
    const uint64_t observed =
        static_cast<uint64_t>(enabled_bytes[0]) |
        (static_cast<uint64_t>(enabled_bytes[1]) << 8U) |
        (static_cast<uint64_t>(enabled_bytes[2]) << 16U);
    if (observed != expected) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: runtime IRQ mask mismatch expected=0x%06llx observed=0x%06llx",
            static_cast<unsigned long long>(expected),
            static_cast<unsigned long long>(observed)
        );
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

// mutex: PMU serialization lock held by the caller.
// operation: Original operation for ownership invariant logging.
// Exception: Stops execution if releasing the owned mutex is rejected.
esp_err_t give_pmu_mutex(
    SemaphoreHandle_t mutex,
    const char *operation
)
{
    if (mutex == nullptr || xSemaphoreGive(mutex) != pdTRUE) {
        ESP_LOGE(
            TAG,
            "%s failed: PMU mutex release rejected",
            operation
        );
        std::abort();
    }
    return ESP_OK;
}

// operation: Original IRQ clear context for failure logging.
// clear_mask: Exact pending IRQ bits to clear.
// remaining_status: Optional receiver for verified remaining pending bits.
// Exception: Returns the original I2C error or failed clear verification.
esp_err_t clear_and_verify_pmu_irq_status(
    const char *operation,
    uint64_t clear_mask = kPmuIrqMask,
    uint64_t *remaining_status = nullptr
)
{
    for (size_t index = 0; index < 3U; ++index) {
        const uint8_t value = static_cast<uint8_t>(
            (clear_mask >> (index * 8U)) & 0xFFU
        );
        if (value == 0U) {
            continue;
        }
        const uint8_t reg = static_cast<uint8_t>(
            XPOWERS_AXP2101_INTSTS1 + index
        );
        const esp_err_t write_error =
            write_register(s_pmu_device, reg, value);
        if (write_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "PMU AXP2101: %s write failed at register=0x%02x mask=0x%02x (%s)",
                operation,
                reg,
                value,
                esp_err_to_name(write_error)
            );
            return write_error;
        }
    }

    uint64_t readback = 0;
    const esp_err_t read_error = read_pmu_irq_status(operation, &readback);
    if (read_error != ESP_OK) {
        return read_error;
    }
    if (remaining_status != nullptr) {
        *remaining_status = readback;
    }
    if ((readback & clear_mask) != 0U) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: %s verification failed: clear=0x%06llx observed=0x%06llx",
            operation,
            static_cast<unsigned long long>(clear_mask),
            static_cast<unsigned long long>(readback)
        );
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

// Exception: Returns the original IRQ read, clear or verification failure.
esp_err_t collect_pmu_irq_events()
{
    uint64_t irq_status = 0;
    esp_err_t error =
        read_pmu_irq_status("read runtime IRQ status", &irq_status);
    if (error != ESP_OK) {
        return error;
    }

    if (irq_status == 0U) {
        // A previous clear may have succeeded even if its verification read did
        // not; retain queued button counts but release their de-duplication mask.
        s_pmu_queued_pkey_status = 0;
        return ESP_OK;
    }

    const uint64_t pkey_status =
        irq_status &
        (XPOWERS_AXP2101_PKEY_SHORT_IRQ |
         XPOWERS_AXP2101_PKEY_LONG_IRQ |
         XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ |
         XPOWERS_AXP2101_PKEY_POSITIVE_IRQ);
    const uint64_t new_pkey_status =
        pkey_status & ~s_pmu_queued_pkey_status;
    if ((new_pkey_status & XPOWERS_AXP2101_PKEY_SHORT_IRQ) != 0U) {
        s_pkey_short_press_pending.fetch_add(1, std::memory_order_release);
    }
    if ((new_pkey_status & XPOWERS_AXP2101_PKEY_LONG_IRQ) != 0U) {
        s_pkey_long_press_pending.fetch_add(1, std::memory_order_release);
    }
    // PWRON is pulled high internally and the physical key connects it to GND.
    // Both accumulated edges have unknown order. Invalidate that interval
    // instead of publishing a fabricated pressed/released level.
    const bool released = (new_pkey_status & XPOWERS_AXP2101_PKEY_POSITIVE_IRQ) != 0U;
    const bool pressed = (new_pkey_status & XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ) != 0U;
    if (released && pressed) {
        s_pkey_sample = {};
    } else if (released) {
        s_pkey_sample = {};
        s_pkey_sample.state_known = true;
    } else if (pressed) {
        s_pkey_sample.state_known = true;
        s_pkey_sample.pressed = true;
        s_pkey_sample.pressed_at_us = esp_timer_get_time();
    }
    // Mark decoded PKEY bits before clearing so a failed clear can be retried
    // without duplicating the already queued physical event.
    s_pmu_queued_pkey_status |= pkey_status;

    ESP_LOGI(
        TAG,
        "PMU AXP2101 events: raw=0x%llx bat+%d bat-%d usb+%d usb-%d key-short=%d key-long=%d charge-start=%d charge-done=%d",
        static_cast<unsigned long long>(irq_status),
        (irq_status & XPOWERS_AXP2101_BAT_INSERT_IRQ) != 0,
        (irq_status & XPOWERS_AXP2101_BAT_REMOVE_IRQ) != 0,
        (irq_status & XPOWERS_AXP2101_VBUS_INSERT_IRQ) != 0,
        (irq_status & XPOWERS_AXP2101_VBUS_REMOVE_IRQ) != 0,
        (irq_status & XPOWERS_AXP2101_PKEY_SHORT_IRQ) != 0,
        (irq_status & XPOWERS_AXP2101_PKEY_LONG_IRQ) != 0,
        (irq_status & XPOWERS_AXP2101_BAT_CHG_START_IRQ) != 0,
        (irq_status & XPOWERS_AXP2101_BAT_CHG_DONE_IRQ) != 0
    );

    uint64_t remaining_status = 0;
    error = clear_and_verify_pmu_irq_status(
        "clear runtime IRQ status",
        irq_status,
        &remaining_status
    );
    if (error != ESP_OK) {
        return error;
    }
    s_pmu_queued_pkey_status &= remaining_status;
    return ESP_OK;
}

// expected: Exact configured precharge current.
// Exception: Returns invalid response or the library read failure.
esp_err_t verify_pmu_precharge_current(xpowers_prechg_t expected)
{
    const xpowers_prechg_t observed = s_power.getPrechargeCurr();
    if (observed == expected) {
        return ESP_OK;
    }
    const unsigned observed_value = static_cast<unsigned>(observed);
    const esp_err_t error =
        observed_value == 0x0FU
            ? ESP_FAIL
            : ESP_ERR_INVALID_RESPONSE;
    ESP_LOGE(
        TAG,
        "PMU AXP2101: precharge-current verification failed: expected=%u observed=%u (%s)",
        static_cast<unsigned>(expected),
        observed_value,
        esp_err_to_name(error)
    );
    return error;
}

// expected: Exact configured charge termination current.
// Exception: Returns invalid response or the library read failure.
esp_err_t verify_pmu_termination_current(
    xpowers_axp2101_chg_iterm_t expected
)
{
    const xpowers_axp2101_chg_iterm_t observed =
        s_power.getChargerTerminationCurr();
    if (observed == expected) {
        return ESP_OK;
    }
    const unsigned observed_value = static_cast<unsigned>(observed);
    const esp_err_t error =
        observed_value == 0x0FU
            ? ESP_FAIL
            : ESP_ERR_INVALID_RESPONSE;
    ESP_LOGE(
        TAG,
        "PMU AXP2101: termination-current verification failed: expected=%u observed=%u (%s)",
        static_cast<unsigned>(expected),
        observed_value,
        esp_err_to_name(error)
    );
    return error;
}
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_BATTERY
// phase: Original sampling context used in publication and logs.
// Exception: Returns the original probe, telemetry or publication failure.
esp_err_t sample_pmu_locked(const char *phase)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_BATTERY && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
    LUNASHELL::BatterySnapshot sample;
    const int64_t attempted_at_us = esp_timer_get_time();
    esp_err_t error =
        i2c_master_probe(s_i2c_bus, AXP2101_ADDRESS, LUNASHELL::snapshot_detail::acquisition_i2c::kTimeoutMs);

    if (error != ESP_OK) {
        const esp_err_t publication_error = update_snapshot(
            [error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                snapshot.battery.level_percent_valid = false;
                snapshot.battery.battery_mv_valid = false;
                mark_sample_failure(
                    snapshot.battery,
                    error,
                    attempted_at_us
                );
            }
        );
        ESP_LOGW(TAG, "PMU AXP2101: %s sample failed (%s)", phase, esp_err_to_name(error));
        return sample_publication_result(
            "PMU AXP2101",
            phase,
            error,
            publication_error
        );
    }

    uint8_t status1 = 0;
    uint8_t status2 = 0;
    uint8_t battery_adc[2] = {};
    uint8_t vbus_adc[2] = {};
    uint8_t system_adc[2] = {};
    uint8_t temperature_adc[2] = {};
    uint8_t percent = 0;
    error = read_pmu_registers(XPOWERS_AXP2101_STATUS1, &status1, 1);
    if (error == ESP_OK) {
        error = read_pmu_registers(XPOWERS_AXP2101_STATUS2, &status2, 1);
    }

    uint8_t key_timing = 0;
    if (error == ESP_OK) {
        error = read_pmu_registers(XPOWERS_AXP2101_IRQ_OFF_ON_LEVEL_CTRL, &key_timing, 1);
    }
    if (error == ESP_OK) {
        sample.power_key_config_valid = true;
        sample.power_on_hold_ms = power_on_hold_ms(key_timing);
        sample.emergency_power_off_hold_ms = static_cast<uint16_t>(4000U + ((key_timing >> 2U) & 3U) * 2000U);
    }

    sample.vbus_good = (status1 & BIT(5)) != 0;
    sample.batfet_enabled = (status1 & BIT(4)) != 0;
    sample.battery_present = (status1 & BIT(3)) != 0;
    sample.battery_active_mode = (status1 & BIT(2)) != 0;
    sample.thermal_regulation_active = (status1 & BIT(1)) != 0;
    sample.input_current_limit_active = (status1 & BIT(0)) != 0;
    sample.usb_present =
        sample.vbus_good && (status2 & BIT(3)) == 0;

    // BAT presence comes from the dedicated detector. Do not touch the cell
    // ADC or percentage registers when no battery is connected: their bytes
    // are not current telemetry and the extra I2C transactions are wasteful.
    if (error == ESP_OK && sample.battery_present) {
        error = read_pmu_registers(
            XPOWERS_AXP2101_ADC_DATA_RELUST0,
            battery_adc,
            sizeof(battery_adc)
        );
    }
    if (error == ESP_OK && sample.battery_present) {
        error = read_pmu_registers(
            XPOWERS_AXP2101_BAT_PERCENT_DATA,
            &percent,
            1
        );
    }
    if (error == ESP_OK) {
        error = read_pmu_registers(
            XPOWERS_AXP2101_ADC_DATA_RELUST4,
            vbus_adc,
            sizeof(vbus_adc)
        );
    }
    if (error == ESP_OK) {
        error = read_pmu_registers(
            XPOWERS_AXP2101_ADC_DATA_RELUST6,
            system_adc,
            sizeof(system_adc)
        );
    }
    if (error == ESP_OK) {
        error = read_pmu_registers(
            XPOWERS_AXP2101_ADC_DATA_RELUST8,
            temperature_adc,
            sizeof(temperature_adc)
        );
    }

    const uint16_t battery_mv = static_cast<uint16_t>(
        ((battery_adc[0] & 0x1FU) << 8U) | battery_adc[1]
    );
    const uint16_t usb_mv = static_cast<uint16_t>(
        ((vbus_adc[0] & 0x3FU) << 8U) | vbus_adc[1]
    );
    const uint16_t system_mv = static_cast<uint16_t>(
        ((system_adc[0] & 0x3FU) << 8U) | system_adc[1]
    );
    const uint16_t temperature_raw = static_cast<uint16_t>(
        ((temperature_adc[0] & 0x3FU) << 8U) | temperature_adc[1]
    );
    const uint8_t charge_state = status2 >> 5U;
    const uint8_t charger_phase = status2 & 0x07U;
    if (error == ESP_OK && charge_state > 2U) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: reserved charge state %u in STATUS2=0x%02x",
            static_cast<unsigned>(charge_state),
            status2
        );
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK && charger_phase > 5U) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101: reserved charger phase %u in STATUS2=0x%02x",
            static_cast<unsigned>(charger_phase),
            status2
        );
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK &&
        ((sample.battery_present &&
          (percent > 100U || battery_mv == 0U)) ||
         (sample.usb_present && usb_mv == 0U) ||
         system_mv == 0U ||
         temperature_raw > 16383U)) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK && sample.battery_present) {
        sample.level_percent_valid = true;
        sample.battery_mv_valid = true;
        sample.level_percent = static_cast<int>(percent);
        sample.battery_mv = battery_mv;
    }
    sample.usb_mv = usb_mv;
    sample.system_mv = system_mv;
    sample.die_temperature_c =
        XPOWERS_AXP2101_CONVERSION(temperature_raw);
    if (error == ESP_OK && !std::isfinite(sample.die_temperature_c)) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error != ESP_OK) {
        const esp_err_t publication_error = update_snapshot(
            [error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
                snapshot.battery.level_percent_valid = false;
                snapshot.battery.battery_mv_valid = false;
                mark_sample_failure(
                    snapshot.battery,
                    error,
                    attempted_at_us
                );
            }
        );
        ESP_LOGW(
            TAG,
            "PMU AXP2101: %s returned invalid telemetry (%s)",
            phase,
            esp_err_to_name(error)
        );
        return sample_publication_result(
            "PMU AXP2101",
            phase,
            error,
            publication_error
        );
    }

    const char *power_state =
        charge_state == 1U
            ? "charging"
            : (charge_state == 2U ? "discharging" : "standby");
    static constexpr std::array<const char *, 6> kChargerPhases = {
        "trickle",
        "precharge",
        "constant-current",
        "constant-voltage",
        "done",
        "stopped",
    };
    strlcpy(sample.power_state, power_state, sizeof(sample.power_state));
    strlcpy(
        sample.charger_phase,
        kChargerPhases[charger_phase],
        sizeof(sample.charger_phase)
    );

    mark_sample_success(sample, attempted_at_us);
    const esp_err_t publication_error = update_snapshot(
        [&sample](LUNASHELL::DeviceSnapshot &snapshot) {
            snapshot.battery = sample;
        }
    );

    const esp_log_level_t success_log_level =
        high_frequency_sample_phase(phase) ? ESP_LOG_DEBUG : ESP_LOG_INFO;
    if (sample.level_percent_valid && sample.battery_mv_valid) {
        ESP_LOG_LEVEL_LOCAL(
            success_log_level,
            TAG,
            "PMU AXP2101 %s: battery=present cell=%d%% %dmV, state=%s phase=%s, USB=%s good=%d %dmV, system=%dmV, die=%.1fC, BATFET=%d active=%d thermal=%d input-limit=%d",
            phase,
            sample.level_percent,
            sample.battery_mv,
            sample.power_state,
            sample.charger_phase,
            sample.usb_present ? "present" : "absent",
            sample.vbus_good,
            sample.usb_mv,
            sample.system_mv,
            sample.die_temperature_c,
            sample.batfet_enabled,
            sample.battery_active_mode,
            sample.thermal_regulation_active,
            sample.input_current_limit_active
        );
    } else {
        ESP_LOG_LEVEL_LOCAL(
            success_log_level,
            TAG,
            "PMU AXP2101 %s: battery=absent cell=UNAVAILABLE, state=%s phase=%s, USB=%s good=%d %dmV, system=%dmV, die=%.1fC, BATFET=%d active=%d thermal=%d input-limit=%d",
            phase,
            sample.power_state,
            sample.charger_phase,
            sample.usb_present ? "present" : "absent",
            sample.vbus_good,
            sample.usb_mv,
            sample.system_mv,
            sample.die_temperature_c,
            sample.batfet_enabled,
            sample.battery_active_mode,
            sample.thermal_regulation_active,
            sample.input_current_limit_active
        );
    }
    return sample_publication_result(
        "PMU AXP2101",
        phase,
        ESP_OK,
        publication_error
    );
#else
    const esp_err_t error = ESP_ERR_NOT_SUPPORTED;
    const esp_err_t publication_error = update_snapshot(
        [error](LUNASHELL::DeviceSnapshot &snapshot) {
            set_component(snapshot.battery, false, error);
        }
    );
    return sample_publication_result(
        "PMU",
        phase,
        error,
        publication_error
    );
#endif
}

// phase: Original sampling context that could not acquire the lock.
// error: Original PMU ownership failure to publish.
// Exception: Returns the lock failure unless canonical publication itself fails.
esp_err_t publish_pmu_lock_failure(
    const char *phase,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    const esp_err_t publication_error = update_snapshot(
        [error, attempted_at_us](LUNASHELL::DeviceSnapshot &snapshot) {
            snapshot.battery.level_percent_valid = false;
            snapshot.battery.battery_mv_valid = false;
            mark_sample_failure(
                snapshot.battery,
                error,
                attempted_at_us
            );
        }
    );
    ESP_LOGW(
        TAG,
        "PMU AXP2101: %s sample lock failed (%s)",
        phase,
        esp_err_to_name(error)
    );
    return sample_publication_result(
        "PMU AXP2101",
        phase,
        error,
        publication_error
    );
}

// phase: Original sampling context used in publication and logs.
// Exception: Returns the original lock, sample or publication failure.
esp_err_t sample_pmu(const char *phase)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_BATTERY && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
    SemaphoreHandle_t pmu_mutex = ensure_pmu_mutex();
    if (pmu_mutex == nullptr) {
        return publish_pmu_lock_failure(phase, ESP_ERR_NO_MEM);
    }
    if (xSemaphoreTake(pmu_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return publish_pmu_lock_failure(phase, ESP_ERR_TIMEOUT);
    }

    esp_err_t result = sample_pmu_locked(phase);
    const esp_err_t release_error =
        give_pmu_mutex(pmu_mutex, "PMU telemetry sample");
    record_first_error(release_error, &result);
    return result;
#else
    return sample_pmu_locked(phase);
#endif
}
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
// Exception: Returns the original I2C device removal error.
esp_err_t remove_pmu_device()
{
    if (s_pmu_device == nullptr) {
        return ESP_OK;
    }
    const esp_err_t error =
        i2c_master_bus_rm_device(s_pmu_device);
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "PMU AXP2101 device cleanup failed (%s)",
            esp_err_to_name(error)
        );
        return error;
    }
    s_pmu_device = nullptr;
    return ESP_OK;
}
#endif

}  // namespace

namespace LUNASHELL::acquisition_pmu {
// bus: Existing BSP-owned I2C bus borrowed by the PMU acquisition component.
// Exception: Returns the original PMU initialization or canonical publication error.
esp_err_t initialize(i2c_master_bus_handle_t bus)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_BATTERY
    s_i2c_bus = bus;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
    esp_err_t error = i2c_master_probe(bus, AXP2101_ADDRESS, LUNASHELL::snapshot_detail::acquisition_i2c::kTimeoutMs);
    if (error == ESP_OK) {
        error = add_i2c_device(
            bus,
            AXP2101_ADDRESS,
            XPOWERSLIB_I2C_MASTER_SPEED,
            &s_pmu_device
        );
    }
    if (error == ESP_OK &&
        !s_power.begin(
            AXP2101_ADDRESS,
            xpowers_read_register,
            xpowers_write_register
        )) {
        error = ESP_FAIL;
    }
    if (error != ESP_OK) {
        record_first_error(remove_pmu_device(), &error);
        update_snapshot([error](LUNASHELL::DeviceSnapshot &snapshot) {
            set_component(snapshot.battery, false, error);
        });
        ESP_LOGW(TAG, "PMU AXP2101: not detected (%s)", esp_err_to_name(error));
        return error;
    }

    // applied: Exact outcome reported by the XPowers write.
    // operation: Original register configuration context for failure logging.
    const auto require_pmu_write = [](bool applied, const char *operation) {
        if (!applied) {
            ESP_LOGE(TAG, "PMU AXP2101: %s failed", operation);
        }
        return applied;
    };
#if CONFIG_LUNASHELL_BUILD_ENABLE_BATTERY
    if (error == ESP_OK) {
        error = enable_and_verify_pmu_battery_detection();
    }
    if (error == ESP_OK &&
        (!require_pmu_write(
            s_power.enableVbusVoltageMeasure(),
            "enable VBUS voltage measurement"
        ) ||
        !require_pmu_write(
            s_power.enableBattVoltageMeasure(),
            "enable battery voltage measurement"
        ) ||
        !require_pmu_write(
            s_power.enableSystemVoltageMeasure(),
            "enable system voltage measurement"
        ) ||
        !require_pmu_write(
            s_power.enableTemperatureMeasure(),
            "enable die temperature measurement"
        ))) {
        error = ESP_FAIL;
    }
#endif
    // The board has no battery NTC on TS; leaving TS measurement enabled can inhibit charging.
    if (error == ESP_OK &&
        !require_pmu_write(
            s_power.disableTSPinMeasure(),
            "disable unused TS measurement"
        )) {
        error = ESP_FAIL;
    }

    if (error == ESP_OK &&
        !require_pmu_write(
            s_power.disableIRQ(XPOWERS_AXP2101_ALL_IRQ),
            "disable existing IRQ mask"
        )) {
        error = ESP_FAIL;
    }
    if (error == ESP_OK) {
        error = clear_and_verify_pmu_irq_status(
            "clear initialization IRQ status"
        );
        if (error == ESP_OK) {
            s_pkey_short_press_pending.store(0, std::memory_order_relaxed);
            s_pkey_long_press_pending.store(0, std::memory_order_relaxed);
            s_pmu_queued_pkey_status = 0;
            s_pkey_sample = {};
        }
    }
    // Give the presentation and owner drains time before emergency hardware off.
    // REG22[0] must select shutdown, not the board's possible restart default.
    if (error == ESP_OK) {
        error = update_and_verify_power_configuration(0x22, 0x03, 0x02);
    }
    if (error == ESP_OK) {
        error = update_and_verify_power_configuration(0x27, 0x0C, 0x0C);
    }
    if (error == ESP_OK) {
        uint8_t key_timing = 0;
        error = read_register(s_pmu_device, XPOWERS_AXP2101_IRQ_OFF_ON_LEVEL_CTRL,
                              &key_timing, sizeof(key_timing));
        if (error == ESP_OK && ((key_timing >> 2) & 3U) != XPOWERS_POWEROFF_10S) {
            ESP_LOGE(TAG, "Emergency PWR shutdown timing verification failed");
            error = ESP_ERR_INVALID_RESPONSE;
        }
        if (error == ESP_OK) {
            ESP_LOGI(TAG, "PKEY configuration readback: power-on hold=%u ms, emergency shutdown=10000 ms; physical timing unmeasured",
                     static_cast<unsigned>(power_on_hold_ms(key_timing)));
        } else {
            ESP_LOGE(TAG, "PKEY timing configuration read failed (%s)", esp_err_to_name(error));
        }
    }
    if (error == ESP_OK &&
        !require_pmu_write(
                s_power.enableIRQ(kPmuRuntimeIrqMask),
                "enable runtime IRQ mask"
            )) {
        error = ESP_FAIL;
    }
    if (error == ESP_OK) {
        error = verify_pmu_irq_enable_mask(kPmuRuntimeIrqMask);
    }
    if (error == ESP_OK) {
        s_power.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
        error = verify_pmu_precharge_current(
            XPOWERS_AXP2101_PRECHARGE_50MA
        );
    }
    if (error == ESP_OK &&
        !require_pmu_write(
            s_power.setChargerConstantCurr(
                XPOWERS_AXP2101_CHG_CUR_400MA
            ),
            "set constant charge current"
        )) {
        error = ESP_FAIL;
    }
    if (error == ESP_OK) {
        s_power.setChargerTerminationCurr(
            XPOWERS_AXP2101_CHG_ITERM_25MA
        );
        error = verify_pmu_termination_current(
            XPOWERS_AXP2101_CHG_ITERM_25MA
        );
    }
    if (error == ESP_OK &&
        !require_pmu_write(
            s_power.setChargeTargetVoltage(
                XPOWERS_AXP2101_CHG_VOL_4V2
            ),
            "set charge target voltage"
        )) {
        error = ESP_FAIL;
    }
    if (error != ESP_OK) {
        record_first_error(remove_pmu_device(), &error);
        update_snapshot([error](LUNASHELL::DeviceSnapshot &snapshot) {
            set_component(snapshot.battery, false, error);
        });
        return error;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_BATTERY
    // ADC channels need one conversion interval before their first coherent sample.
    vTaskDelay(pdMS_TO_TICKS(50));
#endif

    s_pmu_ready = true;
#if CONFIG_LUNASHELL_BUILD_ENABLE_BATTERY
    const esp_err_t publication_error = update_snapshot([](LUNASHELL::DeviceSnapshot &snapshot) {
        set_component(snapshot.battery, true, ESP_OK);
    });
    ESP_LOGI(
        TAG,
        "PMU AXP2101: battery detector verified, ADCs enabled, TS disabled, charge policy=50/400/25mA target=4.20V, events enabled"
    );
#else
    const esp_err_t publication_error = update_snapshot([](LUNASHELL::DeviceSnapshot &snapshot) {
        set_component(snapshot.battery, false, ESP_ERR_NOT_SUPPORTED);
    });
    ESP_LOGI(TAG, "PMU AXP2101: PKEY enabled, TS disabled, charge policy=50/400/25mA target=4.20V; battery telemetry disabled");
#endif
    if (publication_error != ESP_OK) {
        s_pmu_ready = false;
        ESP_LOGE(TAG, "PMU AXP2101: battery state publication failed (%s)",
                 esp_err_to_name(publication_error));
        return publication_error;
    }
    return ESP_OK;
#else
    (void)bus;
    const esp_err_t error = ESP_ERR_NOT_SUPPORTED;
    update_snapshot([error](LUNASHELL::DeviceSnapshot &snapshot) {
        set_component(snapshot.battery, false, error);
    });
    ESP_LOGI(TAG, "PMU: disabled by board hardware selection");
    return error;
#endif
}

// Exception: Reports inability to create the PMU's static mutex before any acquisition.
esp_err_t initialize_synchronization()
{
    return ensure_pmu_mutex() == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
}

// The acquisition orchestrator queries component availability; this is not a sensor read.
bool ready()
{
    return s_pmu_ready;
}

// phase: Original acquisition context used for telemetry publication and logging.
// Exception: Returns the original hardware, locking or publication error.
esp_err_t sample(const char *phase)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_BATTERY
    return sample_pmu(phase);
#else
    (void)phase;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
}  // namespace LUNASHELL::acquisition_pmu

// Exception: Returns original ownership/register errors or ambiguous PMU configuration.
// Success is command delivery only; the running CPU cannot certify its own loss of power.
extern "C" esp_err_t LUNASHELL_hardware_request_power_off(void)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
    if (!LUNASHELL::snapshot_detail::hardware_initialized() || !s_pmu_ready) {
        ESP_LOGE(TAG, "Software power-off rejected: AXP2101 is not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    SemaphoreHandle_t mutex = ensure_pmu_mutex();
    if (mutex == nullptr) {
        ESP_LOGE(TAG, "Software power-off rejected: PMU mutex unavailable");
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        ESP_LOGE(TAG, "Software power-off rejected: PMU lock timeout");
        return ESP_ERR_TIMEOUT;
    }

    uint8_t off_configuration = 0;
    uint8_t common_configuration = 0;
    uint8_t status = 0;
    esp_err_t error = read_pmu_registers(0x22, &off_configuration, 1);
    if (error == ESP_OK && (off_configuration & 0x03U) != 0x02U) {
        ESP_LOGE(TAG, "Software power-off rejected: emergency button configuration=0x%02x is not verified shutdown", off_configuration);
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK) {
        error = read_pmu_registers(0x00, &status, 1);
    }
    if (error == ESP_OK) {
        error = read_pmu_registers(0x10, &common_configuration, 1);
    }
    if (error == ESP_OK && (common_configuration & 0x03U) != 0U) {
        ESP_LOGE(TAG, "Software power-off rejected: reset/off action already pending in REG10=0x%02x", common_configuration);
        error = ESP_ERR_INVALID_STATE;
    }
    if (error == ESP_OK) {
        // VBUS rising, battery recovery and IRQ startup are customized PMU sources,
        // not runtime enable bits in REG20. Never reinterpret source history as a
        // disable mask. Rails/off persistence under USB still needs board evidence.
        ESP_LOGW(TAG, "Software power-off command: VBUS-good=%d; customized automatic startup sources remain hardware-defined",
                 (status & BIT(5)) != 0U);
        error = write_register(s_pmu_device, 0x10, static_cast<uint8_t>(common_configuration | BIT(0)));
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Software power-off command failed (%s)", esp_err_to_name(error));
    }
    const esp_err_t release_error = give_pmu_mutex(mutex, "Software power-off");
    return error != ESP_OK ? error : release_error;
#else
    ESP_LOGE(TAG, "Software power-off rejected: AXP2101 support is disabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// short_presses: Receives queued short IRQs only after successful collection.
// long_presses: Receives queued long IRQs only after successful collection.
// sample: Receives the same acquisition's edge-derived pressed state.
// Exception: Returns invalid arguments, unavailable hardware, locking or original IRQ errors.
extern "C" esp_err_t LUNASHELL_hardware_take_power_key_events(
    uint32_t *short_presses,
    uint32_t *long_presses,
    LUNASHELL_power_key_sample_t *sample
)
{
    if (short_presses == nullptr || long_presses == nullptr || sample == nullptr) {
        ESP_LOGE(TAG, "PKEY event take rejected: output pointer is null");
        return ESP_ERR_INVALID_ARG;
    }
    *short_presses = 0;
    *long_presses = 0;
    *sample = {};

#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS && CONFIG_LUNASHELL_BUILD_PMU_DRIVER_AXP2101
    if (!LUNASHELL::snapshot_detail::hardware_initialized() || !s_pmu_ready) {
        ESP_LOGE(
            TAG,
            "PKEY event take rejected: AXP2101 is not initialized"
        );
        return ESP_ERR_INVALID_STATE;
    }

    SemaphoreHandle_t pmu_mutex = ensure_pmu_mutex();
    if (pmu_mutex == nullptr) {
        ESP_LOGE(TAG, "PKEY event take failed: PMU mutex unavailable");
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(pmu_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        ESP_LOGW(
            TAG,
            "PKEY event take timed out waiting for PMU access"
        );
        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t collect_error = collect_pmu_irq_events();
    if (collect_error == ESP_OK) {
        *short_presses =
            s_pkey_short_press_pending.exchange(0, std::memory_order_acq_rel);
        *long_presses =
            s_pkey_long_press_pending.exchange(0, std::memory_order_acq_rel);
        *sample = s_pkey_sample;
    }
    const esp_err_t release_error = give_pmu_mutex(
        pmu_mutex,
        "PKEY event take"
    );
    if (collect_error != ESP_OK) {
        return collect_error;
    }
    return release_error;
#else
    ESP_LOGE(TAG, "PKEY event take rejected: AXP2101 support is disabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
