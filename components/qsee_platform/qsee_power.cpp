/*
 * Single power-management service for buttons, inactivity, display, audio,
 * and motion wake.
 */

#include "qsee_platform.h"
#include "qsee_device_snapshot.hpp"
#include "qsee_power_policy.hpp"
#include "qsee_settings.hpp"

#include <atomic>
#include <cmath>
#include <cstdlib>

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "qsee_power_manager";
constexpr gpio_num_t kBootButton = GPIO_NUM_0;
constexpr gpio_num_t kPowerButton = GPIO_NUM_41;
constexpr TickType_t kPollTicks = pdMS_TO_TICKS(20);
constexpr TickType_t kPolicyTicks = pdMS_TO_TICKS(100);
constexpr TickType_t kDebounceTicks = pdMS_TO_TICKS(30);
constexpr TickType_t kBootResetPressTicks = pdMS_TO_TICKS(3000);
constexpr TickType_t kPowerLongPressTicks = pdMS_TO_TICKS(1200);
constexpr uint32_t kTaskStackBytes = 8192;
constexpr uint32_t kDisplayFadeDurationMs = 360;
constexpr float kMotionAccelerationDeltaMps2 = 0.80F;
constexpr float kMotionAngularVelocityDeltaDps = 4.0F;

TaskHandle_t s_task;
qsee::InputSnapshot s_inputs;
std::atomic_bool s_initialized{false};
std::atomic<uint32_t> s_activity_sequence{0};
std::atomic<int64_t> s_activity_at_us{0};
std::atomic<qsee::DeviceStateReason> s_activity_reason{
    qsee::DeviceStateReason::Touch
};
std::atomic<uint32_t> s_command_timeout_seconds{
    qsee::power_policy::kDefaultCommandArcTimeoutSeconds
};
std::atomic<uint32_t> s_status_timeout_seconds{
    qsee::power_policy::kDefaultStatusArcTimeoutSeconds
};
std::atomic<uint32_t> s_dim_timeout_seconds{
    qsee::power_policy::kDefaultDisplayDimTimeoutSeconds
};
std::atomic<uint32_t> s_screen_sleep_delay_seconds{
    qsee::power_policy::kDefaultScreenSleepDelaySeconds
};

qsee::DeviceOperatingState s_state =
    qsee::DeviceOperatingState::Active;
qsee::DeviceStateReason s_reason =
    qsee::DeviceStateReason::Startup;
qsee::UiHardwareSnapshot::DisplayPowerMode s_display_stage =
    qsee::UiHardwareSnapshot::DisplayPowerMode::Active;
bool s_audio_suspended;
bool s_command_arc_visible = true;
bool s_status_arc_visible = true;
uint32_t s_transition_sequence;
uint32_t s_consumed_activity_sequence;
int64_t s_last_activity_at_us;
int64_t s_last_imu_sample_us;
float s_last_acceleration_mps2[3] = {};
float s_last_angular_velocity_dps[3] = {};
bool s_motion_baseline_ready;

struct ButtonState {
    bool raw_pressed = false;
    bool stable_pressed = false;
    bool long_press_fired = false;
    TickType_t raw_changed_at = 0;
    TickType_t pressed_at = 0;
};

enum class ButtonEvent : uint8_t {
    None,
    Pressed,
    Released,
    ShortPress,
    LongPress,
};

ButtonState s_boot;
ButtonState s_power;

std::atomic<uint32_t> *timeout_storage(qsee_power_timeout_t timeout)
{
    switch (timeout) {
    case QSEE_POWER_TIMEOUT_COMMAND_ARC:
        return &s_command_timeout_seconds;
    case QSEE_POWER_TIMEOUT_STATUS_ARC:
        return &s_status_timeout_seconds;
    case QSEE_POWER_TIMEOUT_DISPLAY_DIM:
        return &s_dim_timeout_seconds;
    case QSEE_POWER_TIMEOUT_SCREEN_SLEEP:
        return &s_screen_sleep_delay_seconds;
    }
    return nullptr;
}

