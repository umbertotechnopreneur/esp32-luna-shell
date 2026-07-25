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

// Power-state helpers for charging, sleep, and battery display logic.
// Wraps board and device-level power control for the rest of the app.

#include "qsee_platform.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_settings.hpp"

#include <atomic>

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_lv_adapter.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

const char *TAG = "qsee_power";
constexpr gpio_num_t kBootButton = GPIO_NUM_0;
constexpr gpio_num_t kPowerButton = GPIO_NUM_41;
constexpr TickType_t kPollTicks = pdMS_TO_TICKS(20);
constexpr TickType_t kDisplayPolicyTicks = pdMS_TO_TICKS(50);
constexpr TickType_t kDebounceTicks = pdMS_TO_TICKS(30);
constexpr TickType_t kLongPressTicks = pdMS_TO_TICKS(1200);
constexpr TickType_t kBootArmReleaseTicks = pdMS_TO_TICKS(100);
constexpr char kDimTimeoutSetting[] = "dim_after_s";
constexpr char kPanelOffDelaySetting[] = "off_delay_s";
constexpr uint32_t kDefaultDimTimeoutSeconds = 30;
constexpr uint32_t kDefaultPanelOffDelaySeconds = 60;
constexpr uint32_t kIdleFadeMs = 180;
constexpr uint32_t kSleepFadeOutMs = 180;
constexpr uint32_t kWakeFadeInMs = 220;

StaticTask_t s_task_control;
StackType_t s_task_stack[3072];
TaskHandle_t s_task;
StaticTask_t s_display_task_control;
// LCD command transmission plus logging can exceed the old 2 KB budget.
StackType_t s_display_task_stack[4096];
TaskHandle_t s_display_task;
StaticSemaphore_t s_display_policy_mutex_control;
SemaphoreHandle_t s_display_policy_mutex;
qsee::InputSnapshot s_inputs;
std::atomic_bool s_runtime_ready{false};
std::atomic<uint32_t> s_raw_display_inactive_ms{0};
std::atomic_bool s_display_inactivity_available{false};
std::atomic_bool s_rebase_display_inactivity{false};
std::atomic_bool s_display_wake_requested{false};
std::atomic_bool s_display_input_guard_required{false};
std::atomic_bool s_sleep_transition_active{false};
std::atomic_bool s_adapter_resume_pending{false};
std::atomic<uint32_t> s_dim_timeout_seconds{kDefaultDimTimeoutSeconds};
std::atomic<uint32_t> s_panel_off_delay_seconds{
    kDefaultPanelOffDelaySeconds
};
uint32_t s_display_inactivity_baseline_ms;
bool s_last_woke_by_boot;

enum class DisplayIdleState {
    Active,
    Dimmed,
    Off,
};

std::atomic<DisplayIdleState> s_display_idle_state{DisplayIdleState::Active};
bool s_display_transition_dirty;

struct ButtonState {
    bool raw_pressed = false;
    bool stable_pressed = false;
    TickType_t raw_changed_at = 0;
    TickType_t pressed_at = 0;
};

ButtonState s_boot;
ButtonState s_power;

enum class ButtonEvent {
    None,
    Pressed,
    Released,
};

enum class BootSleepState {
    DisarmedUntilReleased,
    Armed,
    AwaitWakeRelease,
};

BootSleepState s_boot_sleep_state = BootSleepState::DisarmedUntilReleased;
bool s_boot_release_timing;
TickType_t s_boot_released_since;

void set_input_component(
    qsee::ComponentSnapshot &component,
    bool ready,
    esp_err_t error
)
{
    component.health = ready
        ? qsee::DeviceHealth::Ready
        : (error == ESP_ERR_NOT_FOUND
               ? qsee::DeviceHealth::Missing
               : qsee::DeviceHealth::Degraded);
    component.last_error = error;
}

// Debounce one active-low button and count its completed press on release.
ButtonEvent poll_button(
    ButtonState &state,
    bool pressed,
    TickType_t now,
    const char *name,
    uint32_t &short_count,
    uint32_t *long_count,
    bool count_release = true
)
{
    if (pressed != state.raw_pressed) {
        state.raw_pressed = pressed;
        state.raw_changed_at = now;
    }
    if (state.raw_pressed != state.stable_pressed &&
        now - state.raw_changed_at >= kDebounceTicks) {
        state.stable_pressed = state.raw_pressed;
        if (state.stable_pressed) {
            state.pressed_at = now;
            ESP_LOGI(TAG, "%s pressed", name);
            return ButtonEvent::Pressed;
        }
        if (!count_release) {
            ESP_LOGI(TAG, "%s wake press released", name);
            return ButtonEvent::Released;
        }
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
        return ButtonEvent::Released;
    }
    return ButtonEvent::None;
}

