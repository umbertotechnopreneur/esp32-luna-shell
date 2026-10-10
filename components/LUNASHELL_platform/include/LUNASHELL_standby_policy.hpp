#pragma once

#include <cstdint>

namespace LUNASHELL::power_policy {

enum class StandbySleep : uint8_t { Software, Deep };

// deep_sleep_enabled: Whether the selected build permits loss of runtime state.
// retained_connection: Confirmed radio retention needed for incoming Codex events.
// voice_listening: Whether the audio owner must keep acquiring standby speech.
// touch_wake_available: Whether this profile can enter the existing deep-sleep path.
[[nodiscard]] constexpr StandbySleep select_standby_sleep(
    bool deep_sleep_enabled, bool retained_connection, bool voice_listening,
    bool touch_wake_available)
{
    // A live service requires software standby even in a Release build.
    return deep_sleep_enabled && touch_wake_available &&
        !retained_connection && !voice_listening ? StandbySleep::Deep : StandbySleep::Software;
}

} // namespace LUNASHELL::power_policy
