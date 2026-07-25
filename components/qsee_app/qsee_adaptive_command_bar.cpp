// Adaptive visibility policy for lower-arc UI commands.

#include "qsee_adaptive_command_bar.hpp"

#include <algorithm>
#include <iterator>

#include "esp_timer.h"
#include "qsee_settings.hpp"

namespace {

constexpr char kTimeoutSetting[] = "cmd_arc_s";
constexpr uint32_t kFadeDurationMs = 280;
constexpr uint32_t kRecentTouchWindowMs = 250;
constexpr float kAccelerationDeltaThresholdMps2 = 0.25F;
constexpr float kAngularVelocityThresholdDps = 2.5F;

bool isSupportedTimeout(uint32_t seconds)
{
    switch (seconds) {
    case 10:
    case 15:
    case 30:
    case 60:
    case 120:
    case 300:
    case 600:
        return true;
    default:
        return false;
    }
}

// Fade button descendants too, without allocating LVGL layered-opacity buffers.
void applyOpacityTree(lv_obj_t *object, lv_opa_t opacity)
{
    if (object == nullptr || !lv_obj_is_valid(object)) {
        return;
    }
    lv_obj_set_style_opa(object, opacity, LV_PART_MAIN);
    const uint32_t child_count = lv_obj_get_child_count(object);
    for (uint32_t index = 0; index < child_count; ++index) {
        applyOpacityTree(lv_obj_get_child(object, index), opacity);
    }
}

}  // namespace