// Publish the debounced button state into the canonical device snapshot.
void publish_inputs()
{
    s_inputs.boot_pressed = s_boot.stable_pressed;
    s_inputs.power_pressed = s_power.stable_pressed;
    qsee::DeviceSnapshotService::instance().reportInputs(s_inputs);
}

// Reconcile the physical panel with one logical idle state while locked.
bool apply_display_idle_state_locked(DisplayIdleState target)
{
    const DisplayIdleState previous = s_display_idle_state.load();
    if (target == previous && !s_display_transition_dirty) {
        return true;
    }

    if (target == DisplayIdleState::Off) {
        // Keep an invisible guard over the UI before the panel goes dark.
        s_display_input_guard_required.store(true);
    }

    esp_err_t result = ESP_OK;
    if (target != DisplayIdleState::Off &&
        (previous == DisplayIdleState::Off ||
         s_display_transition_dirty)) {
        result = qsee_display_set_panel_enabled(true);
    }

    if (result == ESP_OK && target == DisplayIdleState::Active) {
        result = qsee_display_fade_in(kWakeFadeInMs);
    } else if (result == ESP_OK && target == DisplayIdleState::Dimmed) {
        uint32_t dimmed = qsee_display_get_brightness_percent() / 2U;
        if (dimmed == 0U) {
            dimmed = 1U;
        }
        result = qsee_display_fade_to_percent(dimmed, kIdleFadeMs);
    } else if (result == ESP_OK) {
        result = qsee_display_fade_out(kIdleFadeMs);
        if (result == ESP_OK) {
            result = qsee_display_set_panel_enabled(false);
        }
    }

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Display idle transition failed (%s)",
            esp_err_to_name(result)
        );
        s_display_transition_dirty = true;
        if (previous == DisplayIdleState::Off ||
            target == DisplayIdleState::Off) {
            s_display_input_guard_required.store(true);
        }
        return false;
    }

    s_display_idle_state.store(target);
    s_display_transition_dirty = false;
    s_display_input_guard_required.store(
        target == DisplayIdleState::Off
    );
    ESP_LOGI(
        TAG,
        "Display idle state: %s (stack-free-min=%lu B)",
        target == DisplayIdleState::Active ? "active" :
        target == DisplayIdleState::Dimmed ? "dimmed-50-percent" :
        "panel-off",
        static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr))
    );
    return true;
}

// Wake the idle display and rebase LVGL's monotonic inactivity counter.
void note_display_activity()
{
    s_rebase_display_inactivity.store(true);
    s_display_wake_requested.store(true);
}

