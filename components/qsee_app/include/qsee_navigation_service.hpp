#pragma once

#include <optional>
#include <vector>

namespace qsee {

enum class Route {
    Welcome,
    Logs,
    Hardware,
    Audio,
    AudioFft,
    Imu,
    Settings,
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

}  // namespace qsee
