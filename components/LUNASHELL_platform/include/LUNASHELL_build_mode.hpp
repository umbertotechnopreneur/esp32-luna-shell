// Shared firmware identity and runtime policy; hardware profiles stay independent.
#pragma once

#include <cstdint>
#include "sdkconfig.h"
#include "esp_log.h"

namespace LUNASHELL::build_mode {

enum class Mode : uint8_t {
    Debug,
    Release,
};

#if CONFIG_LUNASHELL_BUILD_MODE_DEBUG && !CONFIG_LUNASHELL_BUILD_MODE_RELEASE
inline constexpr Mode kSelectedMode = Mode::Debug;
inline constexpr const char *kName = "Debug";
#elif CONFIG_LUNASHELL_BUILD_MODE_RELEASE && !CONFIG_LUNASHELL_BUILD_MODE_DEBUG
inline constexpr Mode kSelectedMode = Mode::Release;
inline constexpr const char *kName = "Release";
#else
#error "Select exactly one firmware build mode through scripts/device.ps1"
#endif

inline constexpr bool kDevelopmentDiagnosticsEnabled = kSelectedMode == Mode::Debug;
inline constexpr bool kAutomaticLightSleepEnabled = kSelectedMode == Mode::Release;
// Runtime standby policy still excludes deep sleep while a live service is retained.
inline constexpr bool kDeepSleepOnStandbyEnabled = kSelectedMode == Mode::Release;

// No parameters: publish the compiled mode once during real firmware startup.
inline void log_startup_mode()
{
    if constexpr (kAutomaticLightSleepEnabled) {
        // The one Release boot warning explains the real USB-debug limitation.
        ESP_LOGW("LUNASHELL_build", "Firmware mode=Release; warnings/errors only; standby retains live services, otherwise touch/motion Deep-sleep restarts firmware and disconnects USB/Wi-Fi/BLE");
    } else {
        ESP_LOGI("LUNASHELL_build", "Firmware mode=Debug; diagnostics enabled; automatic Light-sleep disabled");
    }
}

}  // namespace LUNASHELL::build_mode
