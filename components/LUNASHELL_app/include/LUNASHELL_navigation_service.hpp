#pragma once

#include <optional>
#include <vector>

namespace LUNASHELL {

enum class Route {
    Welcome,
    Battery,
    Logs,
    Hardware,
    HardwareDetail,
    Audio,
    AudioFft,
    AudioDetail,
    Imu,
    Leg,
    Gps,
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    Tof,
    TofSettings,
#endif
    Settings,
    Storage,
    About,
    PairIntro,
    PairBluetooth,
    PairApp,
    AppStoreQr,
    PairQr,
    PairResult,
};

// Owns app route history independently from the declarative screen renderer.
class NavigationService final {
public:
    void reset(Route initial);
    void navigate(Route route);
    void replace(Route route);
    void clear_history();

    [[nodiscard]] Route current() const;
    [[nodiscard]] bool can_back() const;
    [[nodiscard]] bool can_forward() const;
    [[nodiscard]] std::optional<Route> back_target() const;
    [[nodiscard]] std::optional<Route> forward_target() const;
    [[nodiscard]] bool contains_in_back(Route route) const;

    bool commit_back(Route expected);
    bool commit_forward(Route expected);
    bool return_to(Route expected);

private:
    Route current_ = Route::Welcome;
    std::vector<Route> back_;
    std::vector<Route> forward_;
};

}  // namespace LUNASHELL
