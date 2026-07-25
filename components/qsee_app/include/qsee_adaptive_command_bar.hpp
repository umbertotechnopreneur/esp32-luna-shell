#pragma once

// Reusable inactivity controller for command buttons placed on the lower arc.

#include <cstdint>
#include <vector>

#include "esp_err.h"
#include "lvgl.h"
#include "qsee_device_snapshot.hpp"

namespace qsee {

class AdaptiveCommandBar final {
public:
    static constexpr uint32_t kDefaultTimeoutSeconds = 10;
    static constexpr uint32_t kMinimumTimeoutSeconds = 10;
    static constexpr uint32_t kMaximumTimeoutSeconds = 10 * 60;

    /** Load the persisted visibility timeout, falling back to ten seconds. */
    esp_err_t begin();
    /** Track one command without taking ownership of its LVGL object. */
    void registerControl(lv_obj_t *control);
    /** Reset visibility and activity state whenever the active route changes. */
    void activate(bool has_commands);
    /** Detect touch or motion activity and advance the inactivity fade policy. */
    void update(const DeviceSnapshot &snapshot, uint32_t touch_inactive_ms);
    /** Persist and immediately apply a UI-selected timeout. */
    esp_err_t setTimeoutSeconds(uint32_t seconds);
    uint32_t timeoutSeconds() const;
    bool isActive() const;
    /** Stop pending animations before the registered controls are destroyed. */
    void shutdown();

private:
    static void animationValue(void *context, int32_t opacity);
    static void animationCompleted(lv_anim_t *animation);
    void noteActivity(int64_t now_us);
    bool detectMotion(const ImuSnapshot &imu);
    void animateTo(int32_t opacity);
    void applyOpacity(int32_t opacity);
    void setControlsClickable(bool clickable);

    std::vector<lv_obj_t *> _controls;
    uint32_t _timeout_seconds = kDefaultTimeoutSeconds;
    uint32_t _last_touch_inactive_ms = UINT32_MAX;
    int64_t _last_activity_us = 0;
    int64_t _last_imu_sample_us = 0;
    float _last_acceleration_mps2[3] = {};
    int32_t _opacity = LV_OPA_COVER;
    int32_t _animation_target = LV_OPA_COVER;
    bool _active = false;
    bool _motion_baseline_ready = false;
};

}  // namespace qsee