// Retry a deferred LVGL resume, then derive state from current inactivity.
void service_display_idle_policy()
{
    if (!s_runtime_ready.load() ||
        !s_display_inactivity_available.load() ||
        s_display_policy_mutex == nullptr ||
        xSemaphoreTake(s_display_policy_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (s_sleep_transition_active.load()) {
        xSemaphoreGive(s_display_policy_mutex);
        return;
    }
    if (s_adapter_resume_pending.load()) {
        const esp_err_t resume_result = esp_lv_adapter_resume();
        if (resume_result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "LVGL resume retry failed (%s)",
                esp_err_to_name(resume_result)
            );
            xSemaphoreGive(s_display_policy_mutex);
            return;
        }
        s_adapter_resume_pending.store(false);
    }

    const uint32_t raw_inactive_ms =
        s_raw_display_inactive_ms.load();
    uint32_t inactive_ms = 0;
    if (s_rebase_display_inactivity.exchange(false)) {
        s_display_inactivity_baseline_ms = raw_inactive_ms;
    } else if (raw_inactive_ms < s_display_inactivity_baseline_ms) {
        // Touch input resets LVGL's counter, so discard a button-wake baseline.
        s_display_inactivity_baseline_ms = 0;
        inactive_ms = raw_inactive_ms;
    } else {
        inactive_ms =
            raw_inactive_ms - s_display_inactivity_baseline_ms;
    }

    if (s_display_wake_requested.exchange(false)) {
        (void)apply_display_idle_state_locked(DisplayIdleState::Active);
        xSemaphoreGive(s_display_policy_mutex);
        return;
    }

    const uint32_t dim_timeout_seconds =
        s_dim_timeout_seconds.load();
    const uint32_t panel_off_delay_seconds =
        s_panel_off_delay_seconds.load();
    const uint64_t dim_after_ms =
        static_cast<uint64_t>(dim_timeout_seconds) * 1000ULL;
    const uint64_t panel_off_after_ms =
        panel_off_delay_seconds == 0U
            ? 0ULL
            : (static_cast<uint64_t>(dim_timeout_seconds) +
               static_cast<uint64_t>(panel_off_delay_seconds)) * 1000ULL;
    const DisplayIdleState requested =
        panel_off_after_ms > 0ULL &&
        static_cast<uint64_t>(inactive_ms) >= panel_off_after_ms
            ? DisplayIdleState::Off
            : dim_after_ms > 0ULL &&
              static_cast<uint64_t>(inactive_ms) >= dim_after_ms
                ? DisplayIdleState::Dimmed
                : DisplayIdleState::Active;
    (void)apply_display_idle_state_locked(requested);
    xSemaphoreGive(s_display_policy_mutex);
}

// Run fades outside the BOOT polling task so short button taps are not lost.
void display_policy_task(void *arg)
{
    (void)arg;
    while (true) {
        service_display_idle_policy();
        vTaskDelay(kDisplayPolicyTicks);
    }
}

// Arm BOOT sleep only after startup or wake has a stable released level.
void update_boot_arming(bool boot_pressed, TickType_t now)
{
    if (!s_runtime_ready.load() ||
        s_boot_sleep_state != BootSleepState::DisarmedUntilReleased) {
        s_boot_release_timing = false;
        return;
    }
    if (boot_pressed) {
        s_boot_release_timing = false;
        return;
    }
    if (!s_boot_release_timing) {
        s_boot_release_timing = true;
        s_boot_released_since = now;
        return;
    }
    if (now - s_boot_released_since >= kBootArmReleaseTicks) {
        s_boot_sleep_state = BootSleepState::Armed;
        s_boot_release_timing = false;
        ESP_LOGI(TAG, "BOOT light-sleep action armed");
    }
}

// Poll buttons and consume the wake press before accepting another sleep tap.
void input_task(void *arg)
{
    (void)arg;
    publish_inputs();
    while (true) {
        const TickType_t now = xTaskGetTickCount();
        const bool boot_pressed = gpio_get_level(kBootButton) == 0;
        const bool suppress_wake_release =
            s_boot_sleep_state == BootSleepState::AwaitWakeRelease;
        const ButtonEvent boot_event = poll_button(
            s_boot,
            boot_pressed,
            now,
            "BOOT",
            s_inputs.boot_press_count,
            nullptr,
            !suppress_wake_release
        );
        const ButtonEvent power_event = poll_button(
            s_power,
            gpio_get_level(kPowerButton) == 0,
            now,
            "PWR",
            s_inputs.power_short_press_count,
            &s_inputs.power_long_press_count
        );
        if (boot_event != ButtonEvent::None ||
            power_event != ButtonEvent::None) {
            publish_inputs();
        }
        if (boot_event == ButtonEvent::Pressed) {
            const bool waking_panel =
                s_display_input_guard_required.load();
            note_display_activity();
            if (waking_panel) {
                s_boot_sleep_state = BootSleepState::AwaitWakeRelease;
            }
        }
        if (power_event == ButtonEvent::Pressed) {
            note_display_activity();
        }

        if (suppress_wake_release &&
            boot_event == ButtonEvent::Released) {
            // Consume the wake press instead of treating it as a new sleep tap.
            s_boot_sleep_state = BootSleepState::DisarmedUntilReleased;
            s_boot_release_timing = true;
            s_boot_released_since = now;
        } else {
            update_boot_arming(boot_pressed, now);
        }

        if (boot_event == ButtonEvent::Released &&
            s_boot_sleep_state == BootSleepState::Armed) {
            s_boot_sleep_state = BootSleepState::DisarmedUntilReleased;
            s_boot_release_timing = false;
            const esp_err_t sleep_result = qsee_power_enter_light_sleep(0);
            if (sleep_result != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "BOOT light sleep failed (%s)",
                    esp_err_to_name(sleep_result)
                );
            }

            const TickType_t wake_tick = xTaskGetTickCount();
            const bool still_pressed =
                s_last_woke_by_boot && gpio_get_level(kBootButton) == 0;
            if (s_last_woke_by_boot) {
                ++s_inputs.boot_press_count;
            }
            s_boot.raw_pressed = still_pressed;
            s_boot.stable_pressed = still_pressed;
            s_boot.raw_changed_at = wake_tick;
            s_boot.pressed_at = wake_tick;
            s_boot_sleep_state = still_pressed
                ? BootSleepState::AwaitWakeRelease
                : BootSleepState::DisarmedUntilReleased;
            s_boot_release_timing = !still_pressed;
            s_boot_released_since = wake_tick;
            publish_inputs();
        }
        vTaskDelay(kPollTicks);
    }
}