esp_err_t load_timeout(qsee_power_timeout_t timeout)
{
    auto &settings = qsee::UserSettingsService::instance();
    uint32_t seconds = 0;
    esp_err_t error = settings.getUInt32(
        qsee::power_policy::settings_key(timeout),
        &seconds
    );
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        seconds = qsee::power_policy::default_timeout_seconds(timeout);
        ESP_LOGI(
            kTag,
            "%s timeout absent; persisting explicit default %lu s",
            qsee::power_policy::display_name(timeout),
            static_cast<unsigned long>(seconds)
        );
        error = settings.setUInt32(
            qsee::power_policy::settings_key(timeout),
            seconds
        );
    }
    if (error != ESP_OK) {
        return error;
    }
    if (!qsee::power_policy::timeout_supported(timeout, seconds)) {
        ESP_LOGE(
            kTag,
            "%s timeout contains unsupported value %lu s",
            qsee::power_policy::display_name(timeout),
            static_cast<unsigned long>(seconds)
        );
        return ESP_ERR_INVALID_ARG;
    }
    timeout_storage(timeout)->store(seconds, std::memory_order_release);
    return ESP_OK;
}

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

void publish_inputs()
{
    s_inputs.boot_pressed = s_boot.stable_pressed;
    s_inputs.power_pressed = s_power.stable_pressed;
    qsee::DeviceSnapshotService::instance().reportInputs(s_inputs);
}

void publish_power(esp_err_t error)
{
    qsee::DeviceSnapshotService::instance().reportPowerManagement(
        s_state,
        s_reason,
        s_audio_suspended,
        s_command_arc_visible,
        s_status_arc_visible,
        s_display_stage,
        s_command_timeout_seconds.load(std::memory_order_acquire),
        s_status_timeout_seconds.load(std::memory_order_acquire),
        s_dim_timeout_seconds.load(std::memory_order_acquire),
        s_screen_sleep_delay_seconds.load(std::memory_order_acquire),
        s_transition_sequence,
        s_last_activity_at_us,
        error
    );
}

ButtonEvent poll_button(
    ButtonState &state,
    bool pressed,
    TickType_t now,
    const char *name,
    uint32_t &short_count,
    uint32_t *long_count,
    TickType_t long_press_ticks
)
{
    if (pressed != state.raw_pressed) {
        state.raw_pressed = pressed;
        state.raw_changed_at = now;
    }
    if (state.raw_pressed == state.stable_pressed) {
        if (state.stable_pressed &&
            long_count != nullptr &&
            !state.long_press_fired &&
            now - state.pressed_at >= long_press_ticks) {
            state.long_press_fired = true;
            ++(*long_count);
            ESP_LOGI(
                kTag,
                "%s long press (%lu ms)",
                name,
                static_cast<unsigned long>(
                    (now - state.pressed_at) * portTICK_PERIOD_MS
                )
            );
            return ButtonEvent::LongPress;
        }
        return ButtonEvent::None;
    }
    if (now - state.raw_changed_at < kDebounceTicks) {
        return ButtonEvent::None;
    }

    state.stable_pressed = state.raw_pressed;
    if (state.stable_pressed) {
        state.pressed_at = now;
        state.long_press_fired = false;
        ESP_LOGI(kTag, "%s pressed", name);
        return ButtonEvent::Pressed;
    }

    const TickType_t duration = now - state.pressed_at;
    if (state.long_press_fired) {
        return ButtonEvent::Released;
    }
    ++short_count;
    ESP_LOGI(
        kTag,
        "%s short press (%lu ms)",
        name,
        static_cast<unsigned long>(duration * portTICK_PERIOD_MS)
    );
    return ButtonEvent::ShortPress;
}

bool optional_audio_success(esp_err_t error)
{
    if (error == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(
            kTag,
            "Audio transition skipped: voice AFE disabled by profile"
        );
        return true;
    }
    return error == ESP_OK;
}

bool active_display_snapshot_diverged()
{
    const qsee::DeviceSnapshot snapshot =
        qsee::DeviceSnapshotService::instance().snapshot();
    const auto ready = [](const qsee::ComponentSnapshot &component) {
        return component.health == qsee::DeviceHealth::Ready &&
               component.last_error == ESP_OK;
    };
    if (ready(snapshot.ui.display_power_mode) &&
        snapshot.ui.display_power_mode_state !=
            qsee::UiHardwareSnapshot::DisplayPowerMode::Active) {
        ESP_LOGW(
            kTag,
            "Active display policy disagrees with applied mode; restoring"
        );
        return true;
    }
    if (!ready(snapshot.ui.brightness)) {
        return false;
    }

    const uint32_t applied = snapshot.ui.brightness_percent;
    const uint32_t configured =
        snapshot.ui.configured_brightness_percent;
    const bool brightness_differs =
        applied + 1U < configured || applied > configured + 1U;
    if (brightness_differs) {
        ESP_LOGW(
            kTag,
            "Active display brightness diverged: applied=%lu configured=%lu",
            static_cast<unsigned long>(applied),
            static_cast<unsigned long>(configured)
        );
    }
    return brightness_differs;
}

