/*
 * Single power-management service for buttons, inactivity, display, audio,
 * and motion wake.
 */

#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_liveness.h"
#include "LUNASHELL_power_policy.hpp"
#include "LUNASHELL_scene_standby_policy.hpp"
#include "LUNASHELL_build_mode.hpp"
#include "LUNASHELL_settings.hpp"
#include "LUNASHELL_sleep.hpp"
#include "LUNASHELL_button_hold.hpp"
#include "LUNASHELL_logger.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "LUNASHELL_power_manager";
constexpr gpio_num_t kBootButton = GPIO_NUM_0;
constexpr TickType_t kPollTicks = pdMS_TO_TICKS(20);
// Debug keeps motion observable at 10 Hz. Release enters Deep-sleep after
// successful standby; incomplete standby retains a slower software cadence.
constexpr TickType_t kStandbyPollTicks = pdMS_TO_TICKS(
    LUNASHELL::build_mode::kDevelopmentDiagnosticsEnabled ? 100 : 1000);
constexpr TickType_t kPolicyTicks = pdMS_TO_TICKS(100);
constexpr TickType_t kUiQuiescenceTimeoutTicks = pdMS_TO_TICKS(1500);
constexpr TickType_t kDebounceTicks = pdMS_TO_TICKS(30);
constexpr TickType_t kBootArmReleaseTicks = pdMS_TO_TICKS(500);
constexpr TickType_t kBootResetPressTicks = pdMS_TO_TICKS(3000);
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
constexpr int64_t kPowerResetPressUs = 3000000;
constexpr int64_t kPowerDoublePressWindowUs = 500000;
#endif
constexpr int64_t kPowerKeyFreshnessUs = 500000;
constexpr uint32_t kTaskStackBytes = 8192;
// These copies stay live across nested wake calls on the internal task stack.
static_assert(sizeof(LUNASHELL::PowerControlObservation) <= 768,
              "Review the complete power-task stack budget before growing its inputs");
static_assert(sizeof(LUNASHELL::DisplayPolicyObservation) <= 128,
              "Keep nested display-policy reads bounded on the power task");
constexpr uint32_t kDisplayFadeDurationMs = LUNASHELL::power_policy::kDisplayFadeDurationMs;
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
constexpr int64_t kLegAlertRepeatUs = 3000000LL;
constexpr uint32_t kLegAudioSampleRate = 16000;
constexpr size_t kLegAudioCapacity = 12480;
constexpr float kLegCuePcmAmplitude = 12000.0F;
constexpr int kLegCueVolumePercent = 95;
#endif
constexpr float kMotionAccelerationEnterMps2 =
    LUNASHELL::power_policy::kMotionWakeThresholdMg * 0.00980665F;
constexpr float kMotionAccelerationQuietMps2 = 0.40F;
constexpr float kMotionAngularVelocityQuietDps = 2.0F;
constexpr uint32_t kMotionEnterSampleCount = 3;
constexpr uint32_t kMotionQuietSampleCount = 3;
constexpr int64_t kMotionCooldownUs = 2LL * 1000LL * 1000LL;
constexpr int64_t kMotionGraceUs = 2LL * 1000LL * 1000LL;
constexpr int64_t kPkeyErrorReportIntervalUs = 5LL * 1000LL * 1000LL;
// The USB Serial/JTAG endpoint may enumerate after the immediate wake logs.
// Keep several diagnostic reports pending long enough for a host monitor to reopen.
constexpr int64_t kDeferredStandbyTraceReportDelayUs =
    20LL * 1000LL * 1000LL;
constexpr uint32_t kDeferredStandbyTraceReportCount = 3U;
constexpr int kMaximumCpuFrequencyMhz = 240;
constexpr int kMinimumCpuFrequencyMhz = 80;
constexpr uint32_t kStandbyTraceMagic = 0x53545243U;  // "STRC"
constexpr uint32_t kStandbyTraceVersion = 1U;

TaskHandle_t s_task;
LUNASHELL::InputSnapshot s_inputs;
std::atomic_bool s_initialized{false};
std::atomic<LUNASHELL_display_lifecycle_callback_t> s_display_lifecycle_callback{nullptr};
std::atomic<LUNASHELL_power_short_press_callback_t> s_short_press_callback{nullptr};
std::atomic<uint32_t> s_ui_quiescence_requested_sequence{0};
std::atomic<uint32_t> s_ui_quiescence_acknowledged_sequence{0};
std::atomic<int32_t> s_ui_quiescence_result{ESP_ERR_INVALID_STATE};
std::atomic<uint32_t> s_activity_sequence{0};
std::atomic_bool s_leg_mode_enabled{false};
std::atomic_bool s_leg_wake_enabled{true};
std::atomic_bool s_shake_wake_enabled{true};
std::atomic_bool s_leg_sounds_enabled{true};
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
std::atomic<uint8_t> s_leg_pending_sound{0};
TaskHandle_t s_leg_audio_task;
int16_t *s_leg_audio_buffer;
#endif
std::atomic<int64_t> s_activity_at_us{0};
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
uint64_t s_consumed_wake_word_sequence;
int64_t s_wake_word_standby_entered_at_us;
#endif
std::atomic<LUNASHELL::DeviceStateReason> s_activity_reason{
    LUNASHELL::DeviceStateReason::Touch
};
std::atomic<uint32_t> s_command_timeout_seconds{
    LUNASHELL::power_policy::kDefaultCommandArcTimeoutSeconds
};
std::atomic<uint32_t> s_status_timeout_seconds{
    LUNASHELL::power_policy::kDefaultStatusArcTimeoutSeconds
};
std::atomic<uint32_t> s_dim_timeout_seconds{
    LUNASHELL::power_policy::kDefaultDisplayDimTimeoutSeconds
};
std::atomic<uint32_t> s_screen_sleep_delay_seconds{
    LUNASHELL::power_policy::kDefaultScreenSleepDelaySeconds
};
std::atomic<uint32_t> s_screen_idle_timeout_seconds{
    LUNASHELL::power_policy::kDefaultScreenIdleTimeoutSeconds
};

struct SceneStandbyRequest {
    LUNASHELL::power_policy::SceneStandbyOverride policy{};
    uint32_t sequence = 0;
};
// Only this bounded intent crosses tasks. Display and activity state remain power-owned.
portMUX_TYPE s_scene_standby_mux = portMUX_INITIALIZER_UNLOCKED;
SceneStandbyRequest s_scene_standby_request;
uint32_t s_consumed_scene_standby_sequence = 0;

// No parameters: copy one complete scene intent without holding a lock across I/O.
SceneStandbyRequest read_scene_standby_request()
{
    portENTER_CRITICAL(&s_scene_standby_mux);
    const SceneStandbyRequest request = s_scene_standby_request;
    portEXIT_CRITICAL(&s_scene_standby_mux);
    return request;
}

LUNASHELL::DeviceOperatingState s_state =
    LUNASHELL::DeviceOperatingState::Active;
LUNASHELL::DeviceStateReason s_reason =
    LUNASHELL::DeviceStateReason::Startup;
LUNASHELL::UiHardwareSnapshot::DisplayPowerMode s_display_stage =
    LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active;
bool s_audio_suspended;
bool s_fullscreen_active;
bool s_command_arc_visible = true;
bool s_status_arc_visible = true;
uint32_t s_transition_sequence;
uint32_t s_consumed_activity_sequence;
int64_t s_last_activity_at_us;
int64_t s_last_imu_sample_us;
int64_t s_leg_last_imu_sample_us;
float s_leg_previous_acceleration_mps2[3] = {};
float s_leg_previous_angular_velocity_dps[3] = {};
bool s_leg_previous_valid;
bool s_leg_mode_observed;
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
int64_t s_leg_last_alert_at_us;
uint8_t s_leg_last_alert_level;
#endif
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
esp_err_t s_motion_monitor_error = ESP_ERR_INVALID_STATE;
bool s_boot_armed;
bool s_boot_release_tracking;
bool s_standby_poll_observed;
int64_t s_deferred_standby_trace_report_at_us;
uint32_t s_deferred_standby_trace_reports_remaining;
TickType_t s_boot_release_stable_at;
std::atomic_bool s_boot_rearm_requested{false};
std::atomic<uint32_t> s_pending_wakeup_cause{0};

struct StandbyResourceStatus {
    bool applied = false;
    bool consistent = true;
    bool unsupported = false;
};

struct OptionalResumeErrors {
    esp_err_t storage = ESP_OK;
    esp_err_t wireless = ESP_OK;

    // No parameters: retain the first optional failure in restoration order.
    esp_err_t first_error() const
    {
        return storage != ESP_OK ? storage : wireless;
    }
};

struct StandbyResourceState {
    StandbyResourceStatus full_snapshots;
    StandbyResourceStatus ui_sampling;
    StandbyResourceStatus gps_low_power;
    StandbyResourceStatus distance;
    StandbyResourceStatus storage;
    StandbyResourceStatus imu_low_power;
    StandbyResourceStatus motion_wake_cadence;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    StandbyResourceStatus voice_afe;
#endif
    StandbyResourceStatus base_audio;
    bool display_off = false;
    bool display_consistent = true;
    StandbyResourceStatus lvgl_paused;
#if !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    StandbyResourceStatus wireless;
    StandbyResourceStatus standalone_display;
#endif
};

StandbyResourceState s_standby_resources;
// Routine activity must not erase a radio restore failure before actual recovery.
std::atomic<esp_err_t> s_wireless_resume_error{ESP_OK};
#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
std::atomic<uint32_t> s_light_sleep_cycle_count{0};
std::atomic<uint32_t> s_light_sleep_total_ms{0};
uint32_t s_standby_light_sleep_cycle_base;
uint32_t s_standby_light_sleep_total_ms_base;
int64_t s_standby_started_at_us;
bool s_light_sleep_callback_registered;
#endif

enum class StandbyTracePhase : uint32_t {
    None = 0,
    EntryRequested,
    UiQuiesced,
    ResourcesSuspending,
    RadiosStopping,
    RadiosStopped,
    StandbyEntered,
    WakeRequested,
    ResourcesResuming,
    Awake,
    EntryFailed,
    WakeFailed,
};

// Retain the most recent PWR transition across software resets and automatic
// Light-sleep. It is intentionally diagnostic-only: no flash write is made
// on a user PWR press, and a lost RTC domain is reported as unavailable.
struct StandbyTrace {
    uint32_t magic;
    uint32_t version;
    uint32_t transition_sequence;
    uint32_t phase;
    int32_t error;
    int64_t recorded_at_us;
};

RTC_NOINIT_ATTR StandbyTrace s_retained_standby_trace;

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
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
LUNASHELL::ButtonHold s_power_reset_hold;
bool s_power_single_press_pending;
int64_t s_power_single_press_at_us;
LUNASHELL::DeviceOperatingState s_power_single_press_state =
    LUNASHELL::DeviceOperatingState::Active;
#endif
bool s_power_long_press_pending_release = false;

const char *standby_trace_phase_name(StandbyTracePhase phase)
{
    switch (phase) {
    case StandbyTracePhase::None:
        return "none";
    case StandbyTracePhase::EntryRequested:
        return "entry-requested";
    case StandbyTracePhase::UiQuiesced:
        return "ui-quiesced";
    case StandbyTracePhase::ResourcesSuspending:
        return "resources-suspending";
    case StandbyTracePhase::RadiosStopping:
        return "radios-stopping";
    case StandbyTracePhase::RadiosStopped:
        return "radios-stopped";
    case StandbyTracePhase::StandbyEntered:
        return "standby-entered";
    case StandbyTracePhase::WakeRequested:
        return "wake-requested";
    case StandbyTracePhase::ResourcesResuming:
        return "resources-resuming";
    case StandbyTracePhase::Awake:
        return "awake";
    case StandbyTracePhase::EntryFailed:
        return "entry-failed";
    case StandbyTracePhase::WakeFailed:
        return "wake-failed";
    default:
        return "invalid";
    }
}

// standby: Overall software standby target.
// Listening owns the audio transport even with the display and radios asleep.
bool audio_standby_target(bool standby)
{
#if CONFIG_LUNASHELL_BUILD_WAKE_WORD_STANDBY_LISTENING
    (void)standby;
    return false;
#else
    return standby;
#endif
}

// reason: Typed cause of the most recent power transition.
const char *device_state_reason_name(LUNASHELL::DeviceStateReason reason)
{
    switch (reason) {
    case LUNASHELL::DeviceStateReason::Startup:
        return "startup";
    case LUNASHELL::DeviceStateReason::BootButton:
        return "boot-button";
    case LUNASHELL::DeviceStateReason::PowerButton:
        return "power-button";
    case LUNASHELL::DeviceStateReason::Touch:
        return "touch";
    case LUNASHELL::DeviceStateReason::ImuMotion:
        return "imu-motion";
    case LUNASHELL::DeviceStateReason::WakeWord:
        return "wake-word";
    case LUNASHELL::DeviceStateReason::Inactivity:
        return "inactivity";
    }
    return "unknown";
}

void record_standby_trace(StandbyTracePhase phase, esp_err_t error)
{
    // Publish the magic last so an interrupted RTC write is never presented
    // as a coherent previous trace at the next boot.
    s_retained_standby_trace.magic = 0;
    s_retained_standby_trace.version = kStandbyTraceVersion;
    s_retained_standby_trace.transition_sequence = s_transition_sequence;
    s_retained_standby_trace.phase = static_cast<uint32_t>(phase);
    s_retained_standby_trace.error = static_cast<int32_t>(error);
    s_retained_standby_trace.recorded_at_us = esp_timer_get_time();
    s_retained_standby_trace.magic = kStandbyTraceMagic;
    ESP_LOGI(
        kTag,
        "Standby trace: phase=%s sequence=%lu error=%s",
        standby_trace_phase_name(phase),
        static_cast<unsigned long>(s_transition_sequence),
        esp_err_to_name(error)
    );
}

