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
constexpr TickType_t kPollTicks = pdMS_TO_TICKS(20);
constexpr TickType_t kPolicyTicks = pdMS_TO_TICKS(100);
constexpr TickType_t kDebounceTicks = pdMS_TO_TICKS(30);
constexpr TickType_t kBootArmReleaseTicks = pdMS_TO_TICKS(500);
constexpr TickType_t kBootResetPressTicks = pdMS_TO_TICKS(3000);
constexpr uint32_t kTaskStackBytes = 8192;
constexpr uint32_t kDisplayFadeDurationMs = 360;
constexpr float kMotionAccelerationEnterMps2 = 0.80F;
constexpr float kMotionAccelerationQuietMps2 = 0.40F;
constexpr float kMotionAngularVelocityEnterDps = 4.0F;
constexpr float kMotionAngularVelocityQuietDps = 2.0F;
constexpr uint32_t kMotionEnterSampleCount = 3;
constexpr uint32_t kMotionQuietSampleCount = 3;
constexpr int64_t kMotionCooldownUs = 2LL * 1000LL * 1000LL;
constexpr int64_t kMotionGraceUs = 2LL * 1000LL * 1000LL;
constexpr int64_t kPkeyErrorReportIntervalUs = 5LL * 1000LL * 1000LL;

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
bool s_fullscreen_active;
bool s_command_arc_visible = true;
bool s_status_arc_visible = true;
uint32_t s_transition_sequence;
uint32_t s_consumed_activity_sequence;
int64_t s_last_activity_at_us;
int64_t s_last_imu_sample_us;
float s_motion_baseline_acceleration_mps2[3] = {};
float s_motion_previous_acceleration_mps2[3] = {};
bool s_motion_baseline_ready;
bool s_motion_armed;
uint32_t s_motion_enter_sample_count;
uint32_t s_motion_quiet_sample_count;
int64_t s_motion_cooldown_until_us;
int64_t s_motion_grace_until_us;
int64_t s_last_pkey_error_report_at_us;
esp_err_t s_last_pkey_error = ESP_OK;
uint32_t s_initial_pkey_short_presses;
uint32_t s_initial_pkey_long_presses;
bool s_motion_monitor_started;
bool s_boot_armed;
bool s_boot_release_tracking;
TickType_t s_boot_release_stable_at;
std::atomic_bool s_boot_rearm_requested{false};
std::atomic<uint32_t> s_pending_wakeup_cause{0};

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
    // AXP2101 exposes discrete PKEY IRQs, not a stable pressed level.
    s_inputs.power_pressed = false;
    qsee::DeviceSnapshotService::instance().reportInputs(s_inputs);
}

