/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
 */

#include "qsee_platform.h"
#include "qsee_device_snapshot.hpp"

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

const char *TAG = "qsee_power";
constexpr gpio_num_t kBootButton = GPIO_NUM_0;
constexpr gpio_num_t kPowerButton = GPIO_NUM_41;
constexpr TickType_t kPollTicks = pdMS_TO_TICKS(20);
constexpr TickType_t kDebounceTicks = pdMS_TO_TICKS(30);
constexpr TickType_t kLongPressTicks = pdMS_TO_TICKS(1200);

StaticTask_t s_task_control;
StackType_t s_task_stack[3072];
TaskHandle_t s_task;
qsee::InputSnapshot s_inputs;

struct ButtonState {
    bool raw_pressed = false;
    bool stable_pressed = false;
    TickType_t raw_changed_at = 0;
    TickType_t pressed_at = 0;
};

ButtonState s_boot;
ButtonState s_power;

void set_input_component(
    qsee::ComponentSnapshot &component,
    bool ready,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    component.health = ready
        ? qsee::DeviceHealth::Ready
        : (error == ESP_ERR_NOT_FOUND
               ? qsee::DeviceHealth::Missing
               : qsee::DeviceHealth::Degraded);
    component.last_error = error;
    component.last_attempt_at_us = attempted_at_us;
    if (ready && error == ESP_OK) {
        component.data_valid = true;
        component.data_stale = false;
        component.last_success_at_us = attempted_at_us;
    } else {
        component.data_valid = false;
        component.data_stale = component.last_success_at_us > 0;
    }
}

bool poll_button(
    ButtonState &state,
    bool pressed,
    TickType_t now,
    const char *name,
    uint32_t &short_count,
    uint32_t *long_count
)
{
    bool changed = false;
    if (pressed != state.raw_pressed) {
        state.raw_pressed = pressed;
        state.raw_changed_at = now;
    }
    if (state.raw_pressed != state.stable_pressed &&
        now - state.raw_changed_at >= kDebounceTicks) {
        state.stable_pressed = state.raw_pressed;
        changed = true;
        if (state.stable_pressed) {
            state.pressed_at = now;
            ESP_LOGI(TAG, "%s pressed", name);
        } else {
            const TickType_t duration = now - state.pressed_at;
            if (long_count != nullptr && duration >= kLongPressTicks) {
                ++(*long_count);
                ESP_LOGI(TAG, "%s long press (%lu ms)", name,
                         static_cast<unsigned long>(duration * portTICK_PERIOD_MS));
            } else {
                ++short_count;
                ESP_LOGI(TAG, "%s short press (%lu ms)", name,
                         static_cast<unsigned long>(duration * portTICK_PERIOD_MS));
            }
        }
    }
    return changed;
}

void publish_inputs()
{
    s_inputs.boot_pressed = s_boot.stable_pressed;
    s_inputs.power_pressed = s_power.stable_pressed;
    qsee::DeviceSnapshotService::instance().reportInputs(s_inputs);
}

void input_task(void *arg)
{
    (void)arg;
    publish_inputs();
    while (true) {
        const TickType_t now = xTaskGetTickCount();
        bool changed = poll_button(
            s_boot,
            gpio_get_level(kBootButton) == 0,
            now,
            "BOOT",
            s_inputs.boot_press_count,
            nullptr
        );
        changed |= poll_button(
            s_power,
            gpio_get_level(kPowerButton) == 0,
            now,
            "PWR",
            s_inputs.power_short_press_count,
            &s_inputs.power_long_press_count
        );
        if (changed) {
            publish_inputs();
        }
        vTaskDelay(kPollTicks);
    }
}

esp_err_t configure_wakeup(uint64_t wake_after_us)
{
    esp_err_t result = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_sleep_enable_ext0_wakeup(kBootButton, 0);
    if (result != ESP_OK) {
        return result;
    }
    if (wake_after_us > 0) {
        result = esp_sleep_enable_timer_wakeup(wake_after_us);
    }
    return result;
}

}  // namespace

extern "C" esp_err_t qsee_power_inputs_init(void)
{
    if (s_task != nullptr) {
        return ESP_OK;
    }

    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << kBootButton) | (1ULL << kPowerButton),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t result = gpio_config(&config);
    if (result != ESP_OK) {
        set_input_component(s_inputs.boot_button, false, result);
        set_input_component(s_inputs.power_button, false, result);
        publish_inputs();
        ESP_LOGE(TAG, "Button GPIO configuration failed (%s)", esp_err_to_name(result));
        return result;
    }

