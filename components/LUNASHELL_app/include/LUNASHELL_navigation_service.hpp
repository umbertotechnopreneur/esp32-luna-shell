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
    // initial: Named route starting a fresh history.
    void reset(Route initial);
    // route: Destination whose screen the caller has already prepared.
    void navigate(Route route);
    // route: Replacement that does not create a Back entry.
    void replace(Route route);
    void clear_history();

    [[nodiscard]] Route current() const;
    [[nodiscard]] bool can_back() const;
    [[nodiscard]] bool can_forward() const;
    [[nodiscard]] std::optional<Route> back_target() const;
    [[nodiscard]] std::optional<Route> forward_target() const;
    // route: Named destination to find in retained history.
    [[nodiscard]] bool contains_in_back(Route route) const;

    // expected_route: Previously peeked Back target; reject a changed trail.
    bool commit_back(Route expected_route);
    // expected_route: Previously peeked Forward target; reject a changed trail.
    bool commit_forward(Route expected_route);
    // expected_route: Retained route to revisit while discarding newer entries.
    bool return_to(Route expected_route);

private:
    Route current_route_ = Route::Welcome;
    std::vector<Route> back_history_;
    std::vector<Route> forward_history_;
};

}  // namespace LUNASHELL