void log_retained_standby_trace()
{
    if (s_retained_standby_trace.magic != kStandbyTraceMagic ||
        s_retained_standby_trace.version != kStandbyTraceVersion) {
        ESP_LOGI(kTag, "Previous standby trace unavailable: RTC retention was reset");
        return;
    }
    const auto phase = static_cast<StandbyTracePhase>(
        s_retained_standby_trace.phase
    );
    ESP_LOGW(
        kTag,
        "Retained standby trace: phase=%s sequence=%lu error=%s at=%lld us reset=%u",
        standby_trace_phase_name(phase),
        static_cast<unsigned long>(s_retained_standby_trace.transition_sequence),
        esp_err_to_name(static_cast<esp_err_t>(s_retained_standby_trace.error)),
        static_cast<long long>(s_retained_standby_trace.recorded_at_us),
        static_cast<unsigned>(esp_reset_reason())
    );
}

// timeout: Typed timeout identity, independent of UI option ordering.
std::atomic<uint32_t> *timeout_storage(LUNASHELL_power_timeout_t timeout)
{
    switch (timeout) {
    case LUNASHELL_POWER_TIMEOUT_COMMAND_ARC:
        return &s_command_timeout_seconds;
    case LUNASHELL_POWER_TIMEOUT_STATUS_ARC:
        return &s_status_timeout_seconds;
    case LUNASHELL_POWER_TIMEOUT_DISPLAY_DIM:
        return &s_dim_timeout_seconds;
    case LUNASHELL_POWER_TIMEOUT_SCREEN_SLEEP:
        return &s_screen_sleep_delay_seconds;
    case LUNASHELL_POWER_TIMEOUT_SCREEN_IDLE:
        return &s_screen_idle_timeout_seconds;
    }
    return nullptr;
}

// timeout: Setting loaded strictly; only an absent key receives a persisted default.
esp_err_t load_timeout(LUNASHELL_power_timeout_t timeout)
{
    auto &settings = LUNASHELL::UserSettingsService::instance();
    uint32_t seconds = 0;
    esp_err_t error = settings.getUInt32(
        LUNASHELL::power_policy::settings_key(timeout),
        &seconds
    );
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        seconds = LUNASHELL::power_policy::default_timeout_seconds(timeout);
        ESP_LOGI(
            kTag,
            "%s timeout key '%s' absent; persisting first-boot default %lu s",
            LUNASHELL::power_policy::display_name(timeout),
            LUNASHELL::power_policy::settings_key(timeout),
            static_cast<unsigned long>(seconds)
        );
        error = settings.setUInt32(
            LUNASHELL::power_policy::settings_key(timeout),
            seconds
        );
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Timeout key '%s' read/persist failed (%s)",
                 LUNASHELL::power_policy::settings_key(timeout), esp_err_to_name(error));
        return error;
    }
    if (!LUNASHELL::power_policy::timeout_supported(timeout, seconds)) {
        ESP_LOGE(
            kTag,
            "%s timeout contains unsupported value %lu s",
            LUNASHELL::power_policy::display_name(timeout),
            static_cast<unsigned long>(seconds)
        );
        return ESP_ERR_INVALID_ARG;
    }
    timeout_storage(timeout)->store(seconds, std::memory_order_release);
    return ESP_OK;
}

void set_input_component(
    LUNASHELL::ComponentSnapshot &component,
    bool ready,
    esp_err_t error
)
{
    const int64_t attempted_at_us = esp_timer_get_time();
    component.health = ready
        ? LUNASHELL::DeviceHealth::Ready
        : (error == ESP_ERR_NOT_FOUND
               ? LUNASHELL::DeviceHealth::Missing
               : LUNASHELL::DeviceHealth::Degraded);
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
    LUNASHELL::DeviceSnapshotService::instance().reportInputs(s_inputs);
}

#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
// gesture: Exact validated Debug button gesture requesting this software restart.
[[noreturn]] void restart_preserving_logs(const char *gesture)
{
    ESP_LOGW(kTag, "%s: software restart (settings preserved)", gesture);
    LUNASHELL_logger_sd_status_t sd_status{};
    const esp_err_t status_error = LUNASHELL_logger_get_sd_status(&sd_status);
    if (status_error == ESP_OK && sd_status.enabled) {
        // The existing writer drains its queue and acknowledges close with a
        // one-second bound. Never reset the PMIC or erase settings/log files.
        const esp_err_t drain_error = LUNASHELL_logger_disable_sd();
        if (drain_error != ESP_OK) {
            ESP_LOGE(kTag, "Restart SD log drain failed (%s)", esp_err_to_name(drain_error));
        }
    } else if (status_error != ESP_OK) {
        ESP_LOGE(kTag, "Restart logger status failed (%s)", esp_err_to_name(status_error));
    }
    if (std::fflush(stdout) != 0) {
        ESP_LOGE(kTag, "Restart serial flush failed");
    }
    // Give the USB console a bounded transmit opportunity before esp_restart.
    // The host/SD files survive; the non-retained RAM log ring does not.
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}
#endif

// error: Real transition or settings error published with the current power state.
void publish_power(esp_err_t error)
{
    const esp_err_t publication_error = error != ESP_OK ? error :
        s_wireless_resume_error.load(std::memory_order_acquire);
    const SceneStandbyRequest scene_request = read_scene_standby_request();
    const uint32_t screen_idle_seconds = LUNASHELL::power_policy::scene_standby_timeout_seconds(
        s_screen_idle_timeout_seconds.load(std::memory_order_acquire), scene_request.policy);
    LUNASHELL::DeviceSnapshotService::instance().reportPowerManagement(
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
        screen_idle_seconds,
        s_transition_sequence,
        s_last_activity_at_us,
        publication_error
    );
}

// wifi: Already acquired canonical Wi-Fi lifecycle state.
// ble: Already acquired canonical BLE lifecycle state.
void reconcile_wireless_resume_error(const LUNASHELL::WifiSnapshot &wifi,
                                     const LUNASHELL::BleSnapshot &ble)
{
    if (s_wireless_resume_error.load(std::memory_order_acquire) == ESP_OK) {
        return;
    }
    const auto ready = [](const LUNASHELL::ComponentSnapshot &component) {
        return component.health == LUNASHELL::DeviceHealth::Ready &&
            component.data_valid && !component.data_stale &&
            component.last_error == ESP_OK;
    };
    (void)ready;
    (void)wifi;
    (void)ble;
    bool restored = true;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
    restored = ready(wifi) && wifi.started;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH
    restored = restored && ready(ble);
#endif
    if (restored) {
        s_wireless_resume_error.store(ESP_OK, std::memory_order_release);
        ESP_LOGI(kTag, "Optional wireless restore error cleared by verified canonical recovery");
        publish_power(ESP_OK);
    }
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

using StandbyTransition = esp_err_t (*)(bool);

// Keep the public audio flag aligned with both audio ownership layers.
// No parameters: recompute suspension from the compiled resource owners.
void refresh_audio_suspend_flag()
{
    s_audio_suspended =
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
        (s_standby_resources.voice_afe.consistent &&
         s_standby_resources.voice_afe.applied) ||
#endif
        (s_standby_resources.base_audio.consistent &&
         s_standby_resources.base_audio.applied);
}

// Bridge the BSP LVGL lifecycle only in profiles that own a UI worker.
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
// active: Receives whether the LVGL display progress watchdog is armed.
esp_err_t display_watch_active(bool *active)
{
    if (active == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    LUNASHELL_liveness_status_t status{};
    const esp_err_t result = LUNASHELL_liveness_get_status(&status);
    if (result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Display watchdog status read failed: %s",
            esp_err_to_name(result)
        );
        return result;
    }
    *active =
        (status.active_mask &
         (1U << static_cast<uint32_t>(LUNASHELL_LIVENESS_DISPLAY))) != 0U;
    return ESP_OK;
}

// standby: Whether to pause the UI renderer after the panel is dark.
esp_err_t set_lvgl_standby(bool standby)
{
    if (standby) {
        // Keep the display subscription armed while pause waits for the LVGL
        // worker. A wedged timer handler or panel flush therefore still trips
        // the Task WDT instead of turning standby into an indefinite wait.
        const esp_err_t pause_result = bsp_display_lvgl_pause();
        if (pause_result != ESP_OK) {
            return pause_result;
        }
        return LUNASHELL_liveness_watch_end(
            LUNASHELL_LIVENESS_DISPLAY,
            ESP_OK
        );
    }

    bool watch_active = false;
    esp_err_t result = display_watch_active(&watch_active);
    if (result != ESP_OK) {
        return result;
    }
    if (!watch_active) {
        result = LUNASHELL_liveness_watch_begin(
            LUNASHELL_LIVENESS_DISPLAY
        );
        if (result != ESP_OK) {
            return result;
        }
    }
    result = bsp_display_lvgl_resume();
    if (result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "LVGL resume failed with its watchdog still armed: %s",
            esp_err_to_name(result)
        );
        return result;
    }
    return ESP_OK;
}
#endif

#if !CONFIG_LUNASHELL_BUILD_ENABLE_UI && \
    (CONFIG_LUNASHELL_BUILD_ENABLE_WIFI || CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH)
// standby: Whether to stop or restore the radios selected by the standalone profile.
// Exception: Hardware teardown failures block standby; a separate budget failure stays published.
esp_err_t set_standalone_wireless_standby(bool standby)
{
    esp_err_t result = ESP_ERR_NOT_SUPPORTED;
#if CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH
    if (standby) {
        const auto closure = LUNASHELL_radios_stop();
        result = closure.cleanup_error;
        if (closure.dma_reservation_error != ESP_OK) {
            // Known-off hardware is safe for standby/degraded wake. The radio
            // owner publishes the budget error; the next Start must admit it anew.
            ESP_LOGE(kTag, "Standalone BLE standby reserve unavailable (%s); cleanup=%s",
                     esp_err_to_name(closure.dma_reservation_error),
                     esp_err_to_name(closure.cleanup_error));
        }
    } else {
#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI
        result = LUNASHELL_radios_init();
#else
        result = LUNASHELL_ble_init();
#endif
    }
#else
    result = standby ? LUNASHELL_wifi_stop() : LUNASHELL_wifi_init();
#endif
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Standalone radios %s failed (%s)",
                 standby ? "standby" : "restore", esp_err_to_name(result));
    }
    return result;
}
#endif

// Release reduces sampling during the transaction, then arms hardware WoM.
// Debug keeps the host monitor available for software wake and diagnostics.
// standby: Transactional standby request; Debug retains its normal 10-Hz observation.
esp_err_t set_motion_wake_standby(bool standby)
{
    if (!s_motion_monitor_started) {
        return ESP_OK;
    }
    const bool reduce_sampling = standby &&
        !LUNASHELL::build_mode::kDevelopmentDiagnosticsEnabled;
    return LUNASHELL_hardware_set_ui_motion_monitor_standby(reduce_sampling);
}

// Wireless services are deliberately one-way for the PWR standby policy:
// stopping them releases the Wi-Fi/BLE stacks and wake never restarts them
// without a new explicit Settings action.
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
// No parameters: stop radio hardware and keep memory-budget failure explicit.
// Exception: Return the original teardown or reservation error.
esp_err_t stop_radios_for_standby()
{
    record_standby_trace(StandbyTracePhase::RadiosStopping, ESP_OK);
    const auto closure = LUNASHELL_radios_stop();
    const esp_err_t error = closure.cleanup_error != ESP_OK ?
        closure.cleanup_error : closure.dma_reservation_error;
    if (error == ESP_ERR_NOT_SUPPORTED) {
        record_standby_trace(StandbyTracePhase::RadiosStopped, error);
        ESP_LOGI(
            kTag,
            "Wireless standby transition unavailable in this build profile"
        );
        return ESP_OK;
    }
    if (error != ESP_OK) {
        record_standby_trace(StandbyTracePhase::EntryFailed, error);
        ESP_LOGE(
            kTag,
            "Wireless standby transition failed (%s)",
            esp_err_to_name(error)
        );
        return error;
    }
    record_standby_trace(StandbyTracePhase::RadiosStopped, ESP_OK);
    ESP_LOGI(
        kTag,
        "Wi-Fi and BLE stopped for standby; explicit Settings action is required after wake"
    );
    return ESP_OK;
}
#endif

// Apply the display-off policy before pausing LVGL, or restore it last.
esp_err_t set_display_standby(bool standby)
{
    return LUNASHELL_display_fade_to_power_mode(
        standby ? LUNASHELL_DISPLAY_POWER_OFF : LUNASHELL_DISPLAY_POWER_ACTIVE,
        kDisplayFadeDurationMs
    );
}

// Keep a failed fade from leaving the panel between active and display-off.
// target: Whether the display must end in the standby panel-off state.
esp_err_t transition_display_resource(bool target)
{
    if (s_standby_resources.display_consistent &&
        s_standby_resources.display_off == target) {
        return ESP_OK;
    }
    const esp_err_t error = set_display_standby(target);
#if !CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    if (error == ESP_ERR_NOT_SUPPORTED) {
        s_standby_resources.display_off = false;
        s_standby_resources.display_consistent = true;
        return ESP_OK;
    }
#endif
    if (error == ESP_OK) {
        s_standby_resources.display_off = target;
        s_standby_resources.display_consistent = true;
        return ESP_OK;
    }

    s_standby_resources.display_consistent = false;
    ESP_LOGE(
        kTag,
        "Display transition to %s failed (%s)",
        target ? "standby" : "active",
        esp_err_to_name(error)
    );
    const esp_err_t rollback_error = set_display_standby(!target);
    if (rollback_error == ESP_OK
#if !CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
        || rollback_error == ESP_ERR_NOT_SUPPORTED
#endif
    ) {
        s_standby_resources.display_off = !target;
        s_standby_resources.display_consistent = true;
    } else {
        ESP_LOGE(
            kTag,
            "Display rollback to %s failed (%s)",
            target ? "active" : "standby",
            esp_err_to_name(rollback_error)
        );
    }
    return error;
}