void cleanup_light_wakeup();

// Configure BOOT as the digital wake source plus an optional timer.
esp_err_t configure_light_wakeup(uint64_t wake_after_us)
{
    esp_err_t result = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (result != ESP_OK) {
        return result;
    }
    (void)gpio_wakeup_disable(kBootButton);
    result = gpio_wakeup_enable(kBootButton, GPIO_INTR_LOW_LEVEL);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_sleep_enable_gpio_wakeup();
    if (result != ESP_OK) {
        (void)gpio_wakeup_disable(kBootButton);
        return result;
    }
    if (wake_after_us > 0) {
        result = esp_sleep_enable_timer_wakeup(wake_after_us);
        if (result != ESP_OK) {
            cleanup_light_wakeup();
        }
    }
    return result;
}

// Remove the GPIO and timer wake sources scoped to the completed sleep cycle.
void cleanup_light_wakeup()
{
    (void)gpio_wakeup_disable(kBootButton);
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
}

// Configure RTC-domain BOOT wake plus an optional timer.
esp_err_t configure_deep_wakeup(uint64_t wake_after_us)
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

// Pause idle-display work while an explicit sleep lifecycle owns the panel.
bool begin_display_sleep_transition()
{
    s_sleep_transition_active.store(true);
    if (s_display_policy_mutex == nullptr ||
        xSemaphoreTake(s_display_policy_mutex, portMAX_DELAY) != pdTRUE) {
        s_sleep_transition_active.store(false);
        return false;
    }
    return true;
}

// Return panel ownership to the asynchronous idle-display policy.
void end_display_sleep_transition()
{
    xSemaphoreGive(s_display_policy_mutex);
    s_sleep_transition_active.store(false);
}

}  // namespace

// Start debounced BOOT/PWR observation without arming sleep yet.
extern "C" esp_err_t qsee_power_inputs_init(void)
{
    if (s_task != nullptr) {
        return ESP_OK;
    }
    if (s_display_policy_mutex == nullptr) {
        s_display_policy_mutex =
            xSemaphoreCreateMutexStatic(&s_display_policy_mutex_control);
        if (s_display_policy_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    // EXT0 deep-sleep wake leaves BOOT in RTC mode after restart.
    (void)rtc_gpio_deinit(kBootButton);
    const gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << kBootButton) | (1ULL << kPowerButton),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t result = gpio_config(&button_config);
    if (result != ESP_OK) {
        return result;
    }
    auto &settings = qsee::UserSettingsService::instance();
    if (!settings.isInitialized()) {
        (void)settings.initialize();
    }
    if (settings.isInitialized()) {
        uint32_t dim_seconds = kDefaultDimTimeoutSeconds;
        uint32_t off_seconds = kDefaultPanelOffDelaySeconds;
        (void)settings.getUInt32(
            kDimTimeoutSetting,
            kDefaultDimTimeoutSeconds,
            &dim_seconds
        );
        (void)settings.getUInt32(
            kPanelOffDelaySetting,
            kDefaultPanelOffDelaySeconds,
            &off_seconds
        );
        s_dim_timeout_seconds.store(
            dim_seconds <= 3600U
                ? dim_seconds
                : kDefaultDimTimeoutSeconds
        );
        s_panel_off_delay_seconds.store(
            off_seconds <= 3600U
                ? off_seconds
                : kDefaultPanelOffDelaySeconds
        );
    }

#if CONFIG_PM_ENABLE
    const esp_pm_config_t power_policy = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 80,
        .light_sleep_enable = false,
    };
    result = esp_pm_configure(&power_policy);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Dynamic frequency policy failed: %s", esp_err_to_name(result));
    } else {
        ESP_LOGI(TAG, "Power policy: dynamic CPU 80-240 MHz; automatic light sleep disabled");
    }