esp_err_t restore_active_policy(
    qsee::DeviceStateReason reason,
    bool display_snapshot_diverged
)
{
    esp_err_t error = ESP_OK;
    if (display_snapshot_diverged ||
        s_display_stage !=
            qsee::UiHardwareSnapshot::DisplayPowerMode::Active) {
        error = qsee_display_fade_to_power_mode(
            QSEE_DISPLAY_POWER_ACTIVE,
            kDisplayFadeDurationMs
        );
        if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED) {
            publish_power(error);
            return error;
        }
    }
    s_display_stage =
        qsee::UiHardwareSnapshot::DisplayPowerMode::Active;
    s_command_arc_visible = true;
    s_status_arc_visible = true;
    s_reason = reason;
    s_last_activity_at_us = esp_timer_get_time();
    ++s_transition_sequence;
    publish_power(ESP_OK);
    return ESP_OK;
}

esp_err_t set_standby(
    bool standby,
    qsee::DeviceStateReason reason
)
{
    const bool currently_standby =
        s_state == qsee::DeviceOperatingState::Standby;
    if (standby == currently_standby) {
        return ESP_OK;
    }

    if (standby) {
        s_state = qsee::DeviceOperatingState::EnteringStandby;
        s_reason = reason;
        publish_power(ESP_OK);

        const esp_err_t audio_error =
            qsee_voice_afe_set_standby(true);
        if (!optional_audio_success(audio_error)) {
            s_state = qsee::DeviceOperatingState::Active;
            publish_power(audio_error);
            return audio_error;
        }
        const esp_err_t display_error =
            qsee_display_fade_to_power_mode(
                QSEE_DISPLAY_POWER_OFF,
                kDisplayFadeDurationMs
            );
        if (display_error != ESP_OK &&
            display_error != ESP_ERR_NOT_SUPPORTED) {
            (void)qsee_voice_afe_set_standby(false);
            s_state = qsee::DeviceOperatingState::Active;
            publish_power(display_error);
            return display_error;
        }

        s_state = qsee::DeviceOperatingState::Standby;
        s_audio_suspended = audio_error == ESP_OK;
        s_display_stage =
            qsee::UiHardwareSnapshot::DisplayPowerMode::DisplayOff;
        s_command_arc_visible = false;
        s_status_arc_visible = false;
        ++s_transition_sequence;
        publish_power(ESP_OK);
        ESP_LOGI(
            kTag,
            "PWR standby entered: sequence=%lu",
            static_cast<unsigned long>(s_transition_sequence)
        );
        return ESP_OK;
    }

    s_state = qsee::DeviceOperatingState::Waking;
    s_reason = reason;
    publish_power(ESP_OK);
    const esp_err_t display_error =
        qsee_display_fade_to_power_mode(
            QSEE_DISPLAY_POWER_ACTIVE,
            kDisplayFadeDurationMs
        );
    if (display_error != ESP_OK &&
        display_error != ESP_ERR_NOT_SUPPORTED) {
        s_state = qsee::DeviceOperatingState::Standby;
        publish_power(display_error);
        return display_error;
    }
    const esp_err_t audio_error =
        qsee_voice_afe_set_standby(false);
    const bool audio_ready = optional_audio_success(audio_error);

    s_state = qsee::DeviceOperatingState::Active;
    s_audio_suspended = !audio_ready;
    s_display_stage =
        qsee::UiHardwareSnapshot::DisplayPowerMode::Active;
    s_command_arc_visible = true;
    s_status_arc_visible = true;
    s_last_activity_at_us = esp_timer_get_time();
    ++s_transition_sequence;
    publish_power(audio_ready ? ESP_OK : audio_error);
    ESP_LOGI(
        kTag,
        "Device awake: reason=%s sequence=%lu",
        reason == qsee::DeviceStateReason::ImuMotion
            ? "imu-motion"
            : "pwr-button",
        static_cast<unsigned long>(s_transition_sequence)
    );
    return audio_ready ? ESP_OK : audio_error;
}

