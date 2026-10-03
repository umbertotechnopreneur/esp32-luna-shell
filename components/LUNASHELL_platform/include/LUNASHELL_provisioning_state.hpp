#pragma once

#include <cstdint>

namespace LUNASHELL {

// Shared lifecycle types deliberately do not include BLE, LVGL or manager headers.
enum class ProvisioningState : uint8_t {
    Idle,
    Starting,
    WaitingForApp,
    CredentialsReceived,
    Connecting,
    Success,
    Failed,
    Stopping,
    Deinitializing,
    Stopped,
    Poisoned,
};

enum class ProvisioningFailureReason : uint8_t {
    None,
    Authentication,
    AccessPointNotFound,
    Unknown,
};

}  // namespace LUNASHELL