// Apply one optional resource transition while retaining its exact error.
esp_err_t transition_standby_resource(
    const char *component,
    bool target,
    StandbyResourceStatus &status,
    StandbyTransition transition
)
{
    if (status.unsupported ||
        (status.consistent && status.applied == target)) {
        return ESP_OK;
    }
    const esp_err_t error = transition(target);
    if (error == ESP_ERR_NOT_SUPPORTED) {
        // A build-off implementation is only classified as unavailable from
        // the known active baseline. NOT_SUPPORTED after a successful
        // suspension or an earlier failure cannot prove the hardware state.
        if (status.consistent && !status.applied && target) {
            status.unsupported = true;
            ESP_LOGI(
                kTag,
                "%s standby transition unavailable in this build profile",
                component
            );
            return ESP_OK;
        }
        status.consistent = false;
        ESP_LOGE(
            kTag,
            "%s transition to %s became unavailable after lifecycle use",
            component,
            target ? "standby" : "active"
        );
        return error;
    }
    if (error != ESP_OK) {
        // Component APIs attempt their own rollback, but their return value
        // cannot prove that a nested rollback succeeded. Force the outer
        // recovery to re-assert its target instead of trusting stale state.
        status.consistent = false;
        ESP_LOGE(
            kTag,
            "%s transition to %s failed (%s)",
            component,
            target ? "standby" : "active",
            esp_err_to_name(error)
        );
        return error;
    }
    status.applied = target;
    status.consistent = true;
    status.unsupported = false;
    refresh_audio_suspend_flag();
    return ESP_OK;
}

// Require every mandatory resource to have a known lifecycle state.
// target: Whether every selected resource must be in standby or active.
// allow_storage_degraded: Whether an explicitly optional SD failure may remain visible.
// allow_wireless_degraded: Whether a failed radio restore was followed by a verified stop.
bool standby_resources_match_target(
    bool target,
    bool allow_storage_degraded,
    bool allow_wireless_degraded
)
{
    const auto matches = [target](const StandbyResourceStatus &status) {
        return status.unsupported ||
               (status.consistent && status.applied == target);
    };
    const bool storage_matches =
        allow_storage_degraded || matches(s_standby_resources.storage);
#if !CONFIG_LUNASHELL_BUILD_ENABLE_UI && \
    (CONFIG_LUNASHELL_BUILD_ENABLE_WIFI || CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH)
    const auto &wireless = s_standby_resources.wireless;
    // A degraded active device may retain known-stopped radios, never an
    // uncertain partial initialization or an unconfirmed cleanup result.
    const bool wireless_matches = matches(wireless) ||
        (!target && allow_wireless_degraded && wireless.consistent &&
         wireless.applied && !wireless.unsupported);
#else
    (void)allow_wireless_degraded;
#endif
    const bool audio_target = audio_standby_target(target);
    const auto audio_matches = [audio_target](const StandbyResourceStatus &status) {
        return status.unsupported || (status.consistent && status.applied == audio_target);
    };
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    const bool display_matches =
        s_standby_resources.display_consistent &&
        s_standby_resources.display_off == target;
#else
    const bool display_matches = s_standby_resources.display_consistent;
#endif
    return matches(s_standby_resources.full_snapshots) &&
           matches(s_standby_resources.ui_sampling) &&
           matches(s_standby_resources.gps_low_power) &&
           matches(s_standby_resources.distance) &&
           storage_matches &&
           matches(s_standby_resources.imu_low_power) &&
           matches(s_standby_resources.motion_wake_cadence) &&
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
           audio_matches(s_standby_resources.voice_afe) &&
#endif
           audio_matches(s_standby_resources.base_audio) &&
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
           matches(s_standby_resources.lvgl_paused) &&
#else
#if CONFIG_LUNASHELL_BUILD_ENABLE_WIFI || CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH
           wireless_matches &&
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
           matches(s_standby_resources.standalone_display) &&
#endif
#endif
           display_matches;
}

// Move every active workload to its low-power state in dependency order.
// best_effort: Whether rollback should continue after preserving the first failure.
// Exception: Returns the first resource suspension failure.
esp_err_t suspend_standby_resources(bool best_effort)
{
    esp_err_t first_error = ESP_OK;
    const auto step = [&](const char *name,
                          StandbyResourceStatus &state,
                          StandbyTransition transition,
                          bool requested = true) {
        if (!best_effort && first_error != ESP_OK) {
            return;
        }
        const esp_err_t error = transition_standby_resource(
            name,
            requested,
            state,
            transition
        );
        if (first_error == ESP_OK && error != ESP_OK) {
            first_error = error;
        }
    };

    step(
        "Full Snapshot scheduler",
        s_standby_resources.full_snapshots,
        LUNASHELL_hardware_set_full_snapshot_power_suspended
    );
    step(
        "UI diagnostics and auto-rotation sampling",
        s_standby_resources.ui_sampling,
        LUNASHELL_hardware_set_ui_sampling_power_suspended
    );
    step(
        "GPS LC76G subsystem",
        s_standby_resources.gps_low_power,
        LUNASHELL_hardware_set_gps_power_suspended
    );
    step(
        "Distance acquisition",
        s_standby_resources.distance,
        LUNASHELL_distance_set_power_suspended
    );
    step(
        "Storage polling",
        s_standby_resources.storage,
        LUNASHELL_storage_set_power_suspended
    );
    step(
        "IMU profile",
        s_standby_resources.imu_low_power,
        LUNASHELL_hardware_set_imu_low_power
    );
    step(
        "IMU motion wake cadence",
        s_standby_resources.motion_wake_cadence,
        set_motion_wake_standby
    );
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    step(
        "Voice AFE",
        s_standby_resources.voice_afe,
        LUNASHELL_voice_afe_set_standby,
        audio_standby_target(true)
    );
#endif
    step(
        "Base audio transport",
        s_standby_resources.base_audio,
        LUNASHELL_audio_set_standby,
        audio_standby_target(true)
    );
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
    if (best_effort || first_error == ESP_OK) {
        const esp_err_t radio_error = stop_radios_for_standby();
        if (first_error == ESP_OK && radio_error != ESP_OK) {
            first_error = radio_error;
        }
    }
#elif CONFIG_LUNASHELL_BUILD_ENABLE_WIFI || CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH
    if (best_effort || first_error == ESP_OK) {
        // Pairing can restart radios after a degraded wake. Reassert the stop
        // through their owner rather than trusting the previous power cache.
        s_standby_resources.wireless.consistent = false;
        step("Standalone wireless services", s_standby_resources.wireless,
             set_standalone_wireless_standby);
    }
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    step("Standalone rectangle writes", s_standby_resources.standalone_display,
         LUNASHELL_display_runtime_set_suspended);
#endif
    if (best_effort || first_error == ESP_OK) {
        const esp_err_t display_error =
            transition_display_resource(true);
        if (first_error == ESP_OK && display_error != ESP_OK) {
            first_error = display_error;
        }
    }
    // A visible panel must never be frozen by pausing its renderer first.
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
    if (s_standby_resources.display_consistent &&
        s_standby_resources.display_off) {
        step(
            "LVGL worker",
            s_standby_resources.lvgl_paused,
            set_lvgl_standby
        );
    }
#endif
    refresh_audio_suspend_flag();
    if (!standby_resources_match_target(true, false, false)) {
        ESP_LOGE(
            kTag,
            "Standby resource recovery ended with an inconsistent state"
        );
        if (first_error == ESP_OK) {
            first_error = ESP_ERR_INVALID_STATE;
        }
    }
    return first_error;
}