bool strong_motion_detected(const qsee::ImuSnapshot &imu)
{
    const bool valid =
        imu.health == qsee::DeviceHealth::Ready &&
        imu.last_error == ESP_OK &&
        imu.data_valid &&
        !imu.data_stale &&
        imu.last_success_at_us > 0;
    if (!valid || imu.last_success_at_us == s_last_imu_sample_us) {
        return false;
    }
    s_last_imu_sample_us = imu.last_success_at_us;
    for (size_t axis = 0; axis < 3U; ++axis) {
        if (!std::isfinite(imu.acceleration_mps2[axis]) ||
            !std::isfinite(imu.angular_velocity_dps[axis])) {
            s_motion_baseline_ready = false;
            return false;
        }
    }
    if (!s_motion_baseline_ready) {
        for (size_t axis = 0; axis < 3U; ++axis) {
            s_last_acceleration_mps2[axis] =
                imu.acceleration_mps2[axis];
            s_last_angular_velocity_dps[axis] =
                imu.angular_velocity_dps[axis];
        }
        s_motion_baseline_ready = true;
        return false;
    }

    float acceleration_delta_squared = 0.0F;
    float angular_velocity_delta_squared = 0.0F;
    for (size_t axis = 0; axis < 3U; ++axis) {
        const float acceleration_delta =
            imu.acceleration_mps2[axis] -
            s_last_acceleration_mps2[axis];
        acceleration_delta_squared +=
            acceleration_delta * acceleration_delta;
        const float angular_velocity_delta =
            imu.angular_velocity_dps[axis] -
            s_last_angular_velocity_dps[axis];
        angular_velocity_delta_squared +=
            angular_velocity_delta * angular_velocity_delta;
        s_last_acceleration_mps2[axis] =
            imu.acceleration_mps2[axis];
        s_last_angular_velocity_dps[axis] =
            imu.angular_velocity_dps[axis];
    }
    return acceleration_delta_squared >=
               kMotionAccelerationDeltaMps2 *
                   kMotionAccelerationDeltaMps2 ||
           angular_velocity_delta_squared >=
               kMotionAngularVelocityDeltaDps *
                   kMotionAngularVelocityDeltaDps;
}

esp_err_t apply_idle_policy()
{
    if (s_state != qsee::DeviceOperatingState::Active) {
        return ESP_OK;
    }
    const int64_t elapsed_us =
        esp_timer_get_time() - s_last_activity_at_us;
    const auto expired = [elapsed_us](uint32_t seconds) {
        return seconds > 0U &&
               elapsed_us >= static_cast<int64_t>(seconds) * 1000000LL;
    };

    const bool command_visible = !expired(
        s_command_timeout_seconds.load(std::memory_order_acquire)
    );
    const bool status_visible = !expired(
        s_status_timeout_seconds.load(std::memory_order_acquire)
    );
    qsee::UiHardwareSnapshot::DisplayPowerMode display_stage =
        qsee::UiHardwareSnapshot::DisplayPowerMode::Active;
    const uint32_t dim_seconds =
        s_dim_timeout_seconds.load(std::memory_order_acquire);
    if (expired(dim_seconds)) {
        display_stage =
            qsee::UiHardwareSnapshot::DisplayPowerMode::Dimmed;
        const uint32_t screen_sleep_delay =
            s_screen_sleep_delay_seconds.load(std::memory_order_acquire);
        if (screen_sleep_delay > 0U &&
            elapsed_us >=
                static_cast<int64_t>(
                    dim_seconds + screen_sleep_delay
                ) *
                    1000000LL) {
            display_stage =
                qsee::UiHardwareSnapshot::DisplayPowerMode::DisplayOff;
        }
    }

    bool changed =
        command_visible != s_command_arc_visible ||
        status_visible != s_status_arc_visible;
    if (display_stage != s_display_stage) {
        qsee_display_power_mode_t mode = QSEE_DISPLAY_POWER_ACTIVE;
        if (display_stage ==
            qsee::UiHardwareSnapshot::DisplayPowerMode::Dimmed) {
            mode = QSEE_DISPLAY_POWER_DIMMED;
        } else if (
            display_stage ==
            qsee::UiHardwareSnapshot::DisplayPowerMode::DisplayOff) {
            mode = QSEE_DISPLAY_POWER_OFF;
        }
        const esp_err_t display_error =
            qsee_display_fade_to_power_mode(
                mode,
                kDisplayFadeDurationMs
            );
        if (display_error != ESP_OK &&
            display_error != ESP_ERR_NOT_SUPPORTED) {
            publish_power(display_error);
            return display_error;
        }
        s_display_stage = display_stage;
        changed = true;
    }
    if (!changed) {
        return ESP_OK;
    }

    s_command_arc_visible = command_visible;
    s_status_arc_visible = status_visible;
    ++s_transition_sequence;
    publish_power(ESP_OK);
    ESP_LOGI(
        kTag,
        "Idle policy: command=%s status=%s display=%s",
        command_visible ? "visible" : "hidden",
        status_visible ? "visible" : "hidden",
        s_display_stage ==
                qsee::UiHardwareSnapshot::DisplayPowerMode::Active
            ? "active"
            : s_display_stage ==
                      qsee::UiHardwareSnapshot::DisplayPowerMode::Dimmed
                  ? "dimmed"
                  : "off"
    );
    return ESP_OK;
}