#endif

    s_inputs.wakeup_cause = static_cast<uint32_t>(esp_sleep_get_wakeup_cause());
    set_input_component(s_inputs.boot_button, true, ESP_OK);
    set_input_component(s_inputs.power_button, true, ESP_OK);
    const esp_io_expander_handle_t expander = bsp_io_expander_init();
    set_input_component(
        s_inputs.io_expander,
        expander != nullptr,
        expander != nullptr ? ESP_OK : ESP_ERR_NOT_FOUND
    );

    s_display_task = xTaskCreateStatic(
        display_policy_task,
        "qsee_display_pm",
        sizeof(s_display_task_stack) / sizeof(s_display_task_stack[0]),
        nullptr,
        2,
        s_display_task_stack,
        &s_display_task_control
    );
    if (s_display_task == nullptr) {
        return ESP_ERR_NO_MEM;
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
        vTaskDelete(s_display_task);
        s_display_task = nullptr;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(
        TAG,
        "Input inventory: BOOT=GPIO0 PWR=GPIO41 TCA9554=%s wake-cause=%lu",
        expander != nullptr ? "ready" : "missing",
        static_cast<unsigned long>(s_inputs.wakeup_cause)
    );
    return ESP_OK;
}

// Let BOOT enter sleep only after the application reports a usable UI.
extern "C" void qsee_power_set_runtime_ready(void)
{
    s_runtime_ready.store(true);
    ESP_LOGI(TAG, "BOOT light-sleep action waiting for released state");
}

// Hand the LVGL inactivity sample to the dedicated power-policy task.
extern "C" void qsee_power_report_display_inactive_ms(uint32_t inactive_ms)
{
    s_raw_display_inactive_ms.store(inactive_ms);
    s_display_inactivity_available.store(true);
}

// Queue display recovery without blocking the UI or button polling task.
extern "C" void qsee_power_wake_display(void)
{
    note_display_activity();
}

// Tell the app shell to consume touch while the AMOLED is physically dark.
extern "C" bool qsee_power_display_input_guard_required(void)
{
    return s_display_input_guard_required.load();
}

