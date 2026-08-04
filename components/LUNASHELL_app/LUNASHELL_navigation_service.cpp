#include "LUNASHELL_navigation_service.hpp"

#include <algorithm>
#include <cstddef>

namespace {

constexpr size_t kHistoryCapacity = 16;

void push_route(std::vector<LUNASHELL::Route> &history, LUNASHELL::Route route)
{
    if (history.size() >= kHistoryCapacity) {
        history.erase(history.begin());
    }
    history.push_back(route);
}

}  // namespace

namespace LUNASHELL {

void NavigationService::reset(Route initial)
{
    current_ = initial;
    back_.clear();
    forward_.clear();
    back_.reserve(kHistoryCapacity);
    forward_.reserve(kHistoryCapacity);
}

void NavigationService::navigate(Route route)
{
    if (route == current_) {
        return;
    }
    push_route(back_, current_);
    current_ = route;
    forward_.clear();
}

void NavigationService::replace(Route route)
{
    current_ = route;
    forward_.clear();
}

void NavigationService::clear_history()
{
    back_.clear();
    forward_.clear();
}

Route NavigationService::current() const
{
    return current_;
}

bool NavigationService::can_back() const
{
    return !back_.empty();
}

bool NavigationService::can_forward() const
{
    return !forward_.empty();
}

std::optional<Route> NavigationService::back_target() const
{
    return back_.empty()
        ? std::nullopt
        : std::optional<Route>(back_.back());
}

std::optional<Route> NavigationService::forward_target() const
{
    return forward_.empty()
        ? std::nullopt
        : std::optional<Route>(forward_.back());
}

bool NavigationService::contains_in_back(Route route) const
{
    return std::find(back_.begin(), back_.end(), route) != back_.end();
}

bool NavigationService::commit_back(Route expected)
{
    if (back_.empty() || back_.back() != expected) {
        return false;
    }
    push_route(forward_, current_);
    current_ = back_.back();
    back_.pop_back();
    return true;
}

bool NavigationService::commit_forward(Route expected)
{
    if (forward_.empty() || forward_.back() != expected) {
        return false;
    }
    push_route(back_, current_);
    current_ = forward_.back();
    forward_.pop_back();
    return true;
}

bool NavigationService::return_to(Route expected)
{
    const auto reverse_target =
        std::find(back_.rbegin(), back_.rend(), expected);
    if (reverse_target == back_.rend()) {
        return false;
    }
    const size_t target_index = static_cast<size_t>(
        std::distance(back_.begin(), reverse_target.base()) - 1
    );
    back_.resize(target_index);
    current_ = expected;
    forward_.clear();
    return true;
}

}  // namespace LUNASHELL