void power_task(void *argument)
{
    (void)argument;
    TickType_t last_policy_tick = xTaskGetTickCount();
    publish_inputs();
    publish_power(ESP_OK);
    while (true) {
        const TickType_t now = xTaskGetTickCount();
        const ButtonEvent boot_event = poll_button(
            s_boot,
            gpio_get_level(kBootButton) == 0,
            now,
            "BOOT",
            s_inputs.boot_press_count,
            &s_inputs.boot_long_press_count,
            kBootResetPressTicks
        );
        const ButtonEvent power_event = poll_button(
            s_power,
            gpio_get_level(kPowerButton) == 0,
            now,
            "PWR",
            s_inputs.power_short_press_count,
            &s_inputs.power_long_press_count,
            kPowerLongPressTicks
        );
        if (boot_event != ButtonEvent::None ||
            power_event != ButtonEvent::None) {
            publish_inputs();
        }
        if (boot_event == ButtonEvent::ShortPress) {
            s_activity_at_us.store(
                esp_timer_get_time(),
                std::memory_order_relaxed
            );
            s_activity_reason.store(
                qsee::DeviceStateReason::BootButton,
                std::memory_order_relaxed
            );
            s_activity_sequence.fetch_add(
                1,
                std::memory_order_release
            );
        }
        if (boot_event == ButtonEvent::LongPress) {
            ESP_LOGW(
                kTag,
                "BOOT held for 3 seconds; restarting device"
            );
            esp_restart();
        }
        if (power_event == ButtonEvent::ShortPress) {
            const esp_err_t transition_error = set_standby(
                s_state != qsee::DeviceOperatingState::Standby,
                qsee::DeviceStateReason::PowerButton
            );
            if (transition_error != ESP_OK) {
                ESP_LOGE(
                    kTag,
                    "PWR state transition failed (%s)",
                    esp_err_to_name(transition_error)
                );
            }
        }

        if (now - last_policy_tick < kPolicyTicks) {
            vTaskDelay(kPollTicks);
            continue;
        }
        last_policy_tick = now;
        const qsee::DeviceSnapshot snapshot =
            qsee::DeviceSnapshotService::instance().snapshot();
        if (strong_motion_detected(snapshot.imu)) {
            if (s_state == qsee::DeviceOperatingState::Standby) {
                const esp_err_t wake_error = set_standby(
                    false,
                    qsee::DeviceStateReason::ImuMotion
                );
                if (wake_error != ESP_OK) {
                    ESP_LOGE(
                        kTag,
                        "IMU wake failed (%s)",
                        esp_err_to_name(wake_error)
                    );
                }
            } else if (
                s_state == qsee::DeviceOperatingState::Active) {
                s_last_activity_at_us = esp_timer_get_time();
                const bool display_snapshot_diverged =
                    active_display_snapshot_diverged();
                if (!s_command_arc_visible ||
                    !s_status_arc_visible ||
                    s_display_stage !=
                        qsee::UiHardwareSnapshot::DisplayPowerMode::Active ||
                    display_snapshot_diverged) {
                    (void)restore_active_policy(
                        qsee::DeviceStateReason::ImuMotion,
                        display_snapshot_diverged
                    );
                } else {
                    s_reason = qsee::DeviceStateReason::ImuMotion;
                    publish_power(ESP_OK);
                }
            }
        }

        const uint32_t activity_sequence =
            s_activity_sequence.load(std::memory_order_acquire);
        if (activity_sequence != s_consumed_activity_sequence) {
            s_consumed_activity_sequence = activity_sequence;
            if (s_state == qsee::DeviceOperatingState::Active) {
                s_last_activity_at_us =
                    s_activity_at_us.load(std::memory_order_relaxed);
                const qsee::DeviceStateReason activity_reason =
                    s_activity_reason.load(std::memory_order_relaxed);
                const bool display_snapshot_diverged =
                    active_display_snapshot_diverged();
                if (!s_command_arc_visible ||
                    !s_status_arc_visible ||
                    s_display_stage !=
                        qsee::UiHardwareSnapshot::DisplayPowerMode::Active ||
                    display_snapshot_diverged) {
                    (void)restore_active_policy(
                        activity_reason,
                        display_snapshot_diverged
                    );
                } else {
                    s_reason = activity_reason;
                    publish_power(ESP_OK);
                }
            }
        }
        (void)apply_idle_policy();
        vTaskDelay(kPollTicks);
    }
}

