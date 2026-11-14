#pragma once

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#include "CODY_battery_alerts.hpp"
#include "CODY_companion_gestures.hpp"
#include "CODY_navigation_visibility.hpp"
#include "CODY_screensaver.hpp"
#include "CODY_system_info.hpp"
#include "CODY_token_history.hpp"
#include "CODY_widget_state.hpp"

namespace CODY {

// Volatile interaction state. No persisted preference or hardware observation is
// owned here: the controller supplies confirmed settings and device projections.
struct DeviceUiModel {
    WidgetRouteState widgets{};
    uint8_t voucher_first_row = 0;
    BatteryAlertNotice battery_banner{};
    int64_t battery_banner_until_us = 0;
    ScreensaverModel screensaver{};
    InfoPage info_page = InfoPage::System;
    NavigationVisibility navigation{};
    CompanionShakeState shake{};
    TokenHistoryInterval interval = TokenHistoryInterval::FiveHours;
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    CODY2D::Point voice_touch_start{};
    int64_t voice_touch_started_us = 0;
    int64_t voice_tapped_us = 0;
#endif
};

} // namespace CODY
