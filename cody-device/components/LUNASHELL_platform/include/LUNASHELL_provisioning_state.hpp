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
    AwaitingConfirmation,
};

enum class CodexActivityState : uint8_t {
    None, Active, Working, Editing, Running, ResponseReady, Paused, Interrupted, Ended, Unavailable,
};

}  // namespace LUNASHELL