#if CONFIG_PM_ENABLE
    const esp_pm_config_t power_policy = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 80,
        .light_sleep_enable = false,
    };
    result = esp_pm_configure(&power_policy);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Dynamic frequency policy failed: %s", esp_err_to_name(result));
        return result;
    } else {
        ESP_LOGI(TAG, "Power policy: dynamic CPU 80-240 MHz; automatic light sleep disabled");
    }
#endif

    s_inputs.wakeup_cause = esp_sleep_get_wakeup_causes();
    s_boot.raw_pressed = gpio_get_level(kBootButton) == 0;
    s_boot.stable_pressed = s_boot.raw_pressed;
    s_power.raw_pressed = gpio_get_level(kPowerButton) == 0;
    s_power.stable_pressed = s_power.raw_pressed;
    set_input_component(s_inputs.boot_button, true, ESP_OK);
    set_input_component(s_inputs.power_button, true, ESP_OK);
    esp_io_expander_handle_t expander = nullptr;
    const esp_err_t expander_error =
        bsp_io_expander_init(&expander);
    set_input_component(
        s_inputs.io_expander,
        expander_error == ESP_OK && expander != nullptr,
        expander_error == ESP_OK && expander == nullptr
            ? ESP_ERR_INVALID_STATE
            : expander_error
    );
    if (expander_error != ESP_OK || expander == nullptr) {
        const esp_err_t effective_error =
            expander_error != ESP_OK
                ? expander_error
                : ESP_ERR_INVALID_STATE;
        publish_inputs();
        ESP_LOGE(
            TAG,
            "Input expander initialization failed (%s)",
            esp_err_to_name(effective_error)
        );
        return effective_error;
    }

    s_task = xTaskCreateStatic(
        input_task,
        "qsee_inputs",
        sizeof(s_task_stack) / sizeof(s_task_stack[0]),
        nullptr,
        3,
        s_task_stack,
        &s_task_control
    );
    if (s_task == nullptr) {
        set_input_component(s_inputs.boot_button, false, ESP_ERR_NO_MEM);
        set_input_component(s_inputs.power_button, false, ESP_ERR_NO_MEM);
        publish_inputs();
        ESP_LOGE(TAG, "Input task creation failed");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(
        TAG,
        "Input inventory: BOOT=GPIO0 PWR=GPIO41 TCA9554=%s wake-causes=0x%08lx",
        expander != nullptr ? "ready" : "missing",
        static_cast<unsigned long>(s_inputs.wakeup_cause)
    );
    return ESP_OK;
}

extern "C" esp_err_t qsee_power_enter_light_sleep(uint64_t wake_after_us)
{
    esp_err_t result = configure_wakeup(wake_after_us);
    if (result != ESP_OK) {
        return result;
    }
    result = qsee_display_set_power_mode(QSEE_DISPLAY_POWER_OFF);
    if (result != ESP_OK) {
        return result;
    }
    ESP_LOGI(TAG, "Entering light sleep: BOOT wake + timer=%llu us",
             static_cast<unsigned long long>(wake_after_us));
    const esp_err_t sleep_result = esp_light_sleep_start();
    s_inputs.wakeup_cause = esp_sleep_get_wakeup_causes();
    publish_inputs();
    const esp_err_t resume_error = qsee_display_set_power_mode(
        QSEE_DISPLAY_POWER_ACTIVE
    );
    ESP_LOGI(TAG, "Light sleep exited: result=%s causes=0x%08lx",
             esp_err_to_name(sleep_result),
             static_cast<unsigned long>(s_inputs.wakeup_cause));
    if (sleep_result != ESP_OK) {
        if (resume_error != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Light sleep failed (%s); display resume also failed (%s)",
                esp_err_to_name(sleep_result),
                esp_err_to_name(resume_error)
            );
        }
        return sleep_result;
    }
    return resume_error;
}

extern "C" esp_err_t qsee_power_enter_deep_sleep(uint64_t wake_after_us)
{
    esp_err_t result = configure_wakeup(wake_after_us);
    if (result != ESP_OK) {
        return result;
    }
    result = qsee_display_set_power_mode(QSEE_DISPLAY_POWER_OFF);
    if (result != ESP_OK) {
        return result;
    }
    ESP_LOGI(TAG, "Entering deep sleep: BOOT wake + timer=%llu us",
             static_cast<unsigned long long>(wake_after_us));
    esp_deep_sleep_start();
    return ESP_OK;
}
