/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#include "LUNASHELL_navigation_service.hpp"

#include <algorithm>
#include <cstddef>

namespace {

constexpr size_t kHistoryCapacity = 16;

// history: Bounded route history receiving the previous screen.
// route: Named route to retain for a later navigation action.
// Exceptions: std::bad_alloc if route-history storage cannot be allocated.
void push_route(std::vector<LUNASHELL::Route> &history, LUNASHELL::Route route)
{
    // Keep the recent trail; yesterday's oldest screen can leave the queue.
    if (history.size() >= kHistoryCapacity) {
        history.erase(history.begin());
    }
    history.push_back(route);
}

}  // namespace

namespace LUNASHELL {

// initial: Named route that starts a fresh navigation session.
// Exceptions: std::bad_alloc if route-history storage cannot be allocated.
void NavigationService::reset(Route initial)
{
    current_route_ = initial;
    back_history_.clear();
    forward_history_.clear();
    back_history_.reserve(kHistoryCapacity);
    forward_history_.reserve(kHistoryCapacity);
}

// route: Destination committed by the caller after its screen is ready.
// Exceptions: std::bad_alloc if route-history storage cannot be allocated.
void NavigationService::navigate(Route route)
{
    if (route == current_route_) {
        return;
    }
    push_route(back_history_, current_route_);
    current_route_ = route;
    forward_history_.clear();
}

// route: Destination replacing the current screen without adding a back entry.
void NavigationService::replace(Route route)
{
    current_route_ = route;
    forward_history_.clear();
}

// Start with a clean trail; the visible route stays put.
void NavigationService::clear_history()
{
    back_history_.clear();
    forward_history_.clear();
}

// Return the route that actually owns the visible screen.
Route NavigationService::current() const
{
    return current_route_;
}

// Back is available only while a previous route exists.
bool NavigationService::can_back() const
{
    return !back_history_.empty();
}

// Forward is available only after a committed Back action.
bool NavigationService::can_forward() const
{
    return !forward_history_.empty();
}

// Peek first; loading the target screen may still fail.
std::optional<Route> NavigationService::back_target() const
{
    return back_history_.empty()
        ? std::nullopt
        : std::optional<Route>(back_history_.back());
}

// Peek without moving history until the caller commits the screen.
std::optional<Route> NavigationService::forward_target() const
{
    return forward_history_.empty()
        ? std::nullopt
        : std::optional<Route>(forward_history_.back());
}

// route: Named destination to find in the retained back history.
bool NavigationService::contains_in_back(Route route) const
{
    return std::find(back_history_.begin(), back_history_.end(), route) != back_history_.end();
}

// expected_route: Previously peeked Back target; a changed history rejects the commit.
// Exceptions: std::bad_alloc if route-history storage cannot be allocated.
bool NavigationService::commit_back(Route expected_route)
{
    if (back_history_.empty() || back_history_.back() != expected_route) {
        return false;
    }
    // The caller has already switched the view; now the history can follow.
    push_route(forward_history_, current_route_);
    current_route_ = back_history_.back();
    back_history_.pop_back();
    return true;
}

// expected_route: Previously peeked Forward target, checked before moving history.
// Exceptions: std::bad_alloc if route-history storage cannot be allocated.
bool NavigationService::commit_forward(Route expected_route)
{
    if (forward_history_.empty() || forward_history_.back() != expected_route) {
        return false;
    }
    push_route(back_history_, current_route_);
    current_route_ = forward_history_.back();
    forward_history_.pop_back();
    return true;
}

// expected_route: Existing route to revisit while discarding newer history.
bool NavigationService::return_to(Route expected_route)
{
    const auto reverse_target =
        std::find(back_history_.rbegin(), back_history_.rend(), expected_route);
    if (reverse_target == back_history_.rend()) {
        return false;
    }
    const size_t target_index = static_cast<size_t>(
        std::distance(back_history_.begin(), reverse_target.base()) - 1
    );
    back_history_.resize(target_index);
    current_route_ = expected_route;
    forward_history_.clear();
    return true;
}

}  // namespace LUNASHELL