// Restore producers before exposing the panel and preserve the first failure.
// best_effort: Whether rollback should continue after preserving the first failure.
// optional_errors: Receives real optional SD/radio failures, separate from mandatory recovery.
// Exception: Returns the original mandatory recovery failure.
esp_err_t resume_standby_resources(
    bool best_effort,
    OptionalResumeErrors &optional_errors
)
{
    esp_err_t first_error = ESP_OK;
    optional_errors = {};
    const auto step = [&](const char *name,
                          StandbyResourceStatus &state,
                          StandbyTransition transition) {
        if (!best_effort && first_error != ESP_OK) {
            return;
        }
        const esp_err_t error = transition_standby_resource(
            name,
            false,
            state,
            transition
        );
        if (first_error == ESP_OK && error != ESP_OK) {
            first_error = error;
        }
    };

#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
    step(
        "LVGL worker",
        s_standby_resources.lvgl_paused,
        set_lvgl_standby
    );
#endif
    step(
        "IMU motion wake cadence",
        s_standby_resources.motion_wake_cadence,
        set_motion_wake_standby
    );
    step(
        "IMU profile",
        s_standby_resources.imu_low_power,
        LUNASHELL_hardware_set_imu_low_power
    );
    step(
        "Distance acquisition",
        s_standby_resources.distance,
        LUNASHELL_distance_set_power_suspended
    );
    if (best_effort || first_error == ESP_OK) {
        const esp_err_t storage_error = transition_standby_resource(
            "Storage polling",
            false,
            s_standby_resources.storage,
            LUNASHELL_storage_set_power_suspended
        );
        if (storage_error != ESP_OK) {
            optional_errors.storage = storage_error;
            ESP_LOGW(
                kTag,
                "Wake continues with optional storage unavailable (%s)",
                esp_err_to_name(storage_error)
            );
        }
    }
    step(
        "Base audio transport",
        s_standby_resources.base_audio,
        LUNASHELL_audio_set_standby
    );
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
    step(
        "Voice AFE",
        s_standby_resources.voice_afe,
        LUNASHELL_voice_afe_set_standby
    );
#endif
    step(
        "UI diagnostics and auto-rotation sampling",
        s_standby_resources.ui_sampling,
        LUNASHELL_hardware_set_ui_sampling_power_suspended
    );
    step(
        "GPS LC76G subsystem",
        s_standby_resources.gps_low_power,
        LUNASHELL_hardware_set_gps_power_suspended
    );
    step(
        "Full Snapshot scheduler",
        s_standby_resources.full_snapshots,
        LUNASHELL_hardware_set_full_snapshot_power_suspended
    );
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
    ESP_LOGI(
        kTag,
        "Wake leaves Wi-Fi and BLE stopped; restart remains an explicit Settings action"
    );
#elif CONFIG_LUNASHELL_BUILD_ENABLE_WIFI || CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH
    if (best_effort || first_error == ESP_OK) {
        const esp_err_t radio_error = transition_standby_resource(
            "Standalone wireless services", false, s_standby_resources.wireless,
            set_standalone_wireless_standby);
        if (radio_error == ESP_OK) {
            s_wireless_resume_error.store(ESP_OK, std::memory_order_release);
        }
        if (radio_error != ESP_OK) {
            optional_errors.wireless = radio_error;
            // Initialization can fail after starting only one radio. Establish
            // a known stopped state before allowing Cody to wake without it.
            const esp_err_t cleanup_error = transition_standby_resource(
                "Standalone wireless services cleanup", true,
                s_standby_resources.wireless, set_standalone_wireless_standby);
            if (cleanup_error != ESP_OK) {
                if (first_error == ESP_OK) {
                    first_error = cleanup_error;
                }
                ESP_LOGE(kTag, "Optional radio restore failed (%s); cleanup failed (%s)",
                         esp_err_to_name(radio_error), esp_err_to_name(cleanup_error));
            } else {
                s_wireless_resume_error.store(radio_error, std::memory_order_release);
                ESP_LOGW(kTag, "Wake continues with radios verified stopped; restore error=%s",
                         esp_err_to_name(radio_error));
            }
        }
    }
#endif
    // Restore the standalone frame while dark, then expose it. A previous sleep
    // pose must never flash on screen before the consumer can prepare Wake.
    const auto display_lifecycle = s_display_lifecycle_callback.load(std::memory_order_acquire);
    const bool wake_frame_requested = first_error == ESP_OK && display_lifecycle != nullptr;
    if (wake_frame_requested) {
        // A dark frame needs RAM writes enabled while luminance remains zero.
        // This intermediate physical state must be reasserted during rollback.
        s_standby_resources.display_consistent = false;
        first_error = LUNASHELL_display_prepare_dark_frame();
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    if (first_error == ESP_OK && display_lifecycle == nullptr) {
        // Profiles without a lifecycle consumer retain the ordinary panel wake.
        first_error = transition_display_resource(false);
    }
    if (first_error == ESP_OK) {
        // The worker can draw immediately after this gate opens, so the panel
        // must already accept frames before releasing its parked renderer.
        step("Standalone rectangle writes", s_standby_resources.standalone_display,
             LUNASHELL_display_runtime_set_suspended);
    }
#endif
    if (wake_frame_requested) {
        if (first_error == ESP_OK) {
            first_error = display_lifecycle(false);
        }
        if (first_error != ESP_OK) {
            LUNASHELL::DeviceSnapshotService::instance().reportDisplayPowerMode(
                LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Unknown,
                first_error);
            ESP_LOGE(kTag, "Display Wake preparation failed (%s)", esp_err_to_name(first_error));
        }
    }
    // Keep the panel dark if any data producer or display consumer failed.
    if (first_error == ESP_OK) {
        first_error = transition_display_resource(false);
    }
    refresh_audio_suspend_flag();
    const bool storage_degraded = optional_errors.storage != ESP_OK;
    const bool wireless_degraded = optional_errors.wireless != ESP_OK;
    if (!standby_resources_match_target(false, storage_degraded, wireless_degraded)) {
        ESP_LOGE(
            kTag,
            "Active resource recovery ended with an inconsistent mandatory state"
        );
        if (first_error == ESP_OK) {
            first_error = ESP_ERR_INVALID_STATE;
        }
    }
    return first_error;
}

// Exception: Canonical read failure remains fatal, as with the former full snapshot read.
bool active_display_snapshot_diverged()
{
    LUNASHELL::DisplayPolicyObservation display;
    const esp_err_t read_error =
        LUNASHELL::DeviceSnapshotService::instance().readDisplayPolicyObservation(display);
    if (read_error != ESP_OK) {
        ESP_LOGE(kTag, "Active display policy read failed (%s)", esp_err_to_name(read_error));
        std::abort();
    }
    // A completed wake may have changed the panel after the loop's input read.
    // Keep this fresh, narrow read instead of nesting another full DeviceSnapshot.
    // component: Canonical applied-state health, with its original operation result.
    const auto ready = [](const LUNASHELL::ComponentSnapshot &component) {
        return component.health == LUNASHELL::DeviceHealth::Ready &&
               component.last_error == ESP_OK;
    };
    if (ready(display.display_power_mode) &&
        display.display_power_mode_state !=
            LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active) {
        ESP_LOGW(
            kTag,
            "Active display policy disagrees with applied mode; restoring"
        );
        return true;
    }
    if (!ready(display.brightness)) {
        return false;
    }

    const uint32_t applied = display.brightness_percent;
    const uint32_t configured =
        display.configured_brightness_percent;
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

// Keep PWR and motion wake responsive while allowing longer standby sleeps.
TickType_t power_poll_delay()
{
    return s_state == LUNASHELL::DeviceOperatingState::Standby
        ? kStandbyPollTicks
        : kPollTicks;
}

#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
// Count completed automatic Light-sleep intervals without blocking in IDLE.
esp_err_t record_light_sleep_exit(int64_t sleep_time_us, void *)
{
    if (sleep_time_us <= 0) {
        return ESP_OK;
    }
    s_light_sleep_cycle_count.fetch_add(1U, std::memory_order_relaxed);
    s_light_sleep_total_ms.fetch_add(
        static_cast<uint32_t>(sleep_time_us / 1000LL),
        std::memory_order_relaxed
    );
    return ESP_OK;
}

// Record the automatic Light-sleep baseline for one standby interval.
void begin_standby_sleep_measurement()
{
    s_standby_light_sleep_cycle_base =
        s_light_sleep_cycle_count.load(std::memory_order_relaxed);
    s_standby_light_sleep_total_ms_base =
        s_light_sleep_total_ms.load(std::memory_order_relaxed);
    s_standby_started_at_us = esp_timer_get_time();
}

// Log the automatic Light-sleep residency captured before wake work starts.
void log_standby_sleep_measurement(
    uint32_t ending_cycle_count,
    uint32_t ending_total_ms,
    int64_t ended_at_us
)
{
    const uint32_t cycles =
        ending_cycle_count -
        s_standby_light_sleep_cycle_base;
    const uint32_t slept_ms =
        ending_total_ms -
        s_standby_light_sleep_total_ms_base;
    const int64_t elapsed_us = ended_at_us - s_standby_started_at_us;
    const uint32_t elapsed_ms = elapsed_us > 0
        ? static_cast<uint32_t>(elapsed_us / 1000LL)
        : 0U;
    const uint32_t residency_percent = elapsed_ms > 0U
        ? static_cast<uint32_t>(
              std::min<uint64_t>(
                  100U,
                  (static_cast<uint64_t>(slept_ms) * 100U) / elapsed_ms
              )
          )
        : 0U;
    ESP_LOGI(
        kTag,
        "Standby automatic Light-sleep: cycles=%lu slept=%lu/%lu ms (%lu%%)",
        static_cast<unsigned long>(cycles),
        static_cast<unsigned long>(slept_ms),
        static_cast<unsigned long>(elapsed_ms),
        static_cast<unsigned long>(residency_percent)
    );
}

// Register the residency counter once even if startup is retried after failure.
esp_err_t ensure_light_sleep_callback_registered()
{
    if (s_light_sleep_callback_registered) {
        return ESP_OK;
    }
    esp_pm_sleep_cbs_register_config_t callback_config = {
        .enter_cb = nullptr,
        .exit_cb = record_light_sleep_exit,
        .enter_cb_user_arg = nullptr,
        .exit_cb_user_arg = nullptr,
        .enter_cb_prior = 0,
        .exit_cb_prior = 0,
    };
    const esp_err_t error =
        esp_pm_light_sleep_register_cbs(&callback_config);
    if (error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Automatic Light-sleep residency callback failed (%s)",
            esp_err_to_name(error)
        );
        return error;
    }
    s_light_sleep_callback_registered = true;
    return ESP_OK;
}
#endif

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
        s_inputs.power_button.health == LUNASHELL::DeviceHealth::Ready &&
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

// reason: Observed activity that requires the display and input policy to be active.
// display_snapshot_diverged: Whether the applied panel state disagrees with active policy.
// exit_fullscreen: Whether the activity must also close the transient fullscreen state.
esp_err_t restore_active_policy(
    LUNASHELL::DeviceStateReason reason,
    bool display_snapshot_diverged,
    bool exit_fullscreen = false
)
{
    esp_err_t error = ESP_OK;
    if (display_snapshot_diverged ||
        s_display_stage !=
            LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active) {
        error = LUNASHELL_display_fade_to_power_mode(
            LUNASHELL_DISPLAY_POWER_ACTIVE,
            kDisplayFadeDurationMs
        );
        if (error != ESP_OK
#if !CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
            && error != ESP_ERR_NOT_SUPPORTED
#endif
        ) {
            publish_power(error);
            return error;
        }
    }
    if (exit_fullscreen) {
        s_fullscreen_active = false;
    }
    s_display_stage =
        LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active;
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
    if (s_state != LUNASHELL::DeviceOperatingState::Active) {
        ESP_LOGI(
            kTag,
            "BOOT short press ignored while device state is not active"
        );
        return ESP_OK;
    }

    const bool display_snapshot_diverged =
        active_display_snapshot_diverged();
    if (s_display_stage !=
            LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active ||
        display_snapshot_diverged) {
        ESP_LOGI(
            kTag,
            "BOOT short press waking display before fullscreen toggle"
        );
        return restore_active_policy(
            LUNASHELL::DeviceStateReason::BootButton,
            display_snapshot_diverged,
            true
        );
    }

    if (s_fullscreen_active ||
        (!s_command_arc_visible && !s_status_arc_visible)) {
        ESP_LOGI(kTag, "BOOT fullscreen disabled; restoring both arcs");
        return restore_active_policy(
            LUNASHELL::DeviceStateReason::BootButton,
            false,
            true
        );
    }

    s_fullscreen_active = true;
    s_command_arc_visible = false;
    s_status_arc_visible = false;
    s_reason = LUNASHELL::DeviceStateReason::BootButton;
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

// callback: Registered single display consumer, or nullptr for a profile without one.
// original_error: Real rejected Sleep operation, preserved even if recovery also fails.
// recovery_error: Receives the actual dark-frame, Wake-frame, or fade recovery result.
esp_err_t recover_display_after_rejected_sleep(
    LUNASHELL_display_lifecycle_callback_t callback,
    esp_err_t original_error,
    esp_err_t &recovery_error)
{
    recovery_error = ESP_OK;
    if (callback != nullptr) {
        recovery_error = LUNASHELL_display_prepare_dark_frame();
        if (recovery_error == ESP_OK) {
            recovery_error = callback(false);
        }
        if (recovery_error == ESP_OK) {
            // The original operation can fail before the panel was faded out.
            // Recovery still prepares Wake while dark, then restores visible Active.
            recovery_error = LUNASHELL_display_fade_to_power_mode(
                LUNASHELL_DISPLAY_POWER_ACTIVE, kDisplayFadeDurationMs);
        }
        if (recovery_error != ESP_OK) {
            LUNASHELL::DeviceSnapshotService::instance().reportDisplayPowerMode(
                LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Unknown,
                recovery_error);
            ESP_LOGE(kTag, "Display recovery after rejected Sleep failed (original=%s recovery=%s)",
                esp_err_to_name(original_error), esp_err_to_name(recovery_error));
        }
    }
    return original_error;
}

// transition_sequence: Exact standby generation whose GUI producers must quiesce.
// display_recovery_error: Real recovery result if the standalone Sleep callback rejects.
esp_err_t wait_for_ui_quiescence(
    uint32_t transition_sequence,
    esp_err_t &display_recovery_error)
{
    display_recovery_error = ESP_OK;
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
    const TickType_t started_at = xTaskGetTickCount();
    while (s_ui_quiescence_acknowledged_sequence.load(
               std::memory_order_acquire
           ) != transition_sequence) {
        const TickType_t elapsed = xTaskGetTickCount() - started_at;
        if (elapsed >= kUiQuiescenceTimeoutTicks) {
            s_ui_quiescence_requested_sequence.store(
                0,
                std::memory_order_release
            );
            ESP_LOGE(
                kTag,
                "UI standby acknowledgement timed out: sequence=%lu",
                static_cast<unsigned long>(transition_sequence)
            );
            return ESP_ERR_TIMEOUT;
        }
        (void)ulTaskNotifyTake(
            pdTRUE,
            kUiQuiescenceTimeoutTicks - elapsed
        );
    }
    const esp_err_t result = static_cast<esp_err_t>(
        s_ui_quiescence_result.load(std::memory_order_acquire)
    );
    s_ui_quiescence_requested_sequence.store(0, std::memory_order_release);
    (void)ulTaskNotifyTake(pdTRUE, 0);
    if (result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "UI standby quiescence failed: sequence=%lu error=%s",
            static_cast<unsigned long>(transition_sequence),
            esp_err_to_name(result)
        );
        return result;
    }
    ESP_LOGI(
        kTag,
        "UI standby acknowledged: sequence=%lu",
        static_cast<unsigned long>(transition_sequence)
    );
#else
    (void)transition_sequence;
    const auto display_lifecycle = s_display_lifecycle_callback.load(std::memory_order_acquire);
    if (display_lifecycle != nullptr) {
        const esp_err_t error = display_lifecycle(true);
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "Display Sleep preparation failed (%s)", esp_err_to_name(error));
            return recover_display_after_rejected_sleep(
                display_lifecycle, error, display_recovery_error);
        }
    }
#endif
    return ESP_OK;
}