void publish_power(esp_err_t error)
{
    qsee::DeviceSnapshotService::instance().reportPowerManagement(
        s_state,
        s_reason,
        s_audio_suspended,
        s_fullscreen_active,
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
    bool events_armed,
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
            events_armed &&
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
    if (!events_armed) {
        state.long_press_fired = false;
        return ButtonEvent::Released;
    }
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

void reset_boot_arm_state(TickType_t now)
{
    const bool pressed = gpio_get_level(kBootButton) == 0;
    s_boot.raw_pressed = pressed;
    s_boot.stable_pressed = pressed;
    s_boot.long_press_fired = false;
    s_boot.raw_changed_at = now;
    s_boot.pressed_at = now;
    s_boot_armed = false;
    s_boot_release_tracking = !pressed;
    s_boot_release_stable_at = now;
}

void update_boot_arming(TickType_t now)
{
    if (s_boot_armed) {
        return;
    }
    if (s_boot.raw_pressed || s_boot.stable_pressed) {
        s_boot_release_tracking = false;
        return;
    }
    if (!s_boot_release_tracking) {
        s_boot_release_tracking = true;
        s_boot_release_stable_at = now;
        return;
    }
    if (now - s_boot_release_stable_at < kBootArmReleaseTicks) {
        return;
    }

    s_boot_armed = true;
    s_boot.long_press_fired = false;
    s_boot.pressed_at = now;
    ESP_LOGI(
        kTag,
        "BOOT armed after %lu ms stable release",
        static_cast<unsigned long>(
            kBootArmReleaseTicks * portTICK_PERIOD_MS
        )
    );
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

void begin_motion_grace(int64_t now_us)
{
    s_motion_baseline_ready = false;
    s_motion_armed = false;
    s_motion_enter_sample_count = 0;
    s_motion_quiet_sample_count = 0;
    s_motion_grace_until_us = now_us + kMotionGraceUs;
    if (s_motion_cooldown_until_us < s_motion_grace_until_us) {
        s_motion_cooldown_until_us = s_motion_grace_until_us;
    }
}

bool update_power_key_component(esp_err_t error)
{
    const int64_t now_us = esp_timer_get_time();
    const bool was_ready =
        s_inputs.power_button.health == qsee::DeviceHealth::Ready &&
        s_inputs.power_button.last_error == ESP_OK;
    const bool error_changed = error != s_last_pkey_error;
    set_input_component(
        s_inputs.power_button,
        error == ESP_OK,
        error
    );
    s_last_pkey_error = error;

    if (error == ESP_OK) {
        if (!was_ready) {
            ESP_LOGI(kTag, "AXP2101 PKEY event channel recovered");
            return true;
        }
        return false;
    }

    const bool report_due =
        error_changed ||
        s_last_pkey_error_report_at_us == 0 ||
        now_us - s_last_pkey_error_report_at_us >=
            kPkeyErrorReportIntervalUs;
    if (report_due) {
        s_last_pkey_error_report_at_us = now_us;
        ESP_LOGE(
            kTag,
            "AXP2101 PKEY event poll failed (%s)",
            esp_err_to_name(error)
        );
    }
    return report_due;
}

esp_err_t restore_active_policy(
    qsee::DeviceStateReason reason,
    bool display_snapshot_diverged,
    bool exit_fullscreen = false
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
    if (exit_fullscreen) {
        s_fullscreen_active = false;
    }
    s_display_stage =
        qsee::UiHardwareSnapshot::DisplayPowerMode::Active;
    s_command_arc_visible = !s_fullscreen_active;
    s_status_arc_visible = !s_fullscreen_active;
    s_reason = reason;
    s_last_activity_at_us = esp_timer_get_time();
    ++s_transition_sequence;
    publish_power(ESP_OK);
    return ESP_OK;
}

esp_err_t handle_boot_short_press()
{
    if (s_state != qsee::DeviceOperatingState::Active) {
        ESP_LOGI(
            kTag,
            "BOOT short press ignored while device state is not active"
        );
        return ESP_OK;
    }

    const bool display_snapshot_diverged =
        active_display_snapshot_diverged();
    if (s_display_stage !=
            qsee::UiHardwareSnapshot::DisplayPowerMode::Active ||
        display_snapshot_diverged) {
        ESP_LOGI(
            kTag,
            "BOOT short press waking display before fullscreen toggle"
        );
        return restore_active_policy(
            qsee::DeviceStateReason::BootButton,
            display_snapshot_diverged,
            true
        );
    }

    if (s_fullscreen_active ||
        (!s_command_arc_visible && !s_status_arc_visible)) {
        ESP_LOGI(kTag, "BOOT fullscreen disabled; restoring both arcs");
        return restore_active_policy(
            qsee::DeviceStateReason::BootButton,
            false,
            true
        );
    }

    s_fullscreen_active = true;
    s_command_arc_visible = false;
    s_status_arc_visible = false;
    s_reason = qsee::DeviceStateReason::BootButton;
    s_last_activity_at_us = esp_timer_get_time();
    ++s_transition_sequence;
    publish_power(ESP_OK);
    ESP_LOGI(
        kTag,
        "BOOT fullscreen enabled: both arcs hidden, sequence=%lu",
        static_cast<unsigned long>(s_transition_sequence)
    );
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
            const esp_err_t display_rollback_error =
                qsee_display_fade_to_power_mode(
                    QSEE_DISPLAY_POWER_ACTIVE,
                    kDisplayFadeDurationMs
                );
            if (display_rollback_error != ESP_OK &&
                display_rollback_error != ESP_ERR_NOT_SUPPORTED) {
                ESP_LOGE(
                    kTag,
                    "Display rollback after standby failure failed (%s)",
                    esp_err_to_name(display_rollback_error)
                );
            }
            const esp_err_t rollback_error =
                qsee_voice_afe_set_standby(false);
            if (!optional_audio_success(rollback_error)) {
                ESP_LOGE(
                    kTag,
                    "Audio rollback after display-off failure failed (%s)",
                    esp_err_to_name(rollback_error)
                );
            }
            s_state = qsee::DeviceOperatingState::Active;
            publish_power(display_error);
            return display_error;
        }

        s_state = qsee::DeviceOperatingState::Standby;
        s_audio_suspended = audio_error == ESP_OK;
        s_display_stage =
            qsee::UiHardwareSnapshot::DisplayPowerMode::DisplayOff;
        s_fullscreen_active = false;
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
        const esp_err_t display_rollback_error =
            qsee_display_fade_to_power_mode(
                QSEE_DISPLAY_POWER_OFF,
                kDisplayFadeDurationMs
            );
        if (display_rollback_error != ESP_OK &&
            display_rollback_error != ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGE(
                kTag,
                "Display rollback after wake failure failed (%s)",
                esp_err_to_name(display_rollback_error)
            );
        }
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
    s_fullscreen_active = false;
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

bool qualified_motion_detected(const qsee::ImuSnapshot &imu)
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
            s_motion_armed = false;
            s_motion_enter_sample_count = 0;
            s_motion_quiet_sample_count = 0;
            ESP_LOGW(
                kTag,
                "Motion sample rejected: non-finite IMU value at %lld us",
                static_cast<long long>(imu.last_success_at_us)
            );
            return false;
        }
    }

    const auto update_baseline = [&imu]() {
        for (size_t axis = 0; axis < 3U; ++axis) {
            s_motion_baseline_acceleration_mps2[axis] =
                imu.acceleration_mps2[axis];
        }
    };
    const auto update_previous = [&imu]() {
        for (size_t axis = 0; axis < 3U; ++axis) {
            s_motion_previous_acceleration_mps2[axis] =
                imu.acceleration_mps2[axis];
        }
    };
    if (!s_motion_baseline_ready) {
        update_baseline();
        update_previous();
        s_motion_baseline_ready = true;
        return false;
    }

    float acceleration_delta_squared = 0.0F;
    float acceleration_step_squared = 0.0F;
    float angular_velocity_squared = 0.0F;
    for (size_t axis = 0; axis < 3U; ++axis) {
        const float acceleration_delta =
            imu.acceleration_mps2[axis] -
            s_motion_baseline_acceleration_mps2[axis];
        acceleration_delta_squared +=
            acceleration_delta * acceleration_delta;
        const float acceleration_step =
            imu.acceleration_mps2[axis] -
            s_motion_previous_acceleration_mps2[axis];
        acceleration_step_squared += acceleration_step * acceleration_step;
        angular_velocity_squared +=
            imu.angular_velocity_dps[axis] *
            imu.angular_velocity_dps[axis];
    }
    update_previous();

    const int64_t now_us = esp_timer_get_time();
    if (now_us < s_motion_grace_until_us) {
        update_baseline();
        s_motion_armed = false;
        s_motion_enter_sample_count = 0;
        s_motion_quiet_sample_count = 0;
        return false;
    }

    const bool below_quiet =
        acceleration_step_squared <=
            kMotionAccelerationQuietMps2 *
                kMotionAccelerationQuietMps2 &&
        angular_velocity_squared <=
            kMotionAngularVelocityQuietDps *
                kMotionAngularVelocityQuietDps;
    const bool above_enter =
        acceleration_delta_squared >=
            kMotionAccelerationEnterMps2 *
                kMotionAccelerationEnterMps2 ||
        angular_velocity_squared >=
            kMotionAngularVelocityEnterDps *
                kMotionAngularVelocityEnterDps;

    if (!s_motion_armed) {
        s_motion_enter_sample_count = 0;
        if (below_quiet) {
            ++s_motion_quiet_sample_count;
            update_baseline();
        } else {
            s_motion_quiet_sample_count = 0;
        }
        if (s_motion_quiet_sample_count >= kMotionQuietSampleCount) {
            s_motion_armed = true;
            s_motion_quiet_sample_count = 0;
            ESP_LOGD(kTag, "Motion wake armed after quiet dwell");
        }
        return false;
    }

    if (now_us < s_motion_cooldown_until_us) {
        s_motion_enter_sample_count = 0;
        if (below_quiet) {
            update_baseline();
        }
        return false;
    }

    s_motion_enter_sample_count =
        above_enter ? s_motion_enter_sample_count + 1U : 0U;
    if (below_quiet) {
        update_baseline();
    }
    if (s_motion_enter_sample_count < kMotionEnterSampleCount) {
        return false;
    }

    const float acceleration_delta = std::sqrt(acceleration_delta_squared);
    const float angular_velocity = std::sqrt(angular_velocity_squared);
    s_motion_armed = false;
    s_motion_enter_sample_count = 0;
    s_motion_quiet_sample_count = 0;
    s_motion_cooldown_until_us = now_us + kMotionCooldownUs;
    ESP_LOGI(
        kTag,
        "Qualified motion: acceleration-delta=%.2f m/s2 gyro=%.2f dps",
        static_cast<double>(acceleration_delta),
        static_cast<double>(angular_velocity)
    );
    return true;
}