namespace qsee {

// Load the setting once; runtime behavior remains available if NVS is degraded.
esp_err_t AdaptiveCommandBar::begin()
{
    auto &settings = UserSettingsService::instance();
    esp_err_t result = settings.initialize();
    if (result != ESP_OK) {
        return result;
    }

    uint32_t saved_seconds = kDefaultTimeoutSeconds;
    result = settings.getUInt32(
        kTimeoutSetting,
        kDefaultTimeoutSeconds,
        &saved_seconds
    );
    if (result != ESP_OK) {
        return result;
    }
    _timeout_seconds = isSupportedTimeout(saved_seconds)
        ? saved_seconds
        : kDefaultTimeoutSeconds;
    return ESP_OK;
}

void AdaptiveCommandBar::registerControl(lv_obj_t *control)
{
    if (control == nullptr ||
        std::find(_controls.begin(), _controls.end(), control) !=
            _controls.end()) {
        return;
    }
    _controls.push_back(control);
    applyOpacityTree(control, static_cast<lv_opa_t>(_opacity));
}

// A route entry always starts with usable, fully visible commands.
void AdaptiveCommandBar::activate(bool has_commands)
{
    lv_anim_delete(this, animationValue);
    _active = has_commands && !_controls.empty();
    _last_activity_us = esp_timer_get_time();
    _last_touch_inactive_ms = UINT32_MAX;
    _last_imu_sample_us = 0;
    _motion_baseline_ready = false;
    _animation_target = LV_OPA_COVER;
    applyOpacity(LV_OPA_COVER);
    setControlsClickable(true);
}

// Combine LVGL touch activity and immutable snapshot motion into one timer.
void AdaptiveCommandBar::update(
    const DeviceSnapshot &snapshot,
    uint32_t touch_inactive_ms
)
{
    if (!_active) {
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    const bool touch_activity =
        touch_inactive_ms <= kRecentTouchWindowMs ||
        (_last_touch_inactive_ms != UINT32_MAX &&
         touch_inactive_ms < _last_touch_inactive_ms);
    _last_touch_inactive_ms = touch_inactive_ms;

    if (touch_activity || detectMotion(snapshot.imu)) {
        noteActivity(now_us);
        return;
    }

    const int64_t timeout_us =
        static_cast<int64_t>(_timeout_seconds) * 1000000LL;
    if (now_us - _last_activity_us >= timeout_us &&
        _animation_target != LV_OPA_TRANSP) {
        animateTo(LV_OPA_TRANSP);
    }
}

// Persist only values inside the range exposed by the Settings dropdown.
esp_err_t AdaptiveCommandBar::setTimeoutSeconds(uint32_t seconds)
{
    if (!isSupportedTimeout(seconds)) {
        return ESP_ERR_INVALID_ARG;
    }

    _timeout_seconds = seconds;
    noteActivity(esp_timer_get_time());

    auto &settings = UserSettingsService::instance();
    if (!settings.isInitialized()) {
        const esp_err_t init_result = settings.initialize();
        if (init_result != ESP_OK) {
            return init_result;
        }
    }
    return settings.setUInt32(kTimeoutSetting, seconds);
}

uint32_t AdaptiveCommandBar::timeoutSeconds() const
{
    return _timeout_seconds;
}

bool AdaptiveCommandBar::isActive() const
{
    return _active;
}

void AdaptiveCommandBar::shutdown()
{
    lv_anim_delete(this, animationValue);
    _active = false;
    _controls.clear();
}

void AdaptiveCommandBar::animationValue(void *context, int32_t opacity)
{
    auto *controller = static_cast<AdaptiveCommandBar *>(context);
    if (controller != nullptr) {
        controller->applyOpacity(opacity);
    }
}

// Disable hit targets only after fade-out, never while a command is visible.
void AdaptiveCommandBar::animationCompleted(lv_anim_t *animation)
{
    auto *controller = static_cast<AdaptiveCommandBar *>(
        lv_anim_get_user_data(animation)
    );
    if (controller == nullptr) {
        return;
    }
    if (controller->_animation_target == LV_OPA_TRANSP) {
        controller->setControlsClickable(false);
    }
}

// Any activity makes commands clickable before their opacity starts rising.
void AdaptiveCommandBar::noteActivity(int64_t now_us)
{
    _last_activity_us = now_us;
    if (_animation_target != LV_OPA_COVER || _opacity != LV_OPA_COVER) {
        setControlsClickable(true);
        animateTo(LV_OPA_COVER);
    }
}

// Compare consecutive canonical IMU samples without reading the sensor directly.
bool AdaptiveCommandBar::detectMotion(const ImuSnapshot &imu)
{
    if (!imu.available || imu.sampled_at_us <= 0 ||
        imu.sampled_at_us == _last_imu_sample_us) {
        return false;
    }
    _last_imu_sample_us = imu.sampled_at_us;

    if (!_motion_baseline_ready) {
        std::copy(
            std::begin(imu.acceleration_mps2),
            std::end(imu.acceleration_mps2),
            _last_acceleration_mps2
        );
        _motion_baseline_ready = true;
        return false;
    }

    float acceleration_delta_squared = 0.0F;
    float angular_velocity_squared = 0.0F;
    for (size_t axis = 0; axis < 3U; ++axis) {
        const float acceleration_delta =
            imu.acceleration_mps2[axis] -
            _last_acceleration_mps2[axis];
        acceleration_delta_squared +=
            acceleration_delta * acceleration_delta;
        angular_velocity_squared +=
            imu.angular_velocity_dps[axis] *
            imu.angular_velocity_dps[axis];
        _last_acceleration_mps2[axis] =
            imu.acceleration_mps2[axis];
    }

    return acceleration_delta_squared >=
               kAccelerationDeltaThresholdMps2 *
                   kAccelerationDeltaThresholdMps2 ||
           angular_velocity_squared >=
               kAngularVelocityThresholdDps *
                   kAngularVelocityThresholdDps;
}

// Animate one shared opacity value so every command remains visually synchronized.
void AdaptiveCommandBar::animateTo(int32_t opacity)
{
    opacity = std::clamp(
        opacity,
        static_cast<int32_t>(LV_OPA_TRANSP),
        static_cast<int32_t>(LV_OPA_COVER)
    );
    lv_anim_delete(this, animationValue);
    _animation_target = opacity;

    if (_opacity == opacity) {
        if (opacity == LV_OPA_TRANSP) {
            setControlsClickable(false);
        }
        return;
    }

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, this);
    lv_anim_set_user_data(&animation, this);
    lv_anim_set_values(&animation, _opacity, opacity);
    lv_anim_set_duration(&animation, kFadeDurationMs);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&animation, animationValue);
    lv_anim_set_completed_cb(&animation, animationCompleted);
    (void)lv_anim_start(&animation);
}

void AdaptiveCommandBar::applyOpacity(int32_t opacity)
{
    _opacity = std::clamp(
        opacity,
        static_cast<int32_t>(LV_OPA_TRANSP),
        static_cast<int32_t>(LV_OPA_COVER)
    );
    for (lv_obj_t *control : _controls) {
        if (control != nullptr && lv_obj_is_valid(control)) {
            applyOpacityTree(
                control,
                static_cast<lv_opa_t>(_opacity)
            );
        }
    }
}

void AdaptiveCommandBar::setControlsClickable(bool clickable)
{
    for (lv_obj_t *control : _controls) {
        if (control == nullptr || !lv_obj_is_valid(control)) {
            continue;
        }
        if (clickable) {
            lv_obj_add_flag(control, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_clear_flag(control, LV_OBJ_FLAG_CLICKABLE);
        }
    }
}

}  // namespace qsee