// Coordinate UI quiescence and every hardware resource as one transaction.
// standby: Target standby or active state for all mandatory resources.
// reason: Real user/activity event that caused this transition.
// Exception: Returns the original transition error, retaining rollback state and failures.
esp_err_t set_standby(
    bool standby,
    LUNASHELL::DeviceStateReason reason
)
{
    if (s_state != LUNASHELL::DeviceOperatingState::Active &&
        s_state != LUNASHELL::DeviceOperatingState::Standby) {
        ESP_LOGE(
            kTag,
            "Standby request rejected during incomplete transition: state=%u",
            static_cast<unsigned>(s_state)
        );
        return ESP_ERR_INVALID_STATE;
    }
    const bool currently_standby =
        s_state == LUNASHELL::DeviceOperatingState::Standby;
    if (standby == currently_standby) {
        return ESP_OK;
    }

    if (standby) {
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
        const uint32_t activity_before_standby =
            s_activity_sequence.load(std::memory_order_acquire);
#endif
        s_deferred_standby_trace_report_at_us = 0;
        s_deferred_standby_trace_reports_remaining = 0;
        s_state = LUNASHELL::DeviceOperatingState::EnteringStandby;
        s_reason = reason;
        ++s_transition_sequence;
        record_standby_trace(StandbyTracePhase::EntryRequested, ESP_OK);
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI
        (void)ulTaskNotifyTake(pdTRUE, 0);
        s_ui_quiescence_acknowledged_sequence.store(
            0,
            std::memory_order_release
        );
        s_ui_quiescence_result.store(
            ESP_ERR_INVALID_STATE,
            std::memory_order_release
        );
        s_ui_quiescence_requested_sequence.store(
            s_transition_sequence,
            std::memory_order_release
        );
#endif
        publish_power(ESP_OK);

        esp_err_t display_recovery_error = ESP_OK;
        const esp_err_t ui_quiescence_error =
            wait_for_ui_quiescence(s_transition_sequence, display_recovery_error);
        if (ui_quiescence_error != ESP_OK) {
            record_standby_trace(
                StandbyTracePhase::EntryFailed,
                ui_quiescence_error
            );
            if (display_recovery_error == ESP_OK) {
                s_state = LUNASHELL::DeviceOperatingState::Active;
                if (s_display_lifecycle_callback.load(std::memory_order_acquire) != nullptr) {
                    s_display_stage = LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active;
                }
            } else {
                // The original Sleep error is preserved, but no successful
                // Active state may hide an incomplete Wake frame or dark panel.
                s_state = LUNASHELL::DeviceOperatingState::Waking;
                s_standby_resources.display_consistent = false;
                ESP_LOGE(kTag, "Standby rejected with display recovery incomplete (%s)",
                         esp_err_to_name(display_recovery_error));
            }
            publish_power(ui_quiescence_error);
            ESP_LOGE(
                kTag,
                "Standby entry rejected before hardware suspension (%s)",
                esp_err_to_name(ui_quiescence_error)
            );
            return ui_quiescence_error;
        }

        record_standby_trace(StandbyTracePhase::UiQuiesced, ESP_OK);
        record_standby_trace(StandbyTracePhase::ResourcesSuspending, ESP_OK);
        const esp_err_t transition_error =
            suspend_standby_resources(false);
        if (transition_error != ESP_OK) {
            record_standby_trace(
                StandbyTracePhase::EntryFailed,
                transition_error
            );
            OptionalResumeErrors optional_errors;
            const esp_err_t rollback_error =
                resume_standby_resources(true, optional_errors);
            if (rollback_error == ESP_OK) {
                s_state = LUNASHELL::DeviceOperatingState::Active;
                s_display_stage =
                    LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active;
                const esp_err_t recovery_publication_error =
                    optional_errors.first_error() != ESP_OK
                        ? optional_errors.first_error()
                        : transition_error;
                publish_power(recovery_publication_error);
                ESP_LOGE(
                    kTag,
                    "Standby entry failed; active state restored "
                    "(transition=%s storage=%s wireless=%s)",
                    esp_err_to_name(transition_error),
                    optional_errors.storage == ESP_OK
                        ? "ready"
                        : esp_err_to_name(optional_errors.storage),
                    esp_err_to_name(optional_errors.wireless)
                );
                return transition_error;
            }

            ESP_LOGE(
                kTag,
                "Active rollback after standby failure failed (%s)",
                esp_err_to_name(rollback_error)
            );
            const esp_err_t recovery_error =
                suspend_standby_resources(true);
            if (recovery_error == ESP_OK) {
                s_state = LUNASHELL::DeviceOperatingState::Standby;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
                s_wake_word_standby_entered_at_us = esp_timer_get_time();
#endif
                s_display_stage =
                    LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::DisplayOff;
                s_fullscreen_active = false;
                s_command_arc_visible = false;
                s_status_arc_visible = false;
                s_standby_poll_observed = false;
#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
                begin_standby_sleep_measurement();
#endif
            } else {
                ESP_LOGE(
                    kTag,
                    "Standby recovery also failed (%s); transition remains incomplete",
                    esp_err_to_name(recovery_error)
                );
            }
            publish_power(transition_error);
            return transition_error;
        }

        s_state = LUNASHELL::DeviceOperatingState::Standby;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
        s_wake_word_standby_entered_at_us = esp_timer_get_time();
#endif
        s_display_stage =
            LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::DisplayOff;
        s_fullscreen_active = false;
        s_command_arc_visible = false;
        s_status_arc_visible = false;
        s_standby_poll_observed = false;
#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
        begin_standby_sleep_measurement();
#endif
        publish_power(ESP_OK);
        record_standby_trace(StandbyTracePhase::StandbyEntered, ESP_OK);
        ESP_LOGI(
            kTag,
            "Device standby entered: reason=%s sequence=%lu",
            device_state_reason_name(reason),
            static_cast<unsigned long>(s_transition_sequence)
        );
        if constexpr (LUNASHELL::build_mode::kDeepSleepOnStandbyEnabled) {
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
            const bool activity_during_suspend =
                activity_before_standby != s_activity_sequence.load(std::memory_order_acquire);
            const esp_err_t sleep_error = activity_during_suspend
                ? ESP_ERR_SLEEP_REJECT : LUNASHELL_power_enter_deep_sleep(0);
            // Successful Deep-sleep restarts firmware and never returns here.
            // A rejected wake level or failed arm restores the full active transaction.
            if (!LUNASHELL::sleep::recovery_complete()) {
                s_state = LUNASHELL::DeviceOperatingState::EnteringStandby;
                publish_power(sleep_error);
                ESP_LOGE(kTag, "Deep-sleep recovery incomplete; active operation remains blocked");
                return sleep_error;
            }
            const esp_err_t rollback_error = set_standby(false, reason);
            if (rollback_error != ESP_OK) {
                ESP_LOGE(kTag, "Deep-sleep active rollback failed (%s)", esp_err_to_name(rollback_error));
            }
            publish_power(sleep_error);
            return sleep_error;
#endif
        }
        return ESP_OK;
    }

#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
    const uint32_t standby_ending_cycle_count =
        s_light_sleep_cycle_count.load(std::memory_order_relaxed);
    const uint32_t standby_ending_total_ms =
        s_light_sleep_total_ms.load(std::memory_order_relaxed);
    const int64_t standby_ended_at_us = esp_timer_get_time();
#endif
    record_standby_trace(StandbyTracePhase::WakeRequested, ESP_OK);
    s_state = LUNASHELL::DeviceOperatingState::Waking;
    s_reason = reason;
    publish_power(ESP_OK);
    OptionalResumeErrors optional_errors;
    record_standby_trace(StandbyTracePhase::ResourcesResuming, ESP_OK);
    const esp_err_t transition_error =
        resume_standby_resources(false, optional_errors);
    if (transition_error != ESP_OK) {
        record_standby_trace(
            StandbyTracePhase::WakeFailed,
            transition_error
        );
        const esp_err_t rollback_error =
            suspend_standby_resources(true);
        if (rollback_error == ESP_OK) {
            s_state = LUNASHELL::DeviceOperatingState::Standby;
#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
            s_wake_word_standby_entered_at_us = esp_timer_get_time();
#endif
            s_display_stage =
                LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::DisplayOff;
            s_fullscreen_active = false;
            s_command_arc_visible = false;
            s_status_arc_visible = false;
            publish_power(transition_error);
            ESP_LOGE(
                kTag,
                "Wake failed; standby state restored (%s)",
                esp_err_to_name(transition_error)
            );
            return transition_error;
        }

        ESP_LOGE(
            kTag,
            "Standby rollback after wake failure failed (%s)",
            esp_err_to_name(rollback_error)
        );
        OptionalResumeErrors recovery_optional_errors;
        esp_err_t recovery_publication_error = transition_error;
        const esp_err_t recovery_error =
            resume_standby_resources(true, recovery_optional_errors);
        if (recovery_error == ESP_OK) {
            s_state = LUNASHELL::DeviceOperatingState::Active;
            s_display_stage =
                LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active;
            s_fullscreen_active = false;
            s_command_arc_visible = true;
            s_status_arc_visible = true;
            s_last_activity_at_us = esp_timer_get_time();
            ++s_transition_sequence;
#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
            log_standby_sleep_measurement(
                standby_ending_cycle_count,
                standby_ending_total_ms,
                standby_ended_at_us
            );
#endif
            record_standby_trace(
                StandbyTracePhase::Awake,
                recovery_optional_errors.first_error()
            );
            if (recovery_optional_errors.first_error() != ESP_OK) {
                recovery_publication_error = recovery_optional_errors.first_error();
                ESP_LOGE(
                    kTag,
                    "Active recovery completed with optional resources "
                    "unavailable (transition=%s storage=%s wireless=%s)",
                    esp_err_to_name(transition_error),
                    esp_err_to_name(recovery_optional_errors.storage),
                    esp_err_to_name(recovery_optional_errors.wireless)
                );
            }
        } else {
            ESP_LOGE(
                kTag,
                "Active recovery also failed (%s); transition remains incomplete",
                esp_err_to_name(recovery_error)
            );
        }
        publish_power(recovery_publication_error);
        return transition_error;
    }

    s_state = LUNASHELL::DeviceOperatingState::Active;
    s_display_stage =
        LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active;
    s_fullscreen_active = false;
    s_command_arc_visible = true;
    s_status_arc_visible = true;
    s_last_activity_at_us = esp_timer_get_time();
    ++s_transition_sequence;
#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
    log_standby_sleep_measurement(
        standby_ending_cycle_count,
        standby_ending_total_ms,
        standby_ended_at_us
    );
#endif
    const esp_err_t optional_error = optional_errors.first_error();
    publish_power(optional_error);
    record_standby_trace(StandbyTracePhase::Awake, optional_error);
    if constexpr (LUNASHELL::build_mode::kDevelopmentDiagnosticsEnabled) {
        s_deferred_standby_trace_report_at_us =
            esp_timer_get_time() + kDeferredStandbyTraceReportDelayUs;
        s_deferred_standby_trace_reports_remaining =
            kDeferredStandbyTraceReportCount;
    }
    ESP_LOGI(
        kTag,
        "Device awake: reason=%s sequence=%lu storage_resume=%s wireless=%s",
        device_state_reason_name(reason),
        static_cast<unsigned long>(s_transition_sequence),
        esp_err_to_name(optional_errors.storage),
        esp_err_to_name(optional_errors.wireless)
    );
    return optional_error;
}

// imu: Original valid, fresh measured sample from the canonical snapshot owner.
bool qualified_motion_detected(const LUNASHELL::ImuSnapshot &imu)
{
    const bool valid =
        imu.health == LUNASHELL::DeviceHealth::Ready &&
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
                kMotionAccelerationEnterMps2;

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

struct LegMotionReading {
    uint8_t level = 0;
    float acceleration_step_mps2 = 0.0F;
    float angular_velocity_step_dps = 0.0F;
};

// Compare valid samples so a stationary gyro offset cannot become movement.
// This measures motion only, never force or weight bearing.
LegMotionReading leg_motion_level(const LUNASHELL::ImuSnapshot &imu)
{
    if (imu.health != LUNASHELL::DeviceHealth::Ready ||
        imu.last_error != ESP_OK || !imu.data_valid || imu.data_stale ||
        imu.last_success_at_us <= 0) {
        s_leg_previous_valid = false;
        return {};
    }
    if (imu.last_success_at_us == s_leg_last_imu_sample_us) {
        return {};
    }
    s_leg_last_imu_sample_us = imu.last_success_at_us;
    float peak_step = 0.0F;
    float peak_gyro_step = 0.0F;
    for (size_t axis = 0; axis < 3U; ++axis) {
        const float acceleration = imu.acceleration_mps2[axis];
        const float gyro = imu.angular_velocity_dps[axis];
        if (!std::isfinite(acceleration) || !std::isfinite(gyro)) {
            s_leg_previous_valid = false;
            ESP_LOGE(kTag, "Leg motion rejected non-finite IMU axis %u",
                     static_cast<unsigned>(axis));
            return {};
        }
    }
    const bool previous_valid = s_leg_previous_valid;
    for (size_t axis = 0; axis < 3U; ++axis) {
        const float acceleration = imu.acceleration_mps2[axis];
        const float gyro = imu.angular_velocity_dps[axis];
        if (previous_valid) {
            peak_step = std::max(
                peak_step,
                std::fabs(acceleration - s_leg_previous_acceleration_mps2[axis])
            );
            peak_gyro_step = std::max(
                peak_gyro_step,
                std::fabs(gyro - s_leg_previous_angular_velocity_dps[axis])
            );
        }
        s_leg_previous_acceleration_mps2[axis] = acceleration;
        s_leg_previous_angular_velocity_dps[axis] = gyro;
    }
    s_leg_previous_valid = true;
    if (!previous_valid) {
        return {};
    }
    const float intensity = std::max(peak_step, peak_gyro_step * 0.04F);
    if (intensity >= 3.0F) {
        return {3, peak_step, peak_gyro_step};
    }
    if (intensity >= 1.2F) {
        return {2, peak_step, peak_gyro_step};
    }
    return {static_cast<uint8_t>(intensity >= 0.25F ? 1 : 0),
            peak_step, peak_gyro_step};
}

#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
size_t synthesize_leg_cue(uint8_t level, int16_t *samples)
{
    const uint32_t frequencies[5] = {
        level == 1U ? 659U : level == 2U ? 784U : 988U,
        0U,
        level == 1U ? 0U : level == 2U ? 659U : 988U,
        0U,
        level == 3U ? 740U : 0U,
    };
    const uint32_t durations_ms[5] = {
        level == 3U ? 180U : 160U,
        level >= 2U ? 80U : 0U,
        level == 3U ? 180U : level == 2U ? 160U : 0U,
        level == 3U ? 80U : 0U,
        level == 3U ? 260U : 0U,
    };
    size_t offset = 0;
    for (size_t segment = 0; segment < 5U; ++segment) {
        const size_t length =
            static_cast<size_t>(durations_ms[segment]) *
            kLegAudioSampleRate / 1000U;
        for (size_t index = 0; index < length; ++index) {
            const uint32_t frequency = frequencies[segment];
            float value = 0.0F;
            if (frequency != 0U) {
                const float envelope = std::min({
                    1.0F,
                    static_cast<float>(index) / 192.0F,
                    static_cast<float>(length - index) / 640.0F,
                });
                value = kLegCuePcmAmplitude * envelope * std::sin(
                    6.2831853F * static_cast<float>(frequency) *
                    static_cast<float>(index) /
                    static_cast<float>(kLegAudioSampleRate)
                );
            }
            samples[offset++] = static_cast<int16_t>(std::lround(value));
        }
    }
    return offset;
}

void leg_audio_task(void *)
{
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const uint8_t level = s_leg_pending_sound.exchange(
            0, std::memory_order_acq_rel
        );
        if (level == 0U ||
            !s_leg_mode_enabled.load(std::memory_order_acquire) ||
            !s_leg_sounds_enabled.load(std::memory_order_acquire)) {
            continue;
        }
        const size_t count = synthesize_leg_cue(
            level, s_leg_audio_buffer
        );
        if (count == 0U || count > kLegAudioCapacity) {
            ESP_LOGE(kTag, "Leg sound level %u exceeded cue buffer",
                     static_cast<unsigned>(level));
            continue;
        }
        const esp_err_t result = LUNASHELL_audio_play_pcm16(
            s_leg_audio_buffer, count, kLegAudioSampleRate,
            kLegCueVolumePercent
        );
        if (result != ESP_OK) {
            ESP_LOGE(kTag, "Leg sound level %u playback failed (%s)",
                     static_cast<unsigned>(level), esp_err_to_name(result));
        }
    }
}