esp_err_t handle_power_key_events(
    uint32_t short_presses,
    uint32_t long_presses
)
{
    if (short_presses == 0U && long_presses == 0U) {
        return ESP_OK;
    }
    if (UINT32_MAX - s_inputs.power_short_press_count < short_presses ||
        UINT32_MAX - s_inputs.power_long_press_count < long_presses) {
        ESP_LOGE(
            kTag,
            "PKEY counter overflow rejected: short=%lu long=%lu",
            static_cast<unsigned long>(short_presses),
            static_cast<unsigned long>(long_presses)
        );
        return ESP_ERR_INVALID_SIZE;
    }

    s_inputs.power_short_press_count += short_presses;
    s_inputs.power_long_press_count += long_presses;
    begin_motion_grace(esp_timer_get_time());

    if (long_presses > 0U) {
        ESP_LOGI(
            kTag,
            "PWR long press: requesting standby (count=%lu)",
            static_cast<unsigned long>(long_presses)
        );
        if (s_state == qsee::DeviceOperatingState::Standby) {
            return ESP_OK;
        }
        return set_standby(true, qsee::DeviceStateReason::PowerButton);
    }

    ESP_LOGI(
        kTag,
        "PWR short press: toggling standby (count=%lu)",
        static_cast<unsigned long>(short_presses)
    );
    if ((short_presses & 1U) == 0U) {
        return ESP_OK;
    }
    return set_standby(
        s_state != qsee::DeviceOperatingState::Standby,
        qsee::DeviceStateReason::PowerButton
    );
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

    const bool command_visible =
        !s_fullscreen_active &&
        !expired(
            s_command_timeout_seconds.load(std::memory_order_acquire)
        );
    const bool status_visible =
        !s_fullscreen_active &&
        !expired(
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
        if (s_boot_rearm_requested.exchange(
                false,
                std::memory_order_acq_rel
            )) {
            reset_boot_arm_state(now);
            s_inputs.wakeup_cause =
                s_pending_wakeup_cause.exchange(
                    0,
                    std::memory_order_acq_rel
                );
            publish_inputs();
        }
        const ButtonEvent boot_event = poll_button(
            s_boot,
            gpio_get_level(kBootButton) == 0,
            now,
            "BOOT",
            s_boot_armed,
            s_inputs.boot_press_count,
            &s_inputs.boot_long_press_count,
            kBootResetPressTicks
        );
        update_boot_arming(now);
        if (boot_event != ButtonEvent::None) {
            publish_inputs();
        }
        if (boot_event == ButtonEvent::ShortPress) {
            const esp_err_t fullscreen_error =
                handle_boot_short_press();
            if (fullscreen_error != ESP_OK) {
                ESP_LOGE(
                    kTag,
                    "BOOT fullscreen transition failed (%s)",
                    esp_err_to_name(fullscreen_error)
                );
            }
        }
        if (boot_event == ButtonEvent::LongPress) {
            ESP_LOGW(
                kTag,
                "BOOT held for 3 seconds; restarting device"
            );
            esp_restart();
        }
        if (now - last_policy_tick < kPolicyTicks) {
            vTaskDelay(kPollTicks);
            continue;
        }
        last_policy_tick = now;

        uint32_t short_presses = s_initial_pkey_short_presses;
        uint32_t long_presses = s_initial_pkey_long_presses;
        s_initial_pkey_short_presses = 0;
        s_initial_pkey_long_presses = 0;
        uint32_t sampled_short_presses = 0;
        uint32_t sampled_long_presses = 0;
        const esp_err_t pkey_error =
            qsee_hardware_take_power_key_events(
                &sampled_short_presses,
                &sampled_long_presses
            );
        const bool publish_pkey_health =
            update_power_key_component(pkey_error);
        if (pkey_error == ESP_OK) {
            short_presses += sampled_short_presses;
            long_presses += sampled_long_presses;
        }
        const esp_err_t pkey_transition_error =
            handle_power_key_events(short_presses, long_presses);
        if (short_presses > 0U ||
            long_presses > 0U ||
            publish_pkey_health) {
            publish_inputs();
        }
        if (pkey_transition_error != ESP_OK) {
            ESP_LOGE(
                kTag,
                "PWR event handling failed (%s)",
                esp_err_to_name(pkey_transition_error)
            );
        }

        const qsee::DeviceSnapshot snapshot =
            qsee::DeviceSnapshotService::instance().snapshot();
        if (s_motion_monitor_started &&
            qualified_motion_detected(snapshot.imu)) {
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
                if ((!s_fullscreen_active &&
                     (!s_command_arc_visible ||
                      !s_status_arc_visible)) ||
                    s_display_stage !=
                        qsee::UiHardwareSnapshot::DisplayPowerMode::Active ||
                    display_snapshot_diverged) {
                    const esp_err_t restore_error = restore_active_policy(
                        qsee::DeviceStateReason::ImuMotion,
                        display_snapshot_diverged
                    );
                    if (restore_error != ESP_OK) {
                        ESP_LOGE(
                            kTag,
                            "IMU activity restore failed (%s)",
                            esp_err_to_name(restore_error)
                        );
                    }
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
                if ((!s_fullscreen_active &&
                     (!s_command_arc_visible ||
                      !s_status_arc_visible)) ||
                    s_display_stage !=
                        qsee::UiHardwareSnapshot::DisplayPowerMode::Active ||
                    display_snapshot_diverged) {
                    const esp_err_t restore_error = restore_active_policy(
                        activity_reason,
                        display_snapshot_diverged
                    );
                    if (restore_error != ESP_OK) {
                        ESP_LOGE(
                            kTag,
                            "Input activity restore failed (%s)",
                            esp_err_to_name(restore_error)
                        );
                    }
                } else {
                    s_reason = activity_reason;
                    publish_power(ESP_OK);
                }
            }
        }
        const esp_err_t idle_error = apply_idle_policy();
        if (idle_error != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Idle policy application failed (%s)",
                esp_err_to_name(idle_error)
            );
        }
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

void stop_motion_monitor_after_init_failure(const char *stage)
{
    if (!s_motion_monitor_started) {
        return;
    }
    const esp_err_t error = qsee_hardware_set_ui_motion_monitor(false);
    if (error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "%s cleanup failed to stop the IMU monitor (%s)",
            stage,
            esp_err_to_name(error)
        );
        return;
    }
    s_motion_monitor_started = false;
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

    const esp_err_t motion_monitor_error =
        qsee_hardware_set_ui_motion_monitor(true);
    s_motion_monitor_started = motion_monitor_error == ESP_OK;
    if (!s_motion_monitor_started) {
        ESP_LOGW(
            kTag,
            "IMU wake is unavailable; continuing with PKEY and inactivity "
            "policy (%s)",
            esp_err_to_name(motion_monitor_error)
        );
    }

    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << kBootButton,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    result = gpio_config(&config);
    if (result != ESP_OK) {
        set_input_component(s_inputs.boot_button, false, result);
        set_input_component(
            s_inputs.power_button,
            false,
            ESP_ERR_INVALID_STATE
        );
        publish_inputs();
        stop_motion_monitor_after_init_failure("BOOT GPIO initialization");
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
        stop_motion_monitor_after_init_failure("ESP power policy");
        s_initialized.store(false, std::memory_order_release);
        return result;
    }
#endif

    s_inputs.wakeup_cause = esp_sleep_get_wakeup_causes();
    const TickType_t now = xTaskGetTickCount();
    reset_boot_arm_state(now);
    set_input_component(s_inputs.boot_button, true, ESP_OK);
    result = qsee_hardware_take_power_key_events(
        &s_initial_pkey_short_presses,
        &s_initial_pkey_long_presses
    );
    set_input_component(
        s_inputs.power_button,
        result == ESP_OK,
        result
    );
    if (result != ESP_OK) {
        publish_inputs();
        stop_motion_monitor_after_init_failure("AXP2101 PKEY initialization");
        s_initialized.store(false, std::memory_order_release);
        ESP_LOGE(
            kTag,
            "AXP2101 PKEY initialization failed (%s)",
            esp_err_to_name(result)
        );
        return result;
    }
    s_last_pkey_error = ESP_OK;

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
        const esp_err_t effective_expander_error =
            expander_error != ESP_OK
                ? expander_error
                : ESP_ERR_INVALID_STATE;
        ESP_LOGW(
            kTag,
            "I/O expander unavailable; continuing with direct BOOT and "
            "AXP2101 PKEY inputs (%s)",
            esp_err_to_name(effective_expander_error)
        );
    }

    s_state = qsee::DeviceOperatingState::Active;
    s_reason = qsee::DeviceStateReason::Startup;
    s_audio_suspended = false;
    s_fullscreen_active = false;
    s_command_arc_visible = true;
    s_status_arc_visible = true;
    s_display_stage =
        qsee::UiHardwareSnapshot::DisplayPowerMode::Active;
    s_transition_sequence = 0;
    s_last_activity_at_us = esp_timer_get_time();
    begin_motion_grace(s_last_activity_at_us);
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
        stop_motion_monitor_after_init_failure("Power task creation");
        s_initialized.store(false, std::memory_order_release);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        kTag,
        "Power service ready: BOOT=GPIO0 PWR=AXP2101-PKEY motion-wake=%s, "
        "timers=%lu/%lu/%lu/+%lu s",
        s_motion_monitor_started ? "ready" : "unavailable",
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
    s_pending_wakeup_cause.store(
        esp_sleep_get_wakeup_causes(),
        std::memory_order_relaxed
    );
    s_boot_rearm_requested.store(true, std::memory_order_release);
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