extern "C" esp_err_t qsee_power_set_dim_timeout_seconds(uint32_t seconds)
{
    if (seconds > 3600U) {
        return ESP_ERR_INVALID_ARG;
    }
    auto &settings = qsee::UserSettingsService::instance();
    if (!settings.isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result =
        settings.setUInt32(kDimTimeoutSetting, seconds);
    if (result == ESP_OK) {
        s_dim_timeout_seconds.store(seconds);
    }
    return result;
}

extern "C" uint32_t qsee_power_get_dim_timeout_seconds(void)
{
    return s_dim_timeout_seconds.load();
}

extern "C" esp_err_t qsee_power_set_panel_off_delay_seconds(
    uint32_t seconds
)
{
    if (seconds > 3600U) {
        return ESP_ERR_INVALID_ARG;
    }
    auto &settings = qsee::UserSettingsService::instance();
    if (!settings.isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result =
        settings.setUInt32(kPanelOffDelaySetting, seconds);
    if (result == ESP_OK) {
        s_panel_off_delay_seconds.store(seconds);
    }
    return result;
}

extern "C" uint32_t qsee_power_get_panel_off_delay_seconds(void)
{
    return s_panel_off_delay_seconds.load();
}

// Coordinate sensors, radios, LVGL and fades around one light-sleep cycle.
extern "C" esp_err_t qsee_power_enter_light_sleep(uint64_t wake_after_us)
{
    if (!begin_display_sleep_transition()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t operation_result =
        qsee_hardware_set_sleep_transition(true);
    bool adapter_paused = false;
    bool radios_prepared = false;
    bool sleep_started = false;
    if (operation_result == ESP_OK) {
        operation_result = configure_light_wakeup(wake_after_us);
    }
    if (operation_result == ESP_OK) {
        operation_result = esp_lv_adapter_pause(1000);
        adapter_paused = operation_result == ESP_OK;
    }
    if (operation_result == ESP_OK) {
        operation_result = qsee_radios_prepare_light_sleep();
        radios_prepared = operation_result == ESP_OK;
    }
    if (operation_result == ESP_OK) {
        operation_result = qsee_display_fade_out(kSleepFadeOutMs);
    }
    if (operation_result == ESP_OK) {
        operation_result = qsee_display_set_panel_enabled(false);
    }
    if (operation_result == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Entering light sleep: BOOT + timer=%llu us",
            static_cast<unsigned long long>(wake_after_us)
        );
        sleep_started = true;
        operation_result = esp_light_sleep_start();
    }

    const esp_sleep_wakeup_cause_t wake_cause = sleep_started
        ? esp_sleep_get_wakeup_cause()
        : ESP_SLEEP_WAKEUP_UNDEFINED;
    s_inputs.wakeup_cause = static_cast<uint32_t>(wake_cause);
    s_last_woke_by_boot = wake_cause == ESP_SLEEP_WAKEUP_GPIO;
    // Wake sources are scoped to this sleep request.
    cleanup_light_wakeup();

    esp_err_t fade_in_result = ESP_OK;
    esp_err_t resume_result = ESP_OK;
    esp_err_t panel_result = ESP_OK;
    esp_err_t radio_result = ESP_OK;
    if (adapter_paused) {
        panel_result = qsee_display_set_panel_enabled(true);
        if (radios_prepared) {
            radio_result = qsee_radios_restore_after_light_sleep();
        }
        fade_in_result = qsee_display_fade_in(kWakeFadeInMs);
        resume_result = esp_lv_adapter_resume();
    }
    const bool display_restored =
        !adapter_paused ||
        (panel_result == ESP_OK && fade_in_result == ESP_OK &&
         resume_result == ESP_OK);
    s_adapter_resume_pending.store(
        adapter_paused && resume_result != ESP_OK
    );
    if (display_restored) {
        s_display_idle_state.store(DisplayIdleState::Active);
        s_display_transition_dirty = false;
        s_display_input_guard_required.store(false);
        s_display_wake_requested.store(false);
    } else {
        s_display_transition_dirty = true;
        s_display_input_guard_required.store(true);
        s_display_wake_requested.store(true);
    }
    s_rebase_display_inactivity.store(true);
    const esp_err_t transition_result =
        qsee_hardware_set_sleep_transition(false);
    end_display_sleep_transition();
    publish_inputs();
    ESP_LOGI(
        TAG,
        "Light sleep exited: result=%s cause=%lu source=%s",
        esp_err_to_name(operation_result),
        static_cast<unsigned long>(s_inputs.wakeup_cause),
        s_last_woke_by_boot ? "boot" : "timer/other"
    );

    if (operation_result != ESP_OK) return operation_result;
    if (panel_result != ESP_OK) return panel_result;
    if (radio_result != ESP_OK) return radio_result;
    if (fade_in_result != ESP_OK) return fade_in_result;
    if (resume_result != ESP_OK) return resume_result;
    return transition_result;
}

// Quiesce the display before entering the non-returning deep-sleep path.
extern "C" esp_err_t qsee_power_enter_deep_sleep(uint64_t wake_after_us)
{
    if (!begin_display_sleep_transition()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = qsee_hardware_set_sleep_transition(true);
    bool adapter_paused = false;
    if (result == ESP_OK) {
        result = configure_deep_wakeup(wake_after_us);
    }
    if (result == ESP_OK) {
        result = esp_lv_adapter_pause(1000);
        adapter_paused = result == ESP_OK;
    }
    if (result == ESP_OK) {
        result = qsee_display_fade_out(kSleepFadeOutMs);
    }
    if (result == ESP_OK) {
        result = qsee_display_set_panel_enabled(false);
    }
    if (result != ESP_OK) {
        esp_err_t panel_result = ESP_OK;
        esp_err_t fade_result = ESP_OK;
        esp_err_t resume_result = ESP_OK;
        if (adapter_paused) {
            panel_result = qsee_display_set_panel_enabled(true);
            fade_result = qsee_display_fade_in(kWakeFadeInMs);
            resume_result = esp_lv_adapter_resume();
        }
        const bool display_restored =
            !adapter_paused ||
            (panel_result == ESP_OK && fade_result == ESP_OK &&
             resume_result == ESP_OK);
        s_adapter_resume_pending.store(
            adapter_paused && resume_result != ESP_OK
        );
        s_display_idle_state.store(DisplayIdleState::Active);
        s_display_transition_dirty = !display_restored;
        s_display_input_guard_required.store(!display_restored);
        s_display_wake_requested.store(!display_restored);
        s_rebase_display_inactivity.store(true);
        (void)qsee_hardware_set_sleep_transition(false);
        end_display_sleep_transition();
        return result;
    }
    ESP_LOGI(
        TAG,
        "Entering deep sleep: BOOT + timer=%llu us",
        static_cast<unsigned long long>(wake_after_us)
    );
    esp_deep_sleep_start();
    return ESP_OK;
}