esp_err_t ensure_leg_audio_task()
{
    if (s_leg_audio_task != nullptr) {
        return ESP_OK;
    }
    s_leg_audio_buffer = static_cast<int16_t *>(heap_caps_malloc(
        kLegAudioCapacity * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    ));
    if (s_leg_audio_buffer == nullptr) {
        ESP_LOGE(kTag, "Leg audio PSRAM buffer allocation failed");
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(
            leg_audio_task, "leg_audio", 4096, nullptr,
            tskIDLE_PRIORITY + 2, &s_leg_audio_task
        ) != pdPASS) {
        heap_caps_free(s_leg_audio_buffer);
        s_leg_audio_buffer = nullptr;
        ESP_LOGE(kTag, "Leg audio worker creation failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void queue_leg_sound(const LegMotionReading &motion, int64_t now_us)
{
    const uint8_t level = motion.level;
    if (level == 0U || s_leg_audio_task == nullptr ||
        !s_leg_sounds_enabled.load(std::memory_order_acquire)) {
        return;
    }
    if (s_leg_last_alert_at_us != 0 &&
        now_us - s_leg_last_alert_at_us < kLegAlertRepeatUs &&
        level <= s_leg_last_alert_level) {
        return;
    }
    s_leg_last_alert_at_us = now_us;
    s_leg_last_alert_level = level;
    ESP_LOGI(kTag,
             "Leg movement cue level %u: accel-step=%.2f m/s2 gyro-step=%.2f dps",
             static_cast<unsigned>(level),
             static_cast<double>(motion.acceleration_step_mps2),
             static_cast<double>(motion.angular_velocity_step_dps));
    uint8_t pending = s_leg_pending_sound.load(std::memory_order_relaxed);
    while (pending < level && !s_leg_pending_sound.compare_exchange_weak(
        pending, level, std::memory_order_release,
        std::memory_order_relaxed
    )) {
    }
    xTaskNotifyGive(s_leg_audio_task);
}
#endif

// No parameters: dispatch one resolved short press to the active consumer or standby policy.
// Exception: Returns the original consumer or standby transition error.
esp_err_t dispatch_power_single_press()
{
    if (s_state == LUNASHELL::DeviceOperatingState::Active) {
        const auto callback = s_short_press_callback.load(std::memory_order_acquire);
        if (callback != nullptr) {
            bool claimed = false;
            const esp_err_t callback_error = callback(&claimed);
            if (claimed) {
                // An admitted countdown is activity, including on dimmed displays.
                // The idle policy runs later in this same power loop.
                s_last_activity_at_us = esp_timer_get_time();
                if (callback_error != ESP_OK) {
                    ESP_LOGE(kTag, "Active PWR consumer rejected press (%s)",
                             esp_err_to_name(callback_error));
                }
                return callback_error;
            }
            if (callback_error != ESP_OK) {
                ESP_LOGE(kTag, "Active PWR consumer failed without claiming press (%s)",
                         esp_err_to_name(callback_error));
                return callback_error;
            }
        }
    }
    ESP_LOGI(kTag, "PWR short press: toggling standby (count=%lu)",
             static_cast<unsigned long>(s_inputs.power_short_press_count));
    return set_standby(
        s_state != LUNASHELL::DeviceOperatingState::Standby,
        LUNASHELL::DeviceStateReason::PowerButton
    );
}

#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
// now_us: Monotonic acquisition-loop time for the bounded double-press window.
// new_short_press: A newly acquired completed click may resolve the previous expired click.
// Exception: Returns the original single-press action error after the window expires.
esp_err_t dispatch_expired_power_press(int64_t now_us, bool new_short_press)
{
    if (!s_power_single_press_pending) {
        return ESP_OK;
    }
    if (s_state != s_power_single_press_state) {
        s_power_single_press_pending = false;
        ESP_LOGW(kTag, "Pending Debug PWR click cancelled after operating-state change");
        return ESP_OK;
    }
    if (now_us - s_power_single_press_at_us <= kPowerDoublePressWindowUs) {
        return ESP_OK;
    }
    // A fresh known hold may still become a long press; do not start a capture mid-hold.
    if (!new_short_press && s_inputs.power_pressed_valid && s_inputs.power_pressed) {
        return ESP_OK;
    }
    s_power_single_press_pending = false;
    return dispatch_power_single_press();
}
#endif

// short_presses: Newly acquired discrete short-press IRQ count.
// long_presses: Newly acquired long-press IRQ count; release still enters standby.
// acquisition_error: Real owner acquisition status; invalid input cancels pending Debug gestures.
// Exception: Returns acquisition, counter, ambiguous-event or action errors without guessed gestures.
esp_err_t handle_power_key_events(
    uint32_t short_presses,
    uint32_t long_presses,
    esp_err_t acquisition_error
)
{
    if (UINT32_MAX - s_inputs.power_short_press_count < short_presses ||
        UINT32_MAX - s_inputs.power_long_press_count < long_presses) {
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
        s_power_single_press_pending = false;
#endif
        ESP_LOGE(kTag, "PKEY counter overflow rejected: short=%lu long=%lu",
                 static_cast<unsigned long>(short_presses), static_cast<unsigned long>(long_presses));
        return ESP_ERR_INVALID_SIZE;
    }
    s_inputs.power_short_press_count += short_presses;
    s_inputs.power_long_press_count += long_presses;
    if (acquisition_error != ESP_OK) {
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
        if (s_power_single_press_pending) {
            s_power_single_press_pending = false;
            ESP_LOGE(kTag, "Pending Debug PWR click rejected: input acquisition failed (%s)",
                     esp_err_to_name(acquisition_error));
        }
#endif
        return acquisition_error;
    }
    const int64_t now_us = esp_timer_get_time();
    if (short_presses != 0U || long_presses != 0U) {
        begin_motion_grace(now_us);
    }
    if (long_presses > 0U) {
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
        s_power_single_press_pending = false;
        ESP_LOGI(kTag, "PWR long press: release for standby; Debug 3-second hold restarts");
#else
        ESP_LOGI(kTag, "PWR long press: release for standby");
#endif
        s_power_long_press_pending_release = true;
        return ESP_OK;
    }
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
    if (short_presses > 1U) {
        s_power_single_press_pending = false;
        ESP_LOGE(kTag, "Debug PWR gesture rejected: batched clicks have ambiguous timing (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
        return ESP_ERR_INVALID_RESPONSE;
    }
    const esp_err_t pending_error = dispatch_expired_power_press(now_us, short_presses != 0U);
    if (pending_error != ESP_OK) {
        return pending_error;
    }
    if (short_presses == 0U) {
        return ESP_OK;
    }
    if (s_power_single_press_pending &&
        now_us - s_power_single_press_at_us <= kPowerDoublePressWindowUs) {
        s_power_single_press_pending = false;
        restart_preserving_logs("Debug PWR double press");
    }
    s_power_single_press_pending = true;
    s_power_single_press_at_us = now_us;
    s_power_single_press_state = s_state;
    s_last_activity_at_us = now_us;
    ESP_LOGI(kTag, "Debug PWR short press: waiting 500 ms for a second click");
    return ESP_OK;
#else
    return (short_presses & 1U) == 0U ? ESP_OK : dispatch_power_single_press();
#endif
}

// No parameters: apply real inactivity, or the active scene's independent entry interval.
esp_err_t apply_idle_policy()
{
    if (s_state != LUNASHELL::DeviceOperatingState::Active) {
        return ESP_OK;
    }
    const int64_t now_us = esp_timer_get_time();
    const int64_t elapsed_us = now_us - s_last_activity_at_us;
    const SceneStandbyRequest scene_request = read_scene_standby_request();
    const bool scene_policy_changed = scene_request.sequence != s_consumed_scene_standby_sequence;
    s_consumed_scene_standby_sequence = scene_request.sequence;
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
    LUNASHELL::UiHardwareSnapshot::DisplayPowerMode display_stage =
        LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active;
    const uint32_t dim_seconds =
        s_dim_timeout_seconds.load(std::memory_order_acquire);
    const uint32_t screen_idle_seconds = LUNASHELL::power_policy::scene_standby_timeout_seconds(
        s_screen_idle_timeout_seconds.load(std::memory_order_acquire), scene_request.policy);
    const bool standby_due = scene_request.policy.seconds != 0
        ? LUNASHELL::power_policy::scene_standby_expired(now_us, s_last_activity_at_us, scene_request.policy)
        : expired(screen_idle_seconds);
    if (standby_due) {
        const int64_t standby_requested_at_us = esp_timer_get_time();
        // A timeout commonly expires while the device is still moving. Reset
        // the qualified-motion baseline before suspending so that movement
        // already in progress cannot immediately undo the new standby state.
        begin_motion_grace(standby_requested_at_us);
        ESP_LOGI(
            kTag,
            "Inactivity timeout reached after %lu s; mode=%s fade=%lu ms",
            static_cast<unsigned long>(screen_idle_seconds),
            LUNASHELL::build_mode::kName,
            static_cast<unsigned long>(kDisplayFadeDurationMs)
        );
        return set_standby(
            true,
            LUNASHELL::DeviceStateReason::Inactivity
        );
    }
    // A scene's QR/text must stay readable for its full override interval. Standby
    // above still wins at the deadline, and zero restores the configured dim policy.
    if (scene_request.policy.seconds == 0 && expired(dim_seconds)) {
        display_stage =
            LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Dimmed;
    }

    bool changed =
        scene_policy_changed ||
        command_visible != s_command_arc_visible ||
        status_visible != s_status_arc_visible;
    if (display_stage != s_display_stage) {
        LUNASHELL_display_power_mode_t mode = LUNASHELL_DISPLAY_POWER_ACTIVE;
        if (display_stage ==
            LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Dimmed) {
            mode = LUNASHELL_DISPLAY_POWER_DIMMED;
        }
        const esp_err_t display_error =
            LUNASHELL_display_fade_to_power_mode(
                mode,
                kDisplayFadeDurationMs
            );
        if (display_error != ESP_OK
#if !CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
            && display_error != ESP_ERR_NOT_SUPPORTED
#endif
        ) {
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
                LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active
            ? "active"
            : s_display_stage ==
                      LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Dimmed
                  ? "dimmed"
                  : "off"
    );
    return ESP_OK;
}

// argument: Unused task context; this worker owns the published power timeline.
// Exception: Canonical read failure remains fatal; no policy uses a synthetic observation.
void power_task(void *argument)
{
    (void)argument;
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI && CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
    uint32_t consumed_touch_interrupt_sequence = bsp_touch_interrupt_sequence();
#endif
    TickType_t last_policy_tick = xTaskGetTickCount();
    UBaseType_t reported_stack_minimum = kTaskStackBytes;
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
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
            restart_preserving_logs("Debug BOOT held for 3 seconds");
#endif
        }
        if (now - last_policy_tick < kPolicyTicks) {
            vTaskDelay(power_poll_delay());
            continue;
        }
        last_policy_tick = now;

        if (s_state == LUNASHELL::DeviceOperatingState::Standby &&
            !s_standby_poll_observed) {
            s_standby_poll_observed = true;
            ESP_LOGI(
                kTag,
                "Standby control loop active: PWR poll interval=%lu ms",
                static_cast<unsigned long>(
                    kStandbyPollTicks * portTICK_PERIOD_MS
                )
            );
        }

        uint32_t short_presses = s_initial_pkey_short_presses;
        uint32_t long_presses = s_initial_pkey_long_presses;
        s_initial_pkey_short_presses = 0;
        s_initial_pkey_long_presses = 0;
        uint32_t sampled_short_presses = 0;
        uint32_t sampled_long_presses = 0;
        LUNASHELL_power_key_sample_t pkey_sample{};
        const esp_err_t pkey_error =
            LUNASHELL_hardware_take_power_key_events(
                &sampled_short_presses,
                &sampled_long_presses,
                &pkey_sample
            );
        update_power_key_component(pkey_error);
        if (pkey_error == ESP_OK) {
            short_presses += sampled_short_presses;
            long_presses += sampled_long_presses;
            s_inputs.power_pressed = pkey_sample.pressed;
            s_inputs.power_pressed_valid = pkey_sample.state_known;
            s_inputs.power_pressed_at_us = pkey_sample.pressed_at_us;
        }
        const esp_err_t pkey_transition_error =
            handle_power_key_events(short_presses, long_presses, pkey_error);
        // Publish every acquisition, including failed samples: consumers must
        // not time a hold against stale health from a previous successful poll.
        publish_inputs();
        if (pkey_transition_error != ESP_OK) {
            ESP_LOGE(
                kTag,
                "PWR event handling failed (%s)",
                esp_err_to_name(pkey_transition_error)
            );
        }

        LUNASHELL::PowerControlObservation snapshot;
        const esp_err_t observation_error =
            LUNASHELL::DeviceSnapshotService::instance().readPowerControlObservation(snapshot);
        if (observation_error != ESP_OK) {
            publish_power(observation_error);
            ESP_LOGE(kTag, "Power control observation failed (%s)",
                     esp_err_to_name(observation_error));
            std::abort();
        }
        reconcile_wireless_resume_error(snapshot.wifi, snapshot.ble);
        const auto &key = snapshot.input;
        const int64_t key_now_us = esp_timer_get_time();
        const bool key_current = key.power_button.data_valid && !key.power_button.data_stale &&
            key.power_button.last_error == ESP_OK &&
            key.power_button.last_success_at_us > 0 &&
            key_now_us >= key.power_button.last_success_at_us &&
            key_now_us - key.power_button.last_success_at_us <= kPowerKeyFreshnessUs;
#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG
        if (!key_current) {
            s_power_reset_hold.update(key.power_pressed, key.power_pressed_at_us,
                                      false, key_now_us, kPowerResetPressUs);
        }
        if (key_current && key.power_pressed_valid &&
            s_power_reset_hold.update(key.power_pressed, key.power_pressed_at_us,
                                      true, key_now_us, kPowerResetPressUs)) {
            restart_preserving_logs("Debug PWR held for 3 seconds");
        }
#endif
        if (key_current && key.power_pressed_valid && !key.power_pressed && s_power_long_press_pending_release) {
            s_power_long_press_pending_release = false;
            const esp_err_t standby_error =
                set_standby(true, LUNASHELL::DeviceStateReason::PowerButton);
            if (standby_error != ESP_OK) {
                ESP_LOGE(kTag, "PWR release standby failed (%s)", esp_err_to_name(standby_error));
            }
        }
        const bool leg_mode =
            s_leg_mode_enabled.load(std::memory_order_acquire);
        if (leg_mode != s_leg_mode_observed) {
            s_leg_mode_observed = leg_mode;
            s_leg_previous_valid = false;
            s_leg_last_imu_sample_us = 0;
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
            s_leg_last_alert_at_us = 0;
            s_leg_last_alert_level = 0;
#endif
        }
        const LegMotionReading leg_motion = s_motion_monitor_started && leg_mode
            ? leg_motion_level(snapshot.imu) : LegMotionReading{};
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
        if (leg_mode && leg_motion.level > 0U) {
            queue_leg_sound(leg_motion, esp_timer_get_time());
        }
#endif
        const bool qualified_motion = s_motion_monitor_started &&
            qualified_motion_detected(snapshot.imu);
        const bool wake_on_leg_motion =
            s_leg_wake_enabled.load(std::memory_order_acquire);
        if ((leg_mode && wake_on_leg_motion && leg_motion.level > 0) ||
            (!leg_mode && s_shake_wake_enabled.load(std::memory_order_acquire) && qualified_motion)) {
            if (s_state == LUNASHELL::DeviceOperatingState::Standby) {
                const esp_err_t wake_error = set_standby(
                    false,
                    LUNASHELL::DeviceStateReason::ImuMotion
                );
                if (wake_error != ESP_OK) {
                    ESP_LOGE(
                        kTag,
                        "IMU wake reported an error (%s)",
                        esp_err_to_name(wake_error)
                    );
                }
            } else if (
                s_state == LUNASHELL::DeviceOperatingState::Active) {
                s_last_activity_at_us = esp_timer_get_time();
                const bool display_snapshot_diverged =
                    active_display_snapshot_diverged();
                if ((!s_fullscreen_active &&
                     (!s_command_arc_visible ||
                      !s_status_arc_visible)) ||
                    s_display_stage !=
                        LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active ||
                    display_snapshot_diverged) {
                    const esp_err_t restore_error = restore_active_policy(
                        LUNASHELL::DeviceStateReason::ImuMotion,
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
                    s_reason = LUNASHELL::DeviceStateReason::ImuMotion;
                    publish_power(ESP_OK);
                }
            }
        }

        bool physical_touch_interrupt = false;
#if CONFIG_LUNASHELL_BUILD_ENABLE_UI && CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
        const uint32_t touch_interrupt_sequence = bsp_touch_interrupt_sequence();
        physical_touch_interrupt = touch_interrupt_sequence != consumed_touch_interrupt_sequence;
        consumed_touch_interrupt_sequence = touch_interrupt_sequence;
        if (physical_touch_interrupt && s_state == LUNASHELL::DeviceOperatingState::Standby) {
            // LVGL is parked, but its IRQ observer remains alive. This is wake intent,
            // not a measured touch contact; acquisition publishes the actual sample.
            const esp_err_t wake_error = set_standby(false, LUNASHELL::DeviceStateReason::Touch);
            if (wake_error != ESP_OK) {
                ESP_LOGE(kTag, "Touch IRQ wake reported an error (%s)", esp_err_to_name(wake_error));
            }
        }
#endif

#if CONFIG_LUNASHELL_BUILD_ENABLE_WAKE_WORD
        const auto &voice = snapshot.voice;
        if (voice.detection_sequence != s_consumed_wake_word_sequence) {
            // Consume historical events once, including expired or failed ones.
            // Resuming the display later must never replay an old detection.
            s_consumed_wake_word_sequence = voice.detection_sequence;
            const int64_t now_us = esp_timer_get_time();
            const bool current_detection = voice.detected_word == LUNASHELL::WakeWordId::Sophia &&
                voice.state == LUNASHELL::VoiceState::Listening && voice.data_valid &&
                !voice.data_stale && voice.health == LUNASHELL::DeviceHealth::Ready &&
                voice.last_error == ESP_OK && voice.detected_at_us > 0 &&
                now_us >= voice.detected_at_us && now_us - voice.detected_at_us <= 2000000;
            if (current_detection) {
                // Consume active/transition events too, but wake only from a
                // detection acquired after this settled standby began.
                if (s_state == LUNASHELL::DeviceOperatingState::Standby &&
                    voice.detected_at_us >= s_wake_word_standby_entered_at_us) {
                    const esp_err_t wake_error = set_standby(false, LUNASHELL::DeviceStateReason::WakeWord);
                    if (wake_error != ESP_OK) {
                        ESP_LOGE(kTag, "Wake word power transition failed (%s)", esp_err_to_name(wake_error));
                    }
                }
            }
        }
#endif

        const uint32_t activity_sequence =
            s_activity_sequence.load(std::memory_order_acquire);
        if (activity_sequence != s_consumed_activity_sequence) {
            s_consumed_activity_sequence = activity_sequence;
            const LUNASHELL::DeviceStateReason activity_reason =
                s_activity_reason.load(std::memory_order_relaxed);
            if (s_state == LUNASHELL::DeviceOperatingState::Standby) {
                const esp_err_t wake_error = set_standby(false, activity_reason);
                if (wake_error != ESP_OK) {
                    ESP_LOGE(kTag, "Input activity wake reported an error (%s)", esp_err_to_name(wake_error));
                }
            }
            if (s_state == LUNASHELL::DeviceOperatingState::Active) {
                // A pending pre-suspend event must not backdate a completed wake.
                s_last_activity_at_us = std::max(
                    s_last_activity_at_us, s_activity_at_us.load(std::memory_order_relaxed));
                const bool display_snapshot_diverged =
                    active_display_snapshot_diverged();
                if ((!s_fullscreen_active &&
                     (!s_command_arc_visible ||
                      !s_status_arc_visible)) ||
                    s_display_stage !=
                        LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active ||
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
        // Let the ordinary acquisition path consume a new IRQ before parking it.
        // A raw IRQ never becomes a fabricated touch sample or activity timestamp.
        // Do not enter Release Deep-sleep between the press edge and reset.
        // This is a current physical hold, not a fabricated activity timestamp.
        const bool power_hold_active = key_current && key.power_pressed;
        const esp_err_t idle_error = physical_touch_interrupt || power_hold_active
            ? ESP_OK : apply_idle_policy();
        if (idle_error != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Idle policy application failed (%s)",
                esp_err_to_name(idle_error)
            );
        }
        if (LUNASHELL::build_mode::kDevelopmentDiagnosticsEnabled &&
            s_deferred_standby_trace_report_at_us > 0 &&
            s_state == LUNASHELL::DeviceOperatingState::Active &&
            esp_timer_get_time() >= s_deferred_standby_trace_report_at_us) {
            s_deferred_standby_trace_report_at_us = 0;
            if (s_deferred_standby_trace_reports_remaining == 0U) {
                ESP_LOGE(kTag, "Deferred standby-trace report had no remaining count");
            } else {
                log_retained_standby_trace();
                --s_deferred_standby_trace_reports_remaining;
                if (s_deferred_standby_trace_reports_remaining > 0U) {
                    s_deferred_standby_trace_report_at_us =
                        esp_timer_get_time() + kDeferredStandbyTraceReportDelayUs;
                }
            }
        }
        if (LUNASHELL::build_mode::kDevelopmentDiagnosticsEnabled) {
            // IDF reports bytes here. Log only substantial new minima, after
            // the nested policy/wake paths have returned, to bound log traffic.
            const UBaseType_t minimum_free = uxTaskGetStackHighWaterMark(nullptr);
            if (reported_stack_minimum == kTaskStackBytes ||
                minimum_free + 256U <= reported_stack_minimum) {
                reported_stack_minimum = minimum_free;
                ESP_LOGI(kTag, "Power task stack minimum free: %lu/%lu bytes",
                         static_cast<unsigned long>(minimum_free),
                         static_cast<unsigned long>(kTaskStackBytes));
            }
        }
        vTaskDelay(power_poll_delay());
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

// operation: Display action skipped only when the display is absent from the profile.
// result: Original display result, preserved for every enabled display driver.
esp_err_t allow_disabled_display(const char *operation, esp_err_t result)
{
#if !CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    if (result == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(
            kTag,
            "Display transition '%s' skipped: display disabled by profile",
            operation
        );
        return ESP_OK;
    }
#else
    (void)operation;
#endif
    return result;
}

void stop_motion_monitor_after_init_failure(const char *stage)
{
    if (!s_motion_monitor_started) {
        return;
    }
    const esp_err_t error = LUNASHELL_hardware_set_ui_motion_monitor(false);
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

// callback: Single display consumer; publish once before the power owner can transition.
extern "C" esp_err_t LUNASHELL_power_manager_register_display_lifecycle(
    LUNASHELL_display_lifecycle_callback_t callback)
{
    if (callback == nullptr || s_initialized.load(std::memory_order_acquire)) {
        const esp_err_t error = callback == nullptr ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Display lifecycle registration rejected (%s)", esp_err_to_name(error));
        return error;
    }
    LUNASHELL_display_lifecycle_callback_t expected = nullptr;
    if (!s_display_lifecycle_callback.compare_exchange_strong(expected, callback,
            std::memory_order_release, std::memory_order_acquire)) {
        ESP_LOGE(kTag, "Duplicate display lifecycle owner rejected (%s)",
            esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

// Start the single owner after strict restoration of its persisted settings.
extern "C" esp_err_t LUNASHELL_power_manager_init(void)
{
    if (s_initialized.exchange(true, std::memory_order_acq_rel)) {
        return ESP_OK;
    }
    auto &settings = LUNASHELL::UserSettingsService::instance();
    esp_err_t result =
        settings.isInitialized() ? ESP_OK : settings.initialize();
    if (result == ESP_OK) {
        result = load_timeout(LUNASHELL_POWER_TIMEOUT_COMMAND_ARC);
    }
    if (result == ESP_OK) {
        result = load_timeout(LUNASHELL_POWER_TIMEOUT_STATUS_ARC);
    }
    if (result == ESP_OK) {
        result = load_timeout(LUNASHELL_POWER_TIMEOUT_DISPLAY_DIM);
    }
    if (result == ESP_OK) {
        result = load_timeout(LUNASHELL_POWER_TIMEOUT_SCREEN_SLEEP);
    }
    if (result == ESP_OK) {
        result = load_timeout(LUNASHELL_POWER_TIMEOUT_SCREEN_IDLE);
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

    log_retained_standby_trace();

    const esp_err_t motion_monitor_error =
        LUNASHELL_hardware_set_ui_motion_monitor(true);
    s_motion_monitor_error = motion_monitor_error;
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
    // Debug keeps USB reachable even after a consumer failure. Release may
    // sleep between functional wake checks; the unsafe USB SOF hook stays off.
    constexpr bool automatic_light_sleep =
        LUNASHELL::build_mode::kAutomaticLightSleepEnabled;
    const esp_pm_config_t power_policy = {
        .max_freq_mhz = kMaximumCpuFrequencyMhz,
        .min_freq_mhz = kMinimumCpuFrequencyMhz,
        .light_sleep_enable = automatic_light_sleep,
    };
    result = esp_pm_configure(&power_policy);
    if (result != ESP_OK) {
        stop_motion_monitor_after_init_failure("ESP power policy");
        s_initialized.store(false, std::memory_order_release);
        return result;
    }
#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
    result = ensure_light_sleep_callback_registered();
    if (result != ESP_OK) {
        stop_motion_monitor_after_init_failure(
            "ESP Light-sleep residency callback"
        );
        s_initialized.store(false, std::memory_order_release);
        return result;
    }
#endif
    ESP_LOGI(
        kTag,
        "ESP power policy active: CPU=%d..%d MHz automatic-light-sleep=%s",
        kMinimumCpuFrequencyMhz,
        kMaximumCpuFrequencyMhz,
        automatic_light_sleep ? "yes (Release)" : "no (Debug)"
    );
#if CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION
    ESP_LOGI(
        kTag,
        "USB Serial/JTAG host guard active: connected host holds "
        "ESP_PM_NO_LIGHT_SLEEP"
    );
#endif
#endif

    s_inputs.wakeup_cause = esp_sleep_get_wakeup_causes();
    const TickType_t now = xTaskGetTickCount();
    reset_boot_arm_state(now);
    set_input_component(s_inputs.boot_button, true, ESP_OK);
    LUNASHELL_power_key_sample_t initial_pkey_sample{};
    result = LUNASHELL_hardware_take_power_key_events(
        &s_initial_pkey_short_presses,
        &s_initial_pkey_long_presses,
        &initial_pkey_sample
    );
    s_inputs.power_pressed = initial_pkey_sample.pressed;
    s_inputs.power_pressed_valid = initial_pkey_sample.state_known;
    s_inputs.power_pressed_at_us = initial_pkey_sample.pressed_at_us;
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

    s_state = LUNASHELL::DeviceOperatingState::Active;
    s_reason = LUNASHELL::DeviceStateReason::Startup;
    s_ui_quiescence_requested_sequence.store(0, std::memory_order_release);
    s_ui_quiescence_acknowledged_sequence.store(0, std::memory_order_release);
    s_ui_quiescence_result.store(
        ESP_ERR_INVALID_STATE,
        std::memory_order_release
    );
    s_standby_resources = {};
    s_audio_suspended = false;
    s_fullscreen_active = false;
    s_command_arc_visible = true;
    s_status_arc_visible = true;
    s_standby_poll_observed = false;
    s_display_stage =
        LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Active;
    s_transition_sequence = 0;
    s_last_activity_at_us = esp_timer_get_time();
    begin_motion_grace(s_last_activity_at_us);
    s_consumed_activity_sequence =
        s_activity_sequence.load(std::memory_order_acquire);
    // Keep the only wake-control task independent of PSRAM/cache availability.
    if (xTaskCreatePinnedToCoreWithCaps(
            power_task,
            "LUNASHELL_power",
            kTaskStackBytes,
            nullptr,
            3,
            &s_task,
            tskNO_AFFINITY,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
        ) != pdPASS) {
        publish_power(ESP_ERR_NO_MEM);
        stop_motion_monitor_after_init_failure("Power task creation");
        s_initialized.store(false, std::memory_order_release);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        kTag,
        "Power service ready: BOOT=GPIO0 PWR=AXP2101-PKEY motion-wake=%s, "
        "timers=%lu/%lu/%lu/+%lu s, screen-idle=%lu s",
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
        ),
        static_cast<unsigned long>(
            s_screen_idle_timeout_seconds.load(std::memory_order_acquire)
        )
    );
    return ESP_OK;
}

extern "C" void LUNASHELL_power_manager_note_activity(void)
{
    s_activity_at_us.store(
        esp_timer_get_time(),
        std::memory_order_relaxed
    );
    s_activity_reason.store(
        LUNASHELL::DeviceStateReason::Touch,
        std::memory_order_relaxed
    );
    s_activity_sequence.fetch_add(1, std::memory_order_release);
}

// No parameters: keep a visible PWR countdown active under the existing idle policy.
extern "C" void LUNASHELL_power_manager_note_power_activity(void)
{
    s_activity_at_us.store(esp_timer_get_time(), std::memory_order_relaxed);
    s_activity_reason.store(LUNASHELL::DeviceStateReason::PowerButton,
                            std::memory_order_relaxed);
    s_activity_sequence.fetch_add(1, std::memory_order_release);
}

// seconds: Scene inactivity interval; zero immediately restores configured policy.
// Exception: Returns ESP_ERR_INVALID_STATE until the canonical power owner is initialized.
extern "C" esp_err_t LUNASHELL_power_manager_set_scene_standby_override(uint32_t seconds)
{
    if (!s_initialized.load(std::memory_order_acquire)) {
        ESP_LOGE(kTag, "Scene standby override rejected before power init (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    const int64_t entered_at_us = seconds == 0 ? 0 : esp_timer_get_time();
    portENTER_CRITICAL(&s_scene_standby_mux);
    s_scene_standby_request.policy = {seconds, entered_at_us};
    ++s_scene_standby_request.sequence;
    portEXIT_CRITICAL(&s_scene_standby_mux);
    // Publish intent only. In particular, never acquire the display lock or wake
    // the device from this consumer callback while the power owner awaits Sleep.
    ESP_LOGI(kTag, "Scene standby override: %lu s, entered_at=%lld us; settings unchanged",
             static_cast<unsigned long>(seconds), static_cast<long long>(entered_at_us));
    return ESP_OK;
}

// callback: One nonblocking active PWR consumer; standby and long-hold remain power-owned.
extern "C" esp_err_t LUNASHELL_power_manager_register_short_press(
    LUNASHELL_power_short_press_callback_t callback)
{
    if (callback == nullptr || s_initialized.load(std::memory_order_acquire)) {
        const esp_err_t error = callback == nullptr ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "PWR short-press registration rejected (%s)", esp_err_to_name(error));
        return error;
    }
    LUNASHELL_power_short_press_callback_t expected = nullptr;
    if (!s_short_press_callback.compare_exchange_strong(
            expected, callback, std::memory_order_release, std::memory_order_acquire)) {
        ESP_LOGE(kTag, "Duplicate PWR short-press consumer rejected (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

// enabled: Explicit non-Leg motion-wake policy, independent of IMU acquisition.
// The policy is available before worker startup and does not perform hardware I/O.
extern "C" esp_err_t LUNASHELL_power_manager_set_shake_wake_enabled(bool enabled)
{
    s_shake_wake_enabled.store(enabled, std::memory_order_release);
    return ESP_OK;
}

// active: Whether the existing Leg motion policy owns power decisions.
// wake_on_motion: Explicit Leg wake behavior, independent of Cody's motion gate.
extern "C" esp_err_t LUNASHELL_power_manager_set_leg_mode(
    bool active,
    bool wake_on_motion
)
{
    if (!s_initialized.load(std::memory_order_acquire)) {
        ESP_LOGE(kTag, "Leg display policy rejected before power init");
        return ESP_ERR_INVALID_STATE;
    }
    if (active && !s_motion_monitor_started) {
        ESP_LOGE(kTag, "Leg motion policy rejected: IMU monitor unavailable (%s)",
                 esp_err_to_name(s_motion_monitor_error));
        return s_motion_monitor_error;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    if (active && s_leg_sounds_enabled.load(std::memory_order_acquire)) {
        const esp_err_t audio_error = ensure_leg_audio_task();
        if (audio_error != ESP_OK) {
            return audio_error;
        }
    }
    if (!active) {
        s_leg_pending_sound.store(0, std::memory_order_release);
    }
#endif
    s_leg_wake_enabled.store(wake_on_motion, std::memory_order_release);
    s_leg_mode_enabled.store(active, std::memory_order_release);
    LUNASHELL_power_manager_note_activity();
    ESP_LOGI(kTag, "Leg display policy: active=%s wake=%s screen-idle=%lu s",
             active ? "yes" : "no", wake_on_motion ? "yes" : "no",
             static_cast<unsigned long>(s_screen_idle_timeout_seconds.load(std::memory_order_acquire)));
    return ESP_OK;
}

extern "C" esp_err_t LUNASHELL_power_manager_set_leg_sounds_enabled(bool enabled)
{
    if (!s_initialized.load(std::memory_order_acquire)) {
        ESP_LOGE(kTag, "Leg sound policy rejected before power init");
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311
    if (enabled && s_leg_mode_enabled.load(std::memory_order_acquire)) {
        const esp_err_t audio_error = ensure_leg_audio_task();
        if (audio_error != ESP_OK) {
            return audio_error;
        }
    }
    if (!enabled) {
        s_leg_pending_sound.store(0, std::memory_order_release);
    }
#else
    if (enabled) {
        ESP_LOGE(kTag, "Leg sounds unavailable: no selected speaker");
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif
    s_leg_sounds_enabled.store(enabled, std::memory_order_release);
    return ESP_OK;
}

// Accept only the UI result for the currently published standby generation.
extern "C" void LUNASHELL_power_manager_note_ui_standby_result(
    uint32_t transition_sequence,
    esp_err_t result
)
{
    const bool initialized = s_initialized.load(std::memory_order_acquire);
    const uint32_t requested_sequence =
        s_ui_quiescence_requested_sequence.load(std::memory_order_acquire);
    if (!initialized || transition_sequence == 0 ||
        requested_sequence != transition_sequence) {
        ESP_LOGD(
            kTag,
            "Ignored stale UI standby result: initialized=%s requested=%lu received=%lu result=%s",
            initialized ? "yes" : "no",
            static_cast<unsigned long>(requested_sequence),
            static_cast<unsigned long>(transition_sequence),
            esp_err_to_name(result)
        );
        return;
    }
    s_ui_quiescence_result.store(result, std::memory_order_release);
    s_ui_quiescence_acknowledged_sequence.store(
        transition_sequence,
        std::memory_order_release
    );
    if (s_task != nullptr) {
        xTaskNotifyGive(s_task);
    }
}

// timeout: Typed inactivity setting to update without rebuilding firmware.
// seconds: Supported value in seconds; zero explicitly disables this timeout.
extern "C" esp_err_t LUNASHELL_power_manager_set_timeout(
    LUNASHELL_power_timeout_t timeout,
    uint32_t seconds
)
{
    if (!s_initialized.load(std::memory_order_acquire)) {
        ESP_LOGE(kTag, "Timeout write rejected: power manager is not initialized (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    std::atomic<uint32_t> *storage = timeout_storage(timeout);
    if (storage == nullptr ||
        !LUNASHELL::power_policy::timeout_supported(timeout, seconds)) {
        ESP_LOGE(
            kTag,
            "%s timeout rejected unsupported value %lu s",
            LUNASHELL::power_policy::display_name(timeout),
            static_cast<unsigned long>(seconds)
        );
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t error =
        LUNASHELL::UserSettingsService::instance().setUInt32(
            LUNASHELL::power_policy::settings_key(timeout),
            seconds
        );
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Timeout key '%s' persistence failed (%s)",
                 LUNASHELL::power_policy::settings_key(timeout), esp_err_to_name(error));
        return error;
    }
    storage->store(seconds, std::memory_order_release);
    LUNASHELL_power_manager_note_activity();
    ESP_LOGI(
        kTag,
        "%s timeout updated: %lu s",
        LUNASHELL::power_policy::display_name(timeout),
        static_cast<unsigned long>(seconds)
    );
    return ESP_OK;
}

// timeout: Typed inactivity setting to read.
// seconds: Required output for the live value.
extern "C" esp_err_t LUNASHELL_power_manager_get_timeout(
    LUNASHELL_power_timeout_t timeout,
    uint32_t *seconds
)
{
    if (seconds == nullptr) {
        ESP_LOGE(kTag, "Timeout read rejected: output is null (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized.load(std::memory_order_acquire)) {
        ESP_LOGE(kTag, "Timeout read rejected: power manager is not initialized (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    std::atomic<uint32_t> *storage = timeout_storage(timeout);
    if (storage == nullptr) {
        ESP_LOGE(kTag, "Timeout read rejected: unknown identity %d (%s)",
                 static_cast<int>(timeout), esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    *seconds = storage->load(std::memory_order_acquire);
    return ESP_OK;
}

// wake_after_us: Optional timer deadline in microseconds; zero uses the configured GPIO wake.
extern "C" esp_err_t LUNASHELL_power_enter_light_sleep(uint64_t wake_after_us)
{
#if CONFIG_LUNASHELL_BUILD_WAKE_WORD_STANDBY_LISTENING
    ESP_LOGE(kTag, "Silicon sleep rejected while standby wake-word listening is enabled (%s)",
             esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#endif
    esp_err_t result = configure_wakeup(wake_after_us);
    if (result != ESP_OK) {
        return result;
    }
    esp_err_t display_recovery_error = ESP_OK;
    const auto display_lifecycle = s_display_lifecycle_callback.load(std::memory_order_acquire);
    if (display_lifecycle != nullptr) {
        result = display_lifecycle(true);
        if (result != ESP_OK) {
            ESP_LOGE(kTag, "Explicit Light-sleep display preparation failed (%s)", esp_err_to_name(result));
            return recover_display_after_rejected_sleep(
                display_lifecycle, result, display_recovery_error);
        }
    }
    result = allow_disabled_display(
        "light-sleep-off",
        LUNASHELL_display_fade_to_power_mode(
            LUNASHELL_DISPLAY_POWER_OFF,
            kDisplayFadeDurationMs
        )
    );
    if (result != ESP_OK) {
        return recover_display_after_rejected_sleep(
            display_lifecycle, result, display_recovery_error);
    }
    const esp_err_t sleep_result = esp_light_sleep_start();
    s_pending_wakeup_cause.store(
        esp_sleep_get_wakeup_causes(),
        std::memory_order_relaxed
    );
    s_boot_rearm_requested.store(true, std::memory_order_release);
    esp_err_t wake_frame_error = ESP_OK;
    if (display_lifecycle != nullptr) {
        wake_frame_error = LUNASHELL_display_prepare_dark_frame();
        if (wake_frame_error == ESP_OK) {
            wake_frame_error = display_lifecycle(false);
        }
        if (wake_frame_error != ESP_OK) {
            LUNASHELL::DeviceSnapshotService::instance().reportDisplayPowerMode(
                LUNASHELL::UiHardwareSnapshot::DisplayPowerMode::Unknown,
                wake_frame_error);
            ESP_LOGE(kTag, "Explicit Light-sleep Wake frame failed (%s)", esp_err_to_name(wake_frame_error));
        }
    }
    if (wake_frame_error != ESP_OK) {
        // Keep a failed wake presentation dark; never expose a stale sleep frame.
        return sleep_result != ESP_OK ? sleep_result : wake_frame_error;
    }
    const esp_err_t resume_error = allow_disabled_display(
        "light-sleep-resume",
        LUNASHELL_display_fade_to_power_mode(
            LUNASHELL_DISPLAY_POWER_ACTIVE,
            kDisplayFadeDurationMs
        )
    );
    if (sleep_result != ESP_OK) {
        return sleep_result;
    }
    return resume_error;
}

// wake_after_us: Optional timer in addition to touch/motion; zero adds no periodic wake.
extern "C" esp_err_t LUNASHELL_power_enter_deep_sleep(uint64_t wake_after_us)
{
#if CONFIG_LUNASHELL_BUILD_WAKE_WORD_STANDBY_LISTENING
    ESP_LOGE(kTag, "Deep-sleep rejected while wake-word standby listening is enabled (%s)",
             esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#endif
    if (!LUNASHELL::build_mode::kDeepSleepOnStandbyEnabled ||
        s_state != LUNASHELL::DeviceOperatingState::Standby ||
        xTaskGetCurrentTaskHandle() != s_task ||
        !standby_resources_match_target(true, false, false)) {
        ESP_LOGE(kTag, "Deep-sleep rejected: requires Release and confirmed standby on the power owner (%s)",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    // The standby transaction already retired the Sleep frame and faded out.
    // A second presentation here would target a renderer that is now suspended.
    return LUNASHELL::sleep::try_deep_sleep(wake_after_us);
}
