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

enum class PairingSessionState : uint8_t {
    Idle,
    Starting,
    ReadyToScan,
    Connecting,
    Completing,
    Online,
    Cancelling,
    Cancelled,
    Failed,
    TimedOut,
};

enum class CodexLinkState : uint8_t {
    Idle,
    StartingPairing,
    Pairing,
    Paired,
    Discovering,
    Connected,
    Failed,
    TimedOut,
};

enum class CodexActivityState : uint8_t {
    None, Active, Working, Editing, Running, ResponseReady, Paused, Ended, Unavailable,
};

}  // namespace LUNASHELL