esp_err_t configure_wakeup(uint64_t wake_after_us)
{
    esp_err_t result =
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
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

esp_err_t allow_disabled_display(const char *operation, esp_err_t result)
{
    if (result == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(
            kTag,
            "Display transition '%s' skipped: UI disabled by profile",
            operation
        );
        return ESP_OK;
    }
    return result;
}

}  // namespace

extern "C" esp_err_t qsee_power_manager_init(void)
{
    if (s_initialized.exchange(true, std::memory_order_acq_rel)) {
        return ESP_OK;
    }
    auto &settings = qsee::UserSettingsService::instance();
    esp_err_t result =
        settings.isInitialized() ? ESP_OK : settings.initialize();
    if (result == ESP_OK) {
        result = load_timeout(QSEE_POWER_TIMEOUT_COMMAND_ARC);
    }
    if (result == ESP_OK) {
        result = load_timeout(QSEE_POWER_TIMEOUT_STATUS_ARC);
    }
    if (result == ESP_OK) {
        result = load_timeout(QSEE_POWER_TIMEOUT_DISPLAY_DIM);
    }
    if (result == ESP_OK) {
        result = load_timeout(QSEE_POWER_TIMEOUT_SCREEN_SLEEP);
    }
    if (result != ESP_OK) {
        s_initialized.store(false, std::memory_order_release);
        ESP_LOGE(
            kTag,
            "Power settings initialization failed (%s)",
            esp_err_to_name(result)
        );
        return result;
    }

    result = qsee_hardware_set_ui_motion_monitor(true);
    if (result != ESP_OK) {
        s_initialized.store(false, std::memory_order_release);
        ESP_LOGE(
            kTag,
            "Power-owned IMU monitor failed (%s)",
            esp_err_to_name(result)
        );
        return result;
    }

    const gpio_config_t config = {
        .pin_bit_mask =
            (1ULL << kBootButton) | (1ULL << kPowerButton),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    result = gpio_config(&config);
    if (result != ESP_OK) {
        set_input_component(s_inputs.boot_button, false, result);
        set_input_component(s_inputs.power_button, false, result);
        publish_inputs();
        s_initialized.store(false, std::memory_order_release);
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
        s_initialized.store(false, std::memory_order_release);
        return result;
    }
#endif

    s_inputs.wakeup_cause = esp_sleep_get_wakeup_causes();
    s_boot.raw_pressed = gpio_get_level(kBootButton) == 0;
    s_boot.stable_pressed = s_boot.raw_pressed;
    s_power.raw_pressed = gpio_get_level(kPowerButton) == 0;
    s_power.stable_pressed = s_power.raw_pressed;
    const TickType_t now = xTaskGetTickCount();
    s_boot.pressed_at = now;
    s_power.pressed_at = now;
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
        publish_inputs();
        s_initialized.store(false, std::memory_order_release);
        return expander_error != ESP_OK
            ? expander_error
            : ESP_ERR_INVALID_STATE;
    }

    s_state = qsee::DeviceOperatingState::Active;
    s_reason = qsee::DeviceStateReason::Startup;
    s_audio_suspended = false;
    s_command_arc_visible = true;
    s_status_arc_visible = true;
    s_display_stage =
        qsee::UiHardwareSnapshot::DisplayPowerMode::Active;
    s_transition_sequence = 0;
    s_last_activity_at_us = esp_timer_get_time();
    s_consumed_activity_sequence =
        s_activity_sequence.load(std::memory_order_acquire);
    if (xTaskCreatePinnedToCoreWithCaps(
            power_task,
            "qsee_power",
            kTaskStackBytes,
            nullptr,
            3,
            &s_task,
            tskNO_AFFINITY,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        ) != pdPASS) {
        publish_power(ESP_ERR_NO_MEM);
        s_initialized.store(false, std::memory_order_release);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        kTag,
        "Power service ready: BOOT=GPIO0 PWR=GPIO41, "
        "timers=%lu/%lu/%lu/+%lu s",
        static_cast<unsigned long>(
            s_command_timeout_seconds.load(std::memory_order_acquire)
        ),
        static_cast<unsigned long>(
            s_status_timeout_seconds.load(std::memory_order_acquire)
        ),
        static_cast<unsigned long>(
            s_dim_timeout_seconds.load(std::memory_order_acquire)
        ),
        static_cast<unsigned long>(
            s_screen_sleep_delay_seconds.load(std::memory_order_acquire)
        )
    );
    return ESP_OK;
}

extern "C" void qsee_power_manager_note_activity(void)
{
    s_activity_at_us.store(
        esp_timer_get_time(),
        std::memory_order_relaxed
    );
    s_activity_reason.store(
        qsee::DeviceStateReason::Touch,
        std::memory_order_relaxed
    );
    s_activity_sequence.fetch_add(1, std::memory_order_release);
}

extern "C" esp_err_t qsee_power_manager_set_timeout(
    qsee_power_timeout_t timeout,
    uint32_t seconds
)
{
    if (!s_initialized.load(std::memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    std::atomic<uint32_t> *storage = timeout_storage(timeout);
    if (storage == nullptr ||
        !qsee::power_policy::timeout_supported(timeout, seconds)) {
        ESP_LOGE(
            kTag,
            "%s timeout rejected unsupported value %lu s",
            qsee::power_policy::display_name(timeout),
            static_cast<unsigned long>(seconds)
        );
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t error =
        qsee::UserSettingsService::instance().setUInt32(
            qsee::power_policy::settings_key(timeout),
            seconds
        );
    if (error != ESP_OK) {
        return error;
    }
    storage->store(seconds, std::memory_order_release);
    qsee_power_manager_note_activity();
    ESP_LOGI(
        kTag,
        "%s timeout updated: %lu s",
        qsee::power_policy::display_name(timeout),
        static_cast<unsigned long>(seconds)
    );
    return ESP_OK;
}

extern "C" esp_err_t qsee_power_manager_get_timeout(
    qsee_power_timeout_t timeout,
    uint32_t *seconds
)
{
    if (seconds == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized.load(std::memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    std::atomic<uint32_t> *storage = timeout_storage(timeout);
    if (storage == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *seconds = storage->load(std::memory_order_acquire);
    return ESP_OK;
}

extern "C" esp_err_t qsee_power_enter_light_sleep(uint64_t wake_after_us)
{
    esp_err_t result = configure_wakeup(wake_after_us);
    if (result != ESP_OK) {
        return result;
    }
    result = allow_disabled_display(
        "light-sleep-off",
        qsee_display_fade_to_power_mode(
            QSEE_DISPLAY_POWER_OFF,
            kDisplayFadeDurationMs
        )
    );
    if (result != ESP_OK) {
        return result;
    }
    const esp_err_t sleep_result = esp_light_sleep_start();
    s_inputs.wakeup_cause = esp_sleep_get_wakeup_causes();
    publish_inputs();
    const esp_err_t resume_error = allow_disabled_display(
        "light-sleep-resume",
        qsee_display_fade_to_power_mode(
            QSEE_DISPLAY_POWER_ACTIVE,
            kDisplayFadeDurationMs
        )
    );
    if (sleep_result != ESP_OK) {
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
    result = allow_disabled_display(
        "deep-sleep-off",
        qsee_display_fade_to_power_mode(
            QSEE_DISPLAY_POWER_OFF,
            kDisplayFadeDurationMs
        )
    );
    if (result != ESP_OK) {
        return result;
    }
    esp_deep_sleep_start();
    return ESP_OK;
}
